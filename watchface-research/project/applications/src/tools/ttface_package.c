#include "ttface_package.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define TTFACE_BG_BYTES 153600UL
#define ZIP_LOCAL_SIGNATURE 0x04034b50UL
#define ZIP_CENTRAL_SIGNATURE 0x02014b50UL
#define ZIP_END_SIGNATURE 0x06054b50UL
#define ZIP_IO_CHUNK 4096U

typedef struct {
    const char *name;
    unsigned long minimum_size;
    unsigned long maximum_size;
    unsigned int seen;
} TTFaceZipEntry;

static unsigned short
read_le16(const unsigned char *data)
{
    return (unsigned short)((unsigned short)data[0] |
                            ((unsigned short)data[1] << 8));
}

static unsigned long
read_le32(const unsigned char *data)
{
    return (unsigned long)data[0] |
           ((unsigned long)data[1] << 8) |
           ((unsigned long)data[2] << 16) |
           ((unsigned long)data[3] << 24);
}

static int
read_exact(FILE *file, void *buffer, size_t length)
{
    return fread(buffer, 1, length, file) == length;
}

static int
make_output_path(char *path, size_t capacity, const char *directory,
                 const char *entry, const char *suffix)
{
    int written = snprintf(path, capacity, "%s/%s%s",
                           directory, entry, suffix);
    return written > 0 && (size_t)written < capacity;
}

static int
copy_stored(FILE *archive, FILE *output, unsigned long compressed_size,
            unsigned long expected_size, unsigned long expected_crc)
{
    unsigned char buffer[ZIP_IO_CHUNK];
    unsigned long remaining = compressed_size;
    unsigned long written = 0;
    uLong crc = crc32(0L, Z_NULL, 0);

    if (compressed_size != expected_size)
        return 0;
    while (remaining != 0) {
        size_t amount = remaining > sizeof(buffer) ?
                        sizeof(buffer) : (size_t)remaining;
        if (!read_exact(archive, buffer, amount) ||
            fwrite(buffer, 1, amount, output) != amount)
            return 0;
        crc = crc32(crc, buffer, (uInt)amount);
        remaining -= (unsigned long)amount;
        written += (unsigned long)amount;
    }
    return written == expected_size && (unsigned long)crc == expected_crc;
}

static int
copy_deflated(FILE *archive, FILE *output, unsigned long compressed_size,
              unsigned long expected_size, unsigned long expected_crc)
{
    unsigned char input[ZIP_IO_CHUNK];
    unsigned char decoded[ZIP_IO_CHUNK];
    unsigned long remaining = compressed_size;
    unsigned long written = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    z_stream stream;
    int result;
    int ended = 0;

    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return 0;

    result = Z_OK;
    while (remaining != 0 && result != Z_STREAM_END) {
        size_t amount = remaining > sizeof(input) ?
                        sizeof(input) : (size_t)remaining;
        if (!read_exact(archive, input, amount)) {
            result = Z_DATA_ERROR;
            break;
        }
        remaining -= (unsigned long)amount;
        stream.next_in = input;
        stream.avail_in = (uInt)amount;

        do {
            size_t produced;
            stream.next_out = decoded;
            stream.avail_out = sizeof(decoded);
            result = inflate(&stream, Z_NO_FLUSH);
            produced = sizeof(decoded) - stream.avail_out;
            if (produced != 0) {
                if (written > expected_size ||
                    produced > expected_size - written ||
                    fwrite(decoded, 1, produced, output) != produced) {
                    result = Z_DATA_ERROR;
                    break;
                }
                crc = crc32(crc, decoded, (uInt)produced);
                written += (unsigned long)produced;
            }
            if (result == Z_STREAM_END) {
                if (stream.avail_in != 0 || remaining != 0)
                    result = Z_DATA_ERROR;
                else
                    ended = 1;
                break;
            }
            if (result != Z_OK ||
                (produced == 0 && stream.avail_in == 0))
                break;
        } while (stream.avail_in != 0 || stream.avail_out == 0);

        if (result != Z_OK && result != Z_STREAM_END)
            break;
    }

    if (!ended && result == Z_OK) {
        stream.next_in = Z_NULL;
        stream.avail_in = 0;
        do {
            size_t produced;
            stream.next_out = decoded;
            stream.avail_out = sizeof(decoded);
            result = inflate(&stream, Z_FINISH);
            produced = sizeof(decoded) - stream.avail_out;
            if (produced != 0) {
                if (written > expected_size ||
                    produced > expected_size - written ||
                    fwrite(decoded, 1, produced, output) != produced) {
                    result = Z_DATA_ERROR;
                    break;
                }
                crc = crc32(crc, decoded, (uInt)produced);
                written += (unsigned long)produced;
            }
            if (result == Z_STREAM_END)
                ended = 1;
        } while (result == Z_OK);
    }

    inflateEnd(&stream);
    return ended && written == expected_size &&
           (unsigned long)crc == expected_crc;
}

static int
extract_entry(FILE *archive, const char *staging_directory,
              TTFaceZipEntry *entry, unsigned short method,
              unsigned long crc, unsigned long compressed_size,
              unsigned long uncompressed_size)
{
    char final_path[512];
    char temporary_path[520];
    FILE *output;
    int ok;

    if (entry->seen || uncompressed_size < entry->minimum_size ||
        uncompressed_size > entry->maximum_size ||
        compressed_size > entry->maximum_size + 1024UL ||
        (method != 0 && method != 8))
        return 0;
    if (!make_output_path(final_path, sizeof(final_path),
                          staging_directory, entry->name, "") ||
        !make_output_path(temporary_path, sizeof(temporary_path),
                          staging_directory, entry->name, ".part"))
        return 0;
    output = fopen(temporary_path, "wb");
    if (output == NULL)
        return 0;

    if (method == 0)
        ok = copy_stored(archive, output, compressed_size,
                         uncompressed_size, crc);
    else
        ok = copy_deflated(archive, output, compressed_size,
                           uncompressed_size, crc);

    if (fclose(output) != 0)
        ok = 0;
    if (ok && rename(temporary_path, final_path) != 0)
        ok = 0;
    if (!ok)
        remove(temporary_path);
    else
        entry->seen = 1;
    return ok;
}

int
ttface_unpack_archive(const char *archive_path,
                      const char *staging_directory)
{
    TTFaceZipEntry entries[2];
    FILE *archive;
    unsigned int entry_count = 0;
    unsigned int i;
    int ok = 1;

    if (archive_path == NULL || staging_directory == NULL ||
        staging_directory[0] == '\0')
        return 0;

    entries[0].name = "manifest.json";
    entries[0].minimum_size = 1;
    entries[0].maximum_size = TTFACE_MANIFEST_MAX;
    entries[0].seen = 0;
    entries[1].name = "assets/bg.rgb565";
    entries[1].minimum_size = TTFACE_BG_BYTES;
    entries[1].maximum_size = TTFACE_BG_BYTES;
    entries[1].seen = 0;

    archive = fopen(archive_path, "rb");
    if (archive == NULL)
        return 0;
    if (mkdir(staging_directory, 0755) != 0) {
        fclose(archive);
        return 0;
    }
    {
        char assets_path[512];
        int written = snprintf(assets_path, sizeof(assets_path),
                               "%s/assets", staging_directory);
        if (written <= 0 || (size_t)written >= sizeof(assets_path) ||
            mkdir(assets_path, 0755) != 0)
            ok = 0;
    }

    while (ok) {
        unsigned char signature_bytes[4];
        unsigned char header[26];
        unsigned short flags;
        unsigned short method;
        unsigned short name_length;
        unsigned short extra_length;
        unsigned long crc;
        unsigned long compressed_size;
        unsigned long uncompressed_size;
        char name[128];
        int found = 0;

        if (fread(signature_bytes, 1, 4, archive) != 4) {
            ok = 0;
            break;
        }
        if (read_le32(signature_bytes) == ZIP_CENTRAL_SIGNATURE ||
            read_le32(signature_bytes) == ZIP_END_SIGNATURE)
            break;
        if (read_le32(signature_bytes) != ZIP_LOCAL_SIGNATURE ||
            !read_exact(archive, header, sizeof(header))) {
            ok = 0;
            break;
        }

        flags = read_le16(header + 2);
        method = read_le16(header + 4);
        crc = read_le32(header + 10);
        compressed_size = read_le32(header + 14);
        uncompressed_size = read_le32(header + 18);
        name_length = read_le16(header + 22);
        extra_length = read_le16(header + 24);
        if ((flags & (unsigned short)~0x0006U) != 0 ||
            name_length == 0 || name_length >= sizeof(name)) {
            ok = 0;
            break;
        }
        if (!read_exact(archive, name, name_length) ||
            fseek(archive, (long)extra_length, SEEK_CUR) != 0) {
            ok = 0;
            break;
        }
        name[name_length] = '\0';

        for (i = 0; i < 2; ++i) {
            if (strcmp(name, entries[i].name) == 0) {
                found = 1;
                if (!extract_entry(archive, staging_directory, &entries[i],
                                   method, crc, compressed_size,
                                   uncompressed_size))
                    ok = 0;
                else
                    ++entry_count;
                break;
            }
        }
        if (!found)
            ok = 0;
    }

    if (fclose(archive) != 0)
        ok = 0;
    return ok && entry_count == 2 &&
           entries[0].seen && entries[1].seen;
}
