/* SPIKE: embed real Racket (CS, v9.3) in-process.
   Proves: link libracketcs, boot the runtime, eval Scheme from C, get a value
   back, run (require racket/list). This validates a Racket IScriptHost backend.

   CS embedding has no eval-string, so we build the datum
   (eval (read (open-input-string "<code>"))) via Chez object constructors. */
#include <stdio.h>
#include <string.h>
#include "chezscheme.h"
#include "racketcs.h"

#define RK "/opt/homebrew/Cellar/minimal-racket/9.3"

static ptr sym(const char* s)          { return Sstring_to_symbol(s); }
static ptr list1(ptr a)                { return Scons(a, Snil); }
static ptr list2(ptr a, ptr b)         { return Scons(a, Scons(b, Snil)); }
static ptr list3(ptr a, ptr b, ptr c)  { return Scons(a, Scons(b, Scons(c, Snil))); }

static void report(const char* label, ptr r) {
  if (Sfixnump(r))      printf("%s -> %ld  [fixnum returned to C]\n", label, (long) Sfixnum_value(r));
  else if (r == Strue)  printf("%s -> #t\n", label);
  else if (r == Sfalse) printf("%s -> #f\n", label);
  else if (r == Svoid)  printf("%s -> void\n", label);
  else if (Sflonump(r)) printf("%s -> flonum %g\n", label, Sflonum_value(r));
  else                  printf("%s -> (other) raw=0x%lx\n", label, (unsigned long) r);
}

/* eval a C string of Scheme, return the Chez result ptr */
static ptr eval_str(const char* code) {
  ptr ois = list2(sym("open-input-string"), Sstring(code)); /* (open-input-string "...") */
  ptr rd  = list2(sym("read"), ois);                         /* (read (open-input-string ...)) */
  ptr ev  = list2(sym("eval"), rd);                          /* (eval (read ...)) */
  return racket_eval(ev);
}

int main(int argc, char** argv) {
  (void) argc; (void) argv;

  racket_boot_arguments_t ba;
  memset(&ba, 0, sizeof(ba));
  ba.boot1_path   = RK "/lib/racket/petite.boot";
  ba.boot2_path   = RK "/lib/racket/scheme.boot";
  ba.boot3_path   = RK "/lib/racket/racket.boot";
  ba.exec_file    = "racket-spike";
  ba.collects_dir = RK "/share/racket/collects";
  ba.config_dir   = RK "/etc/racket";
  ba.cs_compiled_subdir = 1;

  racket_boot(&ba);
  printf("=== real Racket embedded (CS) ===\n");
  printf("booted libracketcs OK\n");

  /* require base FIRST so the eval namespace has bindings (#%datum, +, ...) */
  racket_namespace_require(sym("racket/base"));

  /* 0) sanity */
  report("raw Sfixnum(42) no-eval", Sfixnum(42));
  report("eval literal 42",         racket_eval(Sfixnum(42)));

  /* 1) directly-built datum: (+ 40 2) */
  report("(+ 40 2) direct", racket_eval(list3(sym("+"), Sfixnum(40), Sfixnum(2))));

  /* 2) eval a STRING of scheme via (eval (read (open-input-string ...))) */
  report("(* 6 7) via string-eval", eval_str("(* 6 7)"));

  /* 3) side-effecting display (stdout straight from Racket) */
  eval_str("(begin (display \"  hello from racket ~ \") (display (* 6 7)) (newline) (flush-output))");
  fflush(stdout);

  /* 4) a real Racket library: racket/list */
  racket_namespace_require(sym("racket/list"));
  report("(length (range 10)) racket/list", eval_str("(length (range 10))"));

  printf("=== racket spike OK ===\n");
  return 0;
}
