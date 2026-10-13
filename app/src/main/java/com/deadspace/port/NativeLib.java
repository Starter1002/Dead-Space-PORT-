package com.deadspace.port;

import android.content.res.AssetManager;

/** Puente JNI hacia libdsport.so (C++: lector M3G, renderizador GLES 3). */
public final class NativeLib {
    static { System.loadLibrary("dsport"); }

    private NativeLib() {}

    public static native void nativeInit(AssetManager assets);
    public static native boolean nativeSurfaceCreated();
    public static native void nativeResize(int w, int h);
    public static native void nativeDraw();
    public static native void nativeOrbit(float dx, float dy);
    public static native void nativeZoom(float factor);
    /** Debe llamarse desde el hilo GL. Devuelve el resumen del modelo o "ERROR: ...". */
    public static native String nativeLoad(String path);
}
