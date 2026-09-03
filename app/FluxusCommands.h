// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include <functional>

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
  int  flux_build_nurbs_plane(int usegs, int vsegs); // real NURBS surface (GLU)
  int  flux_build_cylinder(double h, double r, int hseg, int rseg);
  int  flux_build_polygons(int type, int nverts);    // empty mesh (0..4 = tristrip/quads/trilist/trifan/polygon)
  int  flux_build_copy(int id);                       // clone an existing primitive
  int  flux_build_locator(void);                      // invisible transform node

  // material (grabbed primitive), like colour/opacity
  void flux_specular(double r, double g, double b);
  void flux_ambient(double r, double g, double b);
  void flux_emissive(double r, double g, double b);
  void flux_shinyness(double s);
  void flux_normal_colour(double r, double g, double b);
  void flux_point_width(double w);

  // render hints (grabbed prim or build context)
  void flux_hint_none(void);
  void flux_hint_normal(int on);
  void flux_hint_points(int on);
  void flux_hint_unlit(int on);
  void flux_hint_vertcols(int on);
  void flux_hint_depth_sort(int on);
  void flux_hint_cull_ccw(int on);
  void flux_hint_origin(int on);
  void flux_hint_cast_shadow(int on);
  void flux_hint_ignore_depth(int on);
  void flux_hint_nozwrite(int on);
  void flux_hint_sphere_map(int on);

  // lights
  int  flux_make_light(int type);                     // 0 point, 1 directional, 2 spot
  void flux_light_position(int id, double x, double y, double z);
  void flux_light_diffuse(int id, double r, double g, double b);
  void flux_light_ambient(int id, double r, double g, double b);
  void flux_light_specular(int id, double r, double g, double b);
  void flux_light_direction(int id, double x, double y, double z);
  void flux_light_spot_angle(int id, double a);
  void flux_light_spot_exponent(int id, double e);
  void flux_light_attenuation(int id, int type, double v);   // type 0 const,1 linear,2 quadratic

  void flux_fog(double r, double g, double b, double density, double start, double end);
  void flux_parent(int id);                            // parent subsequently-built prims to id
  int  flux_select(int x, int y, int size);            // pick a primitive at screen x,y
  void flux_shadow_light(int index);
  void flux_shadow_length(double len);

  // textures: (load-texture path) -> GL id (implemented in TextureLoader.cpp via
  // JUCE image decode); (texture id) applies it to the grabbed prim / next-built.
  unsigned flux_load_texture(const char* path);
  unsigned flux_font_atlas(void);              // 16x16 ASCII glyph atlas (TextureLoader.cpp)
  void     flux_texture(int id);

  int  flux_build_text(const char* str);       // textured glyph-quads via the font atlas
  int  flux_build_pixels(int w, int h);        // a plane backed by a writable pixel buffer ("c")
  void flux_pixels_upload(void);               // push the grabbed pixels' "c" pdata to its texture
  int  flux_pixels_width(void);
  int  flux_pixels_height(void);

  // growing unicode glyph atlas (GlyphAtlas.cpp): lazily bakes any codepoint; cell 0
  // is a reserved opaque-white square (used for terminal background quads).
  unsigned flux_glyph_atlas_texture(void);
  void     flux_glyph_cell(unsigned codepoint, float* s0, float* t0, float* s1, float* t1);

  // terminal: script emits ANSI/VT, libvterm keeps the cell grid, we render it as a
  // glyph-atlas quad grid (per-cell fg/bg colour). build- returns a prim id; the rest
  // are grab-aware (operate on the grabbed terminal), like pixels-upload.
  int  flux_build_terminal(int cols, int rows);
  void flux_terminal_write(const char* bytes);   // feed ANSI bytes to the grabbed terminal
  void flux_terminal_clear(void);                 // reset the grabbed terminal's screen
  void flux_terminal_draw(void);                  // rebuild the mesh from the screen state
  int  flux_terminal_cols(void);
  int  flux_terminal_rows(void);
  void flux_terminal_shape(int mode, double radius);   // 0 flat, 1 sphere (radius<=0 = auto)
  void flux_terminal_bg_alpha(double a);               // bg opacity: 1 opaque, 0 see-through
  void flux_free_terminals(void);   // free all terminal vterms (before a scene wipe)

  // pdata — vertex-level access on a grabbed primitive (fluxus signature feature)
  void   flux_grab(int id);
  void   flux_ungrab(void);
  int    flux_pdata_size(void);
  double flux_pdata_get(const char* name, int i, int comp);       // one component
  void   flux_pdata_set(const char* name, int i, int comp, double val);
  void   flux_pdata_add(const char* name, const char* type);      // "v"/"c"/"f"
  void   flux_pdata_copy(const char* src, const char* dst);
  void   flux_recalc_normals(void);

  // native audio deform of the grabbed prim: for each vertex, p = ori + n*disp,
  // where disp = bands[i % nbands]*bandScale + wobble*travelling-wave. Runs the
  // whole per-vertex loop in C++ (reads the audio bands directly) so a high-res
  // mesh can be deformed every frame WITHOUT per-vertex FFI. Needs pdata "ori"
  // (a copy of the base positions) and "n" (normals). recalcNormals!=0 rebuilds
  // normals afterwards for correct shading.
  void   flux_deform_audio(double bandScale, double wobble, double freq,
                           double speed, int recalcNormals);

  // shape cache: compute an expensive base shape ONCE (in script), snapshot the
  // grabbed prim's positions + normals into a named C++ store, then each frame
  // deform cheaply FROM that cached base (p = base + normal*disp) entirely in
  // C++ — the costly per-vertex build never re-runs. disp uses the audio bands +
  // a travelling wobble; it relaxes back to the cached shape when silent.
  void   flux_cache_shape(const char* name);          // snapshot grabbed prim's p + n
  int    flux_shape_cached(const char* name);          // 1 if a cache exists
  void   flux_deform_cached(const char* name, double bandScale, double wobble,
                            double freq, double speed, int recalcNormals);

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
  void   flux_set_key(int c);           // app pushes last-pressed char
  int    flux_get_key(void);            // (key-poll): consume it (0 if none)
  void   flux_set_key_down(int code, int down);  // app mirrors live physical key up/down
  int    flux_key_is_down(int code);    // (key-down? c): live held state (0/1)
  void   flux_clear_keys_down(void);    // release all (e.g. when the editor takes focus)
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
  void   flux_set_aspect(double ratio);              // lock render AR (w/h); <=0 = auto
  void   flux_scene_clear(void);                     // (clear): wipe the scene graph
  void   flux_destroy(int id);                        // (destroy id): remove one prim (retained persistent scene)
  void   flux_request_window_size(int w, int h);     // (set-window-size w h)
  int    flux_take_window_request(int* w, int* h);   // message thread: pending resize?

  // code-editor overlay visibility ((show-editor)/(hide-editor)/(editor-full-width b)).
  void   flux_set_editor_visible(int visible);       // 0 = hide overlay, 1 = show
  void   flux_set_editor_full_width(int full);       // 0 = left half, 1 = full width
  int    flux_get_editor(int* visible, int* full);   // message thread: 0 if never set

  // frame recording: the View menu toggles it; while on, the scene writes every
  // rendered frame to dir/fNNNNN.png (encode to video with ffmpeg afterwards).
  void   flux_set_recording(int on, const char* dir); // message thread: start/stop
  int    flux_recording_next(char* out, int cap);     // GL thread: next frame path?

  // offline export: frame-locked render (deterministic time) piped to ffmpeg.
  void   flux_set_export(int on, const char* path, int fps);   // message thread
  int    flux_export_state(char* pathOut, int cap, int* fps);  // GL thread: 1 if desired
  // audio-reactive export: an optional soundtrack muxed into the export AND
  // pre-analysed into per-frame features that are fed as the frame's audio state,
  // so the visuals react to the track deterministically (synced to each frame).
  void   flux_set_export_audio(const char* wavPath);           // "" clears the mux track
  int    flux_export_audio_path(char* out, int cap);           // GL thread: mux path? (0 if none)
  void   flux_export_audio_load(const float* gains, const float* bands,
                                int nFrames, int nBands);       // feature table
  void   flux_export_audio_apply(long frame);                  // GL thread: set this frame's audio
  void   flux_export_audio_clear(void);

  // one-shot screenshot: (screenshot "path") requests a grab; captured once per
  // path (calling it every frame is safe — repeats are ignored). The scene grabs
  // the GL framebuffer after Render and writes a PNG via flux_write_png.
  void   flux_screenshot(const char* path);
  int    flux_take_screenshot(char* out, int cap);   // GL thread: pop pending path
  void   flux_write_png(const char* path, const unsigned char* rgba, int w, int h);
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
  void flux_shader_set_int(const char* name, int v);      // e.g. bind a sampler to a unit

  void flux_blend_mode(int src, int dst);                 // GL blend factors (grabbed/ctx)
  void flux_multitexture(int unit, int id);               // set a texture on unit 0..7 (grabbed)

  // full-screen post-processing: install a fragment shader run over the rendered
  // scene (via an FBO in FluxusScene). A passthrough vertex stage + `tex`,
  // `time`, `audio`, `resolution` uniforms are provided automatically.
  void flux_post_shader(const char* frag);   // enable + set fragment source
  void flux_post_off(void);
  void flux_blur(double amount);             // built-in feedback motion-blur (0..~0.97)

  // final-stage NTSC/CRT filter (software, LMP88959/NTSC-CRT). Runs AFTER the
  // scene + any post-shader, over the finished framebuffer, so screenshots and
  // recordings capture it too. See app/NTSCEffect. Toggle + tune monitor knobs.
  void flux_ntsc(int on);                     // enable/disable the NTSC pass
  void flux_ntsc_noise(int n);                // signal noise (0..inf), default 12
  void flux_ntsc_hue(int deg);                // 0-359
  void flux_ntsc_saturation(int s);           // default 10
  void flux_ntsc_brightness(int b);           // default 0
  void flux_ntsc_contrast(int c);             // default 180
  void flux_ntsc_scanlines(int on);           // gaps between scanlines
  void flux_ntsc_monochrome(int on);          // 0 = full colour, 1 = mono
  void flux_ntsc_blend(int on);               // blend field onto previous frame

  void flux_set_antialias(int on);           // GL line/polygon smoothing
  void flux_set_retained(int on);            // retained mode: build once, per-frame thunk only

  // live-tweakable variable, surfaced as a slider in the on-screen tweak panel.
  // Registers `name` with its range on the first call; every later call returns
  // whatever the slider holds — that is what makes an edit survive immediate
  // mode's per-frame re-eval of the whole buffer.
  double flux_tweak(const char* name, double def, double lo, double hi);

  // tweak-panel visibility ((show-tweaks)/(hide-tweaks)), polled by the component
  // on the message thread exactly like flux_get_editor above.
  void   flux_set_tweaks_visible(int visible);
  int    flux_get_tweaks_visible(int* visible);   // 0 if no script ever set it

  // ---- maths primitives (pure; no engine state) ---------------------------
  // Canonical fluxus maths, reimplemented on the engine's dVector/dMatrix/dQuat.
  // Vectors are double[3], matrices double[16] (dMatrix arr() order), quats
  // double[4] (x y z w). Outputs are written through the trailing pointer arg.
  void   flux_vadd(const double a[3], const double b[3], double o[3]);
  void   flux_vsub(const double a[3], const double b[3], double o[3]);
  void   flux_vmul(const double a[3], double s, double o[3]);            // vec * scalar
  void   flux_vdiv(const double a[3], double s, double o[3]);            // vec / scalar
  double flux_vdot(const double a[3], const double b[3]);
  void   flux_vcross(const double a[3], const double b[3], double o[3]);
  double flux_vmag(const double a[3]);
  double flux_vdist(const double a[3], const double b[3]);
  double flux_vdist_sq(const double a[3], const double b[3]);
  void   flux_vnormalise(const double a[3], double o[3]);
  void   flux_vreflect(const double a[3], const double n[3], double o[3]);
  void   flux_vtransform(const double v[3], const double m[16], double o[3]);      // full (w/ translation)
  void   flux_vtransform_rot(const double v[3], const double m[16], double o[3]);  // rotation only

  void flux_mident(double o[16]);
  void flux_mmul(const double a[16], const double b[16], double o[16]);
  void flux_mtranslate(const double v[3], double o[16]);
  void flux_mrotate(const double v[3], double o[16]);       // euler degrees x,y,z
  void flux_mscale(const double v[3], double o[16]);
  void flux_mtranspose(const double a[16], double o[16]);
  void flux_minverse(const double a[16], double o[16]);
  void flux_maim(const double dir[3], const double up[3], double o[16]);

  void flux_qaxisangle(const double axis[3], double angle, double o[4]);  // angle in degrees
  void flux_qmul(const double a[4], const double b[4], double o[4]);
  void flux_qnormalise(const double a[4], double o[4]);
  void flux_qconjugate(const double a[4], double o[4]);
  void flux_qtomatrix(const double a[4], double o[16]);

  double flux_noise(double x, double y, double z);      // classic Perlin (Fluxus::Noise)
  double flux_snoise(double x, double y, double z);     // simplex noise
  void   flux_noise_seed(int seed);
  void   flux_noise_detail(int octaves, double falloff);

  // ---- turtle builder: emit vertices by moving/turning a turtle -------------
  void flux_turtle_prim(int type);        // begin a build prim (0..4 = poly type)
  void flux_turtle_vert(void);            // emit a vertex at the turtle position
  int  flux_turtle_build(void);           // hand the prim to the renderer -> id
  void flux_turtle_move(double d);        // advance d along the turtle's local +X
  void flux_turtle_turn(double x, double y, double z);  // add euler degrees
  void flux_turtle_push(void);
  void flux_turtle_pop(void);
  void flux_turtle_reset(void);
  void flux_turtle_attach(int id);        // deform an existing poly's "p" pdata
  void flux_turtle_skip(int n);
  int  flux_turtle_position(void);
  void flux_turtle_seek(int pos);
  void flux_get_turtle_transform(double out[16]);

  // ---- voxels + blobby (volumetric grid + metaball implicit surface) --------
  int  flux_build_voxels(int w, int h, int d);
  int  flux_voxels_width(void);           // 0 if grabbed prim is not a voxel prim
  int  flux_voxels_height(void);
  int  flux_voxels_depth(void);
  void flux_voxels_calc_gradient(void);
  void flux_voxels_sphere_influence(double px, double py, double pz,
                                    double r, double g, double b, double pow);
  void flux_voxels_sphere_solid(double px, double py, double pz,
                                double r, double g, double b, double radius);
  void flux_voxels_box_solid(double tx, double ty, double tz,
                             double bx, double by, double bz,
                             double r, double g, double b);
  void flux_voxels_threshold(double v);
  void flux_voxels_point_light(double px, double py, double pz,
                               double r, double g, double b);
  int  flux_voxels_to_blobby(int id);
  int  flux_voxels_to_poly(int id, double isolevel);
  int  flux_build_blobby(int count, double dx, double dy, double dz,
                         double sx, double sy, double sz);
  int  flux_blobby_to_poly(int id);

  // ---- pdata-op: pdata operations ("+","*","closest","sin","cos") on grabbed
  // Operand is a scalar, a vector/colour/matrix, or another pdata array's name.
  // Returns 3 and fills out[3] for ops that yield a value ("closest"); else 0.
  int  flux_pdata_op_num(const char* op, const char* name, double val, double out[3]);
  int  flux_pdata_op_vec(const char* op, const char* name, const double* v, int n, double out[3]);
  int  flux_pdata_op_pdata(const char* op, const char* name, const char* other, double out[3]);

  // ---- poly indexing (on grabbed PolyPrimitive) -----------------------------
  int  flux_poly_type(void);              // 0..4 (TRISTRIP..POLYGON), -1 if not poly
  int  flux_poly_indexed(void);           // 1 if in indexed mode
  int  flux_poly_index_count(void);
  void flux_poly_indices(unsigned int* out, int n);      // copy up to n indices
  void flux_poly_set_index(const unsigned int* idx, int n);
  void flux_poly_convert_to_indexed(void);

  // ---- scene-graph queries (on grabbed prim) --------------------------------
  int  flux_get_bb(double outmin[3], double outmax[3]);  // 1 if grabbed, else 0
  int  flux_get_parent(void);             // parent id, -1 if none/root
  int  flux_get_children_count(void);     // children of grabbed (or root if none)
  void flux_get_children(int* out, int n);
  void flux_recalc_bb(void);

  // ---- primitive IO (OBJ meshes) --------------------------------------------
  int  flux_load_primitive(const char* path);   // read a mesh -> new prim id (-1 fail)
  void flux_save_primitive(const char* path);   // write the grabbed prim to path

  void flux_get_transform(double out[16]);         // grabbed prim's local transform (or build ctx)
  void flux_get_global_transform(double out[16]);  // grabbed prim's world transform (scene graph)

  // ---- primitive functions (pfunc) + skinning -------------------------------
  int  flux_pfunc_make(const char* name);  // "arithmetic"|"genskinweights"|"skinning"|"skinweights->vertcols" -> id (-1 unknown)
  void flux_pfunc_set_str(int id, const char* key, const char* v);
  void flux_pfunc_set_int(int id, const char* key, int v);
  void flux_pfunc_set_float(int id, const char* key, double v);
  void flux_pfunc_set_vec(int id, const char* key, double x, double y, double z);
  void flux_pfunc_set_col(int id, const char* key, double r, double g, double b, double a);
  void flux_pfunc_run(int id);             // apply pfunc to the grabbed primitive
  void flux_pfunc_clear(void);             // free all pfuncs

  // ---- colour mode + hsv/rgb -----------------------------------------------
  void flux_colour_mode(int mode);         // 0 rgb, 1 hsv (colour / wire-colour)
  void flux_hsv_to_rgb(const double hsv[3], double rgb[3]);
  void flux_rgb_to_hsv(const double rgb[3], double hsv[3]);

  // ---- MIDI input (MidiHost pushes on a JUCE thread; scripts read) ----------
  void   flux_set_midi_cc(int chan, int ctrl, int val);   // store a CC (val 0..127)
  void   flux_set_midi_note(int pitch, int vel);          // store the last note-on
  int    flux_midi_cc(int chan, int ctrl);                // raw 0..127 (0 if unseen)
  double flux_midi_ccn(int chan, int ctrl);               // normalised 0..1
  int    flux_midi_note(void);                            // last note-on pitch (-1 none)
  int    flux_midi_note_velocity(void);                   // last note-on velocity

  // ---- OSC (OscHost pushes received msgs; send goes via an installed bridge)
  void   flux_set_osc(const char* addr, const double* args, int n);  // received (host push)
  double flux_osc_get(const char* addr, int index);       // an arg of the latest msg at addr
  int    flux_osc_msg(char* out, int cap);                // last received address -> out; len
  void   flux_osc_source(int port);                       // open a receiver on a UDP port
  void   flux_osc_destination(const char* host, int port);// set the send target
  void   flux_osc_send(const char* addr, const double* args, int n); // send float args

  // ---- Hand tracking (HandHost pushes landmarks on a capture thread) --------
  // 21 landmarks/hand, MediaPipe order (0 wrist, thumb, index, ...); x,y in 0..1
  // image space, z relative depth (0 on 2D backends like Apple Vision).
  void   flux_set_hands(int nHands, const float* xyz, int landmarksPerHand); // host push
  int    flux_hand_count(void);                          // tracked hand count
  double flux_hand_joint(int hand, int joint, int axis); // axis 0=x 1=y 2=z (0 if absent)
  double flux_hand_pinch(int hand);                      // thumb-tip(4)..index-tip(8) distance
  void   flux_hand_tracking(int on);                     // script: enable/disable capture (via bridge)

  // scripts report an error string back to the host (or "" to clear)
  void flux_report_error(const char* msg);
}

// C++-side accessor for the host (not FFI).
std::string flux_last_error();

// C++-side: OscHost installs its transport here so the script-facing
// flux_osc_source/destination/send can drive it without FluxusCommands
// depending on JUCE. Any callback may be null (no OSC host wired).
struct FluxOscBridge {
  std::function<void(int)>                                   openSource;   // (port)
  std::function<void(const std::string&, int)>              setDestination; // (host, port)
  std::function<void(const std::string&, const std::vector<double>&)> send; // (addr, args)
};
void flux_osc_install_bridge(const FluxOscBridge& bridge);

// C++-side: HandHost installs its capture control here so the script-facing
// flux_hand_tracking can start/stop it without FluxusCommands depending on the
// platform camera stack. enable may be null (no hand host wired / null backend).
struct FluxHandBridge {
  std::function<void(bool)> enable;   // (on) -> open/close the camera + tracker
};
void flux_hand_install_bridge(const FluxHandBridge& bridge);

// C++-side post-FX accessors for FluxusScene (not FFI).
// returns true if post is enabled; fills frag + feedback; sets dirty=true (and
// clears it) when the fragment source changed since the last call.
bool flux_post_state(std::string& frag, double& feedback, bool& dirty);

// C++-side NTSC-filter state for FluxusScene (not FFI). Common CRT monitor knobs
// plus the software-lib flags; defaults match crt_reset(). flux_ntsc_state fills
// `out` and returns true when the final NTSC pass is enabled.
struct NtscParams {
  int  noise      = 12;
  int  hue        = 0;    // 0-359
  int  saturation = 10;
  int  brightness = 0;
  int  contrast   = 180;
  bool scanlines  = true;
  bool blend      = true;
  bool monochrome = false;
};
bool flux_ntsc_state(NtscParams& out);

// C++-side tweak-registry accessors for the ImGui tweak panel (not FFI). Scripts
// register variables with flux_tweak (above); the panel enumerates them here and
// pushes slider edits back through flux_tweak_set. Deliberately NOT bound into
// either script host — the script only ever reads a tweak, the UI only ever
// writes one.
struct TweakVar {
  std::string name;
  double      value = 0.0, lo = 0.0, hi = 1.0;
};
void flux_tweak_list(std::vector<TweakVar>& out);     // snapshot, in declaration order
void flux_tweak_set(const char* name, double value);  // clamped to the script's [lo,hi]
void flux_tweak_clear();                              // drop all (a different sketch was loaded)

// C++-side: whether the script asked for anti-aliasing (line/polygon smoothing).
bool flux_antialias_on();
// C++-side: whether the script opted into retained mode (build once, thunk/frame).
bool flux_retained_on();
