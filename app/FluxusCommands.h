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

  int  flux_build_cube(void);
  int  flux_build_sphere(int slices, int stacks);
  int  flux_build_torus(double inner, double outer, int slices, int stacks);
  int  flux_build_plane(void);
  int  flux_build_ribbon(int n);        // camera-facing ribbon of n points
  int  flux_build_particles(int n);     // n billboard particles

  // pdata — vertex-level access on a grabbed primitive (fluxus signature feature)
  void   flux_grab(int id);
  void   flux_ungrab(void);
  int    flux_pdata_size(void);
  double flux_pdata_get(const char* name, int i, int comp);       // one component
  void   flux_pdata_set(const char* name, int i, int comp, double val);
  void   flux_recalc_normals(void);

  double flux_time(void);
  int    flux_frame(void);

  // audio-reactive: the audio host writes FFT bands + gain; scripts read them.
  void   flux_set_audio(const float* bands, int n, double gain);
  double flux_audio_harmonic(int n);   // (gh n) — band n magnitude, 0..~1
  double flux_audio_gain(void);        // (gain) — overall level

  // scripts report an error string back to the host (or "" to clear)
  void flux_report_error(const char* msg);
}

// C++-side accessor for the host (not FFI).
std::string flux_last_error();
