// SPDX-License-Identifier: AGPL-3.0-or-later
package cc.fluxus.racket;

import android.app.Activity;
import android.graphics.Typeface;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
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
    private static native void nativeTouch(int action, int pointers,
                                           float x0, float y0, float x1, float y1);
    private static native String nativeGetSketch();
    private static native String nativeGetError();
    private static native void   nativeSetSketch(String source);

    private static final int MATCH = LinearLayout.LayoutParams.MATCH_PARENT;
    private static final int WRAP  = LinearLayout.LayoutParams.WRAP_CONTENT;

    private GLSurfaceView view;
    private LinearLayout  panel;
    private EditText      editor;
    private TextView      status;

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

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    private void extractRuntimeIfNeeded(File filesDir) {
        File marker = new File(filesDir, MARKER);
        if (marker.exists()) return;

        long t0 = System.currentTimeMillis();
        Log.i("fluxus", "extracting the Racket runtime (first launch)...");
        // Wipe the old tree first. Unzipping over it only ADDS and OVERWRITES,
        // so a file the new runtime no longer ships stays behind — which made a
        // trimmed runtime test green on an upgrade and crash on a clean
        // install, because the upgrade was still running the old files.
        // sketch.scm is deliberately not in this list: it is the user's.
        for (String dir : new String[] { "lib", "share", "etc", "fluxus-lib" })
            deleteTree(new File(filesDir, dir));
        File[] old = filesDir.listFiles();
        if (old != null) for (File f : old)
            if (f.getName().startsWith(".runtime-")) f.delete();
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
        // Drag to orbit, pinch to zoom. The gesture arithmetic is on the native
        // side; this only classifies the event and forwards the coordinates, so
        // there is one place to change how the camera responds.
        view.setOnTouchListener(new View.OnTouchListener() {
            private long lastDown = 0;

            public boolean onTouch(View v, MotionEvent e) {
                int action;
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_MOVE:
                        action = 1; break;
                    case MotionEvent.ACTION_DOWN: {
                        // Double tap resets the camera. There is no other way
                        // back once you have orbited off into nothing, and no
                        // UI to put a button on.
                        long now = e.getEventTime();
                        boolean twice = now - lastDown < 300;
                        lastDown = now;
                        if (twice) { nativeTouch(3, 0, 0f, 0f, 0f, 0f); return true; }
                        action = 0; break;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        action = 2; break;
                    default:
                        // DOWN, POINTER_DOWN, POINTER_UP: the finger count is
                        // about to change, so the native side rebases instead
                        // of reading the jump as a drag.
                        action = 0; break;
                }
                int n = e.getPointerCount();
                float x1 = n > 1 ? e.getX(1) : 0f, y1 = n > 1 ? e.getY(1) : 0f;
                nativeTouch(action, n, e.getX(0), e.getY(0), x1, y1);
                return true;
            }
        });
        FrameLayout content = new FrameLayout(this);
        content.addView(view, new FrameLayout.LayoutParams(MATCH, MATCH));
        // Half width, not full: live coding means watching the scene change as
        // you edit it. The activity is landscape-locked, so half the width is
        // always the sensible half.
        int half = getResources().getDisplayMetrics().widthPixels / 2;
        content.addView(buildEditor(), new FrameLayout.LayoutParams(half, MATCH));
        content.addView(buildToggle());
        setContentView(content);
    }

    // The editor is an ordinary Android EditText, not a port of either fluxus
    // editor. The hard part of editing on a phone is the IME — soft keyboard,
    // selection handles, clipboard, autocorrect — and the platform already has
    // it. fluxus's own GLEditor would have meant porting glGenLists/glCallList
    // and ~100 immediate-mode calls to GLES and STILL having no keyboard;
    // JUCE's TextEditor would have meant bringing JUCE to this APK.
    //
    // Run sends the text through nativeSetSketch, which is the same fluxctl
    // buffer `cli/fluxus load` writes. Typing on the phone and loading from a
    // laptop are the same operation downstream.
    private View buildEditor() {
        panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xE60A0A0E);
        panel.setVisibility(View.GONE);

        editor = new EditText(this);
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setTextSize(13f);
        editor.setTextColor(0xFFAEFFC8);
        editor.setBackgroundColor(0x00000000);
        editor.setGravity(Gravity.TOP | Gravity.START);
        // NO_SUGGESTIONS matters: autocorrect on a page of Scheme turns
        // (build-cube) into prose.
        editor.setInputType(InputType.TYPE_CLASS_TEXT
                          | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                          | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setHorizontallyScrolling(false);
        // In landscape the IME defaults to taking over the WHOLE screen with its
        // own extracted text box — the sketch, the scene and the run button all
        // vanish behind a grey page with a DONE button, which is the opposite of
        // live coding. These two flags keep the keyboard docked at the bottom.
        editor.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI
                           | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        panel.addView(editor, new LinearLayout.LayoutParams(MATCH, 0, 1f));

        status = new TextView(this);
        status.setTypeface(Typeface.MONOSPACE);
        status.setTextSize(11f);
        status.setTextColor(0xFFFF8080);
        status.setPadding(12, 4, 12, 4);
        panel.addView(status, new LinearLayout.LayoutParams(MATCH, WRAP));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.addView(button("run", new Runnable() { public void run() { runSketch(); } }),
                    new LinearLayout.LayoutParams(0, WRAP, 1f));
        row.addView(button("hide", new Runnable() { public void run() { showEditor(false); } }),
                    new LinearLayout.LayoutParams(0, WRAP, 1f));
        panel.addView(row, new LinearLayout.LayoutParams(MATCH, WRAP));
        return panel;
    }

    private View buildToggle() {
        Button b = button("</>", new Runnable() { public void run() { showEditor(true); } });
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(WRAP, WRAP);
        lp.gravity = Gravity.TOP | Gravity.END;   // the panel owns the left half
        b.setLayoutParams(lp);
        return b;
    }

    private Button button(String label, final Runnable action) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { action.run(); }
        });
        return b;
    }

    private void showEditor(boolean on) {
        if (on) {
            // Always refill from the native buffer: the control port or the file
            // watcher may have replaced the sketch since the panel was last open.
            editor.setText(nativeGetSketch());
            editor.setSelection(editor.getText().length());   // type at the end
            status.setText("");
            panel.setVisibility(View.VISIBLE);
            editor.requestFocus();
            ime().showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT);
        } else {
            ime().hideSoftInputFromWindow(editor.getWindowToken(), 0);
            panel.setVisibility(View.GONE);
        }
    }

    private void runSketch() {
        nativeSetSketch(editor.getText().toString());
        // The button took focus when it was tapped, and without this the next
        // keystroke goes nowhere — run, then keep typing, is the whole loop.
        editor.requestFocus();
        // The GL thread needs a few frames to evaluate and report back, so read
        // the error after it has had them rather than immediately.
        editor.postDelayed(new Runnable() {
            public void run() {
                String e = nativeGetError();
                status.setText(e.isEmpty() ? "ok" : e);
                status.setTextColor(e.isEmpty() ? 0xFF80FF80 : 0xFFFF8080);
            }
        }, 300);
    }

    private InputMethodManager ime() {
        return (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    }

    @Override protected void onPause()  { super.onPause();  view.onPause(); }
    @Override protected void onResume() { super.onResume(); view.onResume(); }
}
