# ============================================================================
# libfluxus_min.cmake
#
# Minimal STATIC library of the libfluxus (fluxus) fixed-function OpenGL 3D
# engine, cut down for the fluxus->JUCE port (Phase 0).
#
# Target platform : macOS arm64, Apple Clang, legacy OpenGL 2.1 context
#                   (OpenGL/gl.h + OpenGL/glu.h from the system OpenGL.framework)
# External deps   : ONLY the macOS OpenGL framework. No FreeType, ODE, libpng,
#                   GLEW, GLUT, TIFF, FFGL.
#
# Provides target: libfluxus_min  (STATIC)
#   include dir   : <vendor>/fluxus/libfluxus/src  (PUBLIC)
#   link          : "-framework OpenGL"            (PUBLIC)
#
# The excluded subsystems (PixelPrimitive, NURBS-via-GLU is kept, Blobby,
# TypePrimitive/FreeType, Physics/ODE, FFGL, PNG/DDS/TIFF image loaders) are
# simply not compiled. A handful of source edits in the vendored tree remove
# stray includes of those excluded headers and the GLEW/GLUT usage; see the
# port report / the "fluxus->JUCE minimal port" comments in the vendored files.
# ============================================================================

# Resolve the vendored libfluxus source directory relative to this fragment so
# the caller can include() it from anywhere.
get_filename_component(_FLUXUS_MIN_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(FLUXUS_MIN_SRC_DIR
    "${_FLUXUS_MIN_CMAKE_DIR}/../vendor/fluxus/libfluxus/src"
    CACHE PATH "Path to vendored libfluxus/src")
get_filename_component(FLUXUS_MIN_SRC_DIR "${FLUXUS_MIN_SRC_DIR}" ABSOLUTE)

set(FLUXUS_MIN_SOURCES
    # --- math / containers -------------------------------------------------
    ${FLUXUS_MIN_SRC_DIR}/dada.cpp
    ${FLUXUS_MIN_SRC_DIR}/Allocator.cpp
    ${FLUXUS_MIN_SRC_DIR}/PData.cpp
    ${FLUXUS_MIN_SRC_DIR}/PDataContainer.cpp
    ${FLUXUS_MIN_SRC_DIR}/PDataArithmetic.cpp
    ${FLUXUS_MIN_SRC_DIR}/PDataOperator.cpp
    # --- primitive base + poly geometry -----------------------------------
    ${FLUXUS_MIN_SRC_DIR}/Primitive.cpp
    ${FLUXUS_MIN_SRC_DIR}/Evaluator.cpp
    ${FLUXUS_MIN_SRC_DIR}/PolyPrimitive.cpp
    ${FLUXUS_MIN_SRC_DIR}/PolyEvaluator.cpp
    ${FLUXUS_MIN_SRC_DIR}/RibbonPrimitive.cpp      # build-ribbon
    ${FLUXUS_MIN_SRC_DIR}/ParticlePrimitive.cpp    # build-particles
    ${FLUXUS_MIN_SRC_DIR}/LocatorPrimitive.cpp     # build-locator
    ${FLUXUS_MIN_SRC_DIR}/Geometry.cpp             # IntersectLineTriangle (ray tests)
    ${FLUXUS_MIN_SRC_DIR}/GraphicsUtils.cpp        # MakeCube lives here
    # --- extra primitives pulled in by GraphicsUtils / ShadowVolumeGen -----
    ${FLUXUS_MIN_SRC_DIR}/NURBSPrimitive.cpp       # MakeNURBS*, GLU tessellation
    ${FLUXUS_MIN_SRC_DIR}/TextPrimitive.cpp        # MakeText/Teapot (texture font)
    # --- scene graph -------------------------------------------------------
    ${FLUXUS_MIN_SRC_DIR}/Tree.cpp
    ${FLUXUS_MIN_SRC_DIR}/SceneGraph.cpp
    ${FLUXUS_MIN_SRC_DIR}/DepthSorter.cpp
    ${FLUXUS_MIN_SRC_DIR}/ShadowVolumeGen.cpp
    ${FLUXUS_MIN_SRC_DIR}/ImmediateMode.cpp
    # --- state / camera / light -------------------------------------------
    ${FLUXUS_MIN_SRC_DIR}/State.cpp
    ${FLUXUS_MIN_SRC_DIR}/Camera.cpp
    ${FLUXUS_MIN_SRC_DIR}/Light.cpp
    # --- texture / shader (GLEW usage shimmed in OpenGL.h) -----------------
    ${FLUXUS_MIN_SRC_DIR}/TexturePainter.cpp
    ${FLUXUS_MIN_SRC_DIR}/GLSLShader.cpp
    ${FLUXUS_MIN_SRC_DIR}/ShaderCache.cpp
    ${FLUXUS_MIN_SRC_DIR}/DDSLoader.cpp            # DDS texture loader (no ext deps)
    ${FLUXUS_MIN_SRC_DIR}/PNGLoader.cpp            # stubbed (FLUXUS_MINIMAL_NO_PNG)
    # --- misc support ------------------------------------------------------
    ${FLUXUS_MIN_SRC_DIR}/Renderer.cpp
    ${FLUXUS_MIN_SRC_DIR}/PrimitiveIO.cpp
    ${FLUXUS_MIN_SRC_DIR}/SearchPaths.cpp
    ${FLUXUS_MIN_SRC_DIR}/Trace.cpp
    ${FLUXUS_MIN_SRC_DIR}/DebugGL.cpp
    # --- Phase 1: rendering-backend seam (GL <-> raylib swappable) ----------
    ${FLUXUS_MIN_SRC_DIR}/RenderBackend.cpp        # global current-backend
    ${FLUXUS_MIN_SRC_DIR}/GLBackend.cpp            # default 1:1 fixed-function GL
)

add_library(libfluxus_min STATIC ${FLUXUS_MIN_SOURCES})

# Avoid the "liblibfluxus_min.a" double-prefix; produce libfluxus_min.a
set_target_properties(libfluxus_min PROPERTIES PREFIX "" OUTPUT_NAME "libfluxus_min")

# SYSTEM: suppress the legacy warnings from these vendored headers in consumers.
target_include_directories(libfluxus_min SYSTEM PUBLIC ${FLUXUS_MIN_SRC_DIR})

# C++17 (falls back cleanly; the code also builds under C++14).
target_compile_features(libfluxus_min PUBLIC cxx_std_17)

target_compile_definitions(libfluxus_min PRIVATE
    GL_SILENCE_DEPRECATION
    FLUXUS_MAJOR_VERSION=0
    FLUXUS_MINOR_VERSION=18
    FLUXUS_MINIMAL_NO_PNG      # PNGLoader compiles as a no-op stub (no libpng)
    GLSL                       # enable GLSLShader (macOS GL 2.1 has glCreateProgram etc.)
)

# Optional: cache static geometry in GPU vertex buffers (VBO) for the SOLID draw
# pass — build once, drawn from GPU memory each frame. ON by default; turn OFF to
# fall back to the original client-array path (e.g. to A/B the effect):
#   cmake -S . -B build -DFLUXUS_ENABLE_VBO=OFF …
# Note: on Apple's Metal-emulated GL the win is marginal (the per-frame cost is
# draw-call state dispatch, not vertex upload).
option(FLUXUS_ENABLE_VBO "Cache static geometry in GPU vertex buffers (solid pass)" ON)
if(FLUXUS_ENABLE_VBO)
    target_compile_definitions(libfluxus_min PUBLIC FLUXUS_ENABLE_VBO)
    message(STATUS "fluxus: VBO geometry caching ENABLED")
else()
    message(STATUS "fluxus: VBO geometry caching disabled (client arrays)")
endif()

# Silence the (many) legacy warnings.
target_compile_options(libfluxus_min PRIVATE -w)

# The only external dependency: the macOS system OpenGL framework.
target_link_libraries(libfluxus_min PUBLIC "-framework OpenGL")
