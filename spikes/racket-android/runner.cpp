// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Racket-on-Android spike: the driver.
//
// Built twice, from this one file, to separate two questions that look alike:
//
//   runner_dlopen  (default)        — dlopen(RTLD_NOW|RTLD_GLOBAL) at runtime.
//   runner_local   (-DSPIKE_LOCAL)  — dlopen(RTLD_NOW) only. This is what matters:
//                                     Android's System.loadLibrary does NOT pass
//                                     RTLD_GLOBAL, so a symbol that only resolves
//                                     in the GLOBAL build would still be missing
//                                     inside a real APK.
//   runner_linked  (-DSPIKE_LINKED) — links libspike.so at build time (DT_NEEDED).
//                                     If dlopen builds fail but this succeeds, the
//                                     problem is the LOADING shape, not dlsym.
//
// Usage: runner_* <racket-install-prefix>

#include <cstdio>

#ifdef SPIKE_LINKED
extern "C" int  spike_boot(const char* prefix);
extern "C" void spike_eval(const char* code);
#else
#include <dlfcn.h>
#endif

// Three ways to reach a C symbol from Racket, printed side by side.
//   self/#f      — what racket-lib/fluxus-engine.ss actually uses today
//   ffi-lib      — naming the .so explicitly
// and the two probe symbols differ in whether the host called Sregister_symbol,
// so the matrix says whether registration alone is enough to rescue the #f path.
static const char* kProbe =
"(begin"
"  (define (try name lib)"
"    (with-handlers ([(lambda (e) #t)"
"                     (lambda (e) (format \"ERROR ~a\""
"                                   (if (exn? e) (exn-message e) e)))])"
"      (let ([f (get-ffi-obj name lib (_fun _int _int -> _int) (lambda () #f))])"
"        (if f (format \"RESOLVED (6,7) -> ~a\" (f 6 7)) \"NOT FOUND\"))))"
"  (define so"
"    (with-handlers ([(lambda (e) #t)"
"                     (lambda (e) (printf \"  ffi-lib libspike.so FAILED: ~a\\n\""
"                                   (if (exn? e) (exn-message e) e))"
"                                 #f)])"
"      (ffi-lib \"libspike.so\")))"
"  (printf \"\\n--- FFI resolution matrix ---\\n\")"
"  (printf \"self(#f)  flux_probe_add (exported only)   : ~a\\n\""
"          (try \"flux_probe_add\" #f))"
"  (printf \"self(#f)  flux_probe_reg (Sregister_symbol): ~a\\n\""
"          (try \"flux_probe_reg\" #f))"
"  (printf \"ffi-lib   flux_probe_add (exported only)   : ~a\\n\""
"          (if so (try \"flux_probe_add\" so) \"no lib handle\"))"
"  (printf \"ffi-lib   flux_probe_reg (Sregister_symbol): ~a\\n\""
"          (if so (try \"flux_probe_reg\" so) \"no lib handle\"))"
"  (printf \"--- end matrix ---\\n\")"
"  (printf \"racket version: ~a  machine: ~a\\n\""
"          (version) (system-type 'machine))"
// Racket's stdout is block-buffered and the process exits without unwinding,
// so without this the whole matrix is written and then thrown away.
"  (flush-output))";

int main(int argc, char** argv) {
  const char* prefix = argc > 1 ? argv[1] : "/data/local/tmp/racket";

#ifdef SPIKE_LINKED
  std::printf("[runner] mode: LINKED (libspike.so is a DT_NEEDED dependency)\n");
  spike_boot(prefix);
  spike_eval(kProbe);
#else
#ifdef SPIKE_LOCAL
  const int flags = RTLD_NOW;                  // what System.loadLibrary does
  std::printf("[runner] mode: DLOPEN RTLD_NOW (no RTLD_GLOBAL)\n");
#else
  const int flags = RTLD_NOW | RTLD_GLOBAL;
  std::printf("[runner] mode: DLOPEN RTLD_NOW|RTLD_GLOBAL\n");
#endif
  void* h = dlopen("libspike.so", flags);
  if (!h) { std::printf("[runner] dlopen failed: %s\n", dlerror()); return 1; }
  auto boot = (int  (*)(const char*)) dlsym(h, "spike_boot");
  auto ev   = (void (*)(const char*)) dlsym(h, "spike_eval");
  if (!boot || !ev) { std::printf("[runner] dlsym failed: %s\n", dlerror()); return 1; }
  boot(prefix);
  ev(kProbe);
#endif
  std::printf("[runner] done\n");
  return 0;
}
