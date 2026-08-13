package com.qoder.imguibuilder;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLES3JNIView extends GLSurfaceView implements GLSurfaceView.Renderer {

    public GLES3JNIView(Context context) {
        super(context);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setEGLContextClientVersion(3);
        setRenderer(this);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        step();
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        resize(width, height);
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        init(getHolder().getSurface());
    }

    @Override
    protected void onDetachedFromWindow() {
        imgui_Shutdown();
        super.onDetachedFromWindow();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_UP) {
            MotionEventClick(action != MotionEvent.ACTION_UP, event.getX(), event.getY());
            return true;
        }
        return super.onTouchEvent(event);
    }

    public static native void init(android.view.Surface surface);
    public static native void resize(int width, int height);
    public static native void step();
    public static native void imgui_Shutdown();
    public static native void MotionEventClick(boolean down, float x, float y);
    public static native void nativeSetSavePath(String path);
    public static native String getCode();
    public static native String getSaveContent();
    public static native void loadContent(String content);
}
