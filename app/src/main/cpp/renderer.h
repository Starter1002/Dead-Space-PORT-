#pragma once
#include "m3g.h"
#include <GLES3/gl3.h>
#include <chrono>
#include <string>
#include <vector>

namespace ds {

// Renderizador GLES 3.0: dibuja un Model con camara orbital. Solo se usa desde el hilo GL.
class Renderer {
public:
    bool init();                              // crea shaders (llamar en onSurfaceCreated)
    void resize(int w, int h);
    void setModel(const Model& m);            // sube mallas y texturas a la GPU
    void clearModel();
    void draw();
    void orbit(float dx, float dy);           // pixeles de arrastre
    void zoom(float factor);

    // animacion (esqueletos de los SkinnedMesh del modelo cargado)
    std::string animInfo() const;
    std::string nextClip(int delta);          // cambia de clip (circular) y devuelve animInfo()
    std::string togglePlay();                 // pausa/reanuda y devuelve animInfo()

private:
    struct Sub { GLuint vao = 0, ebo = 0; GLsizei count = 0; GLuint tex = 0; bool blend = false; };
    struct GpuMesh { GLuint vbo = 0; int skel = -1; std::vector<Sub> subs; };
    std::vector<GpuMesh> meshes_;
    std::vector<GLuint> textures_;
    GLuint prog_ = 0; GLint uMVP_ = -1, uMV_ = -1, uTex_ = -1, uHasTex_ = -1, uBones_ = -1, uSkinned_ = -1;
    std::vector<Skeleton> skeletons_;
    std::vector<std::vector<float>> palettes_;
    int clip_ = 0, clipCount_ = 0; bool playing_ = true; double timeMs_ = 0;
    std::chrono::steady_clock::time_point last_{};
    int w_ = 1, h_ = 1;
    float center_[3] = {0, 0, 0}, radius_ = 1, dist_ = 3, yaw_ = 0.6f, pitch_ = 0.25f;
};

}  // namespace ds
