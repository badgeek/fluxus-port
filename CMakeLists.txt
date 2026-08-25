cmake_minimum_required(VERSION 3.22)
project(FluxusJuce VERSION 0.1.0 LANGUAGES C CXX)

# fluxus -> JUCE port. The fluxus C++ engine (libfluxus) re-hosted in JUCE, driven
# by a pluggable script host (s7 or real Racket) through a shared command layer.
# Four apps: FluxusApp / FluxusGLApp (s7) and FluxusRacketApp / FluxusGLRacketApp
# (real Racket CS). See README.md for the architecture.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

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

# --- the JUCE app ----------------------------------------------------------
juce_add_gui_app(FluxusApp PRODUCT_NAME "FluxusApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals.")

target_sources(FluxusApp PRIVATE
    app/Main.cpp
    app/FluxusComponent.cpp
    app/FluxusScene.cpp
    app/FluxusCommands.cpp
      app/AudioHost.cpp
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
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

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
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals.")

target_sources(FluxusGLApp PRIVATE
    app/MainGL.cpp
    app/FluxusGLComponent.cpp
    app/EditorOverlay.cpp
    app/FluxusScene.cpp
    app/FluxusCommands.cpp
      app/AudioHost.cpp
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
    fluxus_editor
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ============================================================================
# Variant: FluxusRacketApp — the fluxus engine driven by REAL Racket (CS) via
# ffi/unsafe. Same overlay UI as FluxusApp; script host is Racket, not s7.
# ============================================================================
set(RACKET_DIR "/opt/homebrew/Cellar/minimal-racket/9.3" CACHE PATH "Racket CS install prefix")

if(EXISTS "${RACKET_DIR}/lib/libracketcs.a")
  juce_add_gui_app(FluxusRacketApp PRODUCT_NAME "FluxusRacketApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals.")

  target_sources(FluxusRacketApp PRIVATE
      app/MainRacket.cpp
      app/FluxusComponent.cpp
      app/FluxusScene.cpp
      app/FluxusCommands.cpp
      app/AudioHost.cpp
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
      juce::juce_opengl
      juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
      "${RACKET_DIR}/lib/libracketcs.a"
      "-liconv" "-lncurses"
      "-framework CoreFoundation"
      "-Wl,-export_dynamic"        # export flux_* for get-ffi-obj (dlsym)
      PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)
  # --- FluxusGLRacketApp: fluxus's own GLEditor + REAL Racket ---------------
  juce_add_gui_app(FluxusGLRacketApp PRODUCT_NAME "FluxusGLRacketApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Fluxus uses the microphone for audio-reactive visuals.")
  target_sources(FluxusGLRacketApp PRIVATE
      app/MainGLRacket.cpp
      app/FluxusGLComponent.cpp
      app/EditorOverlay.cpp
      app/FluxusScene.cpp
      app/FluxusCommands.cpp
      app/AudioHost.cpp
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
      "${RACKET_DIR}/lib/libracketcs.a"
      "-liconv" "-lncurses"
      "-framework CoreFoundation"
      "-Wl,-export_dynamic"
      PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)
else()
  message(STATUS "Racket not found at ${RACKET_DIR}; skipping FluxusRacketApp (brew install minimal-racket)")
endif()

# Vendored-header warnings are silenced by marking their include dirs SYSTEM
# (see libfluxus_min.cmake and the fluxus_editor target above).

# --- FFT band-analysis test (console) --------------------------------------
juce_add_console_app(fft_test)
target_sources(fft_test PRIVATE spikes/fft/main.cpp)
target_link_libraries(fft_test PRIVATE
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
