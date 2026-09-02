cmake_minimum_required(VERSION 3.22)
project(FluxusJuce VERSION 0.1.0 LANGUAGES C CXX)

# fluxus -> JUCE port. The fluxus C++ engine (libfluxus) re-hosted in JUCE, driven
# by a pluggable script host (s7 or real Racket) through a shared command layer.
# Four apps: FluxusApp / FluxusGLApp (s7) and FluxusRacketApp / FluxusGLRacketApp
# (real Racket CS). See README.md for the architecture.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- video texture (AVFoundation, Objective-C++) ---------------------------
# app/VideoHost.mm decodes a movie -> GL texture. Needs ARC + these frameworks
# (OpenGL comes in via juce_opengl). Applied to every app target below.
set(FLUXUS_VIDEO_FRAMEWORKS
    "-framework AVFoundation" "-framework CoreVideo"
    "-framework CoreMedia"    "-framework QuartzCore")
set_source_files_properties(app/VideoHost.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

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

# --- NTSC-CRT (LMP88959/NTSC-CRT) final-stage composite filter --------------
# Vendored integer-only NTSC encode/decode (CRT_SYSTEM_NTSC). Header marked
# SYSTEM so its C89 warnings don't leak; app/NTSCEffect.cpp drives it.
add_library(ntsc_crt STATIC
    vendor/ntsc-crt/crt_core.c
    vendor/ntsc-crt/crt_ntsc.c)
target_include_directories(ntsc_crt SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ntsc-crt)
target_compile_options(ntsc_crt PRIVATE -w)

# --- bundled example sketches ----------------------------------------------
# Copy the upstream fluxus examples into <App>.app/Contents/Resources/examples;
# FluxusMenu lists them under File > Examples (AppMenu.h). ~370 KB, so unlike the
# Racket runtime this is unconditional. See cmake/bundle_examples.cmake.
function(fluxus_bundle_examples target)
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
    app/FluxusComponent.cpp
    app/ControlServer.cpp
    app/FluxusScene.cpp
    app/PostFX.cpp
    app/NTSCEffect.cpp
    app/TextureLoader.cpp
    app/FluxusCommands.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/VideoHost.mm
    app/S7ScriptHost.cpp)

target_compile_definitions(FluxusApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    GL_SILENCE_DEPRECATION=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusApp,JUCE_VERSION>")

target_link_libraries(FluxusApp PRIVATE
    libfluxus_min
    s7
    ntsc_crt
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
    ${FLUXUS_VIDEO_FRAMEWORKS}
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
target_link_libraries(fluxus_editor PUBLIC freetype "-framework OpenGL")
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
    app/FluxusGLComponent.cpp
    app/EditorOverlay.cpp
    app/FluxusScene.cpp
    app/PostFX.cpp
    app/NTSCEffect.cpp
    app/TextureLoader.cpp
    app/FluxusCommands.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/VideoHost.mm
    app/S7ScriptHost.cpp)

target_compile_definitions(FluxusGLApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    GL_SILENCE_DEPRECATION=1
    FLUXUS_FONT_PATH="${FLUXUS_FONT}"
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FluxusGLApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FluxusGLApp,JUCE_VERSION>")

target_link_libraries(FluxusGLApp PRIVATE
    libfluxus_min
    s7
    ntsc_crt
    fluxus_editor
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
    ${FLUXUS_VIDEO_FRAMEWORKS}
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ============================================================================
# Variant: FluxusRacketApp — the fluxus engine driven by REAL Racket (CS) via
# ffi/unsafe. Same overlay UI as FluxusApp; script host is Racket, not s7.
# ============================================================================
set(RACKET_DIR "/opt/homebrew/Cellar/minimal-racket/9.3" CACHE PATH "Racket CS install prefix")

# Copy the Racket runtime (boot files + collects + our .ss) into each .app so the
# release bundles run without `brew install minimal-racket`. Costs ~90 MB/app and
# a slow first link, so it is OFF for local dev (the host falls back to the
# absolute RACKET_DIR) and ON in CI. See cmake/bundle_racket.cmake.
option(FLUXUS_BUNDLE_RACKET "Bundle a self-contained Racket runtime into the .app" OFF)

# Add the post-build bundling step to a Racket app target.
function(fluxus_bundle_racket target)
  if(NOT FLUXUS_BUNDLE_RACKET)
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
      app/FluxusComponent.cpp
      app/ControlServer.cpp
      app/FluxusScene.cpp
      app/PostFX.cpp
      app/NTSCEffect.cpp
      app/TextureLoader.cpp
      app/FluxusCommands.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/VideoHost.mm
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
      libfluxus_min
      ntsc_crt
      juce::juce_opengl
      juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
      "${RACKET_DIR}/lib/libracketcs.a"
      "-liconv" "-lncurses"
      "-framework CoreFoundation"
      ${FLUXUS_VIDEO_FRAMEWORKS}
      "-Wl,-export_dynamic"        # export flux_* for get-ffi-obj (dlsym)
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
      app/FluxusGLComponent.cpp
      app/EditorOverlay.cpp
      app/FluxusScene.cpp
      app/PostFX.cpp
      app/NTSCEffect.cpp
      app/TextureLoader.cpp
      app/FluxusCommands.cpp
      app/AudioHost.cpp
      app/MidiHost.cpp
      app/OscHost.cpp
      app/VideoHost.mm
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
      libfluxus_min
      fluxus_editor
      juce::juce_opengl
      juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
      "${RACKET_DIR}/lib/libracketcs.a"
      "-liconv" "-lncurses"
      "-framework CoreFoundation"
      ${FLUXUS_VIDEO_FRAMEWORKS}
      "-Wl,-export_dynamic"
      PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  fluxus_bundle_examples(FluxusGLRacketApp)
  fluxus_bundle_racket(FluxusGLRacketApp)
else()
  message(STATUS "Racket not found at ${RACKET_DIR}; skipping FluxusRacketApp (brew install minimal-racket)")
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
