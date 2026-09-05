#ifndef __OPENGL_H__
#define __OPENGL_H__

#ifdef WIN32
#define _STDCALL_SUPPORTED
#define _M_IX86
#endif

// ---------------------------------------------------------------------------
// fluxus->JUCE minimal port (Phase 0)
//
// The original fluxus build relied on GLEW (for extension entry points) and
// GLUT (for glutBitmapCharacter text). Neither is available/needed in the
// minimal macOS arm64 legacy-OpenGL-2.1 build:
//   * All fixed-function + GL2.1 entry points (glActiveTexture, shader objects,
//     compressed textures, etc.) are exported directly by the OpenGL framework
//     under a legacy context, so GLEW is unnecessary.
//   * GLUT bitmap text is stubbed (see FLUXUS_MINIMAL_NO_GLUT below); the
//     JUCE host draws its own text.
//
// We therefore drop the GLEW / GLUT includes and provide a tiny compatibility
// shim so the handful of GLEW_* references in TexturePainter.cpp /
// GLSLShader.cpp still compile and behave correctly (every extension they probe
// is core in GL2.1).
// ---------------------------------------------------------------------------

#define FLUXUS_MINIMAL_NO_GLUT 1

extern "C" {

#if defined(__ANDROID__)
// fluxus->JUCE port: GLES only — no desktop GL, no compatibility profile. The
// fixed-function half of the engine is supplied by GLCompatES.h as warn-once
// stubs so this compiles; see ANDROID-SUBSET.md for what that costs.
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#endif

}

#if defined(__ANDROID__)
#include "GLCompatES.h"
#endif

// --- Minimal GLEW compatibility shim -------------------------------------
// GLEW is not used in this minimal build. The extensions probed below are all
// part of core OpenGL 2.1 (available under a macOS legacy context), so we
// report them as present. glewInit() is a no-op returning GLEW_OK.
#ifndef __GLEW_H__
#define GLEW_OK 0
typedef unsigned int GLenum_glew_compat;
static inline unsigned int glewInit() { return GLEW_OK; }
static inline unsigned char glewIsSupported(const char *) { return 1; }
#define GLEW_ARB_multitexture              1
#define GLEW_ARB_texture_compression       1
#define GLEW_EXT_texture_compression_s3tc  1
#define GLEW_SGIS_generate_mipmap          1
#endif

#endif
