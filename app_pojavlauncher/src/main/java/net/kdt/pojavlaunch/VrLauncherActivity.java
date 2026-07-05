package net.kdt.pojavlaunch;

import android.graphics.Color;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.TextView;

import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

public class VrLauncherActivity extends LauncherActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enableVrShell();
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
    }
}
