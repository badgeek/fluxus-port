#include "S7ScriptHost.h"
#include "FluxusCommands.h"     // shared engine command layer (same as the Racket host)
#include "VideoHost.h"          // AVFoundation video texture

extern "C" {
#include "s7.h"
}

#include <string>
#include <cstring>

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

// ---- maths marshalling: read a length-n s7 vector, build one ----------------
static bool readVecN(s7_scheme* sc, s7_pointer a, double* out, int n) {
  if (!s7_is_pair(a)) return false;
  s7_pointer v = s7_car(a);
  if (!s7_is_vector(v)) return false;
  int len = (int) s7_vector_length(v);
  for (int i = 0; i < n; ++i)
    out[i] = (i < len) ? s7_number_to_real(sc, s7_vector_ref(sc, v, i)) : 0.0;
  return true;
}
static s7_pointer makeVecN(s7_scheme* sc, const double* v, int n) {
  s7_pointer r = s7_make_vector(sc, n);
  for (int i = 0; i < n; ++i) s7_vector_set(sc, r, i, s7_make_real(sc, v[i]));
  return r;
}
// two vec3 in -> vec3 out / -> scalar out
#define S7_VV_V(fn, cfn) static s7_pointer fn(s7_scheme* sc, s7_pointer a){ double x[3],y[3],o[3]; readVecN(sc,a,x,3); readVecN(sc,s7_cdr(a),y,3); cfn(x,y,o); return makeVecN(sc,o,3); }
#define S7_VV_S(fn, cfn) static s7_pointer fn(s7_scheme* sc, s7_pointer a){ double x[3],y[3]; readVecN(sc,a,x,3); readVecN(sc,s7_cdr(a),y,3); return s7_make_real(sc, cfn(x,y)); }
S7_VV_V(f_vadd,     flux_vadd)
S7_VV_V(f_vsub,     flux_vsub)
S7_VV_V(f_vcross,   flux_vcross)
S7_VV_V(f_vreflect, flux_vreflect)
S7_VV_S(f_vdot,     flux_vdot)
S7_VV_S(f_vdist,    flux_vdist)
S7_VV_S(f_vdist_sq, flux_vdist_sq)
static s7_pointer f_vmul(s7_scheme* sc, s7_pointer a){ double x[3],o[3]; readVecN(sc,a,x,3); flux_vmul(x, s7_number_to_real(sc,s7_cadr(a)), o); return makeVecN(sc,o,3); }
static s7_pointer f_vdiv(s7_scheme* sc, s7_pointer a){ double x[3],o[3]; readVecN(sc,a,x,3); flux_vdiv(x, s7_number_to_real(sc,s7_cadr(a)), o); return makeVecN(sc,o,3); }
static s7_pointer f_vmag(s7_scheme* sc, s7_pointer a){ double x[3]; readVecN(sc,a,x,3); return s7_make_real(sc, flux_vmag(x)); }
static s7_pointer f_vnormalise(s7_scheme* sc, s7_pointer a){ double x[3],o[3]; readVecN(sc,a,x,3); flux_vnormalise(x,o); return makeVecN(sc,o,3); }
static s7_pointer f_vtransform(s7_scheme* sc, s7_pointer a){ double v[3],m[16],o[3]; readVecN(sc,a,v,3); readVecN(sc,s7_cdr(a),m,16); flux_vtransform(v,m,o); return makeVecN(sc,o,3); }
static s7_pointer f_vtransform_rot(s7_scheme* sc, s7_pointer a){ double v[3],m[16],o[3]; readVecN(sc,a,v,3); readVecN(sc,s7_cdr(a),m,16); flux_vtransform_rot(v,m,o); return makeVecN(sc,o,3); }
// matrices (16-vectors)
static s7_pointer f_mident(s7_scheme* sc, s7_pointer){ double o[16]; flux_mident(o); return makeVecN(sc,o,16); }
static s7_pointer f_mmul(s7_scheme* sc, s7_pointer a){ double x[16],y[16],o[16]; readVecN(sc,a,x,16); readVecN(sc,s7_cdr(a),y,16); flux_mmul(x,y,o); return makeVecN(sc,o,16); }
#define S7_V3_M(fn, cfn) static s7_pointer fn(s7_scheme* sc, s7_pointer a){ double v[3],o[16]; readVecN(sc,a,v,3); cfn(v,o); return makeVecN(sc,o,16); }
S7_V3_M(f_mtranslate, flux_mtranslate)
S7_V3_M(f_mrotate,    flux_mrotate)
S7_V3_M(f_mscale,     flux_mscale)
#define S7_M_M(fn, cfn) static s7_pointer fn(s7_scheme* sc, s7_pointer a){ double x[16],o[16]; readVecN(sc,a,x,16); cfn(x,o); return makeVecN(sc,o,16); }
S7_M_M(f_mtranspose, flux_mtranspose)
S7_M_M(f_minverse,   flux_minverse)
static s7_pointer f_maim(s7_scheme* sc, s7_pointer a){ double d[3],u[3],o[16]; readVecN(sc,a,d,3); readVecN(sc,s7_cdr(a),u,3); flux_maim(d,u,o); return makeVecN(sc,o,16); }
// quaternions (4-vectors x y z w)
static s7_pointer f_qaxisangle(s7_scheme* sc, s7_pointer a){ double ax[3],o[4]; readVecN(sc,a,ax,3); flux_qaxisangle(ax, s7_number_to_real(sc,s7_cadr(a)), o); return makeVecN(sc,o,4); }
static s7_pointer f_qmul(s7_scheme* sc, s7_pointer a){ double x[4],y[4],o[4]; readVecN(sc,a,x,4); readVecN(sc,s7_cdr(a),y,4); flux_qmul(x,y,o); return makeVecN(sc,o,4); }
#define S7_Q_Q(fn, cfn) static s7_pointer fn(s7_scheme* sc, s7_pointer a){ double x[4],o[4]; readVecN(sc,a,x,4); cfn(x,o); return makeVecN(sc,o,4); }
S7_Q_Q(f_qnormalise, flux_qnormalise)
S7_Q_Q(f_qconjugate, flux_qconjugate)
static s7_pointer f_qtomatrix(s7_scheme* sc, s7_pointer a){ double x[4],o[16]; readVecN(sc,a,x,4); flux_qtomatrix(x,o); return makeVecN(sc,o,16); }
// noise (1..3 reals)
static double argReal(s7_scheme* sc, s7_pointer a, int i){ for(int k=0;k<i && s7_is_pair(a);++k) a=s7_cdr(a); return s7_is_pair(a)?s7_number_to_real(sc,s7_car(a)):0.0; }
static s7_pointer f_noise(s7_scheme* sc, s7_pointer a){ return s7_make_real(sc, flux_noise(argReal(sc,a,0),argReal(sc,a,1),argReal(sc,a,2))); }
static s7_pointer f_snoise(s7_scheme* sc, s7_pointer a){ return s7_make_real(sc, flux_snoise(argReal(sc,a,0),argReal(sc,a,1),argReal(sc,a,2))); }
static s7_pointer f_noise_seed(s7_scheme* sc, s7_pointer a){ flux_noise_seed((int) argReal(sc,a,0)); return s7_nil(sc); }
static s7_pointer f_noise_detail(s7_scheme* sc, s7_pointer a){ flux_noise_detail((int) argReal(sc,a,0), argReal(sc,a,1)); return s7_nil(sc); }
// grabbed-prim state setters missing from the s7 host (flux_* already exist)
static s7_pointer f_backfacecull(s7_scheme* sc, s7_pointer a){ int on = s7_is_pair(a)?(s7_boolean(sc,s7_car(a))?1:0):1; flux_backfacecull(on); return s7_nil(sc); }
static s7_pointer f_opacity(s7_scheme* sc, s7_pointer a){ if(s7_is_pair(a)) flux_opacity(s7_number_to_real(sc,s7_car(a))); return s7_nil(sc); }
static s7_pointer f_wire_opacity(s7_scheme* sc, s7_pointer a){ if(s7_is_pair(a)) flux_wire_opacity(s7_number_to_real(sc,s7_car(a))); return s7_nil(sc); }
static s7_pointer f_wire_colour(s7_scheme* sc, s7_pointer a){ double x,y,z; if(vec3(sc,a,x,y,z)) flux_wire_colour(x,y,z); return s7_nil(sc); }
static s7_pointer f_key_poll(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_get_key()); }
static s7_pointer f_set_export(s7_scheme* sc, s7_pointer a){ int on = s7_boolean(sc,s7_car(a))?1:0; const char* p = s7_is_string(s7_cadr(a))?s7_string(s7_cadr(a)):""; int fps = (int) s7_number_to_real(sc,s7_caddr(a)); flux_set_export(on,p,fps); return s7_nil(sc); }
// s7-parity sweep: engine-backed commands the s7 host still lacked
static int argInt(s7_scheme* sc, s7_pointer a, int i, int dflt){ for(int k=0;k<i && s7_is_pair(a);++k) a=s7_cdr(a); return s7_is_pair(a)?(int)s7_number_to_real(sc,s7_car(a)):dflt; }
static s7_pointer f_build_nurbs_plane(s7_scheme* sc, s7_pointer a){ return s7_make_integer(sc, flux_build_nurbs_plane(argInt(sc,a,0,5), argInt(sc,a,1,5))); }
static s7_pointer f_build_nurbs_sphere(s7_scheme* sc, s7_pointer a){ return s7_make_integer(sc, flux_build_nurbs_sphere(argInt(sc,a,0,10), argInt(sc,a,1,10))); }
static s7_pointer f_pdata_add(s7_scheme* sc, s7_pointer a){ if(s7_is_string(s7_car(a))&&s7_is_string(s7_cadr(a))) flux_pdata_add(s7_string(s7_car(a)), s7_string(s7_cadr(a))); return s7_nil(sc); }
static s7_pointer f_pdata_copy(s7_scheme* sc, s7_pointer a){ if(s7_is_string(s7_car(a))&&s7_is_string(s7_cadr(a))) flux_pdata_copy(s7_string(s7_car(a)), s7_string(s7_cadr(a))); return s7_nil(sc); }
static s7_pointer f_recalc_normals(s7_scheme* sc, s7_pointer){ flux_recalc_normals(); return s7_nil(sc); }
static s7_pointer f_delta(s7_scheme* sc, s7_pointer){ return s7_make_real(sc, flux_delta()); }
static s7_pointer f_frustum(s7_scheme* sc, s7_pointer a){ flux_set_frustum(argReal(sc,a,0),argReal(sc,a,1),argReal(sc,a,2),argReal(sc,a,3)); return s7_nil(sc); }
static s7_pointer f_ortho(s7_scheme* sc, s7_pointer a){ int on = s7_is_pair(a)?(s7_boolean(sc,s7_car(a))?1:0):1; flux_set_ortho(on); return s7_nil(sc); }
static s7_pointer f_clip(s7_scheme* sc, s7_pointer a){ flux_set_clip(argReal(sc,a,0), argReal(sc,a,1)); return s7_nil(sc); }
static s7_pointer f_viewport(s7_scheme* sc, s7_pointer a){ flux_set_viewport(argReal(sc,a,0),argReal(sc,a,1),argReal(sc,a,2),argReal(sc,a,3)); return s7_nil(sc); }
// turtle builder
static s7_pointer f_turtle_prim(s7_scheme* sc, s7_pointer a){
  int t = 0;
  if (s7_is_pair(a)) { s7_pointer v = s7_car(a);
    if (s7_is_symbol(v)) { const char* n = s7_symbol_name(v);
      if(!strcmp(n,"quad-list"))t=1; else if(!strcmp(n,"triangle-list"))t=2;
      else if(!strcmp(n,"triangle-fan"))t=3; else if(!strcmp(n,"polygon"))t=4; else t=0; }
    else t = (int) s7_number_to_real(sc, v); }
  flux_turtle_prim(t); return s7_nil(sc); }
static s7_pointer f_turtle_vert(s7_scheme* sc, s7_pointer){ flux_turtle_vert(); return s7_nil(sc); }
static s7_pointer f_turtle_build(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_turtle_build()); }
static s7_pointer f_turtle_move(s7_scheme* sc, s7_pointer a){ flux_turtle_move(argReal(sc,a,0)); return s7_nil(sc); }
static s7_pointer f_turtle_turn(s7_scheme* sc, s7_pointer a){ double x,y,z; if(vec3(sc,a,x,y,z)) flux_turtle_turn(x,y,z); return s7_nil(sc); }
static s7_pointer f_turtle_push(s7_scheme* sc, s7_pointer){ flux_turtle_push(); return s7_nil(sc); }
static s7_pointer f_turtle_pop(s7_scheme* sc, s7_pointer){ flux_turtle_pop(); return s7_nil(sc); }
static s7_pointer f_turtle_reset(s7_scheme* sc, s7_pointer){ flux_turtle_reset(); return s7_nil(sc); }
static s7_pointer f_turtle_attach(s7_scheme* sc, s7_pointer a){ flux_turtle_attach(argInt(sc,a,0,-1)); return s7_nil(sc); }
static s7_pointer f_turtle_skip(s7_scheme* sc, s7_pointer a){ flux_turtle_skip(argInt(sc,a,0,0)); return s7_nil(sc); }
static s7_pointer f_turtle_position(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_turtle_position()); }
static s7_pointer f_turtle_seek(s7_scheme* sc, s7_pointer a){ flux_turtle_seek(argInt(sc,a,0,0)); return s7_nil(sc); }
static s7_pointer f_get_turtle_transform(s7_scheme* sc, s7_pointer){ double m[16]; flux_get_turtle_transform(m); return makeVecN(sc,m,16); }
// voxels + blobby
static s7_pointer f_build_voxels(s7_scheme* sc, s7_pointer a){ return s7_make_integer(sc, flux_build_voxels(argInt(sc,a,0,8),argInt(sc,a,1,8),argInt(sc,a,2,8))); }
static s7_pointer f_voxels_width(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_voxels_width()); }
static s7_pointer f_voxels_height(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_voxels_height()); }
static s7_pointer f_voxels_depth(s7_scheme* sc, s7_pointer){ return s7_make_integer(sc, flux_voxels_depth()); }
static s7_pointer f_voxels_calc_gradient(s7_scheme* sc, s7_pointer){ flux_voxels_calc_gradient(); return s7_nil(sc); }
static s7_pointer f_voxels_sphere_influence(s7_scheme* sc, s7_pointer a){ double p[3],c[3]; readVecN(sc,a,p,3); readVecN(sc,s7_cdr(a),c,3); flux_voxels_sphere_influence(p[0],p[1],p[2],c[0],c[1],c[2],argReal(sc,a,2)); return s7_nil(sc); }
static s7_pointer f_voxels_sphere_solid(s7_scheme* sc, s7_pointer a){ double p[3],c[3]; readVecN(sc,a,p,3); readVecN(sc,s7_cdr(a),c,3); flux_voxels_sphere_solid(p[0],p[1],p[2],c[0],c[1],c[2],argReal(sc,a,2)); return s7_nil(sc); }
static s7_pointer f_voxels_box_solid(s7_scheme* sc, s7_pointer a){ double t[3],b[3],c[3]; readVecN(sc,a,t,3); readVecN(sc,s7_cdr(a),b,3); readVecN(sc,s7_cddr(a),c,3); flux_voxels_box_solid(t[0],t[1],t[2],b[0],b[1],b[2],c[0],c[1],c[2]); return s7_nil(sc); }
static s7_pointer f_voxels_threshold(s7_scheme* sc, s7_pointer a){ flux_voxels_threshold(argReal(sc,a,0)); return s7_nil(sc); }
static s7_pointer f_voxels_point_light(s7_scheme* sc, s7_pointer a){ double p[3],c[3]; readVecN(sc,a,p,3); readVecN(sc,s7_cdr(a),c,3); flux_voxels_point_light(p[0],p[1],p[2],c[0],c[1],c[2]); return s7_nil(sc); }
static s7_pointer f_voxels_to_blobby(s7_scheme* sc, s7_pointer a){ return s7_make_integer(sc, flux_voxels_to_blobby(argInt(sc,a,0,-1))); }
static s7_pointer f_voxels_to_poly(s7_scheme* sc, s7_pointer a){ double iso = s7_is_pair(s7_cdr(a))?argReal(sc,a,1):1.0; return s7_make_integer(sc, flux_voxels_to_poly(argInt(sc,a,0,-1), iso)); }
static s7_pointer f_build_blobby(s7_scheme* sc, s7_pointer a){ double d[3],s[3]; readVecN(sc,s7_cdr(a),d,3); readVecN(sc,s7_cddr(a),s,3); return s7_make_integer(sc, flux_build_blobby(argInt(sc,a,0,1),d[0],d[1],d[2],s[0],s[1],s[2])); }
static s7_pointer f_blobby_to_poly(s7_scheme* sc, s7_pointer a){ return s7_make_integer(sc, flux_blobby_to_poly(argInt(sc,a,0,-1))); }

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
s7_pointer f_set_aspect(s7_scheme* sc, s7_pointer a)      { if (s7_is_pair(a)) flux_set_aspect(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_set_window_size(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_request_window_size((int) s7_number_to_real(sc, s7_car(a)),
                             (int) s7_number_to_real(sc, s7_cadr(a)));
  return s7_nil(sc);
}
// audio is auto-started by the JUCE AudioHost, so (start-audio ...) is a no-op
// here (matches the racket lib's stub). Present so scripts written for real
// fluxus load unchanged.
s7_pointer f_start_audio(s7_scheme* sc, s7_pointer a)    { (void) a; return s7_nil(sc); }
s7_pointer f_show_editor(s7_scheme* sc, s7_pointer a)    { (void) a; flux_set_editor_visible(1); return s7_nil(sc); }
s7_pointer f_hide_editor(s7_scheme* sc, s7_pointer a)    { (void) a; flux_set_editor_visible(0); return s7_nil(sc); }
s7_pointer f_editor_full_width(s7_scheme* sc, s7_pointer a) {
  flux_set_editor_full_width(s7_is_pair(a) ? (s7_boolean(sc, s7_car(a)) ? 1 : 0) : 1);
  return s7_nil(sc);
}
s7_pointer f_screenshot(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a) && s7_is_string(s7_car(a))) flux_screenshot(s7_string(s7_car(a)));
  return s7_nil(sc);
}
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
// (clear): wipe the scene graph (see flux_scene_clear). Harmless/redundant in
// immediate mode; needed at the top of a retained sketch's per-frame thunk.
s7_pointer f_clear(s7_scheme* sc, s7_pointer) { flux_scene_clear(); return s7_nil(sc); }
// (destroy id): remove one primitive by id (retained persistent scene).
s7_pointer f_destroy(s7_scheme* sc, s7_pointer a) { if (s7_is_pair(a)) flux_destroy((int) s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }

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
static int blendEnum(s7_scheme* sc, s7_pointer v) {
  if (s7_is_number(v)) return (int) s7_number_to_real(sc, v);
  if (!s7_is_symbol(v)) return 1;
  const std::string n = s7_symbol_name(v);
  if (n == "zero") return 0;                     if (n == "one") return 1;
  if (n == "src-color") return 768;              if (n == "one-minus-src-color") return 769;
  if (n == "src-alpha") return 770;              if (n == "one-minus-src-alpha") return 771;
  if (n == "dst-alpha") return 772;              if (n == "one-minus-dst-alpha") return 773;
  if (n == "dst-color") return 774;              if (n == "one-minus-dst-color") return 775;
  return 1;
}
s7_pointer f_blend_mode(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_blend_mode(blendEnum(sc, s7_car(a)), blendEnum(sc, s7_cadr(a)));
  return s7_nil(sc);
}
s7_pointer f_multitexture(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_multitexture((int) s7_number_to_real(sc, s7_car(a)), (int) s7_number_to_real(sc, s7_cadr(a)));
  return s7_nil(sc);
}
s7_pointer f_shader_set_int(s7_scheme* sc, s7_pointer a) {
  if (s7_is_pair(a) && s7_is_pair(s7_cdr(a)))
    flux_shader_set_int(s7_string(s7_car(a)), (int) s7_number_to_real(sc, s7_cadr(a)));
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
s7_pointer f_load_texture(s7_scheme* sc, s7_pointer a) { return s7_make_integer(sc, s7_is_pair(a) ? (int) flux_load_texture(s7_string(s7_car(a))) : 0); }
s7_pointer f_texture(s7_scheme* sc, s7_pointer a)      { if (s7_is_pair(a)) flux_texture((int) s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_video_open(s7_scheme* sc, s7_pointer a)   { return s7_make_integer(sc, s7_is_pair(a) ? flux_video_open(s7_string(s7_car(a))) : 0); }
s7_pointer f_video_texture(s7_scheme* sc, s7_pointer)  { return s7_make_integer(sc, (int) flux_video_texture()); }
s7_pointer f_video_width(s7_scheme* sc, s7_pointer)    { return s7_make_integer(sc, flux_video_width()); }
s7_pointer f_video_height(s7_scheme* sc, s7_pointer)   { return s7_make_integer(sc, flux_video_height()); }
s7_pointer f_video_duration(s7_scheme* sc, s7_pointer) { return s7_make_real(sc, flux_video_duration()); }
s7_pointer f_video_play(s7_scheme* sc, s7_pointer)     { flux_video_play();  return s7_nil(sc); }
s7_pointer f_video_pause(s7_scheme* sc, s7_pointer)    { flux_video_pause(); return s7_nil(sc); }
s7_pointer f_video_seek(s7_scheme* sc, s7_pointer a)   { if (s7_is_pair(a)) flux_video_seek(s7_number_to_real(sc, s7_car(a))); return s7_nil(sc); }
s7_pointer f_video_close(s7_scheme* sc, s7_pointer)    { flux_video_close(); return s7_nil(sc); }
s7_pointer f_camera_open(s7_scheme* sc, s7_pointer a)  { return s7_make_integer(sc, flux_camera_open(s7_is_pair(a) ? (int) s7_number_to_real(sc, s7_car(a)) : 0)); }
s7_pointer f_camera_texture(s7_scheme* sc, s7_pointer) { return s7_make_integer(sc, (int) flux_camera_texture()); }
s7_pointer f_camera_width(s7_scheme* sc, s7_pointer)   { return s7_make_integer(sc, flux_camera_width()); }
s7_pointer f_camera_height(s7_scheme* sc, s7_pointer)  { return s7_make_integer(sc, flux_camera_height()); }
s7_pointer f_camera_close(s7_scheme* sc, s7_pointer)   { flux_camera_close(); return s7_nil(sc); }
s7_pointer f_build_text(s7_scheme* sc, s7_pointer a)   { return s7_make_integer(sc, flux_build_text(s7_is_pair(a) ? s7_string(s7_car(a)) : "")); }
s7_pointer f_build_pixels(s7_scheme* sc, s7_pointer a) {
  int w = 16, h = 16;
  if (s7_is_pair(a)) { w = (int) s7_number_to_real(sc, s7_car(a)); if (s7_is_pair(s7_cdr(a))) h = (int) s7_number_to_real(sc, s7_cadr(a)); }
  return s7_make_integer(sc, flux_build_pixels(w, h));
}
s7_pointer f_pixels_upload(s7_scheme* sc, s7_pointer)  { flux_pixels_upload(); return s7_nil(sc); }
s7_pointer f_pixels_width(s7_scheme* sc, s7_pointer)   { return s7_make_integer(sc, flux_pixels_width()); }
s7_pointer f_pixels_height(s7_scheme* sc, s7_pointer)  { return s7_make_integer(sc, flux_pixels_height()); }

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
  def("load-texture",      f_load_texture,     1, 0, false);
  def("texture",           f_texture,          1, 0, false);
  def("video-open",        f_video_open,       1, 0, false);
  def("video-texture",     f_video_texture,    0, 0, false);
  def("video-width",       f_video_width,      0, 0, false);
  def("video-height",      f_video_height,     0, 0, false);
  def("video-duration",    f_video_duration,   0, 0, false);
  def("video-play",        f_video_play,       0, 0, false);
  def("video-pause",       f_video_pause,      0, 0, false);
  def("video-seek",        f_video_seek,       1, 0, false);
  def("video-close",       f_video_close,      0, 0, false);
  def("camera-open",       f_camera_open,      0, 1, false);
  def("camera-texture",    f_camera_texture,   0, 0, false);
  def("camera-width",      f_camera_width,     0, 0, false);
  def("camera-height",     f_camera_height,    0, 0, false);
  def("camera-close",      f_camera_close,     0, 0, false);
  def("build-text",        f_build_text,       1, 0, false);
  def("build-pixels",      f_build_pixels,     0, 0, true);
  def("pixels-upload",     f_pixels_upload,    0, 0, false);
  def("pixels-width",      f_pixels_width,     0, 0, false);
  def("pixels-height",     f_pixels_height,    0, 0, false);
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
  def("set-aspect",           f_set_aspect,           1, 0, false);
  def("set-window-size",      f_set_window_size,      2, 0, false);
  def("start-audio",          f_start_audio,          0, 0, true);
  def("show-editor",          f_show_editor,          0, 0, false);
  def("hide-editor",          f_hide_editor,          0, 0, false);
  def("editor-full-width",    f_editor_full_width,    0, 1, false);
  def("screenshot",           f_screenshot,           1, 0, false);
  def("set-ortho",            f_set_ortho,            0, 0, true);
  def("set-ortho-zoom",       f_set_ortho_zoom,       1, 0, false);
  def("get-screen-size",      f_get_screen_size,      0, 0, false);
  def("persist",              f_persist,              2, 0, false);
  def("persist!",             f_persist_bang,         2, 0, false);
  def("clear-state",          f_clear_state,          0, 0, false);
  def("clear",                f_clear,                0, 0, false);
  def("destroy",              f_destroy,              0, 0, true);
  def("shader-source",        f_shader_source,        2, 0, false);
  def("shader-off",           f_shader_off,           0, 0, false);
  def("shader-set-float!",    f_shader_set_float,     2, 0, false);
  def("shader-set-vec!",      f_shader_set_vec,       2, 0, false);
  def("shader-set-int!",      f_shader_set_int,       2, 0, false);
  def("blend-mode",           f_blend_mode,           2, 0, false);
  def("multitexture",         f_multitexture,         2, 0, false);
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

  // ---- maths primitives (native; see FluxusCommands) ----------------------
  def("vadd",           f_vadd,           2, 0, false);
  def("vsub",           f_vsub,           2, 0, false);
  def("vmul",           f_vmul,           2, 0, false);
  def("vdiv",           f_vdiv,           2, 0, false);
  def("vdot",           f_vdot,           2, 0, false);
  def("vcross",         f_vcross,         2, 0, false);
  def("vmag",           f_vmag,           1, 0, false);
  def("vdist",          f_vdist,          2, 0, false);
  def("vdist-sq",       f_vdist_sq,       2, 0, false);
  def("vnormalise",     f_vnormalise,     1, 0, false);
  def("vnormalize",     f_vnormalise,     1, 0, false);
  def("vreflect",       f_vreflect,       2, 0, false);
  def("vtransform",     f_vtransform,     2, 0, false);
  def("vtransform-rot", f_vtransform_rot, 2, 0, false);
  def("mident",         f_mident,         0, 0, false);
  def("mmul",           f_mmul,           2, 0, false);
  def("mtranslate",     f_mtranslate,     1, 0, false);
  def("mrotate",        f_mrotate,        1, 0, false);
  def("mscale",         f_mscale,         1, 0, false);
  def("mtranspose",     f_mtranspose,     1, 0, false);
  def("minverse",       f_minverse,       1, 0, false);
  def("maim",           f_maim,           2, 0, false);
  def("qaxisangle",     f_qaxisangle,     2, 0, false);
  def("qmul",           f_qmul,           2, 0, false);
  def("qnormalise",     f_qnormalise,     1, 0, false);
  def("qconjugate",     f_qconjugate,     1, 0, false);
  def("qtomatrix",      f_qtomatrix,      1, 0, false);
  def("noise",          f_noise,          1, 2, false);
  def("snoise",         f_snoise,         1, 2, false);
  def("noise-seed",     f_noise_seed,     1, 0, false);
  def("noise-detail",   f_noise_detail,   1, 1, false);
  def("backfacecull",   f_backfacecull,   0, 1, false);
  def("opacity",        f_opacity,        1, 0, false);
  def("wire-opacity",   f_wire_opacity,   1, 0, false);
  def("wire-colour",    f_wire_colour,    1, 0, false);
  def("wire-color",     f_wire_colour,    1, 0, false);
  def("key-poll",       f_key_poll,       0, 0, false);
  def("set-export",     f_set_export,     3, 0, false);
  def("build-nurbs-plane",  f_build_nurbs_plane,  0, 2, false);
  def("build-nurbs-sphere", f_build_nurbs_sphere, 0, 2, false);
  def("pdata-add",      f_pdata_add,      2, 0, false);
  def("pdata-copy",     f_pdata_copy,     2, 0, false);
  def("recalc-normals", f_recalc_normals, 0, 0, false);
  def("delta",          f_delta,          0, 0, false);
  def("frustum",        f_frustum,        4, 0, false);
  def("ortho",          f_ortho,          0, 1, false);
  def("clip",           f_clip,           2, 0, false);
  def("viewport",       f_viewport,       4, 0, false);
  def("turtle-prim",     f_turtle_prim,     0, 1, false);
  def("turtle-vert",     f_turtle_vert,     0, 0, false);
  def("turtle-build",    f_turtle_build,    0, 0, false);
  def("turtle-move",     f_turtle_move,     1, 0, false);
  def("turtle-turn",     f_turtle_turn,     1, 0, false);
  def("turtle-push",     f_turtle_push,     0, 0, false);
  def("turtle-pop",      f_turtle_pop,      0, 0, false);
  def("turtle-reset",    f_turtle_reset,    0, 0, false);
  def("turtle-attach",   f_turtle_attach,   1, 0, false);
  def("turtle-skip",     f_turtle_skip,     1, 0, false);
  def("turtle-position", f_turtle_position, 0, 0, false);
  def("turtle-seek",     f_turtle_seek,     1, 0, false);
  def("get-turtle-transform", f_get_turtle_transform, 0, 0, false);
  def("build-voxels",            f_build_voxels,            3, 0, false);
  def("voxels-width",            f_voxels_width,            0, 0, false);
  def("voxels-height",           f_voxels_height,           0, 0, false);
  def("voxels-depth",            f_voxels_depth,            0, 0, false);
  def("voxels-calc-gradient",    f_voxels_calc_gradient,    0, 0, false);
  def("voxels-sphere-influence", f_voxels_sphere_influence, 3, 0, false);
  def("voxels-sphere-solid",     f_voxels_sphere_solid,     3, 0, false);
  def("voxels-box-solid",        f_voxels_box_solid,        3, 0, false);
  def("voxels-threshold",        f_voxels_threshold,        1, 0, false);
  def("voxels-point-light",      f_voxels_point_light,      2, 0, false);
  def("voxels->blobby",          f_voxels_to_blobby,        1, 0, false);
  def("voxels->poly",            f_voxels_to_poly,          1, 1, false);
  def("build-blobby",            f_build_blobby,            3, 0, false);
  def("blobby->poly",            f_blobby_to_poly,          1, 0, false);

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
  // vector component accessors (match Racket's building-blocks.ss) + `sort`
  // alias (s7's builtin is `sort!`) so canonical fluxus .scm sketches load.
  s7_eval_c_string(sc,
    "(begin"
    "  (define (vx v) (vector-ref v 0)) (define (vy v) (vector-ref v 1))"
    "  (define (vz v) (vector-ref v 2)) (define (vw v) (vector-ref v 3))"
    "  (define (vr v) (vector-ref v 0)) (define (vg v) (vector-ref v 1))"
    "  (define (vb v) (vector-ref v 2)) (define (va v) (vector-ref v 3))"
    "  (unless (defined? 'sort) (define sort sort!))"
    "  (unless (defined? 'void) (define (void . _) #f)))");
  // pdata iteration helpers (mirror Racket's building-blocks.ss) so pdata-driven
  // .scm sketches run on s7. proc gets (index write-val read-vals...) -> new val.
  s7_eval_c_string(sc,
    "(begin"
    "  (define (pdata-index-map! proc wname . rnames)"
    "    (let ((n (pdata-size)))"
    "      (let loop ((i 0))"
    "        (when (< i n)"
    "          (pdata-set! wname i"
    "            (apply proc i (pdata-ref wname i)"
    "                   (map (lambda (rn) (pdata-ref rn i)) rnames)))"
    "          (loop (+ i 1))))))"
    "  (define (pdata-map! proc wname . rnames)"
    "    (let ((n (pdata-size)))"
    "      (let loop ((i 0))"
    "        (when (< i n)"
    "          (pdata-set! wname i"
    "            (apply proc (pdata-ref wname i)"
    "                   (map (lambda (rn) (pdata-ref rn i)) rnames)))"
    "          (loop (+ i 1)))))))");
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
