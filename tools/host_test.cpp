// Prueba en PC del nucleo: ./host_test <carpeta_published> <modelo.m3g> [volcado_imagen_n.raw]
#include "../app/src/main/cpp/m3g.h"
#include <cstdio>
#include <fstream>
#include <cmath>
#include <iterator>
int main(int argc, char** argv) {
    if (argc < 3) { puts("uso: host_test <published> <ruta.m3g> [dump.raw]"); return 1; }
    std::string root = argv[1];
    ds::FileReader rd = [&](const std::string& p, std::vector<uint8_t>& o) {
        std::ifstream f(root + "/" + p, std::ios::binary); if (!f) return false;
        o.assign(std::istreambuf_iterator<char>(f), {}); return true; };
    if (argc > 3 && std::string(argv[3]) == "--img") {   // modo textura: volcar la imagen principal
        ds::Image im; std::string e;
        if (!ds::loadLargestImage(rd, argv[2], im, e)) { printf("%s\n", e.c_str()); return 2; }
        std::ofstream o(argv[4], std::ios::binary); o.write((const char*)im.rgba.data(), im.rgba.size());
        printf("imagen %dx%d\n", im.w, im.h); return 0;
    }
    ds::Model m; std::string err;
    bool ok = ds::loadModel(rd, argv[2], m, err);
    printf("%s\n", ok ? m.log.c_str() : err.c_str());
    if (!ok) return 2;
    for (size_t s = 0; s < m.skeletons.size(); s++) {
        const auto& sk = m.skeletons[s]; int dmin = 1 << 30, dmax = 0;
        for (auto& c : sk.clips) { dmin = std::min(dmin, c.duration); dmax = std::max(dmax, c.duration); }
        printf("esqueleto %zu: %zu nodos, %zu huesos, %zu canales, %zu clips%s\n", s, sk.nodes.size(), sk.palette.size(), sk.channels.size(),
               sk.clips.size(), sk.clips.empty() ? "" : (" (duracion " + std::to_string(dmin) + ".." + std::to_string(dmax) + " ms)").c_str());
    }
    printf("bbox: [%.3f %.3f %.3f] - [%.3f %.3f %.3f]\n", m.bmin[0], m.bmin[1], m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2]);
    int sk = 0; for (auto& me : m.meshes) sk += me.skinned; printf("skinned: %d\n", sk);
    if (argc > 3 && !m.images.empty()) {            // vuelca la imagen mas grande, para compararla con Python
        size_t b = 0; for (size_t i = 0; i < m.images.size(); i++) if (m.images[i].rgba.size() > m.images[b].rgba.size()) b = i;
        std::ofstream o(argv[3], std::ios::binary); o.write((const char*)m.images[b].rgba.data(), m.images[b].rgba.size());
        printf("imagen volcada: %dx%d\n", m.images[b].w, m.images[b].h);
    }
    // --skin <out.obj> <clip> <ms> : malla con skinning por CPU en ese clip y tiempo (solo mallas con esqueleto)
    if (argc > 6 && std::string(argv[3]) == "--skin") {
        int clip = atoi(argv[5]); double ms = atof(argv[6]); size_t base = 1;
        std::ofstream o(argv[4]);
        for (auto& me : m.meshes) {
            if (me.skeleton < 0) continue;
            std::vector<float> pal, sp; ds::evalPalette(m.skeletons[me.skeleton], clip, ms, pal); ds::skinPositions(me, pal, sp);
            bool bad = false; for (float x : sp) if (!std::isfinite(x)) bad = true;
            printf("malla %d: %zu vertices skin%s\n", me.id, sp.size() / 3, bad ? " (NaN!)" : "");
            for (size_t v = 0; v < sp.size() / 3; v++) o << "v " << sp[v * 3] << " " << sp[v * 3 + 1] << " " << sp[v * 3 + 2] << "\n";
            for (auto& sm : me.subs) for (size_t i = 0; i + 2 < sm.indices.size(); i += 3)
                o << "f " << sm.indices[i] + base << " " << sm.indices[i + 1] + base << " " << sm.indices[i + 2] + base << "\n";
            base += sp.size() / 3;
        }
    }
    return 0;
}
