#include "m3g.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace ds {
namespace {

// ------------------------------------------------------------------ lector binario (little-endian)
struct Rd {
    const uint8_t* b; size_t n; size_t p = 0; bool ok = true;
    Rd(const uint8_t* b_, size_t n_) : b(b_), n(n_) {}
    bool need(size_t k) { if (p + k > n) { ok = false; return false; } return true; }
    uint8_t u8() { return need(1) ? b[p++] : 0; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = (uint16_t)(b[p] | (b[p + 1] << 8)); p += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24); p += 4; return v; }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    const uint8_t* raw(size_t k) { if (!need(k)) return nullptr; const uint8_t* r = b + p; p += k; return r; }
    // true si caben 'cnt' elementos de 'sz' bytes (evita reservas enormes con datos corruptos)
    bool fits(uint64_t cnt, size_t sz) { return cnt * sz <= (uint64_t)(n - p); }
};

// ------------------------------------------------------------------ matrices 4x4 (fila-mayor)
struct Mat { double m[16]; };
Mat identity() { Mat r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.0 : 0.0; return r; }
Mat mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

// ------------------------------------------------------------------ objetos
struct Obj {
    int type = -1; const uint8_t* d = nullptr; uint32_t len = 0;
    std::string ext;                                   // parametro de usuario 900: ruta de textura externa
    std::string name;                                  // parametro de usuario 0: nombre (huesos, mallas)
    // Transformable
    bool hasTr = false, hasMat = false;
    double tr[3] = {0, 0, 0}, sc[3] = {1, 1, 1}, ang = 0, axis[3] = {0, 0, 1}, mat[16];
    std::vector<uint32_t> kids;                        // Group
    // VertexArray
    int cs = 0, cc = 0; std::vector<float> arr;
    // VertexBuffer
    uint32_t vbPos = 0, vbNrm = 0; float pbias[3] = {0, 0, 0}, pscale = 1;
    struct Tex { uint32_t arr; float bias[3]; float scale; };
    std::vector<Tex> vbTex;
    // TriangleStripArray
    std::vector<uint32_t> idx, lens;
    // Appearance / Texture2D
    std::vector<uint32_t> appTex; uint32_t img = 0;
    // Image2D
    int fmt = 0, iw = 0, ih = 0; const uint8_t* pix = nullptr; uint32_t pixLen = 0;
    // Mesh / SkinnedMesh
    uint32_t vb = 0; std::vector<std::pair<uint32_t, uint32_t>> subs; bool subRefs = false;
    // SubMesh (tipo 100 propio de IM-M3G)
    uint32_t sa = 0, sb = 0;
    // SkinnedMesh
    std::vector<std::pair<uint32_t, uint32_t>> binds;  // (propiedad, nodo) = plantilla de canales de un clip
    uint32_t idxArr = 0, wgtArr = 0; std::vector<uint32_t> bones;
    // AnimationTrack (tipo 2) / KeyframeSequence (tipo 19)
    uint32_t trSeq = 0, trProp = 0;
    int kfInterp = 0, kfCC = 0; uint32_t kfDur = 0; std::vector<uint32_t> kfT; std::vector<float> kfV;
};

struct File {
    std::vector<uint8_t> data;
    std::vector<Obj> objs;        // base 1: objs[0] sin usar
    std::vector<int> types;       // base 1
};

void object3d(Rd& r, Obj& o) {
    r.u32();                                           // userID
    uint32_t nt = r.u32(); if (!r.fits(nt, 4)) { r.ok = false; return; }
    for (uint32_t i = 0; i < nt; i++) r.u32();         // AnimationTracks (no usadas en el visor)
    uint32_t np = r.u32();
    for (uint32_t i = 0; i < np && r.ok; i++) {
        uint32_t id = r.u32(), ln = r.u32();
        const uint8_t* s = r.raw(ln);
        if (s && id == 900) { o.ext.assign((const char*)s, ln); while (!o.ext.empty() && o.ext.back() == 0) o.ext.pop_back(); }
        if (s && id == 0) { o.name.assign((const char*)s, ln); while (!o.name.empty() && o.name.back() == 0) o.name.pop_back(); }
    }
}

void transformable(Rd& r, Obj& o) {
    if (r.u8()) {
        o.hasTr = true;
        for (int i = 0; i < 3; i++) o.tr[i] = r.f32();
        for (int i = 0; i < 3; i++) o.sc[i] = r.f32();
        o.ang = r.f32();
        for (int i = 0; i < 3; i++) o.axis[i] = r.f32();
    }
    if (r.u8()) { o.hasMat = true; for (int i = 0; i < 16; i++) o.mat[i] = r.f32(); }
}

void nodeBasic(Rd& r) { r.u8(); r.u8(); r.u8(); r.u32(); }   // render, pick, alpha, scope
void node(Rd& r) {
    nodeBasic(r);
    if (r.u8()) { r.u8(); r.u8(); r.u32(); r.u32(); }        // alineacion opcional
}

// SkinnedMesh de IM-M3G (layout obtenido por ingenieria inversa, ver paso 4):
//   Object3D | Transformable | render,pick,alpha,scope(7) | flag u8 | N u16 | N x 8 bytes (canales) |
//   bloque extra | vertexBuffer | nSub | sub[] (tipo 100) | skeleton | idxHueso | pesos | nHuesos | hueso[]
// El bloque extra se salta buscando el vertexBuffer cuya cola cuadra exactamente con el final.
bool parseSkinned(const File& f, Obj& o) {
    Rd r(o.d, o.len);
    object3d(r, o); transformable(r, o); r.raw(7); r.u8();
    uint32_t N = r.u16();
    for (uint32_t i = 0; i < N && r.ok; i++) { uint32_t pr = r.u32(), nd = r.u32(); o.binds.push_back({pr, nd}); }
    if (!r.ok) return false;
    for (size_t q = r.p; q + 4 <= o.len; q++) {
        Rd t(o.d, o.len); t.p = q;
        uint32_t vb = t.u32();
        if (vb == 0 || vb >= f.types.size() || f.types[vb] != 21) continue;
        uint32_t ns = t.u32();
        if (ns == 0 || ns > 4096 || !t.fits(ns, 4)) continue;
        std::vector<uint32_t> subs(ns);
        for (auto& s : subs) s = t.u32();
        uint32_t skel = t.u32(), ia = t.u32(), wa = t.u32(), nb = t.u32();
        if (!t.ok || nb > 4096 || !t.fits(nb, 4)) continue;
        std::vector<uint32_t> bn(nb);
        for (auto& b : bn) b = t.u32();
        if (!t.ok || t.p != o.len) continue;
        bool good = skel < f.types.size() && f.types[skel] == 9 && ia < f.types.size() && f.types[ia] == 20 &&
                    wa < f.types.size() && f.types[wa] == 20;
        for (uint32_t s : subs) good = good && s < f.types.size() && f.types[s] == 100;
        if (!good) continue;
        o.vb = vb; o.subRefs = true;
        for (uint32_t s : subs) o.subs.push_back({s, 0});
        o.idxArr = ia; o.wgtArr = wa; o.bones = bn;
        return true;
    }
    return false;
}

bool parseObj(const File& f, Obj& o) {
    Rd r(o.d, o.len);
    switch (o.type) {
    case 2: {                                              // AnimationTrack
        object3d(r, o); o.trSeq = r.u32(); r.u32(); o.trProp = r.u32();
        break; }
    case 19: {                                             // KeyframeSequence
        object3d(r, o);
        o.kfInterp = r.u8(); r.u8(); int enc = r.u8();
        o.kfDur = r.u32(); r.u32(); r.u32();
        uint32_t cc = r.u32(), nk = r.u32();
        if (cc == 0 || cc > 16 || enc > 2) return false;
        size_t per = 4 + (enc == 0 ? cc * 4 : enc == 1 ? cc : cc * 2);
        std::vector<float> bias(cc, 0.f), scale(cc, 1.f);
        if (enc != 0) { for (auto& b : bias) b = r.f32(); for (auto& s : scale) s = r.f32(); }
        if (!r.fits(nk, per)) return false;
        o.kfCC = (int)cc; o.kfT.resize(nk); o.kfV.resize((size_t)nk * cc);
        for (uint32_t k = 0; k < nk; k++) {
            o.kfT[k] = r.u32();
            for (uint32_t j = 0; j < cc; j++)
                o.kfV[(size_t)k * cc + j] = enc == 0 ? r.f32() : enc == 1 ? r.u8() / 255.0f * scale[j] + bias[j]
                                                                         : r.u16() / 65535.0f * scale[j] + bias[j];
        }
        break; }
    case 3: {                                              // Appearance
        object3d(r, o); r.u8(); r.u32(); r.u32(); r.u32(); r.u32();
        uint32_t n = r.u32(); if (!r.fits(n, 4)) return false;
        for (uint32_t i = 0; i < n; i++) o.appTex.push_back(r.u32());
        break; }
    case 9: {                                              // Group
        object3d(r, o); transformable(r, o); node(r);
        uint32_t n = r.u32(); if (!r.fits(n, 4)) return false;
        for (uint32_t i = 0; i < n; i++) o.kids.push_back(r.u32());
        break; }
    case 10: {                                             // Image2D
        object3d(r, o);
        o.fmt = r.u8(); int mut = r.u8(); o.iw = (int)r.u32(); o.ih = (int)r.u32();
        if (!mut) {
            uint32_t pl = r.u32(); r.raw(pl);
            o.pixLen = r.u32(); o.pix = r.raw(o.pixLen);
        }
        break; }
    case 11: {                                             // TriangleStripArray
        object3d(r, o);
        uint8_t enc = r.u8(); uint32_t first = 0; bool explicitIdx = (enc & 0x80) != 0;
        if (explicitIdx) {
            uint32_t n = r.u32(); int sz = enc == 0x80 ? 4 : enc == 0x81 ? 1 : 2;
            if (!r.fits(n, sz)) return false;
            o.idx.resize(n);
            for (uint32_t i = 0; i < n; i++) o.idx[i] = sz == 4 ? r.u32() : sz == 1 ? r.u8() : r.u16();
        } else first = enc == 0 ? r.u32() : enc == 1 ? r.u8() : r.u16();
        uint32_t nl = r.u32(); if (!r.fits(nl, 4)) return false;
        uint64_t sum = 0;
        for (uint32_t i = 0; i < nl; i++) { o.lens.push_back(r.u32()); sum += o.lens.back(); }
        if (!explicitIdx) { if (sum > 50000000) return false; for (uint64_t i = 0; i < sum; i++) o.idx.push_back((uint32_t)(first + i)); }
        break; }
    case 14: {                                             // Mesh
        object3d(r, o); transformable(r, o); node(r);
        o.vb = r.u32(); uint32_t n = r.u32();
        size_t rem = o.len - r.p;
        if (n > 0 && rem == (size_t)n * 4) {               // IM-M3G: refs a objetos tipo 100
            o.subRefs = true;
            for (uint32_t i = 0; i < n; i++) o.subs.push_back({r.u32(), 0});
        } else {                                           // JSR-184: (indexBuffer, appearance)
            if (!r.fits(n, 8)) return false;
            for (uint32_t i = 0; i < n; i++) { uint32_t a = r.u32(), b = r.u32(); o.subs.push_back({a, b}); }
        }
        break; }
    case 16:                                               // SkinnedMesh
        return parseSkinned(f, o);
    case 17: {                                             // Texture2D
        object3d(r, o); transformable(r, o); o.img = r.u32();
        break; }
    case 20: {                                             // VertexArray
        object3d(r, o);
        int cs = r.u8(), cc = r.u8(), enc = r.u8(); int n = r.u16();
        if (cs != 1 && cs != 2 && cs != 4) return false;
        if (cc <= 0 || !r.fits((uint64_t)n * cc, cs)) return false;
        o.cs = cs; o.cc = cc; o.arr.resize((size_t)n * cc);
        for (size_t i = 0; i < o.arr.size(); i++) {
            if (cs == 1) o.arr[i] = (float)(int8_t)r.u8();
            else if (cs == 2) o.arr[i] = (float)(int16_t)r.u16();
            else o.arr[i] = r.f32();
        }
        if (enc == 1 && cs != 4)                           // codificacion delta
            for (int v = 1; v < n; v++) for (int c = 0; c < cc; c++) o.arr[(size_t)v * cc + c] += o.arr[(size_t)(v - 1) * cc + c];
        break; }
    case 21: {                                             // VertexBuffer
        object3d(r, o); r.u32();
        o.vbPos = r.u32(); for (int i = 0; i < 3; i++) o.pbias[i] = r.f32(); o.pscale = r.f32();
        o.vbNrm = r.u32(); r.u32();
        uint32_t nt = r.u32(); if (!r.fits(nt, 20)) return false;
        for (uint32_t i = 0; i < nt; i++) {
            Obj::Tex t; t.arr = r.u32(); for (int k = 0; k < 3; k++) t.bias[k] = r.f32(); t.scale = r.f32();
            o.vbTex.push_back(t);
        }
        break; }
    case 100: {                                            // SubMesh propio de IM-M3G
        object3d(r, o); o.sa = r.u32(); o.sb = r.u32();
        break; }
    default: break;                                        // tipos no usados por el visor
    }
    return r.ok;
}

bool parseFile(File& f, std::string& err) {
    const std::vector<uint8_t>& d = f.data;
    if (d.size() < 21) { err = "archivo demasiado corto"; return false; }
    f.objs.assign(1, Obj()); f.types.assign(1, -1);
    size_t p = 12;
    while (p + 9 <= d.size()) {
        Rd h(d.data() + p, 9);
        uint8_t comp = h.u8(); uint32_t tot = h.u32(); h.u32();
        if (comp != 0) { err = "seccion comprimida (no soportado)"; return false; }
        if (tot < 13 || p + tot > d.size()) { err = "seccion invalida"; return false; }
        size_t q = p + 9, end = p + tot - 4;
        while (q + 5 <= end) {
            Obj o; o.type = d[q];
            o.len = d[q + 1] | (d[q + 2] << 8) | (d[q + 3] << 16) | ((uint32_t)d[q + 4] << 24);
            o.d = d.data() + q + 5;
            if (q + 5 + o.len > end) { err = "objeto fuera de la seccion"; return false; }
            f.objs.push_back(o); f.types.push_back(o.type);
            q += 5 + o.len;
        }
        p += tot;
    }
    for (size_t i = 1; i < f.objs.size(); i++) {
        if (f.objs[i].type == 0) continue;
        Obj tmp = f.objs[i];
        if (parseObj(f, tmp)) f.objs[i] = tmp;
        else f.objs[i].type = -2;                          // objeto roto: se ignora
    }
    return true;
}

// ------------------------------------------------------------------ rutas
std::string normalizePath(const std::string& p) {
    std::vector<std::string> parts; std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {}
        else if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
        else parts.push_back(cur);
        cur.clear();
    };
    for (char c : p) { if (c == '/' || c == '\\') flush(); else cur += c; }
    flush();
    std::string r;
    for (size_t i = 0; i < parts.size(); i++) { if (i) r += '/'; r += parts[i]; }
    return r;
}
std::string dirOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? "" : p.substr(0, i); }

// ------------------------------------------------------------------ imagen
bool decodeImageObj(const Obj& o, Image& im) {
    if (!o.pix || o.iw <= 0 || o.ih <= 0) return false;
    im.w = o.iw; im.h = o.ih;
    size_t n = (size_t)o.iw * o.ih;
    if (o.fmt == 99) {
        if (o.pixLen < n * 3) return false;
        im.rgba.resize(n * 4);
        for (size_t i = 0; i < n; i++) { memcpy(&im.rgba[i * 4], o.pix + i * 3, 3); im.rgba[i * 4 + 3] = 255; }
    } else if (o.fmt == 100) {
        if (o.pixLen < n * 4) return false;
        im.rgba.assign(o.pix, o.pix + n * 4);
    } else if (o.fmt == 124 || o.fmt == 125) {
        if (!decodePVRTC4(o.pix, o.pixLen, o.iw, o.ih, im.rgba)) return false;
    } else return false;
    im.hasAlpha = false;
    for (size_t i = 3; i < im.rgba.size(); i += 4) if (im.rgba[i] < 250) { im.hasAlpha = true; break; }
    return true;
}

struct Loader {
    const FileReader& rd; Model& m; std::map<std::string, int> extCache; std::string warn;
    Loader(const FileReader& r, Model& mm) : rd(r), m(mm) {}

    int addImage(Image&& im) { m.images.push_back(std::move(im)); return (int)m.images.size() - 1; }

    // Imagen de un objeto Image2D; sigue la referencia externa si es un marcador de 1x1.
    int imageFor(const File& f, uint32_t idx, const std::string& path, std::map<uint32_t, int>& cache) {
        auto it = cache.find(idx); if (it != cache.end()) return it->second;
        int res = -1;
        const Obj& o = f.objs[idx];
        if (o.type == 10) {
            if (!o.ext.empty() && o.iw == 1 && o.ih == 1) res = externalImage(path, o.ext);
            else { Image im; if (decodeImageObj(o, im)) res = addImage(std::move(im)); else warn += "imagen fmt " + std::to_string(o.fmt) + " sin decodificar\n"; }
        }
        cache[idx] = res; return res;
    }

    int externalImage(const std::string& from, const std::string& ext) {
        std::string target = normalizePath(dirOf(from) + "/" + ext);
        auto it = extCache.find(target); if (it != extCache.end()) return it->second;
        int res = -1;
        File g; std::string e;
        if (rd(target, g.data) && parseFile(g, e)) {
            const Obj* best = nullptr;
            for (size_t i = 1; i < g.objs.size(); i++) {
                const Obj& o = g.objs[i];
                if (o.type == 10 && o.pix && (!best || (int64_t)o.iw * o.ih > (int64_t)best->iw * best->ih)) best = &o;
            }
            Image im; if (best && decodeImageObj(*best, im)) res = addImage(std::move(im));
        } else if (ext.find("dynamic_text") == std::string::npos) warn += "textura externa no encontrada: " + target + "\n";
        extCache[target] = res; return res;
    }
};

}  // namespace

namespace {

Mat fromTRS(const double tr[3], const double q[4], const double sc[3]) {
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double n = std::sqrt(x * x + y * y + z * z + w * w); if (n < 1e-12) { x = y = z = 0; w = 1; n = 1; }
    x /= n; y /= n; z /= n; w /= n;
    double R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                   2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                   2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    Mat M = identity();
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) M.m[r * 4 + c] = R[r * 3 + c] * sc[c];
        M.m[r * 4 + 3] = tr[r];
    }
    return M;
}

// Inversa 4x4 general (Gauss-Jordan). Si es singular devuelve la identidad.
Mat inverse4(const Mat& a) {
    double m[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { m[i][j] = a.m[i * 4 + j]; m[i][4 + j] = (i == j) ? 1.0 : 0.0; }
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++) if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
        if (std::fabs(m[p][c]) < 1e-12) return identity();
        if (p != c) for (int j = 0; j < 8; j++) std::swap(m[p][j], m[c][j]);
        double d = m[c][c];
        for (int j = 0; j < 8; j++) m[c][j] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { double f = m[r][c]; for (int j = 0; j < 8; j++) m[r][j] -= f * m[c][j]; }
    }
    Mat o;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o.m[i * 4 + j] = m[i][4 + j];
    return o;
}

// Muestrea una pista en el instante t (ms). Interpolaciones M3G: 176 lineal, 177 slerp, 180 escalon.
void sampleTrack(const Skeleton::Track& tr, double t, double out[4]) {
    const int cc = tr.comps; const size_t nk = tr.t.size();
    if (nk == 0 || cc <= 0 || cc > 4) return;
    auto copy = [&](size_t k) { for (int j = 0; j < cc; j++) out[j] = tr.v[k * cc + j]; };
    if (nk == 1 || t <= tr.t[0]) { copy(0); return; }
    if (t >= tr.t[nk - 1]) { copy(nk - 1); return; }
    size_t lo = 0, hi = nk - 1;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (tr.t[mid] <= t) lo = mid; else hi = mid; }
    if (tr.interp == 180) { copy(lo); return; }
    double u = (t - tr.t[lo]) / (double)(tr.t[hi] - tr.t[lo]);
    const float* a = &tr.v[lo * cc]; const float* b = &tr.v[hi * cc];
    if (cc == 4) {                                         // cuaternion: camino corto
        double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        double s = dot < 0 ? -1.0 : 1.0; dot *= s;
        double wa = 1 - u, wb = u;
        if (tr.interp == 177 && dot < 0.9995) {
            double th = std::acos(std::min(1.0, dot)), sn = std::sin(th);
            wa = std::sin((1 - u) * th) / sn; wb = std::sin(u * th) / sn;
        }
        double q[4], n = 0;
        for (int j = 0; j < 4; j++) { q[j] = wa * a[j] + wb * s * b[j]; n += q[j] * q[j]; }
        n = std::sqrt(n); if (n < 1e-12) n = 1;
        for (int j = 0; j < 4; j++) out[j] = q[j] / n;
    } else {
        for (int j = 0; j < cc; j++) out[j] = a[j] + (b[j] - a[j]) * u;
    }
}

}  // namespace

void evalPalette(const Skeleton& sk, int clip, double timeMs, std::vector<float>& out) {
    const size_t n = sk.nodes.size();
    std::vector<std::array<double, 3>> tr(n), sc(n);
    std::vector<std::array<double, 4>> q(n);
    for (size_t i = 0; i < n; i++) {
        const auto& nd = sk.nodes[i];
        for (int k = 0; k < 3; k++) { tr[i][k] = nd.tr[k]; sc[i][k] = nd.sc[k]; }
        for (int k = 0; k < 4; k++) q[i][k] = nd.q[k];
    }
    if (clip >= 0 && (size_t)clip < sk.clips.size()) {
        const auto& cl = sk.clips[clip];
        double dur = cl.duration > 0 ? cl.duration : 1.0;
        double t = std::fmod(timeMs, dur); if (t < 0) t += dur;
        for (size_t k = 0; k < sk.channels.size() && k < cl.tracks.size(); k++) {
            const auto& ch = sk.channels[k]; if (ch.node < 0) continue;
            double v[4] = {0, 0, 0, 1};
            if (ch.prop == 275) { v[0] = tr[ch.node][0]; v[1] = tr[ch.node][1]; v[2] = tr[ch.node][2]; sampleTrack(cl.tracks[k], t, v); for (int j = 0; j < 3; j++) tr[ch.node][j] = v[j]; }
            else if (ch.prop == 268) { sampleTrack(cl.tracks[k], t, v); for (int j = 0; j < 4; j++) q[ch.node][j] = v[j]; }
            else if (ch.prop == 270) { v[0] = sc[ch.node][0]; v[1] = sc[ch.node][1]; v[2] = sc[ch.node][2]; sampleTrack(cl.tracks[k], t, v);
                if (cl.tracks[k].comps == 1) { for (int j = 0; j < 3; j++) sc[ch.node][j] = v[0]; } else for (int j = 0; j < 3; j++) sc[ch.node][j] = v[j]; }
        }
    }
    std::vector<Mat> W(n);
    for (size_t i = 0; i < n; i++) {
        Mat M = fromTRS(tr[i].data(), q[i].data(), sc[i].data());
        int p = sk.nodes[i].parent;
        W[i] = (p >= 0 && (size_t)p < i) ? mul(W[p], M) : M;
    }
    out.assign(sk.palette.size() * 16, 0.f);
    for (size_t b = 0; b < sk.palette.size(); b++) {
        Mat inv; for (int k = 0; k < 16; k++) inv.m[k] = sk.invBind[b * 16 + k];
        Mat P = mul(W[sk.palette[b]], inv);
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[b * 16 + c * 4 + r] = (float)P.m[r * 4 + c];   // columna-mayor
    }
}

void skinPositions(const Mesh& m, const std::vector<float>& pal, std::vector<float>& out) {
    const size_t nv = m.pos.size() / 3;
    out.assign(nv * 3, 0.f);
    for (size_t v = 0; v < nv; v++) {
        const float x = m.pos[v * 3], y = m.pos[v * 3 + 1], z = m.pos[v * 3 + 2];
        float acc[3] = {0, 0, 0};
        for (int k = 0; k < 4; k++) {
            float w = m.boneW[v * 4 + k]; if (w == 0.f) continue;
            const float* M = &pal[(size_t)m.boneIdx[v * 4 + k] * 16];
            acc[0] += w * (M[0] * x + M[4] * y + M[8] * z + M[12]);
            acc[1] += w * (M[1] * x + M[5] * y + M[9] * z + M[13]);
            acc[2] += w * (M[2] * x + M[6] * y + M[10] * z + M[14]);
        }
        for (int k = 0; k < 3; k++) out[v * 3 + k] = acc[k];
    }
}

// ------------------------------------------------------------------ carga del modelo
bool loadModel(const FileReader& rd, const std::string& path, Model& out, std::string& err) {
    out = Model();
    File f;
    if (!rd(path, f.data)) { err = "no se pudo leer " + path; return false; }
    if (!parseFile(f, err)) return false;

    // matrices de mundo (Group/Mesh/SkinnedMesh), pose base
    std::map<uint32_t, uint32_t> parent;
    for (size_t i = 1; i < f.objs.size(); i++)
        if (f.objs[i].type == 9) for (uint32_t k : f.objs[i].kids) parent[k] = (uint32_t)i;
    std::map<uint32_t, Mat> wcache;
    std::function<Mat(uint32_t)> world = [&](uint32_t i) -> Mat {
        auto it = wcache.find(i); if (it != wcache.end()) return it->second;
        Mat M = identity();
        if (i < f.objs.size()) {
            const Obj& o = f.objs[i];
            if (o.hasTr) {
                Mat T = identity(), S = identity(), R = identity();
                for (int k = 0; k < 3; k++) { T.m[k * 4 + 3] = o.tr[k]; S.m[k * 5] = o.sc[k]; }
                double n = std::sqrt(o.axis[0] * o.axis[0] + o.axis[1] * o.axis[1] + o.axis[2] * o.axis[2]);
                if (n > 1e-9 && std::fabs(o.ang) > 1e-9) {
                    double x = o.axis[0] / n, y = o.axis[1] / n, z = o.axis[2] / n, a = o.ang * M_PI / 180.0;
                    double c = std::cos(a), s = std::sin(a), t = 1 - c;
                    double q[9] = {t*x*x + c, t*x*y - s*z, t*x*z + s*y,  t*x*y + s*z, t*y*y + c, t*y*z - s*x,  t*x*z - s*y, t*y*z + s*x, t*z*z + c};
                    for (int r = 0; r < 3; r++) for (int cc = 0; cc < 3; cc++) R.m[r * 4 + cc] = q[r * 3 + cc];
                }
                M = mul(mul(T, R), S);
            }
            if (o.hasMat) { Mat G; for (int k = 0; k < 16; k++) G.m[k] = o.mat[k]; M = mul(M, G); }
        }
        auto pit = parent.find(i);
        if (pit != parent.end() && pit->second != i) M = mul(world(pit->second), M);
        wcache[i] = M; return M;
    };

    // secuencias de fotogramas no enlazadas por ninguna AnimationTrack: bloques de clips de los esqueletos
    std::set<uint32_t> usedSeq;
    for (size_t i = 1; i < f.objs.size(); i++) if (f.objs[i].type == 2) usedSeq.insert(f.objs[i].trSeq);
    std::vector<std::pair<uint32_t, uint32_t>> freeRuns;
    for (size_t i = 1; i < f.objs.size(); i++) {
        if (f.objs[i].type != 19 || usedSeq.count((uint32_t)i)) continue;
        if (!freeRuns.empty() && freeRuns.back().second + 1 == i) freeRuns.back().second = (uint32_t)i;
        else freeRuns.push_back({(uint32_t)i, (uint32_t)i});
    }

    Loader L(rd, out);
    std::map<uint32_t, int> imgCache;
    bool first = true;
    auto isT = [&](uint32_t i, int t) { return i > 0 && i < f.objs.size() && f.objs[i].type == t; };

    for (size_t mi = 1; mi < f.objs.size(); mi++) {
        const Obj& mo = f.objs[mi];
        if (mo.type != 14 && mo.type != 16) continue;
        if (!isT(mo.vb, 21)) { L.warn += "mesh " + std::to_string(mi) + ": vertexbuffer invalido\n"; continue; }
        const Obj& vb = f.objs[mo.vb];
        if (!isT(vb.vbPos, 20)) continue;
        const Obj& pa = f.objs[vb.vbPos];
        size_t nv = pa.arr.size() / (size_t)std::max(pa.cc, 1);
        if (nv == 0 || pa.cc < 3) continue;

        Mesh me; me.id = (int)mi; me.skinned = mo.type == 16;
        Mat W = world((uint32_t)mi);
        me.pos.resize(nv * 3);
        for (size_t v = 0; v < nv; v++) {
            double p[3];
            for (int k = 0; k < 3; k++) p[k] = pa.arr[v * pa.cc + k] * (double)vb.pscale + vb.pbias[k];
            for (int r = 0; r < 3; r++) me.pos[v * 3 + r] = (float)(W.m[r*4]*p[0] + W.m[r*4+1]*p[1] + W.m[r*4+2]*p[2] + W.m[r*4+3]);
        }
        if (isT(vb.vbNrm, 20) && f.objs[vb.vbNrm].arr.size() / std::max(f.objs[vb.vbNrm].cc, 1) == nv && f.objs[vb.vbNrm].cc >= 3) {
            const Obj& na = f.objs[vb.vbNrm];
            me.nrm.resize(nv * 3);
            for (size_t v = 0; v < nv; v++) {
                double n[3] = {na.arr[v*na.cc], na.arr[v*na.cc+1], na.arr[v*na.cc+2]}, o[3];
                for (int r = 0; r < 3; r++) o[r] = W.m[r*4]*n[0] + W.m[r*4+1]*n[1] + W.m[r*4+2]*n[2];
                double l = std::sqrt(o[0]*o[0] + o[1]*o[1] + o[2]*o[2]); if (l < 1e-12) l = 1;
                for (int r = 0; r < 3; r++) me.nrm[v * 3 + r] = (float)(o[r] / l);
            }
        }
        if (!vb.vbTex.empty() && isT(vb.vbTex[0].arr, 20)) {
            const Obj& ua = f.objs[vb.vbTex[0].arr]; const Obj::Tex& t = vb.vbTex[0];
            if (ua.cc >= 2 && ua.arr.size() / ua.cc == nv) {
                me.uv.resize(nv * 2);
                for (size_t v = 0; v < nv; v++)
                    for (int k = 0; k < 2; k++) me.uv[v * 2 + k] = (float)(ua.arr[v * ua.cc + k] * (double)t.scale + t.bias[k]);
            }
        }

        for (auto& s : mo.subs) {
            uint32_t ib = s.first, ap = s.second;
            if (mo.subRefs) {
                if (!isT(s.first, 100)) { L.warn += "mesh " + std::to_string(mi) + ": submesh invalido\n"; continue; }
                ib = f.objs[s.first].sa; ap = f.objs[s.first].sb;
            }
            if (!isT(ib, 11)) continue;
            const Obj& tsa = f.objs[ib];
            SubMesh sm;
            if (isT(ap, 3) && !f.objs[ap].appTex.empty() && isT(f.objs[ap].appTex[0], 17)) {
                uint32_t img = f.objs[f.objs[ap].appTex[0]].img;
                if (isT(img, 10)) sm.image = L.imageFor(f, img, path, imgCache);
            }
            size_t q = 0;
            for (uint32_t len : tsa.lens) {
                if (q + len > tsa.idx.size()) break;
                const uint32_t* st = &tsa.idx[q]; q += len;
                for (uint32_t k = 0; k + 2 < len; k++) {
                    uint32_t a = st[k], b = st[k + 1], c = st[k + 2];
                    if (k & 1) std::swap(a, b);
                    if (a == b || b == c || a == c || a >= nv || b >= nv || c >= nv) continue;
                    sm.indices.push_back(a); sm.indices.push_back(b); sm.indices.push_back(c);
                }
            }
            if (!sm.indices.empty()) me.subs.push_back(std::move(sm));
        }
        if (me.subs.empty()) continue;

        if (me.nrm.empty()) {                              // normales suaves calculadas
            me.nrm.assign(nv * 3, 0.f);
            for (auto& sm : me.subs)
                for (size_t t = 0; t + 2 < sm.indices.size(); t += 3) {
                    const uint32_t i0 = sm.indices[t], i1 = sm.indices[t + 1], i2 = sm.indices[t + 2];
                    const float* p0 = &me.pos[i0 * 3]; const float* p1 = &me.pos[i1 * 3]; const float* p2 = &me.pos[i2 * 3];
                    float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]}, e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
                    float n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
                    for (uint32_t i : {i0, i1, i2}) for (int k = 0; k < 3; k++) me.nrm[i * 3 + k] += n[k];
                }
            for (size_t v = 0; v < nv; v++) {
                float* n = &me.nrm[v * 3]; float l = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (l > 1e-12f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 1; n[2] = 0; }
            }
        }
        for (size_t v = 0; v < nv; v++)
            for (int k = 0; k < 3; k++) {
                float x = me.pos[v * 3 + k];
                if (first) { out.bmin[k] = out.bmax[k] = x; } else { out.bmin[k] = std::min(out.bmin[k], x); out.bmax[k] = std::max(out.bmax[k], x); }
                if (k == 2) first = false;
            }
        // ---- esqueleto y animaciones (SkinnedMesh con <= 48 huesos: skinning en GPU)
        if (mo.type == 16 && !mo.bones.empty() && mo.bones.size() <= 48 && isT(mo.idxArr, 20) && isT(mo.wgtArr, 20)) {
            const Obj& ia = f.objs[mo.idxArr]; const Obj& wa = f.objs[mo.wgtArr];
            bool okArr = ia.cc >= 4 && wa.cc >= 4 && ia.arr.size() / ia.cc == nv && wa.arr.size() / wa.cc == nv;
            bool okBones = true;
            for (uint32_t b : mo.bones) okBones = okBones && isT(b, 9);
            if (okArr && okBones) {
                Skeleton sk; std::map<uint32_t, int> nodeIdx;
                std::function<int(uint32_t)> addNode = [&](uint32_t id) -> int {
                    auto it = nodeIdx.find(id); if (it != nodeIdx.end()) return it->second;
                    int par = -1; auto pit = parent.find(id);
                    if (pit != parent.end() && pit->second != id) par = addNode(pit->second);
                    Skeleton::Node nd; nd.parent = par; const Obj& g = f.objs[id]; nd.name = g.name;
                    if (g.hasTr) {
                        for (int k = 0; k < 3; k++) { nd.tr[k] = (float)g.tr[k]; nd.sc[k] = (float)g.sc[k]; }
                        double ax[3] = {g.axis[0], g.axis[1], g.axis[2]}, n = std::sqrt(ax[0]*ax[0] + ax[1]*ax[1] + ax[2]*ax[2]);
                        if (n > 1e-9 && std::fabs(g.ang) > 1e-9) {
                            double h = g.ang * M_PI / 360.0, s = std::sin(h) / n;
                            nd.q[0] = (float)(ax[0] * s); nd.q[1] = (float)(ax[1] * s); nd.q[2] = (float)(ax[2] * s); nd.q[3] = (float)std::cos(h);
                        }
                    }
                    sk.nodes.push_back(nd); nodeIdx[id] = (int)sk.nodes.size() - 1; return nodeIdx[id];
                };
                for (uint32_t b : mo.bones) sk.palette.push_back(addNode(b));
                for (auto& bd : mo.binds) {
                    Skeleton::Channel ch; ch.prop = (int)bd.first;
                    ch.node = isT(bd.second, 9) ? addNode(bd.second) : -1;
                    sk.channels.push_back(ch);
                }
                // inversa del mundo en reposo, por hueso
                std::vector<Mat> W0(sk.nodes.size());
                for (size_t i = 0; i < sk.nodes.size(); i++) {
                    const auto& nd = sk.nodes[i];
                    double t3[3] = {nd.tr[0], nd.tr[1], nd.tr[2]}, q4[4] = {nd.q[0], nd.q[1], nd.q[2], nd.q[3]}, s3[3] = {nd.sc[0], nd.sc[1], nd.sc[2]};
                    Mat M = fromTRS(t3, q4, s3);
                    W0[i] = nd.parent >= 0 ? mul(W0[nd.parent], M) : M;
                }
                sk.invBind.resize(sk.palette.size() * 16);
                for (size_t b = 0; b < sk.palette.size(); b++) { Mat iv = inverse4(W0[sk.palette[b]]); for (int k = 0; k < 16; k++) sk.invBind[b * 16 + k] = (float)iv.m[k]; }
                // clips: bloques de N secuencias consecutivas con la forma de la plantilla de canales
                const size_t N = sk.channels.size();
                if (N > 0)
                    for (auto& run : freeRuns) {
                        size_t L = run.second - run.first + 1;
                        for (size_t blk = 0; blk < L / N; blk++) {
                            bool good = true;
                            for (size_t k = 0; k < N && good; k++) {
                                const Obj& sq = f.objs[run.first + blk * N + k];
                                int want = sk.channels[k].prop == 268 ? 4 : 3;
                                good = sq.type == 19 && (sq.kfCC == want || (sk.channels[k].prop == 270 && sq.kfCC == 1));
                            }
                            if (!good) break;
                            Skeleton::Clip cl;
                            for (size_t k = 0; k < N; k++) {
                                const Obj& sq = f.objs[run.first + blk * N + k];
                                Skeleton::Track tr; tr.interp = sq.kfInterp; tr.comps = sq.kfCC; tr.t = sq.kfT; tr.v = sq.kfV;
                                cl.duration = std::max<int>(cl.duration, (int)std::max<uint32_t>(sq.kfDur, sq.kfT.empty() ? 0 : sq.kfT.back()));
                                cl.tracks.push_back(std::move(tr));
                            }
                            sk.clips.push_back(std::move(cl));
                        }
                    }
                me.boneIdx.resize(nv * 4); me.boneW.resize(nv * 4);
                for (size_t v = 0; v < nv; v++)
                    for (int k = 0; k < 4; k++) {
                        me.boneIdx[v * 4 + k] = std::max(0.f, std::min((float)(mo.bones.size() - 1), ia.arr[v * ia.cc + k]));
                        me.boneW[v * 4 + k] = wa.arr[v * wa.cc + k];
                    }
                out.skeletons.push_back(std::move(sk));
                me.skeleton = (int)out.skeletons.size() - 1;
            }
        }
        out.vertices += nv;
        for (auto& sm : me.subs) out.triangles += sm.indices.size() / 3;
        out.meshes.push_back(std::move(me));
    }
    size_t nclips = 0; for (auto& s : out.skeletons) nclips = std::max(nclips, s.clips.size());
    out.log = path + (out.skeletons.empty() ? "" : " [" + std::to_string(out.skeletons.size()) + " esqueleto(s), " + std::to_string(nclips) + " clips]") + ": " + std::to_string(out.meshes.size()) + " mallas, " + std::to_string(out.vertices) + " vertices, " +
              std::to_string(out.triangles) + " triangulos, " + std::to_string(out.images.size()) + " texturas";
    if (!L.warn.empty()) out.log += "\n" + L.warn;
    if (out.meshes.empty()) { err = "el archivo no contiene mallas"; return false; }
    return true;
}

bool loadLargestImage(const FileReader& rd, const std::string& path, Image& out, std::string& err) {
    File g;
    if (!rd(path, g.data)) { err = "no se pudo leer " + path; return false; }
    if (!parseFile(g, err)) return false;
    const Obj* best = nullptr;
    for (size_t i = 1; i < g.objs.size(); i++) {
        const Obj& o = g.objs[i];
        if (o.type == 10 && o.pix && (!best || (int64_t)o.iw * o.ih > (int64_t)best->iw * best->ih)) best = &o;
    }
    if (!best) { err = "el archivo no contiene imagenes"; return false; }
    if (!decodeImageObj(*best, out)) { err = "formato de imagen no soportado: " + std::to_string(best->fmt); return false; }
    return true;
}

}  // namespace ds
