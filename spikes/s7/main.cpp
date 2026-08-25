// SPIKE: s7 Scheme as the fluxus scripting host.
// Proves: embed s7, bind C++ engine calls, eval a LIVE code string (live-coding),
// capture (display ...) output, handle a script error without crashing.
#include <iostream>
#include <string>
extern "C" {
#include "s7.h"
}
#include "SpikeEngine.h"

static SpikeEngine &E = SpikeEngine::get();

// --- foreign functions: Scheme -> SpikeEngine ------------------------------
static s7_pointer f_clear (s7_scheme *sc, s7_pointer)      { E.clear();  return s7_nil(sc); }
static s7_pointer f_push  (s7_scheme *sc, s7_pointer)      { E.push();   return s7_nil(sc); }
static s7_pointer f_pop   (s7_scheme *sc, s7_pointer)      { E.pop();    return s7_nil(sc); }
static s7_pointer f_cube  (s7_scheme *sc, s7_pointer)      { E.drawCube();   return s7_nil(sc); }
static s7_pointer f_sphere(s7_scheme *sc, s7_pointer)      { E.drawSphere(); return s7_nil(sc); }

static s7_pointer f_translate(s7_scheme *sc, s7_pointer a) {
  E.translate(s7_number_to_real(sc, s7_car(a)),
              s7_number_to_real(sc, s7_cadr(a)),
              s7_number_to_real(sc, s7_caddr(a)));
  return s7_nil(sc);
}
static s7_pointer f_colour(s7_scheme *sc, s7_pointer a) {
  E.colour(s7_number_to_real(sc, s7_car(a)),
           s7_number_to_real(sc, s7_cadr(a)),
           s7_number_to_real(sc, s7_caddr(a)));
  return s7_nil(sc);
}

// --- eval a string, capturing console output + catching errors -------------
// wrap=true guards the eval in a catch (for calls that may error). Top-level
// defines must NOT be wrapped, else they bind inside the catch-lambda's scope.
static void evalLive(s7_scheme *sc, const std::string &label, const std::string &code,
                     bool wrap = true) {
  std::cout << ">>> " << label << "\n";
  s7_pointer out = s7_open_output_string(sc);
  s7_pointer old = s7_set_current_output_port(sc, out);
  std::string run = wrap
    ? "(catch #t (lambda () " + code + ") "
      "(lambda (type info) (format #t \"[caught ~A: ~A]\" type info) 'error))"
    : code;
  s7_pointer r = s7_eval_c_string(sc, run.c_str());
  std::string captured = s7_get_output_string(sc, out);
  s7_set_current_output_port(sc, old);
  s7_close_output_port(sc, out);
  if (!captured.empty()) std::cout << "  [console] " << captured << "\n";
  std::cout << "  [result] " << s7_object_to_c_string(sc, r) << "\n";
}

int main() {
  s7_scheme *sc = s7_init();

  s7_define_function(sc, "clear",     f_clear,     0, 0, false, "(clear)");
  s7_define_function(sc, "push",      f_push,      0, 0, false, "(push)");
  s7_define_function(sc, "pop",       f_pop,       0, 0, false, "(pop)");
  s7_define_function(sc, "draw-cube", f_cube,      0, 0, false, "(draw-cube)");
  s7_define_function(sc, "draw-sphere", f_sphere,  0, 0, false, "(draw-sphere)");
  s7_define_function(sc, "translate", f_translate, 3, 0, false, "(translate x y z)");
  s7_define_function(sc, "colour",    f_colour,    3, 0, false, "(colour r g b)");

  std::cout << "=== s7 Scheme spike ===\n";

  // 1) define a frame proc (live-coding: this is what the user types)
  evalLive(sc, "define frame v1",
    "(define (frame) (clear) (colour 1 0 0) (push) (translate 1 2 3)"
    " (draw-cube) (pop) (draw-sphere) (display \"frame v1 ran\"))", false);
  evalLive(sc, "call (frame)", "(frame)");
  std::cout << "  engine log:\n" << E.dump();

  // 2) HOT RE-EVAL: redefine frame at runtime, call again (proves live-coding)
  evalLive(sc, "redefine frame v2 (live edit)",
    "(define (frame) (clear) (colour 0 1 0) (draw-cube) (draw-cube)"
    " (display \"frame v2 ran\"))", false);
  evalLive(sc, "call (frame)", "(frame)");
  std::cout << "  engine log:\n" << E.dump();

  // 3) ERROR handling: a bad script must not crash the host
  evalLive(sc, "deliberate error", "(translate 1 2)");   // missing arg

  std::cout << "=== s7 spike OK ===\n";
  return 0;
}
