// SPDX-License-Identifier: AGPL-3.0-or-later
package cc.fluxus.racket;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

// Same shape as the engine-only APK spike, with one addition: the native side
// needs a real filesystem path for the Racket runtime, because Racket opens its
// boot files and collects with ordinary file I/O and cannot read from inside the
// APK. getFilesDir() is that path; the build script populates it.
public class MainActivity extends Activity {

    static { System.loadLibrary("fluxusracket"); }

    private static native void nativeInit(String racketRoot);
    private static native void nativeResize(int w, int h);
    private static native void nativeDraw();

    private GLSurfaceView view;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        final String root = getFilesDir().getAbsolutePath();
        view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(3);
        view.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
        view.setRenderer(new GLSurfaceView.Renderer() {
            public void onSurfaceCreated(GL10 gl, EGLConfig cfg) { nativeInit(root); }
            public void onSurfaceChanged(GL10 gl, int w, int h)  { nativeResize(w, h); }
            public void onDrawFrame(GL10 gl)                      { nativeDraw(); }
        });
        setContentView(view);
    }

    @Override protected void onPause()  { super.onPause();  view.onPause(); }
    @Override protected void onResume() { super.onResume(); view.onResume(); }
}
