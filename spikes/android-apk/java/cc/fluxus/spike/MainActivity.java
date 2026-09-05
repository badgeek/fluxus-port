// SPDX-License-Identifier: AGPL-3.0-or-later
package cc.fluxus.spike;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

// The whole Java side of the spike. A GLSurfaceView owns the EGL context and
// calls us back on its own render thread; everything else is C++.
//
// This is the shape openFrameworks uses too (see ofxAndroid's OFGLSurfaceView) —
// GLSurfaceView rather than NativeActivity, because it hands you a working EGL
// context and a render thread without any of the lifecycle plumbing.
public class MainActivity extends Activity {

    static { System.loadLibrary("fluxusspike"); }

    private static native void nativeInit();
    private static native void nativeResize(int w, int h);
    private static native void nativeDraw();

    private GLSurfaceView view;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(3);
        view.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
        view.setRenderer(new GLSurfaceView.Renderer() {
            public void onSurfaceCreated(GL10 gl, EGLConfig cfg) { nativeInit(); }
            public void onSurfaceChanged(GL10 gl, int w, int h)  { nativeResize(w, h); }
            public void onDrawFrame(GL10 gl)                      { nativeDraw(); }
        });
        setContentView(view);
    }

    @Override protected void onPause()  { super.onPause();  view.onPause(); }
    @Override protected void onResume() { super.onResume(); view.onResume(); }
}
