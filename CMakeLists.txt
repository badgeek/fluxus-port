cmake_minimum_required(VERSION 3.22)
project(FluxusJuce VERSION 0.1.0 LANGUAGES C CXX)

# fluxus -> JUCE port. The fluxus C++ engine (libfluxus) re-hosted in JUCE, driven
# by a pluggable script host (s7 or real Racket) through a shared command layer.
# Four apps: FluxusApp / FluxusGLApp (s7) and FluxusRacketApp / FluxusGLRacketApp
# (real Racket CS). See README.md for the architecture.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- platform layer --------------------------------------------------------
# Everything OS-specific: the system OpenGL target (fluxus_gl), the
# whole-archive helper, which backend TU implements video / hand tracking /
# NTSC (a real one or its null stub), and the Racket link extras. Nothing below
# this line needs an if(APPLE) of its own.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform.cmake)

# --- JUCE ------------------------------------------------------------------
set(JUCE_TAG "8.0.4" CACHE STRING "JUCE git tag")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- minimal libfluxus (produced by the build-minimal-libfluxus step) ------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfluxus_min.cmake)

# --- s7 Scheme (scripting host) --------------------------------------------
FetchContent_Declare(s7
    GIT_REPOSITORY https://github.com/radiganm/s7
    GIT_TAG        master
    SOURCE_SUBDIR  _no_auto_add)
FetchContent_MakeAvailable(s7)
add_library(s7 STATIC ${s7_SOURCE_DIR}/s7.c)
target_include_directories(s7 PUBLIC ${s7_SOURCE_DIR})
target_compile_definitions(s7 PRIVATE WITH_C_LOADER=0 WITH_SYSTEM_EXTRAS=0)
target_compile_options(s7 PRIVATE -w)

# --- ntsc-rs final-stage NTSC/VHS composite filter (Rust) -------------------
# The whole build's only Rust dependency, so it is switchable: with
# -DFLUXUS_ENABLE_NTSC=OFF no cargo is needed and app/NTSCEffectNull.cpp
# stands in (see cmake/platform.cmake).
if(FLUXUS_ENABLE_NTSC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ntsc_rs.cmake)
  set(FLUXUS_NTSC_LIB ntsc_ffi)
else()
  set(FLUXUS_NTSC_LIB "")
endif()

# --- libvterm (neovim/libvterm) VT/ANSI parser ------------------------------
# Vendored C99 terminal-sequence parser + screen model. Feeds the (build-terminal
# …) primitive: scripts emit ANSI, libvterm maintains the cell grid, FluxusCommands
# renders it as a glyph-atlas quad grid. Pre-generated encoding .inc tables are
# vendored so no Perl step is needed. include/ marked SYSTEM (its warnings stay in).
add_library(vterm STATIC
    vendor/libvterm/src/encoding.c vendor/libvterm/src/keyboard.c
    vendor/libvterm/src/mouse.c    vendor/libvterm/src/parser.c
    vendor/libvterm/src/pen.c      vendor/libvterm/src/screen.c
    vendor/libvterm/src/state.c    vendor/libvterm/src/unicode.c
    vendor/libvterm/src/vterm.c)
target_include_directories(vterm SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libvterm/include)
target_include_directories(vterm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libvterm/src)
set_target_properties(vterm PROPERTIES C_STANDARD 99)
target_compile_options(vterm PRIVATE -w)

# --- Dear ImGui (ocornut/imgui) on-screen tweak panel ----------------------
# Vendored core + the fixed-function OpenGL2 renderer backend, which is the one
# that matches libfluxus's own legacy GL. No platform backend: app/ImguiOverlay
# hand-feeds input from JUCE, the same way EditorOverlay drives fluxus's GLEditor.
# Include dirs marked SYSTEM so its warnings stay out of our sources.
add_library(imgui STATIC
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_opengl2.cpp)
target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends)
target_compile_definitions(imgui PRIVATE GL_SILENCE_DEPRECATION)
target_compile_options(imgui PRIVATE -w)
target_link_libraries(imgui PUBLIC fluxus_gl)

# --- assimp: 3D model import (fbx/gltf/glb/dae/ply/stl/3ds) -----------------
# Optional, like the Racket targets: without it the apps still build, they just
# lack (load-model …). brew install assimp. NOTE brew ships a DYLIB only, so a
# self-contained release .app would need a vendored static assimp (BUILD_SHARED_LIBS
# =OFF, importers trimmed) or a dylib copy + rpath fix + a
# `codesign --force --deep --sign -` re-sign, the way cmake/bundle_racket.cmake
# does for the Racket runtime. Dev builds link the brew dylib directly.
find_package(assimp CONFIG QUIET)
if(assimp_FOUND)
  message(STATUS "fluxus: assimp found (${assimp_VERSION}) — (load-model) enabled")
  set(FLUXUS_MODEL_SRC app/FluxusCommandsModel.cpp)
else()
  message(STATUS "fluxus: assimp NOT found — (load-model) disabled (brew install assimp)")
  set(FLUXUS_MODEL_SRC "")
endif()

# --- shared fluxus command layer -------------------------------------------
# The extern "C" flux_* engine binding surface both script hosts drive, split by
# domain across these TUs (app/FluxusCommandsInternal.h holds what they share).
# Every target that links a script host needs the whole set, so they are listed
# once here — see app/FluxusCommands.h for the API itself.
set(FLUXUS_CMD_SOURCES
    app/FluxusCommandsCore.cpp
    app/FluxusCommandsPdata.cpp
    app/FluxusCommandsMaths.cpp
    app/FluxusCommandsTerminal.cpp
    app/FluxusCommandsGpu.cpp
    app/FluxusCommandsInput.cpp
    app/FluxusCommandsFx.cpp)

# --- fluxus_core / fluxus_render: the JUCE-free TUs, compiled ONCE ----------
# These sources used to be listed in every app target, so each was compiled four
# (five, with pdata_bench) times. They are all JUCE-free by design — CRITICAL
# gotcha 2 in CLAUDE.md: juce_opengl hides the system GL symbols in juce::gl, so
# engine-side code must never see a juce_* header — which is exactly what makes
# them safe to share as plain static libs. Anything that DOES include juce_*
# (TextureLoader, GlyphAtlas, the components/Main*, Audio/Midi/Osc/Control hosts)
# stays per-target.
#
# Two libs, not one, so pdata_bench keeps its current dependency footprint
# (libfluxus_min + vterm + OpenGL — no GL context, no imgui, no Rust/ntsc-rs):
#   fluxus_core   — the flux_* command layer alone; what pdata_bench needs.
#   fluxus_render — the on-GL-thread app pieces every app target needs.
# juce_recommended_{warning,config}_flags carry no headers, only flags: linking
# them keeps these TUs compiling under exactly the flags the app targets used.
add_library(fluxus_core STATIC ${FLUXUS_CMD_SOURCES})
target_include_directories(fluxus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_compile_definitions(fluxus_core PRIVATE GL_SILENCE_DEPRECATION=1)
# PUBLIC: frame-begin calls into the model TU (which lives in fluxus_render, so
# pdata_bench keeps its dependency footprint), and that call must compile away
# when there is no assimp.
if(assimp_FOUND)
  target_compile_definitions(fluxus_core PUBLIC FLUXUS_HAVE_ASSIMP=1)
endif()
target_link_libraries(fluxus_core
    PUBLIC libfluxus_min vterm fluxus_gl
           juce::juce_recommended_config_flags
    PRIVATE juce::juce_recommended_warning_flags)
# Whole-archive: these TUs used to be compiled straight into each executable, so
# every object was present. From an archive the linker would keep only what
# something references — and the Racket host reaches flux_* by dlsym at runtime
# (get-ffi-obj), which the linker cannot see. Keeping the whole archive keeps
# the link content identical to before, so no binding silently falls back to its
# failure thunk. Same reasoning for fluxus_render below; the per-platform flag
# lives in cmake/platform.cmake.
fluxus_whole_archive(fluxus_core)

# EditorOverlay.cpp is JUCE-free too but only the GLEditor apps use it, and it
# would drag fluxus_editor/FreeType into the others — so it stays per-target.
# The platform sources (energy policy, video, hand tracking, NTSC) come from
# cmake/platform.cmake — each is either a real backend or its null stub, so the
# flux_* surface every script host binds is the same on every platform.
add_library(fluxus_render STATIC
    app/FluxusScene.cpp
    app/PostFX.cpp
    app/ImguiOverlay.cpp
    ${FLUXUS_MODEL_SRC}
    ${FLUXUS_PLATFORM_SOURCES})
target_compile_definitions(fluxus_render PRIVATE GL_SILENCE_DEPRECATION=1)
target_link_libraries(fluxus_render
    PUBLIC fluxus_core imgui ${FLUXUS_NTSC_LIB} ${FLUXUS_PLATFORM_LIBS}
    PRIVATE juce::juce_recommended_warning_flags)
# assimp lives here, not in fluxus_core: pdata_bench links fluxus_core and must
# keep its (deliberately small) dependency footprint.
if(assimp_FOUND)
  target_link_libraries(fluxus_render PUBLIC assimp::assimp)
endif()
fluxus_whole_archive(fluxus_render)

# --- bundled example sketches ----------------------------------------------
# Copy the upstream fluxus examples into <App>.app/Contents/Resources/examples;
# FluxusMenu lists them under File > Examples (AppMenu.h). ~370 KB, so unlike the
# Racket runtime this is unconditional. See cmake/bundle_examples.cmake.
function(fluxus_bundle_examples target)
  # Bundling is a .app concept; TARGET_BUNDLE_DIR has no meaning elsewhere. On
  # other platforms the examples are read from the source tree (a proper
  # install(DIRECTORY …) rule is the follow-up when there is a Linux package).
  if(NOT APPLE)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DEXAMPLES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/vendor/fluxus/examples
            -DAPP_DIR=$<TARGET_BUNDLE_DIR:${target}>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_examples.cmake
    VERBATIM COMMENT "Bundling example sketches into ${target}.app")
endfunction()

# --- the JUCE app ----------------------------------------------------------
juce_add_gui_app(FluxusApp PRODUCT_NAME "FluxusApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals."
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Fluxus uses the camera as a live video texture.")

target_sources(FluxusApp PRIVATE
    app/Main.cpp
    app/GlyphAtlas.cpp
    app/FluxusComponent.cpp
    app/ControlServer.cpp
    app/TextureLoader.cpp
    app/AudioHost.cpp
    app/MidiHost.cpp
    app/OscHost.cpp
    app/S7ScriptHost.cpp)

target_compile_definitions(FluxusApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    GL_SILENCE_DEPRECATION=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusApp,JUCE_VERSION>")

target_link_libraries(FluxusApp PRIVATE
    fluxus_render
    s7
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

fluxus_bundle_examples(FluxusApp)

# ============================================================================
# Variant: FluxusGLApp — uses fluxus's OWN GL text editor (GLEditor/PolyGlyph)
# instead of a JUCE TextEditor. Needs FreeType (font -> glyph meshes).
# ============================================================================

# --- FreeType (for PolyGlyph) ----------------------------------------------
set(FT_DISABLE_HARFBUZZ ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB     ON  CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype
    GIT_TAG        VER-2-13-2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(freetype)

# --- fluxus's own GL editor (GLEditor + PolyGlyph + Unicode) ----------------
set(FLUXUS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/fluxus/src)
add_library(fluxus_editor STATIC
    ${FLUXUS_SRC}/GLEditor.cpp
    ${FLUXUS_SRC}/PolyGlyph.cpp
    ${FLUXUS_SRC}/Unicode.cpp)
target_include_directories(fluxus_editor SYSTEM PUBLIC ${FLUXUS_SRC})
target_compile_definitions(fluxus_editor PRIVATE GL_SILENCE_DEPRECATION)
target_compile_options(fluxus_editor PRIVATE -w)
target_link_libraries(fluxus_editor PUBLIC freetype fluxus_gl)
set_target_properties(fluxus_editor PROPERTIES CXX_STANDARD 14)  # fluxus 'byte' vs std::byte

# --- the variant app -------------------------------------------------------
set(FLUXUS_FONT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fluxus/modules/material/fonts/DejaVuSansMono.ttf")

juce_add_gui_app(FluxusGLApp PRODUCT_NAME "FluxusGLApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals."
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Fluxus uses the camera as a live video texture.")

target_sources(FluxusGLApp PRIVATE
    app/MainGL.cpp
    app/GlyphAtlas.cpp
    app/FluxusGLComponent.cpp
    app/EditorOverlay.cpp
    app/TextureLoader.cpp
    app/AudioHost.cpp
    app/MidiHost.cpp
    app/OscHost.cpp
    app/S7ScriptHost.cpp)

target_compile_definitions(FluxusGLApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    GL_SILENCE_DEPRECATION=1
    FLUXUS_FONT_PATH="${FLUXUS_FONT}"
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusGLApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusGLApp,JUCE_VERSION>")

target_link_libraries(FluxusGLApp PRIVATE
    fluxus_render
    s7
    fluxus_editor
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ============================================================================
# Variant: FluxusRacketApp — the fluxus engine driven by REAL Racket (CS) via
# ffi/unsafe. Same overlay UI as FluxusApp; script host is Racket, not s7.
# ============================================================================
set(RACKET_DIR "${FLUXUS_RACKET_DEFAULT_DIR}" CACHE PATH "Racket CS install prefix")

# Copy the Racket runtime (boot files + collects + our .ss) into each .app so the
# release bundles run without `brew install minimal-racket`. Costs ~90 MB/app and
# a slow first link, so it is OFF for local dev (the host falls back to the
# absolute RACKET_DIR) and ON in CI. See cmake/bundle_racket.cmake.
option(FLUXUS_BUNDLE_RACKET "Bundle a self-contained Racket runtime into the .app" OFF)

# Add the post-build bundling step to a Racket app target.
function(fluxus_bundle_racket target)
  if(NOT FLUXUS_BUNDLE_RACKET OR NOT APPLE)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DRACKET_DIR=${RACKET_DIR}
            -DFLUXUS_LIB=${CMAKE_CURRENT_SOURCE_DIR}/racket-lib
            -DAPP_DIR=$<TARGET_BUNDLE_DIR:${target}>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_racket.cmake
    VERBATIM COMMENT "Bundling self-contained Racket runtime into ${target}.app")
endfunction()

if(EXISTS "${RACKET_DIR}/lib/libracketcs.a")
  juce_add_gui_app(FluxusRacketApp PRODUCT_NAME "FluxusRacketApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals."
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Fluxus uses the camera as a live video texture.")

  target_sources(FluxusRacketApp PRIVATE
      app/MainRacket.cpp
      app/GlyphAtlas.cpp
      app/FluxusComponent.cpp
      app/ControlServer.cpp
      app/TextureLoader.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/RacketScriptHost.cpp)

  target_include_directories(FluxusRacketApp PRIVATE "${RACKET_DIR}/include/racket")

  target_compile_definitions(FluxusRacketApp PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      GL_SILENCE_DEPRECATION=1
      RACKET_DIR="${RACKET_DIR}"
      RACKET_LIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/racket-lib"
      JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusRacketApp,JUCE_PRODUCT_NAME>"
      JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusRacketApp,JUCE_VERSION>")

  target_link_libraries(FluxusRacketApp PRIVATE
      fluxus_render
      juce::juce_opengl
      juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
      "${RACKET_DIR}/lib/libracketcs.a"
      # per-platform extras + the export-dynamic flag that makes flux_* visible
      # to get-ffi-obj (dlsym) — see cmake/platform.cmake
      ${FLUXUS_RACKET_PLATFORM_LIBS}
      PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  # examples FIRST: bundle_racket re-signs, and adding files after a sign breaks it
  fluxus_bundle_examples(FluxusRacketApp)
  fluxus_bundle_racket(FluxusRacketApp)

  # --- FluxusGLRacketApp: fluxus's own GLEditor + REAL Racket ---------------
  juce_add_gui_app(FluxusGLRacketApp PRODUCT_NAME "FluxusGLRacketApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals."
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Fluxus uses the camera as a live video texture.")
  target_sources(FluxusGLRacketApp PRIVATE
      app/MainGLRacket.cpp
      app/GlyphAtlas.cpp
      app/FluxusGLComponent.cpp
      app/EditorOverlay.cpp
      app/TextureLoader.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/RacketScriptHost.cpp)
  target_include_directories(FluxusGLRacketApp PRIVATE "${RACKET_DIR}/include/racket")
  target_compile_definitions(FluxusGLRacketApp PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      GL_SILENCE_DEPRECATION=1
      RACKET_DIR="${RACKET_DIR}"
      RACKET_LIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/racket-lib"
      FLUXUS_FONT_PATH="${FLUXUS_FONT}"
      JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusGLRacketApp,JUCE_PRODUCT_NAME>"
      JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusGLRacketApp,JUCE_VERSION>")
  target_link_libraries(FluxusGLRacketApp PRIVATE
      fluxus_render
      fluxus_editor
      juce::juce_opengl
      juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
      "${RACKET_DIR}/lib/libracketcs.a"
      ${FLUXUS_RACKET_PLATFORM_LIBS}
      PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  fluxus_bundle_examples(FluxusGLRacketApp)
  fluxus_bundle_racket(FluxusGLRacketApp)
else()
  message(STATUS "Racket CS not found at ${RACKET_DIR}; skipping the Racket apps. "
                 "Install it (macOS: brew install minimal-racket; elsewhere: a "
                 "Racket CS distribution with lib/libracketcs.a) and point "
                 "-DRACKET_DIR= at the prefix.")
endif()

# Vendored-header warnings are silenced by marking their include dirs SYSTEM
# (see libfluxus_min.cmake and the fluxus_editor target above).

# --- audio input probe (console) -------------------------------------------
juce_add_console_app(audio_probe)
target_sources(audio_probe PRIVATE spikes/audioprobe/main.cpp)
target_compile_definitions(audio_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(audio_probe PRIVATE
    juce::juce_audio_devices
    PUBLIC juce::juce_recommended_config_flags)

# --- AudioHost analysis probe (console; real AudioHost, stub sink) ----------
juce_add_console_app(audio_host_probe)
target_sources(audio_host_probe PRIVATE spikes/audiohostprobe/main.cpp app/AudioHost.cpp)
target_include_directories(audio_host_probe PRIVATE app)
target_compile_definitions(audio_host_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(audio_host_probe PRIVATE
    juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# --- FFT band-analysis test (console) --------------------------------------
juce_add_console_app(fft_test)
target_sources(fft_test PRIVATE spikes/fft/main.cpp)
target_link_libraries(fft_test PRIVATE
    juce::juce_dsp
    juce::juce_osc
    PUBLIC juce::juce_recommended_config_flags)

# --- pdata FFI-layer benchmark + test (headless, no GL context, no JUCE) ----
add_executable(pdata_bench spikes/pdata-bench/main.cpp)
target_compile_definitions(pdata_bench PRIVATE GL_SILENCE_DEPRECATION=1)
target_link_libraries(pdata_bench PRIVATE fluxus_core)

# --- dada math regression test (dMatrix::inverse / dQuat fixes) -------------
add_executable(math_test spikes/math-test/main.cpp)
target_link_libraries(math_test PRIVATE libfluxus_min)

# --- assimp model import test (headless, no GL context, no JUCE) ------------
# Compiles the model TU straight in rather than linking fluxus_render, which
# would drag JUCE/imgui into a console test. Only built when assimp is present.
if(assimp_FOUND)
  add_executable(model_test spikes/model-load/main.cpp app/FluxusCommandsModel.cpp)
  target_compile_definitions(model_test PRIVATE GL_SILENCE_DEPRECATION=1)
  target_link_libraries(model_test PRIVATE fluxus_core assimp::assimp)
endif()
