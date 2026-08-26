#pragma once
#include <string>

// Shared fluxus command layer — the immediate-mode "turtle" build API operating
// on a global build context + current Renderer. C-callable (extern "C") so BOTH
// script hosts drive it the same way:
//   - s7   calls these directly from its C bindings
//   - Racket calls them via ffi/unsafe get-ffi-obj (Sregister_symbol'd)
// This is the engine binding surface; add a primitive here and both languages
// can expose it.
extern "C" {
  void flux_set_renderer(void* renderer);          // Fluxus::Renderer*
  void flux_frame_begin(double timeSeconds, int frame);  // reset turtle each frame

  void flux_background(double r, double g, double b);
  void flux_colour(double r, double g, double b);
  void flux_translate(double x, double y, double z);
  void flux_rotate(double x, double y, double z);
  void flux_scale(double x, double y, double z);
  void flux_identity(void);
  void flux_push(void);
  void flux_pop(void);

  void flux_hint_wire(int on);    // wireframe overlay on built prims
  void flux_hint_solid(int on);
  void flux_line_width(double w);
  void flux_opacity(double o);          // grabbed-prim opacity
  void flux_wire_opacity(double o);
  void flux_wire_colour(double r, double g, double b);
  void flux_backfacecull(int on);

  int  flux_build_cube(void);
  int  flux_build_sphere(int slices, int stacks);
  int  flux_build_torus(double inner, double outer, int slices, int stacks);
  int  flux_build_plane(void);
  int  flux_build_seg_plane(int xsegs, int ysegs);   // subdivided grid (terrain)
  int  flux_build_ribbon(int n);        // camera-facing ribbon of n points
  int  flux_build_particles(int n);     // n billboard particles
  int  flux_build_nurbs_sphere(int hseg, int rseg);  // (poly approximation)

  // pdata — vertex-level access on a grabbed primitive (fluxus signature feature)
  void   flux_grab(int id);
  void   flux_ungrab(void);
  int    flux_pdata_size(void);
  double flux_pdata_get(const char* name, int i, int comp);       // one component
  void   flux_pdata_set(const char* name, int i, int comp, double val);
  void   flux_pdata_add(const char* name, const char* type);      // "v"/"c"/"f"
  void   flux_pdata_copy(const char* src, const char* dst);
  void   flux_recalc_normals(void);

  double flux_time(void);
  int    flux_frame(void);
  double flux_delta(void);

  // audio-reactive: the audio host writes FFT bands + gain; scripts read them.
  void   flux_set_audio(const float* bands, int n, double gain);
  double flux_audio_harmonic(int n);   // (gh n) — band n magnitude, 0..~1
  double flux_audio_gain(void);        // (gain) — overall level

  // mouse + orbit camera (host feeds events; camera applied each frame)
  void   flux_set_mouse(double x, double y, int button);
  double flux_mouse_x(void);
  double flux_mouse_y(void);
  int    flux_mouse_button(void);
  void   flux_camera_drag(double dx, double dy);   // orbit
  void   flux_camera_zoom(double d);               // dolly
  double flux_camera_dist(void);                   // wheel-accumulated dolly distance
  double flux_camera_yaw(void);                    // mouse-drag orbit angles
  double flux_camera_pitch(void);

  // script-driven camera (scripts run on the GL thread, so these may touch the
  // renderer's camera directly). set-camera-transform overrides the mouse orbit
  // until flux_camera_reset. Matrices are 16 doubles, column-major (fluxus order).
  void   flux_set_camera_transform(const double* m16);
  void   flux_get_camera_transform(double* out16);
  void   flux_set_camera_position(double x, double y, double z);
  void   flux_camera_reset(void);                    // back to mouse orbit
  void   flux_set_fov(double vfovDeg);               // vertical fov -> frustum
  void   flux_set_frustum(double l, double r, double b, double t);
  void   flux_set_ortho(int on);
  void   flux_set_ortho_zoom(double z);
  void   flux_set_clip(double front, double back);
  void   flux_set_viewport(double x, double y, double w, double h);
  void   flux_set_resolution(int w, int h);          // host feeds pixel size
  void   flux_get_screen_size(double* out2);         // -> #(w h)

  // persistent per-session state: a string-keyed store of double arrays that
  // SURVIVES the per-frame buffer re-eval + scene Clear(). Lets scripts keep
  // mutable state across frames (real fluxus-style damping/inertia) even though
  // this port re-runs the whole program every frame.
  //   flux_state_get: fills out[0..n) and returns 1 if key exists, else 0.
  int  flux_state_get(const char* key, double* out, int n);
  void flux_state_set(const char* key, const double* v, int n);
  void flux_state_clear(void);

  // GLSL shaders (per-primitive). Source strings are compiled once + cached;
  // set on the grabbed prim if grabbed, else on subsequently-built prims.
  // Uniforms are set on the currently-targeted shader's program (persist to render).
  void flux_shader_source(const char* vert, const char* frag);
  void flux_shader_clear(void);
  void flux_shader_set_float(const char* name, double v);
  void flux_shader_set_vec(const char* name, double x, double y, double z);

  // full-screen post-processing: install a fragment shader run over the rendered
  // scene (via an FBO in FluxusScene). A passthrough vertex stage + `tex`,
  // `time`, `audio`, `resolution` uniforms are provided automatically.
  void flux_post_shader(const char* frag);   // enable + set fragment source
  void flux_post_off(void);
  void flux_blur(double amount);             // built-in feedback motion-blur (0..~0.97)

  // scripts report an error string back to the host (or "" to clear)
  void flux_report_error(const char* msg);
}

// C++-side accessor for the host (not FFI).
std::string flux_last_error();

// C++-side post-FX accessors for FluxusScene (not FFI).
// returns true if post is enabled; fills frag + feedback; sets dirty=true (and
// clears it) when the fragment source changed since the last call.
bool flux_post_state(std::string& frag, double& feedback, bool& dirty);
