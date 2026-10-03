package com.aresx.neonrush;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.widget.FrameLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private NeonSurface surface;
    private TextView scoreView;
    private TextView healthView;
    private TextView statusView;
    private final Handler uiHandler = new Handler(Looper.getMainLooper());

    private final Runnable uiUpdater = new Runnable() {
        @Override public void run() {
            if (surface != null) {
                scoreView.setText(String.format("SCORE %06d", surface.getScore()));
                healthView.setText(String.format("HP %03d", surface.getHealth()));
                if (surface.isGameOver()) {
                    statusView.setText("SYSTEM FAILURE\nTAP ANYWHERE TO RESTART");
                    statusView.setVisibility(TextView.VISIBLE);
                } else {
                    statusView.setText("");
                    statusView.setVisibility(TextView.GONE);
                }
            }
            uiHandler.postDelayed(this, 80);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().setFlags(
                android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN,
                android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN
        );

        FrameLayout root = new FrameLayout(this);

        surface = new NeonSurface();
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));

        scoreView = hudText(22f);
        scoreView.setText("SCORE 000000");
        FrameLayout.LayoutParams scoreLp = new FrameLayout.LayoutParams(-2, -2);
        scoreLp.leftMargin = 30;
        scoreLp.topMargin = 20;
        root.addView(scoreView, scoreLp);

        healthView = hudText(18f);
        healthView.setText("HP 100");
        FrameLayout.LayoutParams healthLp = new FrameLayout.LayoutParams(-2, -2);
        healthLp.leftMargin = 30;
        healthLp.topMargin = 58;
        root.addView(healthView, healthLp);

        TextView hintView = new TextView(this);
        hintView.setTextColor(Color.LTGRAY);
        hintView.setTextSize(12f);
        hintView.setText("LEFT: MOVE     RIGHT: FIRE\nHITS DAMAGE ENEMIES     CONTACT DAMAGES YOU");
        FrameLayout.LayoutParams hintLp = new FrameLayout.LayoutParams(-2, -2);
        hintLp.leftMargin = 30;
        hintLp.topMargin = 86;
        root.addView(hintView, hintLp);

        statusView = hudText(24f);
        statusView.setTextColor(Color.WHITE);
        statusView.setGravity(android.view.Gravity.CENTER);
        statusView.setText("SYSTEM FAILURE\nTAP ANYWHERE TO RESTART");
        statusView.setVisibility(TextView.GONE);
        FrameLayout.LayoutParams statusLp = new FrameLayout.LayoutParams(-2, -2);
        statusLp.gravity = android.view.Gravity.CENTER;
        root.addView(statusView, statusLp);

        setContentView(root);
        uiHandler.post(uiUpdater);
    }

    private TextView hudText(float size) {
        TextView tv = new TextView(this);
        tv.setTextColor(Color.WHITE);
        tv.setTextSize(size);
        tv.setTypeface(Typeface.DEFAULT_BOLD);
        tv.setShadowLayer(10f, 0f, 0f, Color.CYAN);
        return tv;
    }

    @Override
    protected void onPause() {
        if (surface != null) surface.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (surface != null) surface.onResume();
    }

    @Override
    protected void onDestroy() {
        uiHandler.removeCallbacks(uiUpdater);
        if (surface != null) surface.releaseNative();
        super.onDestroy();
    }

    private final class NeonSurface extends GLSurfaceView {
        private final Renderer renderer;

        NeonSurface() {
            super(MainActivity.this);
            setEGLContextClientVersion(2);
            renderer = new Renderer();
            setRenderer(renderer);
            setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
            setFocusable(true);
            requestFocus();
        }

        int getScore() {
            return nativeGetScore();
        }

        int getHealth() {
            return nativeGetHealth();
        }

        boolean isGameOver() {
            return nativeIsGameOver();
        }

        void releaseNative() {
            queueEvent(NeonSurface::nativeShutdown);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            final int action = event.getActionMasked();
            final int pointerIndex = event.getActionIndex();
            final int width = getWidth();
            final int height = getHeight();

            if (action == MotionEvent.ACTION_MOVE) {
                // ACTION_MOVE may contain multiple pointers. Forward all of them
                // so the native game can keep movement and fire held at once.
                final int count = event.getPointerCount();
                for (int i = 0; i < count; i++) {
                    final int pointerId = event.getPointerId(i);
                    final float x = event.getX(i);
                    final float y = event.getY(i);
                    queueEvent(() -> nativeTouch(action, x, y, width, height, pointerId));
                }
            } else {
                final int pointerId = event.getPointerId(pointerIndex);
                final float x = event.getX(pointerIndex);
                final float y = event.getY(pointerIndex);
                queueEvent(() -> nativeTouch(action, x, y, width, height, pointerId));
            }
            return true;
        }

        private final class Renderer implements GLSurfaceView.Renderer {
            @Override public void onSurfaceCreated(
                    javax.microedition.khronos.egl.EGLConfig config) {
                nativeInit();
            }

            @Override public void onSurfaceChanged(int width, int height) {
                nativeResize(width, height);
            }

            @Override public void onDrawFrame(
                    javax.microedition.khronos.opengles.GL10 gl) {
                nativeStep();
            }
        }

        private static native void nativeInit();
        private static native void nativeShutdown();
        private static native void nativeResize(int width, int height);
        private static native void nativeStep();
        private static native void nativeTouch(int action, float x, float y, int width, int height, int pointerId);
        private static native int nativeGetScore();
        private static native int nativeGetHealth();
        private static native boolean nativeIsGameOver();
    }

    static {
        System.loadLibrary("neonrush");
    }
}
