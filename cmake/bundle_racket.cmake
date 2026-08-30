# Copy a self-contained Racket CS runtime into an .app bundle.
#
# Run in -P script mode with:
#   RACKET_DIR   Racket CS install prefix (e.g. $(brew --prefix minimal-racket))
#   FLUXUS_LIB   the repo's racket-lib/ (our .ss + compiled/*.zo)
#   APP_DIR      the built <Name>.app
#
# Produces <App>.app/Contents/Resources/racket/:
#   lib/racket/{petite,scheme,racket}.boot   Chez boot files
#   share/racket/collects/                   the collects tree, with the install's
#                                            SEPARATE compiled root merged back
#                                            IN-TREE so nothing is path-absolute
#   share/racket/{pkgs,links.rktd,info-cache.rktd}
#   etc/racket/config.rktd                   relative dirs, compiled-file-roots '(same)
#   fluxus-lib/                              our .ss library + compiled/*.zo
#
# RacketScriptHost::bundleRoot() finds this at runtime relative to the executable
# and boots from it; when it is absent it falls back to the absolute RACKET_DIR.

if(NOT RACKET_DIR OR NOT APP_DIR OR NOT FLUXUS_LIB)
  message(FATAL_ERROR "bundle_racket.cmake: need -DRACKET_DIR=… -DFLUXUS_LIB=… -DAPP_DIR=…")
endif()

set(DEST "${APP_DIR}/Contents/Resources/racket")

# The linker ad-hoc-signs the .app at link time; adding files to it afterwards
# breaks that seal ("code has no resources but signature indicates they must be
# present"). Gatekeeper then refuses the DOWNLOADED app with the misleading
# "is damaged and can't be opened" — that is NOT a quarantine prompt, and
# `xattr -dr com.apple.quarantine` does not clear it. So re-sign ad-hoc after
# touching the bundle — including on the already-bundled path, since a relink
# re-signs the executable and invalidates the seal again.
function(resign_bundle app)
  find_program(CODESIGN codesign)
  if(NOT CODESIGN)
    message(WARNING "bundle_racket: codesign not found; the bundled .app will fail "
                    "Gatekeeper with \"is damaged and can't be opened\"")
    return()
  endif()
  execute_process(COMMAND "${CODESIGN}" --force --deep --sign - "${app}"
                  RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${app}: ${_err}")
  endif()
  execute_process(COMMAND "${CODESIGN}" --verify --deep "${app}"
                  RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "codesign verify failed for ${app}: ${_err}")
  endif()
  message(STATUS "Re-signed (ad-hoc) ${app}")
endfunction()

# Up-to-date check: the stamp is the last thing written (see below), so if it is
# there the ~90 MB copy is already done — but still re-sign.
if(EXISTS "${DEST}/.bundled")
  message(STATUS "Racket runtime already bundled in ${APP_DIR}")
  resign_bundle("${APP_DIR}")
  return()
endif()

message(STATUS "Bundling Racket runtime -> ${DEST}")
file(MAKE_DIRECTORY "${DEST}/lib/racket" "${DEST}/share/racket" "${DEST}/etc/racket")

# --- collects + pkgs -------------------------------------------------------
file(COPY "${RACKET_DIR}/share/racket/collects" DESTINATION "${DEST}/share/racket")
if(EXISTS "${RACKET_DIR}/share/racket/pkgs")
  file(COPY "${RACKET_DIR}/share/racket/pkgs" DESTINATION "${DEST}/share/racket")
endif()
foreach(f links.rktd info-cache.rktd)
  if(EXISTS "${RACKET_DIR}/share/racket/${f}")
    file(COPY "${RACKET_DIR}/share/racket/${f}" DESTINATION "${DEST}/share/racket")
  endif()
endforeach()

# --- merge the out-of-tree compiled mirror INTO the collects tree ----------
# The install keeps its .zo under <prefix>/lib/racket/compiled/<ABSOLUTE PATH OF
# THE SOURCE>/…  — that mirror is only reachable via an absolute
# current-compiled-file-roots entry, which would not survive relocation. Copy the
# .zo back next to their sources instead, so 'same finds them anywhere.
#
# The mirror is keyed by the install's REAL path (brew's Cellar dir), which need
# not equal RACKET_DIR (often the /opt/homebrew/opt/... symlink) — so find it by
# globbing for the ".../share/racket/collects" leaf rather than assuming a path.
set(_mirror "")
foreach(_d "*" "*/*" "*/*/*" "*/*/*/*" "*/*/*/*/*" "*/*/*/*/*/*")
  if(_mirror STREQUAL "")
    file(GLOB _hit LIST_DIRECTORIES TRUE
         "${RACKET_DIR}/lib/racket/compiled/${_d}/share/racket")
    foreach(_h ${_hit})
      if(IS_DIRECTORY "${_h}/collects" AND _mirror STREQUAL "")
        set(_mirror "${_h}")
      endif()
    endforeach()
  endif()
endforeach()

if(IS_DIRECTORY "${_mirror}/collects")
  file(GLOB _mirror_top LIST_DIRECTORIES TRUE "${_mirror}/collects/*")
  file(COPY ${_mirror_top} DESTINATION "${DEST}/share/racket/collects")
  if(IS_DIRECTORY "${_mirror}/pkgs")
    file(GLOB _mirror_pkgs LIST_DIRECTORIES TRUE "${_mirror}/pkgs/*")
    file(COPY ${_mirror_pkgs} DESTINATION "${DEST}/share/racket/pkgs")
  endif()
else()
  message(WARNING "bundle_racket: no compiled mirror under ${RACKET_DIR}/lib/racket/compiled; "
                  "the bundled app will recompile collects from source on first launch")
endif()

# --- our fluxus .ss library (+ precompiled .zo, if `make precompile` was run) --
file(GLOB _ss "${FLUXUS_LIB}/*.ss")
file(COPY ${_ss} DESTINATION "${DEST}/fluxus-lib")
if(IS_DIRECTORY "${FLUXUS_LIB}/compiled")
  file(COPY "${FLUXUS_LIB}/compiled" DESTINATION "${DEST}/fluxus-lib")
endif()

# --- relocatable config ----------------------------------------------------
# Relative dirs are resolved against the collects dir, which the embedded boot
# passes in absolutely (racket_boot_arguments_t::collects_dir).
file(WRITE "${DEST}/etc/racket/config.rktd"
";; generated by cmake/bundle_racket.cmake — relocatable bundle config.\n"
";; Paths are relative to <bundle>/share/racket/collects; compiled-file-roots is\n"
";; 'same because the install's separate compiled root was merged back in-tree.\n"
"#hash(\n"
"      (lib-dir . \"../../../lib/racket\")\n"
"      (share-dir . \"../..\")\n"
"      (pkgs-dir . \"../../pkgs\")\n"
"      (include-dir . \"../../../include/racket\")\n"
"      (bin-dir . \"../../../bin\")\n"
"      (doc-dir . \"../../doc\")\n"
"      (absolute-installation? . #f)\n"
"      (compiled-file-roots . (same))\n"
"      (build-stamp . \"\")\n"
"      (default-scope . \"installation\")\n"
"      (catalogs . (#f))\n"
")\n")

# --- boot files last, then the stamp (so a half-copy is never marked done) --
foreach(b petite scheme racket)
  file(COPY "${RACKET_DIR}/lib/racket/${b}.boot" DESTINATION "${DEST}/lib/racket")
endforeach()
file(WRITE "${DEST}/.bundled" "${RACKET_DIR}\n")

# brew ships the collects/boot files mode 444. Copied verbatim into the bundle
# that makes `xattr -dr com.apple.quarantine <App>.app` fail with "Permission
# denied" on thousands of files — the step every user has to run on an unsigned
# download. Make them user-writable (which also lets users edit the bundled .ss).
file(GLOB_RECURSE _ro "${DEST}/*")
foreach(f ${_ro})
  file(CHMOD "${f}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endforeach()

resign_bundle("${APP_DIR}")

message(STATUS "Bundled Racket runtime into ${APP_DIR}")
