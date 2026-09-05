#ifndef __OPENGL_H__
#define __OPENGL_H__

// GLES cube spike — the compatibility shim.
//
// Force-included ahead of everything (see build.sh) and claiming the vendored
// __OPENGL_H__ guard, so vendor/fluxus/libfluxus/src/OpenGL.h becomes a no-op
// and the engine compiles against GLES **without a single edit under vendor/**.
// That is the whole point: this spike must not touch shared code.
//
// Three kinds of entry live here:
//   1. missing ENUMS — given arbitrary distinct values; nothing reaches real GL
//      with them because every consumer is stubbed or filtered below.
//   2. no-op STUBS for fixed-function calls that have no GLES meaning at all
//      (immediate mode, lighting, texgen, stipple, selection buffer).
//   3. CAPTURING stubs. These are the interesting ones: the client-array
//      pointers, the current colour and the polygon mode are recorded, and
//      glDrawArrays/glDrawElements are macro-redirected into the spike so the
//      engine's raw WIRE pass — which bypasses IRenderBackend entirely
//      (PolyPrimitive.cpp:290-313) — can still be drawn by GLESBackend.
//
// The solid pass needs none of this: it already goes through
// Backend()->drawArrays (PolyPrimitive.cpp:277).

#define FLUXUS_MINIMAL_NO_GLUT 1

extern "C" {
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
}

// --- 1. enums GLES does not define -----------------------------------------
#define GL_QUADS                    0x0007
#define GL_POLYGON                  0x0009
#define GL_MODULATE                 0x2100
#define GL_LIGHTING                 0x0B50
#define GL_LIGHT0                   0x4000
#define GL_COLOR_MATERIAL           0x0B57
#define GL_NORMALIZE                0x0BA1
#define GL_RESCALE_NORMAL           0x803A
#define GL_LINE_SMOOTH              0x0B20
#define GL_POINT_SMOOTH             0x0B10
#define GL_LINE_STIPPLE             0x0B24
#define GL_VERTEX_ARRAY             0x8074
#define GL_NORMAL_ARRAY             0x8075
#define GL_COLOR_ARRAY              0x8076
#define GL_TEXTURE_COORD_ARRAY      0x8078
#define GL_MODELVIEW                0x1700
#define GL_PROJECTION               0x1701
#define GL_MODELVIEW_MATRIX         0x0BA6
#define GL_PROJECTION_MATRIX        0x0BA7
#define GL_FILL                     0x1B02
#define GL_LINE                     0x1B01
#define GL_POINT                    0x1B00
#define GL_S                        0x2000
#define GL_T                        0x2001
#define GL_SPHERE_MAP               0x2402
#define GL_TEXTURE_GEN_MODE         0x2500
#define GL_TEXTURE_GEN_S            0x0C60
#define GL_TEXTURE_GEN_T            0x0C61
#define GL_TEXTURE_ENV              0x2300
#define GL_TEXTURE_ENV_MODE         0x2200
#define GL_TEXTURE_ENV_COLOR        0x2201
#define GL_TEXTURE_PRIORITY         0x8066
#define GL_GENERATE_MIPMAP_SGIS     0x8191
#define GL_BGR_EXT                  0x80E0
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#define GL_AMBIENT                  0x1200
#define GL_DIFFUSE                  0x1201
#define GL_SPECULAR                 0x1202
#define GL_EMISSION                 0x1600
#define GL_SHININESS                0x1601
#define GL_GEOMETRY_VERTICES_OUT_EXT 0x8DDA
#define GL_GEOMETRY_INPUT_TYPE_EXT   0x8DDB
#define GL_GEOMETRY_OUTPUT_TYPE_EXT  0x8DDC

// GLU types survive as opaque pointer members in headers this subset pulls in
// transitively (NURBSPrimitive.h, via Renderer.h -> SceneGraph.h ->
// ShadowVolumeGen.h). Nothing in the cube path ever calls GLU.
typedef struct GLUnurbsObjOpaque  GLUnurbsObj;
typedef struct GLUquadricObjOpaque GLUquadricObj;
typedef struct GLUtesselatorOpaque GLUtesselator;

// --- 3. capture state the wire pass needs -----------------------------------
// Declarations live in LegacyCapture.h so GLESBackend.cpp can share them
// without including this shim (whose macros would hijack its real GLES calls).
#include "LegacyCapture.h"

static inline void glVertexPointer(int, unsigned, int s, const void* p)
    { fluxspike::legacy().pos = p; fluxspike::legacy().posStride = s; }
static inline void glNormalPointer(unsigned, int s, const void* p)
    { fluxspike::legacy().nrm = p; fluxspike::legacy().nrmStride = s; }
static inline void glColorPointer(int, unsigned, int s, const void* p)
    { fluxspike::legacy().col = p; fluxspike::legacy().colStride = s; }
static inline void glTexCoordPointer(int, unsigned, int, const void*) {}
static inline void glColor4fv(const float* c)
    { for (int i = 0; i < 4; ++i) fluxspike::legacy().colour[i] = c[i]; }
static inline void glColor3fv(const float* c)
    { for (int i = 0; i < 3; ++i) fluxspike::legacy().colour[i] = c[i];
      fluxspike::legacy().colour[3] = 1.0f; }
static inline void glColor4f(float r, float g, float b, float a)
    { float c[4] = {r, g, b, a}; glColor4fv(c); }
static inline void glColor3f(float r, float g, float b) { glColor4f(r, g, b, 1); }
static inline void glPolygonMode(unsigned, unsigned m)
    { fluxspike::legacy().polygonMode = m; }

// glEnable/glDisable are real in GLES, but the engine passes fixed-function
// enums to them; let those through and GL_INVALID_ENUM pollutes the error
// state, which then looks like a bug in our own code.
#define glEnable(c)  do { if (!fluxspike::enumIsLegacy(c)) ::glEnable(c);  } while (0)
#define glDisable(c) do { if (!fluxspike::enumIsLegacy(c)) ::glDisable(c); } while (0)

// The wire/points passes call these directly with the client arrays above.
#define glDrawArrays(m, f, c)          fluxspike::legacyDraw(m, f, c)
#define glDrawElements(m, c, t, i)     fluxspike::legacyDrawElements(m, c, t, i)

// --- 2. pure no-ops ---------------------------------------------------------
static inline void glBegin(unsigned) {}
static inline void glEnd() {}
static inline void glVertex3f(float, float, float) {}
static inline void glVertex3fv(const float*) {}
static inline void glNormal3fv(const float*) {}
static inline void glTexCoord2f(float, float) {}
static inline void glEnableClientState(unsigned) {}
static inline void glDisableClientState(unsigned) {}
static inline void glClientActiveTexture(unsigned) {}
static inline void glMatrixMode(unsigned) {}
static inline void glPushMatrix() {}
static inline void glPopMatrix() {}
static inline void glLoadIdentity() {}
static inline void glLoadMatrixf(const float*) {}
static inline void glMultMatrixf(const float*) {}
static inline void glTranslatef(float, float, float) {}
static inline void glFrustum(double, double, double, double, double, double) {}
static inline void glOrtho(double, double, double, double, double, double) {}
static inline void glLightfv(unsigned, unsigned, const float*) {}
static inline void glLightf(unsigned, unsigned, float) {}
static inline void glLightModeli(unsigned, int) {}
static inline void glMaterialfv(unsigned, unsigned, const float*) {}
static inline void glTexEnvi(unsigned, unsigned, int) {}
static inline void glTexEnvfv(unsigned, unsigned, const float*) {}
static inline void glTexGeni(unsigned, unsigned, int) {}
static inline void glLineStipple(int, unsigned short) {}
static inline void glPointSize(float) {}
static inline void glShadeModel(unsigned) {}
static inline void glFogf(unsigned, float) {}
static inline void glFogfv(unsigned, const float*) {}
static inline void glFogi(unsigned, int) {}
static inline void glRasterPos3f(float, float, float) {}
static inline void glAccum(unsigned, float) {}
static inline void glDrawBuffer(unsigned) {}
static inline void glSelectBuffer(int, unsigned*) {}
static inline void glInitNames() {}
static inline void glPushName(unsigned) {}
static inline void glPopName() {}
static inline int  glRenderMode(unsigned) { return 0; }
static inline void glPrioritizeTextures(int, const unsigned*, const float*) {}
static inline unsigned char glAreTexturesResident(int, const unsigned*, unsigned char*) { return 1; }
static inline void glCompressedTexImage2DARB(unsigned, int, unsigned, int, int, int, int, const void*) {}
// GLES 3.2 DOES have geometry shaders (see spikes/gles-audit/README.md) — it just
// configures them with in-shader layout qualifiers instead of these EXT calls.
static inline void glProgramParameteriEXT(unsigned, unsigned, int) {}
static inline int  gluBuild2DMipmaps(unsigned, int, int, int, unsigned, unsigned, const void*) { return 0; }
static inline const char* gluErrorString(unsigned) { return "gl-error"; }
static inline void gluPickMatrix(double, double, double, double, const int*) {}

// GLEW compatibility, same as the real header.
#ifndef __GLEW_H__
#define GLEW_OK 0
static inline unsigned int glewInit() { return GLEW_OK; }
static inline unsigned char glewIsSupported(const char*) { return 1; }
#define GLEW_ARB_multitexture              1
#define GLEW_ARB_texture_compression       1
#define GLEW_EXT_texture_compression_s3tc  1
#define GLEW_SGIS_generate_mipmap          1
#endif

#endif
