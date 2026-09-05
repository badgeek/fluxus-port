// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// System OpenGL headers for the JUCE-free, engine-side TUs.
//
// Those files must never include juce_opengl (CRITICAL gotcha 2 in CLAUDE.md:
// it hides the GL symbols in juce::gl and guards <OpenGL/gl.h>), so they reach
// for the system headers directly — and the path to those differs per OS. Say
// it once here instead of a dozen scattered <OpenGL/gl.h> includes, so a new
// platform is one edit.
//
// The vendored engine carries its own copy of this switch (plus the GLEW/GLUT
// shim) in libfluxus/src/OpenGL.h; this is the app-side equivalent for TUs that
// don't pull an engine header in.

#if defined(__ANDROID__)
// GLES only — there is no desktop GL and no compatibility profile on Android, so
// the fixed-function half of the engine has no symbols here at all. See
// ANDROID-SUBSET.md for what that costs and spikes/gles-audit for the count.
// GLES 3.2 is the widest surface Android offers (and, unlike the Pi's v3d, it
// does include geometry shaders).
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
// Mesa/GLVND hide the post-1.1 entry points behind this; without it every
// GL 1.5+ call (glBindBuffer, the shader objects, FBOs) is an implicit-decl
// error rather than a link against the real symbol.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
