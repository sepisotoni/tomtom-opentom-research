#include "deflate.h"

#include <cstring>

namespace tt {

namespace {

// ------------------------------------------------------------------ inflate
struct InStream {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;

    int bits(int need) {
        uint32_t val = buf;
        while (cnt < need) {
            if (pos >= size) { bad = true; return 0; }
            val |= static_cast<uint32_t>(data[pos++]) << cnt;
            cnt += 8;
        }
        buf = val >> need;
        cnt -= need;
        return static_cast<int>(val & ((1u << need) - 1u));
    }
};

struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

// Returns 0 for a complete code, >0 for incomplete, <0 for over-subscribed.
int construct(Huffman& h, const uint16_t* length, int n) {
    for (int l = 0; l <= 15; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) ++h.count[length[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= 15; ++l) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<uint16_t>(offs[l] + h.count[l]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<uint16_t>(s);
    return left;
}

int decode_symbol(InStream& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= s.bits(1);
        if (s.bad) return -1;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_codes(InStream& s, Bytes& out, size_t max_out, const Huffman& lit, const Huffman& dist) {
    while (true) {
        int sym = decode_symbol(s, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= max_out) return false;
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = kLenBase[sym] + static_cast<size_t>(s.bits(kLenExtra[sym]));
            int ds = decode_symbol(s, dist);
            if (ds < 0 || ds >= 30 || s.bad) return false;
            size_t d = kDistBase[ds] + static_cast<size_t>(s.bits(kDistExtra[ds]));
            if (s.bad || d > out.size() || out.size() + len > max_out) return false;
            size_t from = out.size() - d;
            for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
        }
        if (s.bad) return false;
    }
}

}  // namespace

bool inflate_raw(const uint8_t* in, size_t size, Bytes& out, size_t max_out, size_t* consumed) {
    out.clear();
    InStream s{in, size};
    static Huffman fixed_lit, fixed_dist;
    static bool fixed_ready = false;
    if (!fixed_ready) {
        uint16_t lengths[288];
        int k = 0;
        for (; k < 144; ++k) lengths[k] = 8;
        for (; k < 256; ++k) lengths[k] = 9;
        for (; k < 280; ++k) lengths[k] = 7;
        for (; k < 288; ++k) lengths[k] = 8;
        construct(fixed_lit, lengths, 288);
        for (k = 0; k < 30; ++k) lengths[k] = 5;
        construct(fixed_dist, lengths, 30);
        fixed_ready = true;
    }
    int last;
    do {
        last = s.bits(1);
        int type = s.bits(2);
        if (s.bad) return false;
        if (type == 0) {
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.size) return false;
            unsigned len = in[s.pos] | (in[s.pos + 1] << 8);
            unsigned nlen = in[s.pos + 2] | (in[s.pos + 3] << 8);
            s.pos += 4;
            if (len != (~nlen & 0xFFFFu)) return false;
            if (s.pos + len > s.size || out.size() + len > max_out) return false;
            out.insert(out.end(), in + s.pos, in + s.pos + len);
            s.pos += len;
        } else if (type == 1) {
            if (!inflate_codes(s, out, max_out, fixed_lit, fixed_dist)) return false;
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint16_t lengths[320];
            int nlen = s.bits(5) + 257;
            int ndist = s.bits(5) + 1;
            int ncode = s.bits(4) + 4;
            if (s.bad || nlen > 286 || ndist > 30) return false;
            int idx = 0;
            for (; idx < ncode; ++idx) lengths[order[idx]] = static_cast<uint16_t>(s.bits(3));
            for (; idx < 19; ++idx) lengths[order[idx]] = 0;
            if (s.bad) return false;
            Huffman lencode;
            if (construct(lencode, lengths, 19) != 0) return false;
            idx = 0;
            while (idx < nlen + ndist) {
                int sym = decode_symbol(s, lencode);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[idx++] = static_cast<uint16_t>(sym);
                } else {
                    uint16_t len = 0;
                    int rep;
                    if (sym == 16) {
                        if (idx == 0) return false;
                        len = lengths[idx - 1];
                        rep = 3 + s.bits(2);
                    } else if (sym == 17) {
                        rep = 3 + s.bits(3);
                    } else {
                        rep = 11 + s.bits(7);
                    }
                    if (s.bad || idx + rep > nlen + ndist) return false;
                    while (rep--) lengths[idx++] = len;
                }
            }
            if (lengths[256] == 0) return false;
            Huffman lit, dist;
            int err = construct(lit, lengths, nlen);
            if (err && (err < 0 || nlen != lit.count[0] + lit.count[1])) return false;
            err = construct(dist, lengths + nlen, ndist);
            if (err && (err < 0 || ndist != dist.count[0] + dist.count[1])) return false;
            if (!inflate_codes(s, out, max_out, lit, dist)) return false;
        } else {
            return false;
        }
    } while (!last);
    if (consumed) *consumed = s.pos;
    return true;
}

// ------------------------------------------------------------------ deflate
namespace {

struct BitWriter {
    Bytes out;
    uint32_t buf = 0;
    int cnt = 0;
    void put(uint32_t value, int n) {
        buf |= value << cnt;
        cnt += n;
        while (cnt >= 8) {
            out.push_back(static_cast<uint8_t>(buf & 0xFF));
            buf >>= 8;
            cnt -= 8;
        }
    }
    void put_code(uint32_t code, int len) {  // Huffman codes are sent MSB first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
        put(r, len);
    }
    void flush() {
        if (cnt) { out.push_back(static_cast<uint8_t>(buf & 0xFF)); buf = 0; cnt = 0; }
    }
};

void put_literal_length(BitWriter& w, int sym) {
    if (sym < 144) w.put_code(0x30u + static_cast<uint32_t>(sym), 8);
    else if (sym < 256) w.put_code(0x190u + static_cast<uint32_t>(sym - 144), 9);
    else if (sym < 280) w.put_code(static_cast<uint32_t>(sym - 256), 7);
    else w.put_code(0xC0u + static_cast<uint32_t>(sym - 280), 8);
}

}  // namespace

Bytes deflate_raw(const uint8_t* in, size_t n) {
    BitWriter w;
    w.out.reserve(n / 4 + 64);
    w.put(1, 1);  // BFINAL
    w.put(1, 2);  // BTYPE = fixed Huffman

    const int kHashBits = 15;
    const size_t kWindow = 32768;
    const int kMaxChain = 48;
    std::vector<int32_t> head(size_t(1) << kHashBits, -1);
    std::vector<int32_t> prev(n ? n : 1, -1);
    auto hash3 = [&](size_t i) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        return static_cast<size_t>((v * 2654435761u) >> (32 - kHashBits));
    };
    auto insert = [&](size_t i) {
        if (i + 3 <= n) {
            size_t h = hash3(i);
            prev[i] = head[h];
            head[h] = static_cast<int32_t>(i);
        }
    };

    size_t i = 0;
    while (i < n) {
        size_t best_len = 0, best_dist = 0;
        if (i + 3 <= n) {
            size_t maxlen = n - i < 258 ? n - i : 258;
            int32_t cand = head[hash3(i)];
            for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
                size_t dist = i - static_cast<size_t>(cand);
                if (dist > kWindow) break;
                size_t len = 0;
                while (len < maxlen && in[cand + len] == in[i + len]) ++len;
                if (len > best_len) {
                    best_len = len;
                    best_dist = dist;
                    if (len >= maxlen) break;
                }
                cand = prev[static_cast<size_t>(cand)];
            }
        }
        if (best_len >= 3) {
            int li = 28;
            if (best_len < 258) {
                li = 0;
                while (li < 28 && kLenBase[li + 1] <= best_len) ++li;
            }
            put_literal_length(w, 257 + li);
            if (kLenExtra[li]) w.put(static_cast<uint32_t>(best_len - kLenBase[li]), kLenExtra[li]);
            int di = 0;
            while (di < 29 && kDistBase[di + 1] <= best_dist) ++di;
            w.put_code(static_cast<uint32_t>(di), 5);
            if (kDistExtra[di]) w.put(static_cast<uint32_t>(best_dist - kDistBase[di]), kDistExtra[di]);
            for (size_t k = 0; k < best_len; ++k) insert(i + k);
            i += best_len;
        } else {
            put_literal_length(w, in[i]);
            insert(i);
            ++i;
        }
    }
    put_literal_length(w, 256);
    w.flush();
    return w.out;
}

Bytes zlib_compress(const uint8_t* in, size_t size) {
    Bytes out = {0x78, 0x9C};
    Bytes body = deflate_raw(in, size);
    out.insert(out.end(), body.begin(), body.end());
    uint32_t a = adler32(in, size);
    out.push_back(static_cast<uint8_t>(a >> 24));
    out.push_back(static_cast<uint8_t>(a >> 16));
    out.push_back(static_cast<uint8_t>(a >> 8));
    out.push_back(static_cast<uint8_t>(a));
    return out;
}

bool zlib_decompress(const uint8_t* in, size_t size, Bytes& out, size_t max_out) {
    if (size < 6) return false;
    if ((in[0] & 0x0F) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return false;
    size_t used = 0;
    if (!inflate_raw(in + 2, size - 2, out, max_out, &used)) return false;
    if (2 + used + 4 > size) return false;
    const uint8_t* t = in + 2 + used;
    uint32_t want = (uint32_t(t[0]) << 24) | (uint32_t(t[1]) << 16) | (uint32_t(t[2]) << 8) | t[3];
    return want == adler32(out.data(), out.size());
}

}  // namespace tt
