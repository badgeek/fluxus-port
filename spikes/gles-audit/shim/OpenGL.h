#ifndef __OPENGL_H__
#define __OPENGL_H__

// GLES audit shim (spikes/gles-audit).
//
// Stands in for vendor/fluxus/libfluxus/src/OpenGL.h by sitting EARLIER on the
// include path, so the vendored engine sources compile against Android's GLES
// headers without editing a single one of them. Everything the engine reaches
// for that GLES does not have then shows up as a compile error, which is the
// measurement this spike exists to take.
//
// GLES 3.2 is used deliberately — the largest surface Android offers, so a
// symbol missing here is missing everywhere, not an artifact of picking GLES 2.
// There is no GLU on Android at all, so nothing stands in for <glu.h>.

#define FLUXUS_MINIMAL_NO_GLUT 1

extern "C" {
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
}

// Same GLEW compatibility shim as the real header: the engine probes these
// extensions and they are core in GL 2.1 / GLES 3.
#ifndef __GLEW_H__
#define GLEW_OK 0
static inline unsigned int glewInit() { return GLEW_OK; }
static inline unsigned char glewIsSupported(const char *) { return 1; }
#define GLEW_ARB_multitexture              1
#define GLEW_ARB_texture_compression       1
#define GLEW_EXT_texture_compression_s3tc  1
#define GLEW_SGIS_generate_mipmap          1
#endif

#endif
