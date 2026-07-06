package net.kdt.pojavlaunch;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import net.kdt.pojavlaunch.prefs.LauncherPreferences;
import net.kdt.pojavlaunch.tasks.AsyncAssetManager;

public class VrLauncherActivity extends LauncherActivity {
    private static final int REQUEST_STORAGE_REQUEST_CODE = 2;
    static {
        try {
            System.loadLibrary("amethyst_vr");
        } catch (UnsatisfiedLinkError e) {
            // library may not be available on non-native builds
        }
    }

    // JNI bridge
    private native boolean nativeInitOpenXR();
    private native boolean nativeStartOpenXRSession();
    private native void nativeStopOpenXRSession();

    private boolean openxrAvailable = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (needsStoragePermission()) {
            requestStoragePermission();
            return;
        }

        initializeRuntime();
        super.onCreate(savedInstanceState);
        enableVrShell();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_REQUEST_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                initializeRuntime();
                super.onCreate(null);
                enableVrShell();
            } else {
                Toast.makeText(this, R.string.toast_permission_denied, Toast.LENGTH_LONG).show();
                requestStoragePermission();
            }
        }
    }

    private boolean needsStoragePermission() {
        return Build.VERSION.SDK_INT >= 23 && Build.VERSION.SDK_INT < 29
                && !isStorageAllowed(this);
    }

    private boolean isStorageAllowed(android.content.Context context) {
        int result1 = ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE);
        int result2 = ContextCompat.checkSelfPermission(context, Manifest.permission.READ_EXTERNAL_STORAGE);
        return result1 == PackageManager.PERMISSION_GRANTED && result2 == PackageManager.PERMISSION_GRANTED;
    }

    private void requestStoragePermission() {
        ActivityCompat.requestPermissions(this, new String[]{
                Manifest.permission.WRITE_EXTERNAL_STORAGE,
                Manifest.permission.READ_EXTERNAL_STORAGE
        }, REQUEST_STORAGE_REQUEST_CODE);
    }

    private void initializeRuntime() {
        if (!Tools.checkStorageRoot(this)) {
            startActivity(new Intent(this, MissingStorageActivity.class));
            finish();
            return;
        }

        LauncherPreferences.loadPreferences(this);
        AsyncAssetManager.unpackComponents(this);
        AsyncAssetManager.unpackSingleFiles(this);
    }

    private void enableVrShell() {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        window.setBackgroundDrawable(null);

        View decorView = window.getDecorView();
        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        );

        WindowInsetsControllerCompat controller = new WindowInsetsControllerCompat(window, decorView);
        controller.hide(WindowInsetsCompat.Type.systemBars());
        controller.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);

        ViewGroup contentRoot = findViewById(android.R.id.content);
        if (contentRoot == null) return;

        View contentView = contentRoot.getChildAt(0);
        if (contentView != null) {
            contentView.setScaleX(1.06f);
            contentView.setScaleY(1.06f);
            contentView.setRotationX(4f);
            contentView.setRotationY(-8f);
            contentView.setTranslationZ(8f);
            contentView.setBackgroundColor(Color.BLACK);
        }

        TextView vrBadge = new TextView(this);
        vrBadge.setText("OpenXR");
        vrBadge.setTextColor(Color.WHITE);
        vrBadge.setTextSize(20f);
        vrBadge.setPadding(24, 16, 24, 16);
        vrBadge.setBackgroundColor(Color.argb(180, 0, 0, 0));
        vrBadge.setAlpha(0.9f);

        ViewGroup.LayoutParams params = new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        );

        contentRoot.addView(vrBadge, params);
        vrBadge.setX(24f);
        vrBadge.setY(24f);

        // Try to initialize OpenXR native bridge; if available, start the session.
        new Thread(() -> {
            try {
                openxrAvailable = nativeInitOpenXR();
                if (openxrAvailable) {
                    boolean started = nativeStartOpenXRSession();
                    if (!started) openxrAvailable = false;
                }
            } catch (Throwable t) {
                // ignore - native bridge may be missing on some environments
            }
        }).start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try {
            if (openxrAvailable) nativeStopOpenXRSession();
        } catch (Throwable ignored) {}
    }
}
