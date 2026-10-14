#include "renderer.h"
#include <android/log.h>
#include <jni.h>
#include <cstdio>
#include <string>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "DSPort", __VA_ARGS__)

namespace {
ds::Renderer gRenderer;
std::string gRoot;    // carpeta "published" del juego en el movil, p. ej. /storage/emulated/0/DeadSpace/published

bool readFile(const std::string& rel, std::vector<uint8_t>& out) {
    FILE* f = fopen((gRoot + "/" + rel).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

jstring toJ(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeInit(JNIEnv* env, jclass, jstring root) {
    const char* c = env->GetStringUTFChars(root, nullptr);
    gRoot = c;
    env->ReleaseStringUTFChars(root, c);
    LOG("raiz de assets: %s", gRoot.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_deadspace_port_NativeLib_nativeSurfaceCreated(JNIEnv*, jclass) {
    return gRenderer.init() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeResize(JNIEnv*, jclass, jint w, jint h) {
    gRenderer.resize(w, h);
}

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeDraw(JNIEnv*, jclass) { gRenderer.draw(); }

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeOrbit(JNIEnv*, jclass, jfloat dx, jfloat dy) {
    gRenderer.orbit(dx, dy);
}

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeZoom(JNIEnv*, jclass, jfloat f) { gRenderer.zoom(f); }

// Carga un modelo (ruta relativa a published/). Devuelve el resumen o el error. Hilo GL.
JNIEXPORT jstring JNICALL Java_com_deadspace_port_NativeLib_nativeLoad(JNIEnv* env, jclass, jstring jpath) {
    const char* c = env->GetStringUTFChars(jpath, nullptr);
    std::string path = c;
    env->ReleaseStringUTFChars(jpath, c);
    ds::Model model; std::string err;
    if (!ds::loadModel(readFile, path, model, err)) {
        gRenderer.clearModel();
        LOG("error cargando %s: %s", path.c_str(), err.c_str());
        return toJ(env, "ERROR: " + err);
    }
    gRenderer.setModel(model);
    LOG("%s", model.log.c_str());
    return toJ(env, model.log);
}

JNIEXPORT jstring JNICALL Java_com_deadspace_port_NativeLib_nativeAnimInfo(JNIEnv* env, jclass) {
    return toJ(env, gRenderer.animInfo());
}

JNIEXPORT jstring JNICALL Java_com_deadspace_port_NativeLib_nativeNextClip(JNIEnv* env, jclass, jint delta) {
    return toJ(env, gRenderer.nextClip(delta));
}

JNIEXPORT jstring JNICALL Java_com_deadspace_port_NativeLib_nativeTogglePlay(JNIEnv* env, jclass) {
    return toJ(env, gRenderer.togglePlay());
}

}  // extern "C"
