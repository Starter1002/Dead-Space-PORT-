package com.deadspace.port;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Visor de modelos .m3g originales de Dead Space (iOS) en Android arm64.
 * Los archivos del juego NO van dentro del APK: se leen de /sdcard/DeadSpace/published/.
 * Un dedo: orbitar. Pellizco: zoom. Botones: modelo anterior / siguiente.
 */
public class MainActivity extends Activity {
    private static final String ROOT_REL = "DeadSpace/published";

    private GLSurfaceView glView;
    private TextView infoView, nameView;
    private Button permBtn;
    private final List<String> models = new ArrayList<>();
    private int current = 0;
    private volatile boolean surfaceReady = false;
    private String root;
    private float lastX, lastY;
    private boolean dragging = false;
    private ScaleGestureDetector scale;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        root = new File(Environment.getExternalStorageDirectory(), ROOT_REL).getAbsolutePath();
        NativeLib.nativeInit(root);

        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
        glView.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 gl, EGLConfig c) {
                if (!NativeLib.nativeSurfaceCreated()) { showInfo("ERROR: shaders GLES 3 no compilan"); return; }
                surfaceReady = true;
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

        FrameLayout rootView = new FrameLayout(this);
        rootView.addView(glView);

        infoView = new TextView(this);
        infoView.setTextColor(Color.WHITE); infoView.setTextSize(11); infoView.setPadding(16, 16, 16, 16);
        infoView.setBackgroundColor(0x88000000);
        rootView.addView(infoView, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP));

        permBtn = new Button(this);
        permBtn.setText("Conceder acceso a archivos");
        permBtn.setVisibility(View.GONE);
        permBtn.setOnClickListener(v -> requestAccess());
        rootView.addView(permBtn, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL); bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(0x88000000);
        final Button prev = new Button(this); prev.setText("<");
        final Button next = new Button(this); next.setText(">");
        nameView = new TextView(this);
        nameView.setTextColor(Color.WHITE); nameView.setGravity(Gravity.CENTER); nameView.setTextSize(15);
        bar.addView(prev, new LinearLayout.LayoutParams(-2, -2));
        bar.addView(nameView, new LinearLayout.LayoutParams(0, -2, 1f));
        bar.addView(next, new LinearLayout.LayoutParams(-2, -2));
        rootView.addView(bar, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
        setContentView(rootView);

        View.OnClickListener nav = v -> {
            if (models.isEmpty()) return;
            current = (current + (v == next ? 1 : models.size() - 1)) % models.size();
            glView.queueEvent(this::loadCurrentOnGlThread);
        };
        prev.setOnClickListener(nav); next.setOnClickListener(nav);
    }

    // ------------------------------------------------------------------ permisos y lista de modelos
    private boolean hasAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        refreshModels();
    }

    private void refreshModels() {
        final boolean wasEmpty = models.isEmpty();
        String keep = models.isEmpty() ? "isaac.m3g" : models.get(current);
        models.clear();
        if (!hasAccess()) {
            permBtn.setVisibility(View.VISIBLE);
            showInfo("Falta el permiso de acceso a archivos.\nPulsa el boton y activalo para esta app.");
            return;
        }
        permBtn.setVisibility(View.GONE);
        String[] names = new File(root, "models").list();
        if (names != null)
            for (String n : names)
                if (n.endsWith(".m3g") && !n.startsWith("texture_")) models.add(n);   // texture_* son solo imagenes
        Collections.sort(models);
        if (models.isEmpty()) {
            showInfo("No hay modelos en:\n" + root + "/models\nCopia ahi la carpeta 'published' del IPA (ver README).");
            return;
        }
        current = Math.max(0, models.indexOf(keep));
        if (wasEmpty && surfaceReady) glView.queueEvent(this::loadCurrentOnGlThread);
    }

    private void loadCurrentOnGlThread() {
        if (models.isEmpty()) return;
        final String name = models.get(current);
        final String info = NativeLib.nativeLoad("models/" + name);
        final int pos = current + 1, total = models.size();
        runOnUiThread(() -> { nameView.setText(pos + "/" + total + "  " + name); infoView.setText(info); });
    }

    private void showInfo(final String s) { runOnUiThread(() -> infoView.setText(s)); }

    @Override protected void onPause() { super.onPause(); glView.onPause(); }
    @Override protected void onResume() { super.onResume(); glView.onResume(); refreshModels(); }
}
