#include "tomtom-display-protocol.h"

unsigned int
tomtom_display_read_u16(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] |
        ((unsigned int)bytes[1] << 8);
}

unsigned long
tomtom_display_read_u32(const unsigned char *bytes)
{
    return (unsigned long)bytes[0] |
        ((unsigned long)bytes[1] << 8) |
        ((unsigned long)bytes[2] << 16) |
        ((unsigned long)bytes[3] << 24);
}

void
tomtom_display_write_u16(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8) & 0xffU);
}

void
tomtom_display_write_u32(unsigned char *bytes, unsigned long value)
{
    bytes[0] = (unsigned char)(value & 0xffUL);
    bytes[1] = (unsigned char)((value >> 8) & 0xffUL);
    bytes[2] = (unsigned char)((value >> 16) & 0xffUL);
    bytes[3] = (unsigned char)((value >> 24) & 0xffUL);
}

int
tomtom_display_parse_header(const unsigned char *bytes, unsigned long length,
                            struct tomtom_display_frame *frame)
{
    if (bytes == 0 || frame == 0 ||
        length != TOMTOM_DISPLAY_HEADER_SIZE ||
        bytes[0] != 'T' || bytes[1] != 'T' ||
        bytes[2] != 'D' || bytes[3] != 'P' ||
        bytes[4] != TOMTOM_DISPLAY_VERSION ||
        bytes[5] != TOMTOM_DISPLAY_MESSAGE_FRAME ||
        tomtom_display_read_u16(bytes + 6) != TOMTOM_DISPLAY_HEADER_SIZE ||
        tomtom_display_read_u16(bytes + 8) != TOMTOM_DISPLAY_WIDTH ||
        tomtom_display_read_u16(bytes + 10) != TOMTOM_DISPLAY_HEIGHT ||
        tomtom_display_read_u16(bytes + 12) !=
            TOMTOM_DISPLAY_FORMAT_RGB565_LE ||
        tomtom_display_read_u32(bytes + 18) != TOMTOM_DISPLAY_FRAME_BYTES)
        return 0;
    frame->sequence = tomtom_display_read_u32(bytes + 14);
    return 1;
}

void
tomtom_display_make_ack(unsigned char *bytes, unsigned long sequence,
                        unsigned int status)
{
    bytes[0] = 'T';
    bytes[1] = 'T';
    bytes[2] = 'A';
    bytes[3] = '1';
    tomtom_display_write_u32(bytes + 4, sequence);
    bytes[8] = (unsigned char)status;
}
