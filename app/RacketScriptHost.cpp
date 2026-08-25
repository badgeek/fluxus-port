#include "RacketScriptHost.h"
#include "FluxusCommands.h"

extern "C" {
#include "chezscheme.h"
#include "racketcs.h"
}

#include <cstring>
#include <string>

#ifndef RACKET_DIR
#define RACKET_DIR ""
#endif
#ifndef RACKET_LIB_DIR
#define RACKET_LIB_DIR ""
#endif

namespace {
bool g_booted = false;

ptr sym(const char* s) { return Sstring_to_symbol(s); }

// eval a C string of Scheme: (eval (read (open-input-string "<code>")))
ptr eval_cstr(const char* code) {
  ptr ois = Scons(sym("open-input-string"), Scons(Sstring(code), Snil));
  ptr rd  = Scons(sym("read"), Scons(ois, Snil));
  ptr ev  = Scons(sym("eval"), Scons(rd, Snil));
  return racket_eval(ev);
}

// The fluxus command API now comes from the loaded fluxus .ss library (see
// RacketScriptHost::init). Here we only add host infrastructure: an error
// reporter + the guarded per-frame runner.
const char* kHostPrelude =
"(begin"
"  (define _report (get-ffi-obj \"flux_report_error\" #f (_fun _string -> _void)))"
"  (define (flux-run-guarded s)"
"    (_report \"\")"
"    (with-handlers ((( lambda (e) #t)"
"                     (lambda (e) (_report (string-append \"; error: \""
"                                  (if (exn? e) (exn-message e) (format \"~a\" e)))))))"
"      (let ((p (open-input-string s)))"
"        (let loop () (let ((f (read p))) (unless (eof-object? f) (eval f (current-namespace)) (loop)))))))"
")";

std::string requireLibForm() {
  std::string lib = RACKET_LIB_DIR;
  // load fluxus-modules (engine commands via FFI) + real fluxus library files
  return "(require (file \"" + lib + "/fluxus-modules.ss\")"
         "         (file \"" + lib + "/building-blocks.ss\")"   // vadd/vsub/vmul/...
         "         (file \"" + lib + "/maths.ss\")"
         "         (file \"" + lib + "/shapes.ss\"))";
}
} // namespace

RacketScriptHost::RacketScriptHost()  = default;
RacketScriptHost::~RacketScriptHost() = default;

void RacketScriptHost::init() {
  if (!g_booted) {
    racket_boot_arguments_t ba;
    std::memset(&ba, 0, sizeof(ba));
    static std::string b1 = std::string(RACKET_DIR) + "/lib/racket/petite.boot";
    static std::string b2 = std::string(RACKET_DIR) + "/lib/racket/scheme.boot";
    static std::string b3 = std::string(RACKET_DIR) + "/lib/racket/racket.boot";
    static std::string cd = std::string(RACKET_DIR) + "/share/racket/collects";
    static std::string cf = std::string(RACKET_DIR) + "/etc/racket";
    ba.boot1_path = b1.c_str();
    ba.boot2_path = b2.c_str();
    ba.boot3_path = b3.c_str();
    ba.exec_file  = "FluxusRacketApp";
    ba.collects_dir = cd.c_str();
    ba.config_dir   = cf.c_str();
    ba.cs_compiled_subdir = 1;
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
  eval_cstr(requireLibForm().c_str());   // load the fluxus .ss library (FFI-backed)
  eval_cstr(kHostPrelude);               // host infra (error reporter + runner)
}

void RacketScriptHost::setRenderer(Fluxus::Renderer* r) { flux_set_renderer((void*) r); }

void RacketScriptHost::setFrameInfo(double t, int frame) { flux_frame_begin(t, frame); }

bool RacketScriptHost::eval(const std::string& code, std::string& errorOut) {
  // (flux-run-guarded "<code>") — the string is passed as a Chez/Racket string,
  // so no escaping needed. Errors are reported via flux_report_error.
  ptr call = Scons(sym("flux-run-guarded"), Scons(Sstring(code.c_str()), Snil));
  racket_eval(call);
  errorOut = flux_last_error();
  return errorOut.empty();
}
