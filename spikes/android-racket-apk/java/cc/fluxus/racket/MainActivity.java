// SPDX-License-Identifier: AGPL-3.0-or-later
package cc.fluxus.racket;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
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

    // The Racket runtime ships as ONE compressed asset rather than a tree of
    // files. Racket opens its boot files and collects with ordinary file I/O and
    // cannot read from inside an APK, so it has to land on the filesystem — and
    // the collects alone are thousands of small files, which AssetManager copies
    // far more slowly than a single ZipInputStream pass.
    //
    // Guarded by a marker file: extraction happens on first launch and after an
    // upgrade, never on an ordinary start.
    private static final String RUNTIME_ASSET = "runtime.zip";
    private static final String MARKER = ".runtime-" + BuildId.ID;

    private void extractRuntimeIfNeeded(File filesDir) {
        File marker = new File(filesDir, MARKER);
        if (marker.exists()) return;

        long t0 = System.currentTimeMillis();
        Log.i("fluxus", "extracting the Racket runtime (first launch)...");
        try (InputStream raw = getAssets().open(RUNTIME_ASSET);
             ZipInputStream zin = new ZipInputStream(raw)) {
            byte[] buf = new byte[64 * 1024];
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                File out = new File(filesDir, e.getName());
                if (e.isDirectory()) { out.mkdirs(); continue; }
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zin.read(buf)) > 0) os.write(buf, 0, n);
                }
            }
            marker.createNewFile();
            Log.i("fluxus", "runtime extracted in "
                            + (System.currentTimeMillis() - t0) + " ms");
        } catch (Exception ex) {
            // Deliberately not fatal here: the native side reports a missing
            // runtime far more precisely than a stack trace at this point would.
            Log.e("fluxus", "runtime extraction failed: " + ex);
        }
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        extractRuntimeIfNeeded(getFilesDir());
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
