# Copy the upstream fluxus example sketches into an .app bundle.
#
# Run in -P script mode with:
#   EXAMPLES_DIR  source dir of *.scm (vendor/fluxus/examples)
#   APP_DIR       the built <Name>.app
#
# Produces <App>.app/Contents/Resources/examples/*.scm, which FluxusMenu finds
# relative to the executable and lists under File > Examples (see AppMenu.h).
#
# Small (~370 KB) next to the Racket runtime, so this always runs — it does not
# need the FLUXUS_BUNDLE_RACKET opt-in. NOTE: adding files to a signed .app
# breaks its seal, so this must run BEFORE bundle_racket.cmake's re-sign step;
# when Racket bundling is off, it re-signs itself (see below).

if(NOT EXAMPLES_DIR OR NOT APP_DIR)
  message(FATAL_ERROR "bundle_examples.cmake: need -DEXAMPLES_DIR=… -DAPP_DIR=…")
endif()

set(DEST "${APP_DIR}/Contents/Resources/examples")

file(GLOB _scm "${EXAMPLES_DIR}/*.scm")
if(NOT _scm)
  message(WARNING "bundle_examples: no .scm under ${EXAMPLES_DIR}")
  return()
endif()

file(MAKE_DIRECTORY "${DEST}")
file(COPY ${_scm} DESTINATION "${DEST}")

# Sketches only. Some upstream examples also want data files (bot.obj,
# refmap.png, *.glsl — they live in vendor/fluxus/modules/material) and load them
# by BARE filename, which the port cannot resolve: fluxus's searchpaths/fullpath
# are not ported yet, so there is no path to make them findable from a bundle.
# Those sketches error on load; the rest run. See ROADMAP.md.

list(LENGTH _scm _n)
message(STATUS "Bundled ${_n} example sketches into ${APP_DIR}")

# Keep the ad-hoc signature valid (see the long note in bundle_racket.cmake).
# When Racket bundling is ON that script re-signs after this one runs, so this is
# redundant-but-harmless; when it is OFF this is the only re-sign.
find_program(CODESIGN codesign)
if(CODESIGN)
  execute_process(COMMAND "${CODESIGN}" --force --deep --sign - "${APP_DIR}"
                  RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${APP_DIR}: ${_err}")
  endif()
endif()
