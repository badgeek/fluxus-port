#include "S7ScriptHost.h"
#include "FluxusCommands.h"     // shared engine command layer (same as the Racket host)

extern "C" {
#include "s7.h"
}

#include <string>

// s7 bindings are thin: parse args, call the shared flux_* commands. Both s7 and
// Racket drive the SAME FluxusCommands layer, so a primitive added there is
// available to every app once bound in each host.

namespace {
// read a vec3 from args: a single s7 vector #(x y z) or three reals
bool vec3(s7_scheme* sc, s7_pointer a, double& x, double& y, double& z) {
  if (!s7_is_pair(a)) return false;
  s7_pointer first = s7_car(a);
  if (s7_is_vector(first) && s7_vector_length(first) >= 3) {
    x = s7_number_to_real(sc, s7_vector_ref(sc, first, 0));
    y = s7_number_to_real(sc, s7_vector_ref(sc, first, 1));
    z = s7_number_to_real(sc, s7_vector_ref(sc, first, 2));
    return true;
  }
  x = s7_number_to_real(sc, s7_car(a));
  y = s7_number_to_real(sc, s7_cadr(a));
  z = s7_number_to_real(sc, s7_caddr(a));
  return true;
}

s7_pointer f_colour(s7_scheme* sc, s7_pointer a)     { double x,y,z; if (vec3(sc,a,x,y,z)) flux_colour(x,y,z);     return s7_nil(sc); }
s7_pointer f_background(s7_scheme* sc, s7_pointer a)  { double x,y,z; if (vec3(sc,a,x,y,z)) flux_background(x,y,z); return s7_nil(sc); }
s7_pointer f_translate(s7_scheme* sc, s7_pointer a)   { double x,y,z; if (vec3(sc,a,x,y,z)) flux_translate(x,y,z);  return s7_nil(sc); }
s7_pointer f_rotate(s7_scheme* sc, s7_pointer a)      { double x,y,z; if (vec3(sc,a,x,y,z)) flux_rotate(x,y,z);     return s7_nil(sc); }
s7_pointer f_scale(s7_scheme* sc, s7_pointer a)       { double x,y,z; if (vec3(sc,a,x,y,z)) flux_scale(x,y,z);      return s7_nil(sc); }
s7_pointer f_identity(s7_scheme* sc, s7_pointer)      { flux_identity(); return s7_nil(sc); }
s7_pointer f_push(s7_scheme* sc, s7_pointer)          { flux_push();     return s7_nil(sc); }
s7_pointer f_pop(s7_scheme* sc, s7_pointer)           { flux_pop();      return s7_nil(sc); }

s7_pointer f_hint_wire(s7_scheme* sc, s7_pointer a)   { int on = s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1; flux_hint_wire(on);  return s7_nil(sc); }
s7_pointer f_hint_solid(s7_scheme* sc, s7_pointer a)  { int on = s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1; flux_hint_solid(on); return s7_nil(sc); }
s7_pointer f_line_width(s7_scheme* sc, s7_pointer a)  { if (s7_is_pair(a)) flux_line_width(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }

s7_pointer f_build_cube(s7_scheme* sc, s7_pointer)    { return s7_make_integer(sc, flux_build_cube()); }
s7_pointer f_build_plane(s7_scheme* sc, s7_pointer)   { return s7_make_integer(sc, flux_build_plane()); }
s7_pointer f_build_seg_plane(s7_scheme* sc, s7_pointer a) {
  int x = 10, y = 10;
  if (s7_is_pair(a)) { x = (int) s7_number_to_real(sc, s7_car(a)); if (s7_is_pair(s7_cdr(a))) y = (int) s7_number_to_real(sc, s7_cadr(a)); }
  return s7_make_integer(sc, flux_build_seg_plane(x, y));
}
s7_pointer f_build_ribbon(s7_scheme* sc, s7_pointer a){ int n = s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 1; return s7_make_integer(sc, flux_build_ribbon(n)); }
s7_pointer f_build_particles(s7_scheme* sc, s7_pointer a){ int n = s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 1; return s7_make_integer(sc, flux_build_particles(n)); }
s7_pointer f_build_sphere(s7_scheme* sc, s7_pointer a){
  int sl = 10, st = 10;
  if (s7_is_pair(a)) { sl = (int) s7_number_to_real(sc, s7_car(a)); if (s7_is_pair(s7_cdr(a))) st = (int) s7_number_to_real(sc, s7_cadr(a)); }
  return s7_make_integer(sc, flux_build_sphere(sl, st));
}
s7_pointer f_build_torus(s7_scheme* sc, s7_pointer a){
  double in = 0.5, out = 1.0; int sl = 12, st = 12;
  s7_pointer p = a;
  if (s7_is_pair(p)) { in  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { out = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { sl  = (int) s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { st  = (int) s7_number_to_real(sc, s7_car(p)); }
  return s7_make_integer(sc, flux_build_torus(in, out, sl, st));
}
s7_pointer f_time (s7_scheme* sc, s7_pointer) { return s7_make_real(sc, flux_time()); }
s7_pointer f_frame(s7_scheme* sc, s7_pointer) { return s7_make_integer(sc, flux_frame()); }
s7_pointer f_gh(s7_scheme* sc, s7_pointer a)  { int n = s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 0; return s7_make_real(sc, flux_audio_harmonic(n)); }
s7_pointer f_gain(s7_scheme* sc, s7_pointer)  { return s7_make_real(sc, flux_audio_gain()); }
s7_pointer f_mouse_x(s7_scheme* sc, s7_pointer) { return s7_make_real(sc, flux_mouse_x()); }
s7_pointer f_mouse_y(s7_scheme* sc, s7_pointer) { return s7_make_real(sc, flux_mouse_y()); }
s7_pointer f_mouse_button(s7_scheme* sc, s7_pointer) { return s7_make_integer(sc, flux_mouse_button()); }
s7_pointer f_camera_dist(s7_scheme* sc, s7_pointer)  { return s7_make_real(sc, flux_camera_dist()); }
s7_pointer f_camera_yaw(s7_scheme* sc, s7_pointer)   { return s7_make_real(sc, flux_camera_yaw()); }
s7_pointer f_camera_pitch(s7_scheme* sc, s7_pointer) { return s7_make_real(sc, flux_camera_pitch()); }

// ---- camera (matrices are 16-element s7 vectors, column-major) -------------
s7_pointer f_set_camera_transform(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a)) {
    s7_pointer v = s7_car(a);
    if (s7_is_vector(v) && s7_vector_length(v) >= 16) {
      double m[16];
      for (int i = 0; i < 16; ++i) m[i] = s7_number_to_real(sc, s7_vector_ref(sc, v, i));
      flux_set_camera_transform(m);
    }
  }
  return s7_nil(sc);
}
s7_pointer f_get_camera_transform(s7_scheme* sc, s7_pointer) {
  double m[16]; flux_get_camera_transform(m);
  s7_pointer v = s7_make_vector(sc, 16);
  for (int i = 0; i < 16; ++i) s7_vector_set(sc, v, i, s7_make_real(sc, m[i]));
  return v;
}
s7_pointer f_set_camera_position(s7_scheme* sc, s7_pointer a) { double x,y,z; if (vec3(sc,a,x,y,z)) flux_set_camera_position(x,y,z); return s7_nil(sc); }
s7_pointer f_camera_reset(s7_scheme* sc, s7_pointer)      { flux_camera_reset(); return s7_nil(sc); }
s7_pointer f_set_fov(s7_scheme* sc, s7_pointer a)         { if (s7_is_pair(a)) flux_set_fov(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_set_ortho(s7_scheme* sc, s7_pointer a)       { int on = s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1; flux_set_ortho(on); return s7_nil(sc); }
s7_pointer f_set_ortho_zoom(s7_scheme* sc, s7_pointer a)  { if (s7_is_pair(a)) flux_set_ortho_zoom(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_get_screen_size(s7_scheme* sc, s7_pointer)   { double s[2]; flux_get_screen_size(s); s7_pointer v = s7_make_vector(sc, 3); s7_vector_set(sc, v, 0, s7_make_real(sc, s[0])); s7_vector_set(sc, v, 1, s7_make_real(sc, s[1])); s7_vector_set(sc, v, 2, s7_make_real(sc, 0)); return v; }

// ---- persistent script state (survives per-frame re-eval) ------------------
s7_pointer f_persist(s7_scheme* sc, s7_pointer a) {   // (persist key default-vector)
  const char* key = s7_string(s7_car(a));
  s7_pointer d = s7_cadr(a);
  int n = (int) s7_vector_length(d);
  if (n > 64) n = 64;
  double buf[64];
  if (flux_state_get(key, buf, n)) {
    s7_pointer v = s7_make_vector(sc, n);
    for (int i = 0; i < n; ++i) s7_vector_set(sc, v, i, s7_make_real(sc, buf[i]));
    return v;
  }
  for (int i = 0; i < n; ++i) buf[i] = s7_number_to_real(sc, s7_vector_ref(sc, d, i));
  flux_state_set(key, buf, n);
  return d;
}
s7_pointer f_persist_bang(s7_scheme* sc, s7_pointer a) {  // (persist! key vector)
  const char* key = s7_string(s7_car(a));
  s7_pointer v = s7_cadr(a);
  int n = (int) s7_vector_length(v);
  if (n > 64) n = 64;
  double buf[64];
  for (int i = 0; i < n; ++i) buf[i] = s7_number_to_real(sc, s7_vector_ref(sc, v, i));
  flux_state_set(key, buf, n);
  return s7_nil(sc);
}
s7_pointer f_clear_state(s7_scheme* sc, s7_pointer) { flux_state_clear(); return s7_nil(sc); }

// ---- GLSL shaders ----------------------------------------------------------
s7_pointer f_shader_source(s7_scheme* sc, s7_pointer a) {   // (shader-source vert frag)
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_shader_source(s7_string(s7_car(a)), s7_string(s7_cadr(a)));
  return s7_nil(sc);
}
s7_pointer f_shader_off(s7_scheme* sc, s7_pointer)  { flux_shader_clear(); return s7_nil(sc); }
s7_pointer f_shader_set_float(s7_scheme* sc, s7_pointer a) {  // (shader-set-float! name v)
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_shader_set_float(s7_string(s7_car(a)), s7_number_to_real(sc, s7_cadr(a)));
  return s7_nil(sc);
}
s7_pointer f_post_shader(s7_scheme* sc, s7_pointer a) { if (s7_is_pair(a)) flux_post_shader(s7_string(s7_car(a))); return s7_nil(sc); }
s7_pointer f_post_off(s7_scheme* sc, s7_pointer)      { flux_post_off(); return s7_nil(sc); }
s7_pointer f_blur(s7_scheme* sc, s7_pointer a)        { if (s7_is_pair(a)) flux_blur(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_antialias(s7_scheme* sc, s7_pointer a)   { int on = s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1; flux_set_antialias(on); return s7_nil(sc); }
s7_pointer f_shader_set_vec(s7_scheme* sc, s7_pointer a) {    // (shader-set-vec! name vec)
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a))) {
    const char* name = s7_string(s7_car(a));
    s7_pointer v = s7_cadr(a);
    double x = s7_number_to_real(sc, s7_vector_ref(sc, v, 0));
    double y = s7_number_to_real(sc, s7_vector_ref(sc, v, 1));
    double z = s7_number_to_real(sc, s7_vector_ref(sc, v, 2));
    flux_shader_set_vec(name, x, y, z);
  }
  return s7_nil(sc);
}

// ---- extra builders / material / hints / lights / fog / parent / select ----
s7_pointer f_build_cylinder(s7_scheme* sc, s7_pointer a) {
  double h = 1, r = 1; int hs = 10, rs = 10; s7_pointer p = a;
  if (s7_is_pair(p)) { h  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { r  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { hs = (int) s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { rs = (int) s7_number_to_real(sc, s7_car(p)); }
  return s7_make_integer(sc, flux_build_cylinder(h, r, hs, rs));
}
s7_pointer f_build_polygons(s7_scheme* sc, s7_pointer a) {
  int type = s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 0;
  int nv   = (s7_is_pair(a) && s7_is_pair(s7_cdr(a))) ? (int) s7_number_to_real(sc, s7_cadr(a)) : 0;
  return s7_make_integer(sc, flux_build_polygons(type, nv));
}
s7_pointer f_build_copy(s7_scheme* sc, s7_pointer a)    { return s7_make_integer(sc, flux_build_copy(s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : -1)); }
s7_pointer f_build_locator(s7_scheme* sc, s7_pointer)   { return s7_make_integer(sc, flux_build_locator()); }

#define S7_VEC3(fn, cfn) s7_pointer fn(s7_scheme* sc, s7_pointer a) { double x,y,z; if (vec3(sc,a,x,y,z)) cfn(x,y,z); return s7_nil(sc); }
S7_VEC3(f_specular,      flux_specular)
S7_VEC3(f_ambient,       flux_ambient)
S7_VEC3(f_emissive,      flux_emissive)
S7_VEC3(f_normal_colour, flux_normal_colour)
s7_pointer f_shinyness(s7_scheme* sc, s7_pointer a)   { if (s7_is_pair(a)) flux_shinyness(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_point_width(s7_scheme* sc, s7_pointer a) { if (s7_is_pair(a)) flux_point_width(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }

#define S7_HINT(fn, cfn) s7_pointer fn(s7_scheme* sc, s7_pointer a) { int on = s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1; cfn(on); return s7_nil(sc); }
s7_pointer f_hint_none(s7_scheme* sc, s7_pointer) { flux_hint_none(); return s7_nil(sc); }
S7_HINT(f_hint_normal,       flux_hint_normal)
S7_HINT(f_hint_points,       flux_hint_points)
S7_HINT(f_hint_unlit,        flux_hint_unlit)
S7_HINT(f_hint_vertcols,     flux_hint_vertcols)
S7_HINT(f_hint_depth_sort,   flux_hint_depth_sort)
S7_HINT(f_hint_cull_ccw,     flux_hint_cull_ccw)
S7_HINT(f_hint_origin,       flux_hint_origin)
S7_HINT(f_hint_cast_shadow,  flux_hint_cast_shadow)
S7_HINT(f_hint_ignore_depth, flux_hint_ignore_depth)
S7_HINT(f_hint_nozwrite,     flux_hint_nozwrite)
S7_HINT(f_hint_sphere_map,   flux_hint_sphere_map)

s7_pointer f_make_light(s7_scheme* sc, s7_pointer a) { return s7_make_integer(sc, flux_make_light(s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 0)); }
#define S7_LIGHTV(fn, cfn) s7_pointer fn(s7_scheme* sc, s7_pointer a) { \
  int id = (int) s7_number_to_real(sc, s7_car(a)); double x,y,z; if (vec3(sc, s7_cdr(a), x, y, z)) cfn(id, x, y, z); return s7_nil(sc); }
S7_LIGHTV(f_light_position,  flux_light_position)
S7_LIGHTV(f_light_diffuse,   flux_light_diffuse)
S7_LIGHTV(f_light_ambient,   flux_light_ambient)
S7_LIGHTV(f_light_specular,  flux_light_specular)
S7_LIGHTV(f_light_direction, flux_light_direction)
s7_pointer f_light_spot_angle(s7_scheme* sc, s7_pointer a) { flux_light_spot_angle((int) s7_number_to_real(sc, s7_car(a)), s7_number_to_real(sc, s7_cadr(a))); return s7_nil(sc); }

s7_pointer f_fog(s7_scheme* sc, s7_pointer a) {   // (fog colour density start end)
  double x,y,z; vec3(sc, a, x, y, z);
  s7_pointer p = s7_cdr(a);
  double d = s7_is_pair(p) ? s7_number_to_real(sc, s7_car(p)) : 0; if (s7_is_pair(p)) p = s7_cdr(p);
  double st = s7_is_pair(p) ? s7_number_to_real(sc, s7_car(p)) : 0; if (s7_is_pair(p)) p = s7_cdr(p);
  double en = s7_is_pair(p) ? s7_number_to_real(sc, s7_car(p)) : 100;
  flux_fog(x, y, z, d, st, en); return s7_nil(sc);
}
s7_pointer f_parent(s7_scheme* sc, s7_pointer a) { if (s7_is_pair(a)) flux_parent((int) s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_select(s7_scheme* sc, s7_pointer a) {
  int x = (int) s7_number_to_real(sc, s7_car(a));
  int y = (int) s7_number_to_real(sc, s7_cadr(a));
  int sz = (s7_is_pair(s7_cddr(a))) ? (int) s7_number_to_real(sc, s7_caddr(a)) : 5;
  return s7_make_integer(sc, flux_select(x, y, sz));
}
s7_pointer f_shadow_light(s7_scheme* sc, s7_pointer a)  { if (s7_is_pair(a)) flux_shadow_light((int) s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_shadow_length(s7_scheme* sc, s7_pointer a) { if (s7_is_pair(a)) flux_shadow_length(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }

s7_pointer f_grab(s7_scheme* sc, s7_pointer a)  { if (s7_is_pair(a)) flux_grab((int) s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_ungrab(s7_scheme* sc, s7_pointer)  { flux_ungrab(); return s7_nil(sc); }
s7_pointer f_pdata_size(s7_scheme* sc, s7_pointer) { return s7_make_integer(sc, flux_pdata_size()); }
s7_pointer f_deform_audio(s7_scheme* sc, s7_pointer a) {  // (deform-audio bandScale [wobble freq speed recalc])
  double bs = 1, wob = 0, fr = 6, sp = 1; int rc = 1;
  s7_pointer p = a;
  if (s7_is_pair(p)) { bs  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { wob = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { fr  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { sp  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { rc  = s7_boolean(sc, s7_car(p)) ? 1 : 0; }
  flux_deform_audio(bs, wob, fr, sp, rc);
  return s7_nil(sc);
}
s7_pointer f_cache_shape(s7_scheme* sc, s7_pointer a)  { if (s7_is_pair(a)) flux_cache_shape(s7_string(s7_car(a))); return s7_nil(sc); }
s7_pointer f_shape_cached(s7_scheme* sc, s7_pointer a) { return s7_make_boolean(sc, s7_is_pair(a) && flux_shape_cached(s7_string(s7_car(a)))); }
s7_pointer f_deform_cached(s7_scheme* sc, s7_pointer a) {  // (deform-cached name bandScale [wobble freq speed recalc])
  if (!s7_is_pair(a)) return s7_nil(sc);
  const char* name = s7_string(s7_car(a));
  double bs = 1, wob = 0, fr = 6, sp = 1; int rc = 1;
  s7_pointer p = s7_cdr(a);
  if (s7_is_pair(p)) { bs  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { wob = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { fr  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { sp  = s7_number_to_real(sc, s7_car(p)); p = s7_cdr(p); }
  if (s7_is_pair(p)) { rc  = s7_boolean(sc, s7_car(p)) ? 1 : 0; }
  flux_deform_cached(name, bs, wob, fr, sp, rc);
  return s7_nil(sc);
}
s7_pointer f_pdata_ref(s7_scheme* sc, s7_pointer a) {   // (pdata-ref name i)
  const char* name = s7_string(s7_car(a));
  int i = (int) s7_number_to_real(sc, s7_cadr(a));
  s7_pointer v = s7_make_vector(sc, 3);
  for (int c = 0; c < 3; ++c) s7_vector_set(sc, v, c, s7_make_real(sc, flux_pdata_get(name, i, c)));
  return v;
}
s7_pointer f_pdata_set(s7_scheme* sc, s7_pointer a) {   // (pdata-set! name i vec)
  const char* name = s7_string(s7_car(a));
  int i = (int) s7_number_to_real(sc, s7_cadr(a));
  s7_pointer vec = s7_caddr(a);
  for (int c = 0; c < 3; ++c) flux_pdata_set(name, i, c, s7_number_to_real(sc, s7_vector_ref(sc, vec, c)));
  return s7_nil(sc);
}
} // namespace

S7ScriptHost::S7ScriptHost()  = default;
S7ScriptHost::~S7ScriptHost() = default;

void S7ScriptHost::init() {
  sc = s7_init();
  auto def = [&](const char* name, s7_function fn, int req, int opt, bool rest) {
    s7_define_function(sc, name, fn, req, opt, rest, name);
  };
  def("colour",       f_colour,       0, 0, true);
  def("color",        f_colour,       0, 0, true);
  def("background",   f_background,   0, 0, true);
  def("translate",    f_translate,    0, 0, true);
  def("rotate",       f_rotate,       0, 0, true);
  def("scale",        f_scale,        0, 0, true);
  def("identity",     f_identity,     0, 0, false);
  def("push",         f_push,         0, 0, false);
  def("pop",          f_pop,          0, 0, false);
  def("hint-wire",    f_hint_wire,    0, 0, true);
  def("hint-solid",   f_hint_solid,   0, 0, true);
  def("line-width",   f_line_width,   0, 0, true);
  def("build-cube",      f_build_cube,      0, 0, false);
  def("build-plane",     f_build_plane,     0, 0, false);
  def("build-seg-plane", f_build_seg_plane, 0, 0, true);
  def("build-ribbon",    f_build_ribbon,    1, 0, false);
  def("build-particles", f_build_particles, 1, 0, false);
  def("build-sphere", f_build_sphere, 0, 0, true);
  def("build-torus",  f_build_torus,  0, 0, true);
  def("build-cylinder",  f_build_cylinder,  0, 0, true);
  def("build-polygons",  f_build_polygons,  2, 0, false);
  def("build-copy",      f_build_copy,      1, 0, false);
  def("build-locator",   f_build_locator,   0, 0, false);
  def("specular",        f_specular,        0, 0, true);
  def("ambient",         f_ambient,         0, 0, true);
  def("emissive",        f_emissive,        0, 0, true);
  def("normal-colour",   f_normal_colour,   0, 0, true);
  def("shinyness",       f_shinyness,       1, 0, false);
  def("point-width",     f_point_width,     1, 0, false);
  def("hint-none",         f_hint_none,        0, 0, false);
  def("hint-normal",       f_hint_normal,      0, 0, true);
  def("hint-points",       f_hint_points,      0, 0, true);
  def("hint-unlit",        f_hint_unlit,       0, 0, true);
  def("hint-vertcols",     f_hint_vertcols,    0, 0, true);
  def("hint-depth-sort",   f_hint_depth_sort,  0, 0, true);
  def("hint-cull-ccw",     f_hint_cull_ccw,    0, 0, true);
  def("hint-origin",       f_hint_origin,      0, 0, true);
  def("hint-cast-shadow",  f_hint_cast_shadow, 0, 0, true);
  def("hint-ignore-depth", f_hint_ignore_depth,0, 0, true);
  def("hint-nozwrite",     f_hint_nozwrite,    0, 0, true);
  def("hint-sphere-map",   f_hint_sphere_map,  0, 0, true);
  def("make-light",        f_make_light,       0, 0, true);
  def("light-position",    f_light_position,   2, 0, false);
  def("light-diffuse",     f_light_diffuse,    2, 0, false);
  def("light-ambient",     f_light_ambient,    2, 0, false);
  def("light-specular",    f_light_specular,   2, 0, false);
  def("light-direction",   f_light_direction,  2, 0, false);
  def("light-spot-angle",  f_light_spot_angle, 2, 0, false);
  def("fog",               f_fog,              0, 0, true);
  def("parent",            f_parent,           1, 0, false);
  def("select",            f_select,           2, 0, true);
  def("shadow-light",      f_shadow_light,     1, 0, false);
  def("shadow-length",     f_shadow_length,    1, 0, false);
  def("draw-cube",    f_build_cube,   0, 0, false);   // immediate draw = build here
  def("draw-plane",   f_build_plane,  0, 0, false);
  def("draw-sphere",  f_build_sphere, 0, 0, true);
  def("draw-torus",   f_build_torus,  0, 0, true);
  def("time",         f_time,         0, 0, false);
  def("frame",        f_frame,        0, 0, false);
  def("gh",           f_gh,           1, 0, false);
  def("gain",         f_gain,         0, 0, false);
  def("mouse-x",      f_mouse_x,      0, 0, false);
  def("mouse-y",      f_mouse_y,      0, 0, false);
  def("mouse-button", f_mouse_button, 0, 0, false);
  def("camera-dist",  f_camera_dist,  0, 0, false);
  def("camera-yaw",   f_camera_yaw,   0, 0, false);
  def("camera-pitch", f_camera_pitch, 0, 0, false);
  def("set-camera-transform", f_set_camera_transform, 1, 0, false);
  def("get-camera-transform", f_get_camera_transform, 0, 0, false);
  def("set-camera",           f_set_camera_transform, 1, 0, false);  // alias
  def("get-camera",           f_get_camera_transform, 0, 0, false);  // alias
  def("set-camera-position",  f_set_camera_position,  0, 0, true);
  def("camera-reset",         f_camera_reset,         0, 0, false);
  def("set-fov",              f_set_fov,              1, 0, false);
  def("set-ortho",            f_set_ortho,            0, 0, true);
  def("set-ortho-zoom",       f_set_ortho_zoom,       1, 0, false);
  def("get-screen-size",      f_get_screen_size,      0, 0, false);
  def("persist",              f_persist,              2, 0, false);
  def("persist!",             f_persist_bang,         2, 0, false);
  def("clear-state",          f_clear_state,          0, 0, false);
  def("shader-source",        f_shader_source,        2, 0, false);
  def("shader-off",           f_shader_off,           0, 0, false);
  def("shader-set-float!",    f_shader_set_float,     2, 0, false);
  def("shader-set-vec!",      f_shader_set_vec,       2, 0, false);
  def("post-shader",          f_post_shader,          1, 0, false);
  def("post-off",             f_post_off,             0, 0, false);
  def("blur",                 f_blur,                 1, 0, false);
  def("anti-alias",           f_antialias,            0, 0, true);
  def("hint-anti-alias",      f_antialias,            0, 0, true);
  def("grab",         f_grab,         0, 0, true);
  def("ungrab",       f_ungrab,       0, 0, false);
  def("pdata-size",   f_pdata_size,   0, 0, false);
  def("deform-audio", f_deform_audio, 0, 0, true);
  def("cache-shape",  f_cache_shape,  1, 0, false);
  def("shape-cached?",f_shape_cached, 1, 0, false);
  def("deform-cached",f_deform_cached, 1, 0, true);
  def("pdata-ref",    f_pdata_ref,    2, 0, false);
  def("pdata-set!",   f_pdata_set,    3, 0, false);

  s7_eval_c_string(sc,
    "(define-macro (with-state . body)"
    "  `(begin (push) (let ((__r (begin ,@body))) (pop) __r)))");
  s7_eval_c_string(sc,
    "(define-macro (with-primitive id . body)"
    "  `(begin (grab ,id) (let ((__r (begin ,@body))) (ungrab) __r)))");
  // s7 host is immediate-only: every-frame just runs its body each re-eval, and
  // retained is a no-op (retained mode is a Racket-host feature).
  s7_eval_c_string(sc, "(define-macro (every-frame . body) `(begin ,@body))");
  s7_eval_c_string(sc, "(define (retained . _) #f)");
}

void S7ScriptHost::setRenderer(Fluxus::Renderer* r) { flux_set_renderer((void*) r); }

void S7ScriptHost::setFrameInfo(double t, int frame) { flux_frame_begin(t, frame); }

bool S7ScriptHost::eval(const std::string& code, std::string& errorOut) {
  if (!sc) { errorOut = "s7 not initialised"; return false; }
  s7_pointer out = s7_open_output_string(sc);
  s7_pointer old = s7_set_current_output_port(sc, out);
  std::string wrapped =
    "(catch #t (lambda () " + code + " ) "
    "(lambda (type info) (format #t \"; error: ~A: ~A\" type info) 'error))";
  s7_eval_c_string(sc, wrapped.c_str());
  std::string cap = s7_get_output_string(sc, out);
  s7_set_current_output_port(sc, old);
  s7_close_output_port(sc, out);
  bool err = cap.find("; error:") != std::string::npos;
  errorOut = err ? cap : "";
  return !err;
}
