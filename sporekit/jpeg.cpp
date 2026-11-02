// Baseline JPEG encoder (ITU T.81: 8-bit, 4:2:0, the Annex K quantization and Huffman tables) and a
// box filter shrink, for the screenshots in report archives (reportshot.cpp). Written for SporeKit:
// small and dependency free; a 1280x720 frame takes some tens of ms on a worker thread.
#include "jpeg.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace sk {

namespace {

const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

const uint8_t kQLuma[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                            14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                            18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                            49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const uint8_t kQChroma[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                              24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                              99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                              99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

const uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t kDcVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t kAcLumaVals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
const uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const uint8_t kAcChromaVals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct Huff {
    uint16_t code[256];
    uint8_t len[256];
};

void build_huff(const uint8_t* bits, const uint8_t* vals, Huff& h)
{
    memset(&h, 0, sizeof h);
    int k = 0;
    uint16_t code = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < bits[l - 1]; i++, k++) {
            h.code[vals[k]] = code++;
            h.len[vals[k]] = (uint8_t)l;
        }
        code <<= 1;
    }
}

struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int n = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void put(uint32_t bits, int len)
    {
        acc = (acc << len) | (bits & ((1u << len) - 1));
        n += len;
        while (n >= 8) {
            uint8_t b = (uint8_t)(acc >> (n - 8));
            out.push_back(b);
            if (b == 0xFF)
                out.push_back(0); // byte stuffing
            n -= 8;
        }
        acc &= (1u << n) - 1;
    }
    void flush()
    {
        if (n > 0)
            put(0x7F, 8 - n); // pad with ones
    }
};

void put16(std::vector<uint8_t>& o, int v)
{
    o.push_back((uint8_t)(v >> 8));
    o.push_back((uint8_t)v);
}

void marker_dqt(std::vector<uint8_t>& o, int id, const uint8_t* q)
{
    o.push_back(0xFF), o.push_back(0xDB);
    put16(o, 67);
    o.push_back((uint8_t)id);
    for (int k = 0; k < 64; k++)
        o.push_back(q[kZigzag[k]]);
}

void marker_dht(std::vector<uint8_t>& o, int cls_id, const uint8_t* bits, const uint8_t* vals)
{
    int n = 0;
    for (int i = 0; i < 16; i++)
        n += bits[i];
    o.push_back(0xFF), o.push_back(0xC4);
    put16(o, 3 + 16 + n);
    o.push_back((uint8_t)cls_id);
    o.insert(o.end(), bits, bits + 16);
    o.insert(o.end(), vals, vals + n);
}

float g_cos[8][8]; // g_cos[x][u] = C(u)/2 * cos((2x+1) u pi / 16)
bool g_cos_ready;

void init_cos()
{
    if (g_cos_ready)
        return;
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            g_cos[x][u] = (float)((u == 0 ? std::sqrt(0.5) : 1.0) * 0.5 * std::cos((2 * x + 1) * u * 3.14159265358979 / 16));
    g_cos_ready = true;
}

// in: 8x8 samples already shifted by -128; q: the quantization table in natural order
void fdct_quant(const float* in, const uint8_t* q, int* out)
{
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int x = 0; x < 8; x++)
                s += in[y * 8 + x] * g_cos[x][u];
            tmp[y * 8 + u] = s;
        }
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int y = 0; y < 8; y++)
                s += tmp[y * 8 + u] * g_cos[y][v];
            out[v * 8 + u] = (int)std::lround(s / q[v * 8 + u]);
        }
}

int bit_size(int v)
{
    if (v < 0)
        v = -v;
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

void encode_block(BitWriter& bw, const int* coef, int& prev_dc, const Huff& dc, const Huff& ac)
{
    int d = coef[0] - prev_dc;
    prev_dc = coef[0];
    int s = bit_size(d);
    bw.put(dc.code[s], dc.len[s]);
    if (s)
        bw.put(d < 0 ? (uint32_t)(d - 1) : (uint32_t)d, s);
    int run = 0;
    for (int k = 1; k < 64; k++) {
        int v = coef[kZigzag[k]];
        if (v == 0) {
            run++;
            continue;
        }
        while (run >= 16) {
            bw.put(ac.code[0xF0], ac.len[0xF0]);
            run -= 16;
        }
        int sz = bit_size(v);
        if (sz > 10) { // out of the baseline range: clamp
            v = v < 0 ? -1023 : 1023;
            sz = 10;
        }
        int sym = (run << 4) | sz;
        bw.put(ac.code[sym], ac.len[sym]);
        bw.put(v < 0 ? (uint32_t)(v - 1) : (uint32_t)v, sz);
        run = 0;
    }
    if (run)
        bw.put(ac.code[0x00], ac.len[0x00]);
}

} // namespace

bool jpeg_encode(const uint8_t* rgb, int w, int h, int quality, std::vector<uint8_t>& o, const std::string& comment)
{
    o.clear();
    if (!rgb || w <= 0 || h <= 0 || w > 65535 || h > 65535)
        return false;
    init_cos();
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    uint8_t ql[64], qc[64];
    for (int i = 0; i < 64; i++) {
        int a = (kQLuma[i] * scale + 50) / 100, b = (kQChroma[i] * scale + 50) / 100;
        ql[i] = (uint8_t)(a < 1 ? 1 : a > 255 ? 255 : a);
        qc[i] = (uint8_t)(b < 1 ? 1 : b > 255 ? 255 : b);
    }
    Huff dcl, dcc, acl, acc;
    build_huff(kDcLumaBits, kDcVals, dcl);
    build_huff(kDcChromaBits, kDcVals, dcc);
    build_huff(kAcLumaBits, kAcLumaVals, acl);
    build_huff(kAcChromaBits, kAcChromaVals, acc);

    o.reserve((size_t)w * h / 4 + 1024);
    o.push_back(0xFF), o.push_back(0xD8); // SOI
    static const uint8_t app0[] = {0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    o.insert(o.end(), app0, app0 + sizeof app0);
    if (!comment.empty()) {
        size_t n = comment.size() > 60000 ? 60000 : comment.size();
        o.push_back(0xFF), o.push_back(0xFE);
        put16(o, (int)n + 2);
        o.insert(o.end(), comment.begin(), comment.begin() + (std::ptrdiff_t)n);
    }
    marker_dqt(o, 0, ql);
    marker_dqt(o, 1, qc);
    o.push_back(0xFF), o.push_back(0xC0); // SOF0
    put16(o, 17);
    o.push_back(8);
    put16(o, h);
    put16(o, w);
    o.push_back(3);
    o.push_back(1), o.push_back(0x22), o.push_back(0); // Y: 2x2, table 0
    o.push_back(2), o.push_back(0x11), o.push_back(1); // Cb
    o.push_back(3), o.push_back(0x11), o.push_back(1); // Cr
    marker_dht(o, 0x00, kDcLumaBits, kDcVals);
    marker_dht(o, 0x10, kAcLumaBits, kAcLumaVals);
    marker_dht(o, 0x01, kDcChromaBits, kDcVals);
    marker_dht(o, 0x11, kAcChromaBits, kAcChromaVals);
    static const uint8_t sos[] = {0xFF, 0xDA, 0, 12, 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    o.insert(o.end(), sos, sos + sizeof sos);

    BitWriter bw(o);
    int pdy = 0, pdb = 0, pdr = 0;
    float Y[256], cb[64], cr[64], blk[64];
    int coef[64];
    for (int my = 0; my < h; my += 16)
        for (int mx = 0; mx < w; mx += 16) {
            float cbs[256], crs[256];
            for (int y = 0; y < 16; y++) {
                int sy = my + y < h ? my + y : h - 1;
                for (int x = 0; x < 16; x++) {
                    int sx = mx + x < w ? mx + x : w - 1;
                    const uint8_t* p = rgb + ((size_t)sy * w + sx) * 3;
                    float r = p[0], g = p[1], b = p[2];
                    Y[y * 16 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.f;
                    cbs[y * 16 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                    crs[y * 16 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
                }
            }
            for (int by = 0; by < 2; by++)
                for (int bx = 0; bx < 2; bx++) {
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x++)
                            blk[y * 8 + x] = Y[(by * 8 + y) * 16 + bx * 8 + x];
                    fdct_quant(blk, ql, coef);
                    encode_block(bw, coef, pdy, dcl, acl);
                }
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int i = (y * 2) * 16 + x * 2;
                    cb[y * 8 + x] = (cbs[i] + cbs[i + 1] + cbs[i + 16] + cbs[i + 17]) * 0.25f;
                    cr[y * 8 + x] = (crs[i] + crs[i + 1] + crs[i + 16] + crs[i + 17]) * 0.25f;
                }
            fdct_quant(cb, qc, coef);
            encode_block(bw, coef, pdb, dcc, acc);
            fdct_quant(cr, qc, coef);
            encode_block(bw, coef, pdr, dcc, acc);
        }
    bw.flush();
    o.push_back(0xFF), o.push_back(0xD9); // EOI
    return true;
}

void image_shrink(const uint8_t* rgb, int w, int h, int max_w, std::vector<uint8_t>& out, int& ow, int& oh)
{
    if (w <= max_w || max_w <= 0) {
        ow = w;
        oh = h;
        out.assign(rgb, rgb + (size_t)w * h * 3);
        return;
    }
    ow = max_w;
    oh = (int)((int64_t)h * max_w / w);
    if (oh < 1)
        oh = 1;
    out.assign((size_t)ow * oh * 3, 0);
    for (int y = 0; y < oh; y++) {
        int y0 = (int)((int64_t)y * h / oh), y1 = (int)((int64_t)(y + 1) * h / oh);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < ow; x++) {
            int x0 = (int)((int64_t)x * w / ow), x1 = (int)((int64_t)(x + 1) * w / ow);
            if (x1 <= x0)
                x1 = x0 + 1;
            uint32_t s[3] = {0, 0, 0};
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t* p = rgb + ((size_t)yy * w + xx) * 3;
                    s[0] += p[0], s[1] += p[1], s[2] += p[2];
                }
            uint32_t n = (uint32_t)((y1 - y0) * (x1 - x0));
            uint8_t* d = &out[((size_t)y * ow + x) * 3];
            for (int c = 0; c < 3; c++)
                d[c] = (uint8_t)((s[c] + n / 2) / n);
        }
    }
}

} // namespace sk
