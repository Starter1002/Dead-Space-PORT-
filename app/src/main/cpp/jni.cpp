#include "renderer.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <jni.h>
#include <string>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "DSPort", __VA_ARGS__)

namespace {
AAssetManager* gAssets = nullptr;
ds::Renderer gRenderer;
std::string kRoot = "published/";    // los .m3g originales van en app/src/main/assets/published/

bool readAsset(const std::string& rel, std::vector<uint8_t>& out) {
    if (!gAssets) return false;
    AAsset* a = AAssetManager_open(gAssets, (kRoot + rel).c_str(), AASSET_MODE_STREAMING);
    if (!a) return false;
    off_t len = AAsset_getLength(a);
    out.resize((size_t)len);
    size_t got = 0;
    while (got < out.size()) {
        int r = AAsset_read(a, out.data() + got, out.size() - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    AAsset_close(a);
    return got == out.size();
}

jstring toJ(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_deadspace_port_NativeLib_nativeInit(JNIEnv* env, jclass, jobject assetManager) {
    gAssets = AAssetManager_fromJava(env, assetManager);
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
    if (!ds::loadModel(readAsset, path, model, err)) {
        gRenderer.clearModel();
        LOG("error cargando %s: %s", path.c_str(), err.c_str());
        return toJ(env, "ERROR: " + err);
    }
    gRenderer.setModel(model);
    LOG("%s", model.log.c_str());
    return toJ(env, model.log);
}

}  // extern "C"
