#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ds {
namespace {

const char* kVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aBoneIdx;
layout(location=4) in vec4 aBoneW;
uniform mat4 uMVP;
uniform mat4 uMV;
uniform mat4 uBones[48];
uniform int uSkinned;
out vec3 vN;
out vec2 vUv;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 n = aNrm;
    if (uSkinned == 1) {
        mat4 m = aBoneW.x * uBones[int(aBoneIdx.x)] + aBoneW.y * uBones[int(aBoneIdx.y)] +
                 aBoneW.z * uBones[int(aBoneIdx.z)] + aBoneW.w * uBones[int(aBoneIdx.w)];
        p = m * p;
        n = mat3(m) * n;
    }
    vN = mat3(uMV) * n;
    vUv = aUv;
    gl_Position = uMVP * p;
}
)";

const char* kFS = R"(#version 300 es
precision mediump float;
in vec3 vN;
in vec2 vUv;
uniform sampler2D uTex;
uniform int uHasTex;
out vec4 oColor;
void main() {
    vec4 c = (uHasTex == 1) ? texture(uTex, vUv) : vec4(0.72, 0.72, 0.72, 1.0);
    if (c.a < 0.02) discard;
    vec3 n = normalize(vN);
    if (!gl_FrontFacing) n = -n;
    float l = 0.38 + 0.62 * max(dot(n, normalize(vec3(0.35, 0.55, 0.75))), 0.0);
    oColor = vec4(c.rgb * l, c.a);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

// ---- matrices 4x4 en columna-mayor (convencion OpenGL)
struct M4 { float m[16]; };
M4 ident() { M4 r; std::memset(r.m, 0, sizeof r.m); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
M4 perspective(float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / std::tan(fovy * 0.5f);
    M4 r; std::memset(r.m, 0, sizeof r.m);
    r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1; r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}
M4 lookAt(const float* e, const float* c, const float* up) {
    float f[3] = {c[0] - e[0], c[1] - e[1], c[2] - e[2]};
    float fl = std::sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]); for (float& v : f) v /= fl;
    float s[3] = {f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0]};
    float sl = std::sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]); for (float& v : s) v /= sl;
    float u[3] = {s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0]};
    M4 r = ident();
    r.m[0] = s[0]; r.m[4] = s[1]; r.m[8] = s[2];
    r.m[1] = u[0]; r.m[5] = u[1]; r.m[9] = u[2];
    r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2];
    r.m[12] = -(s[0]*e[0] + s[1]*e[1] + s[2]*e[2]);
    r.m[13] = -(u[0]*e[0] + u[1]*e[1] + u[2]*e[2]);
    r.m[14] = f[0]*e[0] + f[1]*e[1] + f[2]*e[2];
    return r;
}

}  // namespace

bool Renderer::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    uMVP_ = glGetUniformLocation(prog_, "uMVP"); uMV_ = glGetUniformLocation(prog_, "uMV");
    uTex_ = glGetUniformLocation(prog_, "uTex"); uHasTex_ = glGetUniformLocation(prog_, "uHasTex");
    uBones_ = glGetUniformLocation(prog_, "uBones"); uSkinned_ = glGetUniformLocation(prog_, "uSkinned");
    glEnable(GL_DEPTH_TEST);
    return true;
}

void Renderer::resize(int w, int h) { w_ = std::max(w, 1); h_ = std::max(h, 1); glViewport(0, 0, w_, h_); }

void Renderer::clearModel() {
    for (auto& m : meshes_) {
        glDeleteBuffers(1, &m.vbo);
        for (auto& s : m.subs) { glDeleteVertexArrays(1, &s.vao); glDeleteBuffers(1, &s.ebo); }
    }
    meshes_.clear();
    skeletons_.clear(); palettes_.clear(); clipCount_ = 0; clip_ = 0; timeMs_ = 0;
    if (!textures_.empty()) glDeleteTextures((GLsizei)textures_.size(), textures_.data());
    textures_.clear();
}

void Renderer::setModel(const Model& m) {
    clearModel();
    textures_.resize(m.images.size());
    if (!textures_.empty()) glGenTextures((GLsizei)textures_.size(), textures_.data());
    for (size_t i = 0; i < m.images.size(); i++) {
        const Image& im = m.images[i];
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    for (const Mesh& me : m.meshes) {
        const size_t nv = me.pos.size() / 3;
        const bool skinned = me.skeleton >= 0 && me.boneIdx.size() == nv * 4 && me.boneW.size() == nv * 4;
        std::vector<float> vb(nv * 16, 0.f);               // pos3 nrm3 uv2 boneIdx4 boneW4 intercalado (64 bytes)
        for (size_t v = 0; v < nv; v++) {
            std::memcpy(&vb[v * 16], &me.pos[v * 3], 12);
            std::memcpy(&vb[v * 16 + 3], &me.nrm[v * 3], 12);
            if (!me.uv.empty()) std::memcpy(&vb[v * 16 + 6], &me.uv[v * 2], 8);
            if (skinned) {
                std::memcpy(&vb[v * 16 + 8], &me.boneIdx[v * 4], 16);
                std::memcpy(&vb[v * 16 + 12], &me.boneW[v * 4], 16);
            }
        }
        GpuMesh gm;
        gm.skel = skinned ? me.skeleton : -1;
        glGenBuffers(1, &gm.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vb.size() * sizeof(float)), vb.data(), GL_STATIC_DRAW);
        for (const SubMesh& sm : me.subs) {
            Sub s;
            glGenVertexArrays(1, &s.vao); glGenBuffers(1, &s.ebo);
            glBindVertexArray(s.vao);
            glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 64, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 64, (void*)12);
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 64, (void*)24);
            if (skinned) {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 64, (void*)32);
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 64, (void*)48);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sm.indices.size() * 4), sm.indices.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);
            s.count = (GLsizei)sm.indices.size();
            if (sm.image >= 0 && (size_t)sm.image < textures_.size()) { s.tex = textures_[sm.image]; s.blend = m.images[sm.image].hasAlpha; }
            gm.subs.push_back(s);
        }
        meshes_.push_back(std::move(gm));
    }
    skeletons_ = m.skeletons;
    palettes_.assign(skeletons_.size(), {});
    for (auto& s : skeletons_) clipCount_ = std::max<int>(clipCount_, (int)s.clips.size());
    clip_ = 0; timeMs_ = 0; playing_ = true; last_ = std::chrono::steady_clock::now();
    float d[3];
    for (int k = 0; k < 3; k++) { center_[k] = 0.5f * (m.bmin[k] + m.bmax[k]); d[k] = m.bmax[k] - m.bmin[k]; }
    radius_ = std::max(0.5f * std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]), 0.01f);
    dist_ = radius_ * 2.4f; yaw_ = 0.6f; pitch_ = 0.25f;
}

void Renderer::orbit(float dx, float dy) {
    yaw_ -= dx * 0.008f;
    pitch_ = std::max(-1.5f, std::min(1.5f, pitch_ + dy * 0.008f));
}
std::string Renderer::animInfo() const {
    if (clipCount_ == 0) return "sin animaciones";
    int dur = 0;
    for (auto& s : skeletons_) if ((size_t)clip_ < s.clips.size()) dur = std::max(dur, s.clips[clip_].duration);
    return "clip " + std::to_string(clip_ + 1) + "/" + std::to_string(clipCount_) + " - " + std::to_string(dur) + " ms - " +
           (playing_ ? "reproduciendo" : "pausa");
}
std::string Renderer::nextClip(int delta) {
    if (clipCount_ > 0) { clip_ = ((clip_ + delta) % clipCount_ + clipCount_) % clipCount_; timeMs_ = 0; }
    return animInfo();
}
std::string Renderer::togglePlay() { playing_ = !playing_; last_ = std::chrono::steady_clock::now(); return animInfo(); }

void Renderer::zoom(float f) { dist_ = std::max(radius_ * 0.2f, std::min(radius_ * 12.f, dist_ / f)); }

void Renderer::draw() {
    glClearColor(0.07f, 0.08f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!prog_ || meshes_.empty()) return;
    {   // avanzar la animacion y calcular las matrices de hueso
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
        if (playing_) timeMs_ += std::min(dt, 100.0);
        for (size_t s = 0; s < skeletons_.size(); s++)
            evalPalette(skeletons_[s], (size_t)clip_ < skeletons_[s].clips.size() ? clip_ : -1, timeMs_, palettes_[s]);
    }
    const float cp = std::cos(pitch_), sp = std::sin(pitch_);
    const float eye[3] = {center_[0] + dist_ * cp * std::sin(yaw_), center_[1] + dist_ * sp, center_[2] + dist_ * cp * std::cos(yaw_)};
    const float up[3] = {0, 1, 0};
    M4 view = lookAt(eye, center_, up);
    M4 proj = perspective(0.8f, (float)w_ / (float)h_, std::max(dist_ * 0.01f, 0.01f), dist_ + radius_ * 20.f);
    M4 mvp = mul(proj, view);
    glUseProgram(prog_);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(uMV_, 1, GL_FALSE, view.m);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    for (int pass = 0; pass < 2; pass++) {                 // 0: opacos, 1: con alfa
        if (pass == 1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
        for (auto& m : meshes_)
            for (auto& s : m.subs) {
                if (s.blend != (pass == 1)) continue;
                glUniform1i(uHasTex_, s.tex ? 1 : 0);
                glUniform1i(uSkinned_, m.skel >= 0 ? 1 : 0);
                if (m.skel >= 0) glUniformMatrix4fv(uBones_, (GLsizei)(palettes_[m.skel].size() / 16), GL_FALSE, palettes_[m.skel].data());
                if (s.tex) glBindTexture(GL_TEXTURE_2D, s.tex);
                glBindVertexArray(s.vao);
                glDrawElements(GL_TRIANGLES, s.count, GL_UNSIGNED_INT, nullptr);
            }
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindVertexArray(0);
}

}  // namespace ds
