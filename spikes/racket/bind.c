/* SPIKE 2: Racket-calls-C — the actual fluxus binding pattern.
   C exposes "engine" primitives; Racket calls them via ffi/unsafe. This mirrors
   how fluxus-engine exposes the C++ renderer to Scheme.

   Two lookup mechanisms tried together (whichever the CS ffi uses):
     - Sregister_symbol(name, ptr)  (Chez foreign symbol table)
     - exported symbol via -Wl,-export_dynamic (dlsym on the process). */
#include <stdio.h>
#include <string.h>
#include "chezscheme.h"
#include "racketcs.h"

#define RK "/opt/homebrew/Cellar/minimal-racket/9.3"

/* --- the fake C "engine" primitives Racket will drive --- */
void spike_cube(double x, double y, double z) {
  printf("  [C ENGINE] build-cube  at (%.1f, %.1f, %.1f)\n", x, y, z);
  fflush(stdout);
}
void spike_colour(double r, double g, double b) {
  printf("  [C ENGINE] colour      (%.2f, %.2f, %.2f)\n", r, g, b);
  fflush(stdout);
}
int spike_add(int a, int b) { return a + b; }

static ptr sym(const char* s) { return Sstring_to_symbol(s); }
static ptr eval_str(const char* code) {
  ptr ois = Scons(sym("open-input-string"), Scons(Sstring(code), Snil));
  ptr rd  = Scons(sym("read"), Scons(ois, Snil));
  ptr ev  = Scons(sym("eval"), Scons(rd, Snil));
  return racket_eval(ev);
}

int main(void) {
  racket_boot_arguments_t ba;
  memset(&ba, 0, sizeof(ba));
  ba.boot1_path = RK "/lib/racket/petite.boot";
  ba.boot2_path = RK "/lib/racket/scheme.boot";
  ba.boot3_path = RK "/lib/racket/racket.boot";
  ba.exec_file  = "racket-bind-spike";
  ba.collects_dir = RK "/share/racket/collects";
  ba.config_dir   = RK "/etc/racket";
  ba.cs_compiled_subdir = 1;
  racket_boot(&ba);

  /* register the C primitives in Chez's foreign symbol table */
  Sregister_symbol("spike_cube",   (void*) spike_cube);
  Sregister_symbol("spike_colour", (void*) spike_colour);
  Sregister_symbol("spike_add",    (void*) spike_add);

  racket_namespace_require(sym("racket/base"));
  racket_namespace_require(sym("ffi/unsafe"));

  printf("=== Racket-calls-C spike ===\n");

  /* a tiny fluxus-like program: bind C prims, then "draw" from Scheme */
  eval_str(
    "(begin"
    "  (define cube   (get-ffi-obj \"spike_cube\"   #f (_fun _double _double _double -> _void)))"
    "  (define colour (get-ffi-obj \"spike_colour\" #f (_fun _double _double _double -> _void)))"
    "  (define add    (get-ffi-obj \"spike_add\"    #f (_fun _int _int -> _int)))"
    "  (display \"  [racket] calling C engine primitives:\") (newline)"
    "  (colour 0.9 0.5 0.2)"
    "  (cube 0.0 0.0 0.0)"
    "  (for ([i (in-range 3)]) (cube (exact->inexact (* i 2)) 0.0 0.0))"
    "  (display \"  [racket] (spike_add 40 2) returned: \") (display (add 40 2)) (newline)"
    "  (flush-output))");

  printf("=== spike OK (Racket drove the C engine) ===\n");
  return 0;
}
