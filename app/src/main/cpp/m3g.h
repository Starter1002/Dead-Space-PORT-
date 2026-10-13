// Dead Space iOS -> Android | nucleo portable: lector de .m3g (IM-M3G / JSR-184) y PVRTC 4bpp.
// Sin dependencias de Android: se compila igual en el movil (NDK) y en el PC para pruebas.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ds {

// Lee un archivo (ruta relativa a "published/") y lo devuelve en out. false si no existe.
using FileReader = std::function<bool(const std::string& path, std::vector<uint8_t>& out)>;

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // w*h*4
    bool hasAlpha = false;       // algun pixel con alfa < 250
};

struct SubMesh {
    std::vector<uint32_t> indices;  // triangulos (lista), ya convertidos desde strips
    int image = -1;                 // indice en Model::images, -1 = sin textura
};

struct Mesh {
    int id = 0;                      // indice de objeto dentro del .m3g
    bool skinned = false;            // SkinnedMesh (se dibuja en pose base)
    std::vector<float> pos;          // 3 por vertice, en espacio de mundo
    std::vector<float> nrm;          // 3 por vertice (calculadas si el archivo no las trae)
    std::vector<float> uv;           // 2 por vertice (vacio si no hay)
    std::vector<SubMesh> subs;
};

struct Model {
    std::vector<Mesh> meshes;
    std::vector<Image> images;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    size_t vertices = 0, triangles = 0;
    std::string log;                 // resumen / avisos para mostrar en pantalla
};

// Carga un .m3g completo. 'path' relativo a published/ (p. ej. "models/isaac.m3g").
// Las texturas externas (parametro de usuario 900) se resuelven con el mismo lector.
bool loadModel(const FileReader& rd, const std::string& path, Model& out, std::string& err);

// Carga la imagen mas grande de un .m3g de texturas (p. ej. "textures/texture_corer.m3g").
bool loadLargestImage(const FileReader& rd, const std::string& path, Image& out, std::string& err);

// PVRTC1 4 bpp (nivel 0, cuadrado) -> RGBA8 w*h*4. Interpolacion identica al script de Python.
bool decodePVRTC4(const uint8_t* data, size_t size, int w, int h, std::vector<uint8_t>& rgba);

}  // namespace ds
