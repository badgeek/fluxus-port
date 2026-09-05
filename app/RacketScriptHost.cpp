// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RacketScriptHost.h"
#include "FluxusCommands.h"

extern "C" {
#include "chezscheme.h"
#include "racketcs.h"
}

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef RACKET_DIR
#define RACKET_DIR ""
#endif
#ifndef RACKET_LIB_DIR
#define RACKET_LIB_DIR ""
#endif

namespace {
bool g_booted = false;

// The prelude's runner procedures, resolved ONCE at init and locked against GC.
// Calling them via racket_apply skips the per-frame expand+compile that
// racket_eval of a call form pays (small but every frame, both modes).
ptr g_runGuarded = nullptr;   // (flux-run-guarded "<code>")
ptr g_runFrame   = nullptr;   // (flux-run-frame)

bool dirExists(const std::string& p) {
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// --- self-contained bundle -------------------------------------------------
// A packaged .app carries its own Racket runtime under
//   <App>.app/Contents/Resources/racket/{lib/racket/*.boot, share/racket/collects,
//                                        etc/racket/config.rktd, fluxus-lib/}
// (see cmake/bundle_racket.cmake). Everything there is RELATIVE, so the app runs
// on any machine. Dev builds have no Resources/racket and fall back to the
// absolute RACKET_DIR / RACKET_LIB_DIR baked in at configure time.
//
// Returns "" when there is no bundled runtime.
const std::string& bundleRoot() {
  static std::string root = [] () -> std::string {
#ifdef __APPLE__
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return {};
    std::string exe(buf);                                  // …/Contents/MacOS/App
    auto cut = exe.find_last_of('/');                      // …/Contents/MacOS
    if (cut == std::string::npos) return {};
    std::string macos = exe.substr(0, cut);
    cut = macos.find_last_of('/');                         // …/Contents
    if (cut == std::string::npos) return {};
    std::string cand = macos.substr(0, cut) + "/Resources/racket";
    if (dirExists(cand)) return cand;
#endif
    return {};
  }();
  return root;
}

// Racket install prefix to boot from: the bundled one when present.
std::string racketPrefix() {
  const std::string& b = bundleRoot();
  return b.empty() ? std::string(RACKET_DIR) : b;
}

// Where the fluxus .ss library lives: bundled copy when present.
std::string fluxusLibDir() {
  const std::string& b = bundleRoot();
  if (!b.empty() && dirExists(b + "/fluxus-lib")) return b + "/fluxus-lib";
  return std::string(RACKET_LIB_DIR);
}

ptr sym(const char* s) { return Sstring_to_symbol(s); }

// eval a C string of Scheme: (eval (read (open-input-string "<code>")))
ptr eval_cstr(const char* code) {
  // Sstring_utf8 (not Sstring) so multibyte source is decoded as UTF-8 — otherwise
  // Chez reads the bytes as Latin-1 and unicode (box-drawing/blocks for build-terminal) mangles.
  ptr ois = Scons(sym("open-input-string"), Scons(Sstring_utf8(code, -1), Snil));
  ptr rd  = Scons(sym("read"), Scons(ois, Snil));
  ptr ev  = Scons(sym("eval"), Scons(rd, Snil));
  return racket_eval(ev);
}

// The fluxus command API now comes from the loaded fluxus .ss library (see
// RacketScriptHost::init). Here we only add host infrastructure: an error
// reporter + the guarded per-frame runner.
// flux-run-guarded compiles ONCE per buffer: immediate mode re-runs the whole
// buffer every frame, and read+EXPAND+compile dominated the frame cost (the
// profile sat in the expander, not the drawing). The compiled forms are cached
// keyed on the exact buffer string; an unchanged buffer just re-evals the
// compiled code objects (defines re-run — identical semantics, no expander).
// First pass compiles and evals form-BY-form so a sketch-local define-syntax
// is live before later forms expand, and top-level (begin …) forms are
// spliced first — Racket's eval expands begin bodies incrementally but
// compile forces the whole form at once, which would break that pattern.
const char* kHostPrelude =
"(begin"
"  (define _report (get-ffi-obj \"flux_report_error\" #f (_fun _string -> _void)))"
"  (define _cc-src #f)"
"  (define _cc '())"
"  (define (_read-all p)"
"    (let loop ((acc '()))"
"      (let ((f (read p)))"
"        (if (eof-object? f) (reverse acc) (loop (cons f acc))))))"
"  (define (_splice-begins fs)"
"    (cond ((null? fs) '())"
"          ((and (pair? (car fs)) (eq? (caar fs) 'begin))"
"           (_splice-begins (append (cdar fs) (cdr fs))))"
"          (else (cons (car fs) (_splice-begins (cdr fs))))))"
"  (define (_compile-run forms)"
"    (if (null? forms) '()"
"        (let ((c (compile (car forms))))"
"          (eval c (current-namespace))"
"          (cons c (_compile-run (cdr forms))))))"
"  (define (flux-run-guarded s)"
"    (_report \"\")"
"    (with-handlers ((( lambda (e) #t)"
"                     (lambda (e) (set! _cc-src #f)"
"                                 (_report (string-append \"; error: \""
"                                  (if (exn? e) (exn-message e) (format \"~a\" e)))))))"
"      (if (and _cc-src (string=? s _cc-src))"
"          (for-each (lambda (c) (eval c (current-namespace))) _cc)"
"          (begin"
"            (set! _cc-src #f)"
"            (set! _cc (_compile-run (_splice-begins (_read-all (open-input-string s)))))"
"            (set! _cc-src s)))))"
"  (define (flux-run-frame)"
"    (_report \"\")"
"    (with-handlers ((( lambda (e) #t)"
"                     (lambda (e) (_report (string-append \"; error: \""
"                                  (if (exn? e) (exn-message e) (format \"~a\" e)))))))"
"      ((unbox frame-callback))))"     // retained-mode: run the every-frame thunk
")";

std::string requireLibForm() {
  std::string lib = fluxusLibDir();
  // load fluxus-modules (engine commands via FFI) + real fluxus library files
  return "(require (file \"" + lib + "/fluxus-modules.ss\")"
         "         (file \"" + lib + "/building-blocks.ss\")"   // vadd/vsub/vmul, with-state, pdata-map!
         "         (file \"" + lib + "/maths.ss\")"             // vmix, lerp, hermite
         "         (file \"" + lib + "/randomness.ss\")"        // rndf, crndf, crndvec
         "         (file \"" + lib + "/poly-tools.ss\")"        // poly/pdata helpers
         "         (file \"" + lib + "/shapes.ss\")"
         "         (file \"" + lib + "/input.ss\")"            // keys/mouse-over/register-down
         "         (file \"" + lib + "/camera.ss\")"           // input-camera, reset-camera
         "         (file \"" + lib + "/mouse.ss\")"            // mouse-pos, world-pos, 2dvec->angle
         "         (file \"" + lib + "/help.ss\")"             // help/set-help-locale!
         "         (file \"" + lib + "/tasks.ss\")"           // spawn-task/rm-task (+ time.ss)
         "         (file \"" + lib + "/pixels-tools.ss\")"     // pixels-circle/dodge/burn
         "         (file \"" + lib + "/voxels-tools.ss\")"     // voxels-index/pos/sphere
         "         (file \"" + lib + "/planetarium.ss\")"      // dome-* projection helpers
         "         (file \"" + lib + "/ansi.ss\")"             // ANSI string helpers for build-terminal
         "         (file \"" + lib + "/gpu-noise.ss\")"        // GLSL curl-noise sources for GPU particles
         "         (file \"" + lib + "/tricks.ss\")"           // expand, cheap-toon, occlusion-texture-bake
         "         (file \"" + lib + "/model.ss\")"            // assimp model import ergonomics
         "         (file \"" + lib + "/collada-import.ss\"))"; // collada-import
}
} // namespace

RacketScriptHost::RacketScriptHost()  = default;
RacketScriptHost::~RacketScriptHost() = default;

void RacketScriptHost::init() {
  if (!g_booted) {
    racket_boot_arguments_t ba;
    std::memset(&ba, 0, sizeof(ba));
    const std::string prefix = racketPrefix();
    static std::string b1 = prefix + "/lib/racket/petite.boot";
    static std::string b2 = prefix + "/lib/racket/scheme.boot";
    static std::string b3 = prefix + "/lib/racket/racket.boot";
    static std::string cd = prefix + "/share/racket/collects";
    // The bundle ships its own etc/racket/config.rktd (relative paths +
    // compiled-file-roots '(same)). brew's Cellar prefix has no etc/racket —
    // Racket then just uses an empty config, and the block below sets the roots.
    static std::string cf = prefix + "/etc/racket";
    ba.boot1_path = b1.c_str();
    ba.boot2_path = b2.c_str();
    ba.boot3_path = b3.c_str();
    ba.exec_file  = "FluxusRacketApp";
    ba.collects_dir = cd.c_str();
    ba.config_dir   = cf.c_str();
    ba.cs_compiled_subdir = 1;
    std::fprintf(stderr, "[fluxus] Racket boot: %s (%s)\n", prefix.c_str(),
                 bundleRoot().empty() ? "external install" : "bundled, self-contained");
    racket_boot(&ba);
    g_booted = true;
  }

  // register the engine primitives so Racket's ffi can find them
  Sregister_symbol("flux_background",   (void*) flux_background);
  Sregister_symbol("flux_colour",       (void*) flux_colour);
  Sregister_symbol("flux_translate",    (void*) flux_translate);
  Sregister_symbol("flux_rotate",       (void*) flux_rotate);
  Sregister_symbol("flux_scale",        (void*) flux_scale);
  Sregister_symbol("flux_identity",     (void*) flux_identity);
  Sregister_symbol("flux_push",         (void*) flux_push);
  Sregister_symbol("flux_pop",          (void*) flux_pop);
  Sregister_symbol("flux_build_cube",   (void*) flux_build_cube);
  Sregister_symbol("flux_build_sphere", (void*) flux_build_sphere);
  Sregister_symbol("flux_build_torus",  (void*) flux_build_torus);
  Sregister_symbol("flux_build_plane",  (void*) flux_build_plane);
  Sregister_symbol("flux_time",         (void*) flux_time);
  Sregister_symbol("flux_frame",        (void*) flux_frame);
  Sregister_symbol("flux_report_error", (void*) flux_report_error);

  racket_namespace_require(sym("racket/base"));
  racket_namespace_require(sym("ffi/unsafe"));

  // The install keeps its compiled collects in a SEPARATE root
  // (<RACKET_DIR>/lib/racket/compiled), which the plain `racket` CLI has on
  // current-compiled-file-roots but the embedded boot does NOT — so without this
  // the require below recompiles the whole collects tree from source every launch
  // (~22s). Point the roots (and use-compiled-file-paths) where the CLI does so we
  // load bytecode: cuts init from ~25s to ~4s. Also lets our racket-lib/compiled
  // *.zo be used (precompile with: raco make / managed-compile-zo on racket-lib).
  // In the self-contained bundle that separate root is merged back IN-TREE, so
  // 'same alone is enough (and stays relocatable).
  {
    std::string form =
      "(begin"
      "  (use-compiled-file-paths (list (string->path \"compiled\")))"
      "  (current-compiled-file-roots (list 'same";
    if (bundleRoot().empty())
      form += " (string->path \"" + std::string(RACKET_DIR) + "/lib/racket/compiled\")";
    form += ")))";
    eval_cstr(form.c_str());
  }

  eval_cstr(requireLibForm().c_str());   // load the fluxus .ss library (FFI-backed)
  eval_cstr(kHostPrelude);               // host infra (error reporter + runner)

  // resolve the runners once; locked so the C globals stay valid across GCs
  ptr rg = eval_cstr("flux-run-guarded");
  ptr rf = eval_cstr("flux-run-frame");
  if (Sprocedurep(rg)) { Slock_object(rg); g_runGuarded = rg; }
  if (Sprocedurep(rf)) { Slock_object(rf); g_runFrame   = rf; }
}

void RacketScriptHost::setRenderer(Fluxus::Renderer* r) { flux_set_renderer((void*) r); }

void RacketScriptHost::setFrameInfo(double t, int frame) { flux_frame_begin(t, frame); }

bool RacketScriptHost::eval(const std::string& code, std::string& errorOut) {
  // (flux-run-guarded "<code>") — the string is passed as a Chez/Racket string,
  // so no escaping needed. Errors are reported via flux_report_error.
  ptr str = Sstring_utf8(code.c_str(), (iptr) code.size());
  if (g_runGuarded) racket_apply(g_runGuarded, Scons(str, Snil));
  else racket_eval(Scons(sym("flux-run-guarded"), Scons(str, Snil)));
  errorOut = flux_last_error();
  return errorOut.empty();
}

bool RacketScriptHost::runFrame(std::string& errorOut) {
  // invoke the registered every-frame thunk
  if (g_runFrame) racket_apply(g_runFrame, Snil);
  else racket_eval(Scons(sym("flux-run-frame"), Snil));
  errorOut = flux_last_error();
  return errorOut.empty();
}
