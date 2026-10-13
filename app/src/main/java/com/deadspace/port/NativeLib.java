package com.deadspace.port;

/** Puente JNI hacia libdsport.so (C++: lector M3G, renderizador GLES 3). */
public final class NativeLib {
    static { System.loadLibrary("dsport"); }

    private NativeLib() {}

    /** root = carpeta "published" del juego en el movil. */
    public static native void nativeInit(String root);
    public static native boolean nativeSurfaceCreated();
    public static native void nativeResize(int w, int h);
    public static native void nativeDraw();
    public static native void nativeOrbit(float dx, float dy);
    public static native void nativeZoom(float factor);
    /** Debe llamarse desde el hilo GL. Devuelve el resumen del modelo o "ERROR: ...". */
    public static native String nativeLoad(String path);
}
