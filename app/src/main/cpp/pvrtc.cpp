#include "m3g.h"
#include <algorithm>
#include <cmath>

namespace ds {
namespace {

inline int e5(int v) { return (v << 3) | (v >> 2); }
inline int e4(int v) { return (v << 4) | v; }
inline int e3(int v) { return (v << 5) | (v << 2) | (v >> 1); }

// bits de y en las posiciones pares y de x en las impares (convencion PowerVR)
inline uint32_t morton(uint32_t x, uint32_t y) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) r |= (((y >> i) & 1u) << (2 * i)) | (((x >> i) & 1u) << (2 * i + 1));
    return r;
}

struct Col { int c[4]; };

inline Col unpackA(int ca) {  // 15 bits: bit14 = opaco
    Col o;
    if ((ca >> 14) & 1) {
        int b4 = ca & 15;
        o.c[0] = e5((ca >> 9) & 31); o.c[1] = e5((ca >> 4) & 31);
        o.c[2] = e5((b4 << 1) | (b4 >> 3)); o.c[3] = 255;
    } else {
        int b3 = ca & 7;
        o.c[0] = e4((ca >> 7) & 15); o.c[1] = e4((ca >> 3) & 15);
        o.c[2] = e4((b3 << 1) | (b3 >> 2)); o.c[3] = e3((ca >> 11) & 7);
    }
    return o;
}

inline Col unpackB(int cb) {  // 16 bits: bit15 = opaco
    Col o;
    if ((cb >> 15) & 1) {
        o.c[0] = e5((cb >> 10) & 31); o.c[1] = e5((cb >> 5) & 31); o.c[2] = e5(cb & 31); o.c[3] = 255;
    } else {
        o.c[0] = e4((cb >> 8) & 15); o.c[1] = e4((cb >> 4) & 15); o.c[2] = e4(cb & 15);
        o.c[3] = e3((cb >> 12) & 7);
    }
    return o;
}

}  // namespace

bool decodePVRTC4(const uint8_t* data, size_t size, int w, int h, std::vector<uint8_t>& rgba) {
    if (w <= 0 || h <= 0 || w != h) return false;           // todas las del juego son cuadradas
    const int bw = std::max(w / 4, 2), bh = std::max(h / 4, 2);
    if (size < (size_t)bw * bh * 8) return false;
    std::vector<Col> A((size_t)bw * bh), B((size_t)bw * bh);
    std::vector<uint32_t> mod((size_t)bw * bh);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t* p = data + (size_t)morton(bx, by) * 8;
            uint32_t m = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
            uint32_t c = p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24);
            size_t i = (size_t)by * bw + bx;
            mod[i] = m;
            A[i] = unpackA((int)((c >> 1) & 0x7FFF));
            B[i] = unpackB((int)((c >> 16) & 0xFFFF));
        }
    // el texel del bloque b esta en el pixel 4b+2  ->  u = b + (px-2)/4
    static const int dOfs[4] = {-1, -1, 0, 0};
    static const double tOfs[4] = {0.5, 0.75, 0.0, 0.25};
    static const double W[4] = {0.0, 3.0 / 8, 5.0 / 8, 1.0};
    const int W_ = bw * 4, H_ = bh * 4;
    std::vector<uint8_t> full((size_t)W_ * H_ * 4);
    for (int y = 0; y < H_; y++) {
        const int by = y / 4, py = y % 4;
        const int y0 = ((by + dOfs[py]) % bh + bh) % bh, y1 = (y0 + 1) % bh;
        const double ty = tOfs[py];
        for (int x = 0; x < W_; x++) {
            const int bx = x / 4, px = x % 4;
            const int x0 = ((bx + dOfs[px]) % bw + bw) % bw, x1 = (x0 + 1) % bw;
            const double tx = tOfs[px];
            const double w00 = (1 - tx) * (1 - ty), w01 = tx * (1 - ty), w10 = (1 - tx) * ty, w11 = tx * ty;
            const size_t i00 = (size_t)y0 * bw + x0, i01 = (size_t)y0 * bw + x1;
            const size_t i10 = (size_t)y1 * bw + x0, i11 = (size_t)y1 * bw + x1;
            const double mw = W[(mod[(size_t)by * bw + bx] >> (2 * (py * 4 + px))) & 3];
            for (int k = 0; k < 4; k++) {
                double a = A[i00].c[k] * w00 + A[i01].c[k] * w01 + A[i10].c[k] * w10 + A[i11].c[k] * w11;
                double b = B[i00].c[k] * w00 + B[i01].c[k] * w01 + B[i10].c[k] * w10 + B[i11].c[k] * w11;
                double v = a + (b - a) * mw + 0.5;
                full[((size_t)y * W_ + x) * 4 + k] = (uint8_t)std::min(255.0, std::max(0.0, std::floor(v)));
            }
        }
    }
    rgba.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++)
        std::copy(full.begin() + (size_t)y * W_ * 4, full.begin() + ((size_t)y * W_ + w) * 4, rgba.begin() + (size_t)y * w * 4);
    return true;
}

}  // namespace ds
