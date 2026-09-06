#lang racket/base
;; SPDX-License-Identifier: AGPL-3.0-or-later
;;
;; Which collections does the Android boot path actually load?
;;
;; The runtime trim in build.sh removes whole collections, and a collection is
;; only ever loaded on demand — so a wrong guess is invisible until the device
;; requires it and Racket dies with "collection not found". This derives the
;; list instead of guessing: it wraps the load handler BEFORE requiring
;; anything, then requires exactly what RacketScriptHost::init and
;; racket-lib/*.ss require.
;;
;;   racket spikes/android-racket-apk/collections-used.rkt
;;
;; Run it on the host install (same version as the cross-built one) after
;; changing what the host or the .ss library requires, and keep build.sh's
;; removal list disjoint from the output. It found `pkg` and `planet`, which are
;; pulled in by compiler/cm and were not obvious from any require line.
(define seen (make-hash))
(define old (current-load/use-compiled))
(current-load/use-compiled
 (lambda (p n) (hash-set! seen (path->string p) #t) (old p n)))

;; RacketScriptHost::init
(dynamic-require 'ffi/unsafe #f)
(void (dynamic-require 'compiler/cm 'managed-compile-zo))
;; EVERY .ss file, not just the ones fluxus-modules.ss re-exports. The host runs
;; managed-compile-zo over the whole directory, so a collection reached by a
;; file nobody requires still has to be there — that is how collada-import.ss's
;; `(prefix-in x: xml/xml)` took the runtime down on the device, invisible both
;; to a grep for (require …) and to requiring the library entry point.
(define dir (build-path (current-directory) "racket-lib"))
(unless (directory-exists? dir)
  (eprintf "run this from the repo root — no ~a\n" dir))
(for ([f (in-list (directory-list dir))]
      #:when (regexp-match? #rx"[.]ss$" (path->string f)))
  (with-handlers ([exn:fail? (lambda (e)
                               (eprintf "~a: ~a\n" f (exn-message e)))])
    (dynamic-require (build-path dir f) #f)))
;; commonly reached for from a sketch
(dynamic-require 'racket/class #f)
(dynamic-require 'racket/math #f)

(define colls (make-hash))
(for ([p (in-hash-keys seen)])
  (let ([m (regexp-match #rx"/collects/([^/]+)" p)])
    (when m (hash-set! colls (cadr m) #t))))
(for ([c (sort (hash-keys colls) string<?)]) (displayln c))
