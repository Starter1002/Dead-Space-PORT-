package com.deadspace.port;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Visor de modelos .m3g originales de Dead Space (iOS) en Android arm64.
 * Un dedo: orbitar. Pellizco: zoom. Botones: modelo anterior / siguiente.
 */
public class MainActivity extends Activity {
    private static final String MODELS_DIR = "published/models";

    private GLSurfaceView glView;
    private TextView infoView, nameView;
    private final List<String> models = new ArrayList<>();
    private int current = 0;
    private float lastX, lastY;
    private boolean dragging = false;
    private ScaleGestureDetector scale;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        NativeLib.nativeInit(getAssets());
        try {
            String[] names = getAssets().list(MODELS_DIR);
            if (names != null) for (String n : names) if (n.endsWith(".m3g") && !n.startsWith("texture_")) models.add(n);   // texture_* son solo imagenes
        } catch (IOException ignored) { }
        Collections.sort(models);
        int start = models.indexOf("isaac.m3g");
        current = start >= 0 ? start : 0;

        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
        glView.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 gl, EGLConfig c) {
                if (!NativeLib.nativeSurfaceCreated()) { showInfo("ERROR: shaders GLES 3 no compilan"); return; }
                loadCurrentOnGlThread();
            }
            @Override public void onSurfaceChanged(GL10 gl, int w, int h) { NativeLib.nativeResize(w, h); }
            @Override public void onDrawFrame(GL10 gl) { NativeLib.nativeDraw(); }
        });

        scale = new ScaleGestureDetector(this, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector d) {
                final float f = d.getScaleFactor();
                glView.queueEvent(() -> NativeLib.nativeZoom(f));
                return true;
            }
        });
        glView.setOnTouchListener((v, e) -> {
            scale.onTouchEvent(e);
            if (scale.isInProgress() || e.getPointerCount() > 1) { dragging = false; return true; }
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: lastX = e.getX(); lastY = e.getY(); dragging = true; break;
                case MotionEvent.ACTION_MOVE:
                    if (dragging) {
                        final float dx = e.getX() - lastX, dy = e.getY() - lastY;
                        lastX = e.getX(); lastY = e.getY();
                        glView.queueEvent(() -> NativeLib.nativeOrbit(dx, dy));
                    } else { lastX = e.getX(); lastY = e.getY(); dragging = true; }
                    break;
                default: dragging = false;
            }
            return true;
        });

        FrameLayout root = new FrameLayout(this);
        root.addView(glView);

        infoView = new TextView(this);
        infoView.setTextColor(Color.WHITE); infoView.setTextSize(11); infoView.setPadding(16, 16, 16, 16);
        infoView.setBackgroundColor(0x88000000);
        root.addView(infoView, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL); bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(0x88000000);
        Button prev = new Button(this); prev.setText("<");
        Button next = new Button(this); next.setText(">");
        nameView = new TextView(this);
        nameView.setTextColor(Color.WHITE); nameView.setGravity(Gravity.CENTER); nameView.setTextSize(15);
        bar.addView(prev, new LinearLayout.LayoutParams(-2, -2));
        bar.addView(nameView, new LinearLayout.LayoutParams(0, -2, 1f));
        bar.addView(next, new LinearLayout.LayoutParams(-2, -2));
        root.addView(bar, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
        setContentView(root);

        View.OnClickListener nav = v -> {
            if (models.isEmpty()) return;
            current = (current + (v == next ? 1 : models.size() - 1)) % models.size();
            glView.queueEvent(this::loadCurrentOnGlThread);
        };
        prev.setOnClickListener(nav); next.setOnClickListener(nav);

        if (models.isEmpty()) {
            showInfo("No hay modelos.\nCopia la carpeta 'published' del IPA a app/src/main/assets/ (ver README).");
        }
    }

    private void loadCurrentOnGlThread() {
        if (models.isEmpty()) return;
        final String name = models.get(current);
        final String info = NativeLib.nativeLoad(MODELS_DIR.substring("published/".length()) + "/" + name);
        runOnUiThread(() -> { nameView.setText((current + 1) + "/" + models.size() + "  " + name); infoView.setText(info); });
    }

    private void showInfo(final String s) { runOnUiThread(() -> infoView.setText(s)); }

    @Override protected void onPause() { super.onPause(); glView.onPause(); }
    @Override protected void onResume() { super.onResume(); glView.onResume(); }
}
