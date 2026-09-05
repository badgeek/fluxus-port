# ============================================================================
# platform.cmake — the one place that knows which OS we are building for.
#
# Everything OS-specific the app targets need lives here: how to link the system
# OpenGL, how to force a whole static archive into a link, which backend TU
# implements each optional subsystem (a real one or its null stub), and the
# extra libraries the embedded Racket CS runtime wants. The targets in
# CMakeLists.txt then carry no if(APPLE) of their own.
#
# Provides
#   fluxus_gl                      INTERFACE target: the system OpenGL (+ GLU)
#   fluxus_whole_archive(<target>) keep every object of a static lib in the link
#   FLUXUS_PLATFORM_SOURCES        backend TUs for fluxus_render
#   FLUXUS_PLATFORM_LIBS           extra system libs those TUs need
#   FLUXUS_RACKET_PLATFORM_LIBS    extra libs/flags for the Racket CS link
#   FLUXUS_RACKET_DEFAULT_DIR      where to look for a Racket CS install
#   FLUXUS_ENABLE_{NTSC,VIDEO,HAND}  optional-subsystem switches
#
# Porting note: a new platform should only need edits in this file, plus a null
# (or real) backend TU per subsystem. app/GLHeaders.h holds the matching switch
# on the include side.
# ============================================================================

get_filename_component(_FLUXUS_APP_DIR "${CMAKE_CURRENT_LIST_DIR}/../app" ABSOLUTE)

# --- system OpenGL ----------------------------------------------------------
# libfluxus tessellates NURBS through GLU, which is a separate library
# everywhere except the macOS OpenGL framework (which carries both).
find_package(OpenGL REQUIRED)
add_library(fluxus_gl INTERFACE)
if(APPLE)
  target_link_libraries(fluxus_gl INTERFACE "-framework OpenGL")
else()
  target_link_libraries(fluxus_gl INTERFACE OpenGL::GL OpenGL::GLU)
endif()

# --- whole-archive ----------------------------------------------------------
# The Racket host resolves every flux_* command by dlsym at RUNTIME
# (get-ffi-obj), which the linker cannot see — so it would drop the archive
# members nothing references, and each binding in a dropped TU would silently
# fall back to its failure thunk (no error, the command just becomes a no-op).
# Canary after touching this: `nm <app binary> | grep flux_vadd`.
function(fluxus_whole_archive target)
  if(APPLE)
    target_link_options(${target} INTERFACE "-Wl,-force_load,$<TARGET_FILE:${target}>")
  else()
    # GNU ld / lld: bracket the archive itself. It also appears again via the
    # normal link line; the second occurrence resolves nothing new, so no
    # duplicate symbols.
    target_link_options(${target} INTERFACE
        "-Wl,--whole-archive" "$<TARGET_FILE:${target}>" "-Wl,--no-whole-archive")
  endif()
endfunction()

# --- optional subsystems ----------------------------------------------------
# Default ON where a backend actually exists. Everything else compiles its null
# TU, so the script bindings — and therefore every sketch — stay identical.
option(FLUXUS_ENABLE_NTSC "Final-stage NTSC/VHS filter (needs cargo/Rust)" ON)
if(APPLE)
  option(FLUXUS_ENABLE_VIDEO "Movie + webcam GL textures (AVFoundation)" ON)
  option(FLUXUS_ENABLE_HAND  "Hand-pose tracking (Apple Vision)" ON)
else()
  option(FLUXUS_ENABLE_VIDEO "Movie + webcam GL textures (no backend yet)" OFF)
  option(FLUXUS_ENABLE_HAND  "Hand-pose tracking (no backend yet)" OFF)
endif()

if(NOT APPLE AND (FLUXUS_ENABLE_VIDEO OR FLUXUS_ENABLE_HAND))
  message(FATAL_ERROR
      "FLUXUS_ENABLE_VIDEO / FLUXUS_ENABLE_HAND are macOS-only for now — the "
      "backends are AVFoundation and Vision. Port app/VideoHost.mm (V4L2 or "
      "GStreamer) / app/HandHostVision.mm first, or configure with them OFF.")
endif()

set(FLUXUS_PLATFORM_SOURCES "")
set(FLUXUS_PLATFORM_LIBS    "")

# process energy policy (macOS App Nap / E-core demotion; see AppActivity.h)
if(APPLE)
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/AppActivity.mm)
else()
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/AppActivityNull.cpp)
endif()

# movie + webcam GL textures
if(FLUXUS_ENABLE_VIDEO)
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/VideoHost.mm)
  list(APPEND FLUXUS_PLATFORM_LIBS
       "-framework AVFoundation" "-framework CoreVideo"
       "-framework CoreMedia"    "-framework QuartzCore")
  set_source_files_properties(${_FLUXUS_APP_DIR}/VideoHost.mm
                              PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/VideoHostNull.cpp)
endif()

# hand-pose tracking
if(FLUXUS_ENABLE_HAND)
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/HandHostVision.mm)
  list(APPEND FLUXUS_PLATFORM_LIBS "-framework Vision")
  set_source_files_properties(${_FLUXUS_APP_DIR}/HandHostVision.mm
                              PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/HandHostNull.cpp)
endif()

# final-stage NTSC/VHS filter (the Rust dependency of the whole build)
if(FLUXUS_ENABLE_NTSC)
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/NTSCEffect.cpp)
else()
  list(APPEND FLUXUS_PLATFORM_SOURCES ${_FLUXUS_APP_DIR}/NTSCEffectNull.cpp)
endif()

# --- embedded Racket CS -----------------------------------------------------
# -export_dynamic/--export-dynamic: same reason as the whole-archive above —
# get-ffi-obj needs the flux_* symbols in the executable's dynamic table.
if(APPLE)
  set(FLUXUS_RACKET_PLATFORM_LIBS
      "-liconv" "-lncurses" "-framework CoreFoundation" "-Wl,-export_dynamic")
  set(FLUXUS_RACKET_DEFAULT_DIR "/opt/homebrew/Cellar/minimal-racket/9.3")
else()
  set(FLUXUS_RACKET_PLATFORM_LIBS
      "-lncurses" "-ldl" "-lpthread" "-lm" "-Wl,--export-dynamic")
  set(FLUXUS_RACKET_DEFAULT_DIR "/usr/local/racket")
endif()

message(STATUS "fluxus platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} "
               "— ntsc=${FLUXUS_ENABLE_NTSC} video=${FLUXUS_ENABLE_VIDEO} "
               "hand=${FLUXUS_ENABLE_HAND}")
