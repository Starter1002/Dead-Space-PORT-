// Prueba en PC del nucleo: ./host_test <carpeta_published> <modelo.m3g> [volcado_imagen_n.raw]
#include "../app/src/main/cpp/m3g.h"
#include <cstdio>
#include <fstream>
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
    printf("bbox: [%.3f %.3f %.3f] - [%.3f %.3f %.3f]\n", m.bmin[0], m.bmin[1], m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2]);
    int sk = 0; for (auto& me : m.meshes) sk += me.skinned; printf("skinned: %d\n", sk);
    if (argc > 3 && !m.images.empty()) {            // vuelca la imagen mas grande, para compararla con Python
        size_t b = 0; for (size_t i = 0; i < m.images.size(); i++) if (m.images[i].rgba.size() > m.images[b].rgba.size()) b = i;
        std::ofstream o(argv[3], std::ios::binary); o.write((const char*)m.images[b].rgba.data(), m.images[b].rgba.size());
        printf("imagen volcada: %dx%d\n", m.images[b].w, m.images[b].h);
    }
    return 0;
}
