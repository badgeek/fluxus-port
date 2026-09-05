// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Racket-on-Android spike: the shared library.
//
// Mirrors what app/RacketScriptHost.cpp does — boot an embedded Racket CS,
// require racket/base + ffi/unsafe, pin the compiled-file roots — but with no
// JUCE, no GL and no engine. It exports two flux_*-shaped C functions so the
// Racket side can try to reach them the way racket-lib/fluxus-engine.ss does.
//
// The point of the .so (rather than putting this in the executable) is that it
// reproduces the Android app layout: on Android our code never IS the main
// executable — that is app_process/zygote — it is a library the Activity loads.
// racket-lib/fluxus-engine.ss:15 binds every command with
//     (get-ffi-obj cname #f ...)
// where #f means "the current process", so whether that still finds our symbols
// is the question this spike exists to answer.

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "chezscheme.h"
#include "racketcs.h"
}

// --- the two probe symbols -------------------------------------------------
// Same shape as a real flux_* command. _add is ONLY exported (the case of the
// ~300 commands fluxus-engine.ss binds); _reg is additionally handed to
// Sregister_symbol below, the way RacketScriptHost::init registers its 14.
extern "C" int flux_probe_add(int a, int b) { return a + b; }
extern "C" int flux_probe_reg(int a, int b) { return a * b; }

namespace {
bool g_booted = false;

ptr sym(const char* s) { return Sstring_to_symbol(s); }

ptr eval_cstr(const char* code) {
  ptr ois = Scons(sym("open-input-string"), Scons(Sstring_utf8(code, -1), Snil));
  ptr rd  = Scons(sym("read"), Scons(ois, Snil));
  ptr ev  = Scons(sym("eval"), Scons(rd, Snil));
  return racket_eval(ev);
}
}  // namespace

// Boot the embedded runtime against an unpacked Racket install at `prefix`.
extern "C" int spike_boot(const char* prefix) {
  if (g_booted) return 1;

  racket_boot_arguments_t ba;
  std::memset(&ba, 0, sizeof(ba));
  static std::string b1, b2, b3, cd, cf;
  b1 = std::string(prefix) + "/lib/racket/petite.boot";
  b2 = std::string(prefix) + "/lib/racket/scheme.boot";
  b3 = std::string(prefix) + "/lib/racket/racket.boot";
  cd = std::string(prefix) + "/share/racket/collects";
  cf = std::string(prefix) + "/etc/racket";
  ba.boot1_path = b1.c_str();
  ba.boot2_path = b2.c_str();
  ba.boot3_path = b3.c_str();
  ba.exec_file  = "spike";
  ba.collects_dir = cd.c_str();
  ba.config_dir   = cf.c_str();
  ba.cs_compiled_subdir = 1;

  std::fprintf(stderr, "[spike] booting Racket CS from %s\n", prefix);
  racket_boot(&ba);          // if Android's W^X blocks Chez codegen, we die here
  g_booted = true;
  std::fprintf(stderr, "[spike] racket_boot returned\n");

  // Only ONE of the two probes is registered — the asymmetry is the experiment.
  Sregister_symbol("flux_probe_reg", (void*) flux_probe_reg);

  racket_namespace_require(sym("racket/base"));
  racket_namespace_require(sym("ffi/unsafe"));
  std::fprintf(stderr, "[spike] namespace ready\n");

  // Same startup fix as the real host: without this the embedded boot ignores
  // the install's separate compiled root and recompiles collects from source.
  std::string form =
      "(begin"
      "  (use-compiled-file-paths (list (string->path \"compiled\")))"
      "  (current-compiled-file-roots (list 'same (string->path \"" +
      std::string(prefix) + "/lib/racket/compiled\"))))";
  eval_cstr(form.c_str());
  return 1;
}

extern "C" void spike_eval(const char* code) { eval_cstr(code); }
