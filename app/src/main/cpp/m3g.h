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

// Esqueleto + animaciones de un SkinnedMesh (ver paso 4/6: cada clip es un bloque de N secuencias
// consecutivas cuyo orden sigue la lista de canales (propiedad, hueso) del SkinnedMesh).
struct Skeleton {
    struct Node {
        int parent = -1;                                   // indice en nodes (los padres van antes)
        float tr[3] = {0, 0, 0}, sc[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};   // pose de reposo (cuaternion x,y,z,w)
        std::string name;
    };
    struct Channel { int node = -1; int prop = 0; };       // prop: 275 traslacion, 268 orientacion, 270 escala
    struct Track { int interp = 176; int comps = 0; std::vector<uint32_t> t; std::vector<float> v; };  // t en ms
    struct Clip { int duration = 0; std::vector<Track> tracks; };      // tracks paralelo a channels
    std::vector<Node> nodes;
    std::vector<int> palette;                              // palette[i] = nodo del hueso i (los indices de vertice apuntan aqui)
    std::vector<Channel> channels;
    std::vector<Clip> clips;
    std::vector<float> invBind;                            // 16 floats por hueso (fila-mayor): inversa del mundo en reposo
};

// Matrices de hueso (columna-mayor, listas para OpenGL) del esqueleto en el clip y tiempo dados.
// clip < 0 o fuera de rango = pose de reposo (matrices identidad).
void evalPalette(const Skeleton& sk, int clip, double timeMs, std::vector<float>& out);

struct Mesh;
// Skinning por CPU de las posiciones (misma matematica que el shader); para pruebas.
void skinPositions(const Mesh& m, const std::vector<float>& palette, std::vector<float>& out);

struct Mesh {
    int id = 0;                      // indice de objeto dentro del .m3g
    int skeleton = -1;               // indice en Model::skeletons (-1 = malla estatica)
    std::vector<float> boneIdx, boneW;   // 4 por vertice (solo con esqueleto)
    bool skinned = false;            // SkinnedMesh (se dibuja en pose base)
    std::vector<float> pos;          // 3 por vertice, en espacio de mundo
    std::vector<float> nrm;          // 3 por vertice (calculadas si el archivo no las trae)
    std::vector<float> uv;           // 2 por vertice (vacio si no hay)
    std::vector<SubMesh> subs;
};

struct Model {
    std::vector<Mesh> meshes;
    std::vector<Skeleton> skeletons;
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
