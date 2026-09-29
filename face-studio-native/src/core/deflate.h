// RFC 1951 inflate and a compact LZ77 + fixed-Huffman deflate encoder, plus
// RFC 1950 zlib wrappers. Written from the specifications so the application
// needs no zlib dependency.
#pragma once
#include "codec.h"

namespace tt {

// Decompresses a raw deflate stream. Fails (returns false) on malformed input or
// when the output would exceed `max_out`. `consumed` (optional) receives the
// number of input bytes used.
bool inflate_raw(const uint8_t* in, size_t size, Bytes& out, size_t max_out, size_t* consumed = nullptr);

// Compresses with greedy LZ77 (32 KiB window, hash chains) and fixed Huffman codes.
Bytes deflate_raw(const uint8_t* in, size_t size);

Bytes zlib_compress(const uint8_t* in, size_t size);
bool zlib_decompress(const uint8_t* in, size_t size, Bytes& out, size_t max_out);

}  // namespace tt
