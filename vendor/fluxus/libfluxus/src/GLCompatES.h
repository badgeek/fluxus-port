#ifndef __GL_COMPAT_ES_H__
#define __GL_COMPAT_ES_H__

// fluxus->JUCE port: fixed-function compatibility layer for OpenGL ES.
//
// GLES has no fixed-function pipeline and no compatibility profile, so roughly
// a fifth of the engine simply has no symbols to link against there (measured:
// spikes/gles-audit). This header supplies them so the engine COMPILES on
// Android, and every feature it stands in for is listed as unsupported in
// ANDROID-SUBSET.md.
//
// The stubs WARN ONCE on stderr rather than doing nothing quietly. That is
// deliberate: silent no-ops are the failure mode that has cost this project
// whole sessions — a command appears to work, draws nothing, and the search
// starts in the wrong place. Same reasoning as VideoHostNull / NTSCEffectNull.
//
// This is a stopgap, not the destination. Each drawing path that moves behind
// IRenderBackend (RibbonPrimitive and ParticlePrimitive already have) stops
// depending on these stubs and starts working for real on GLES. When a stub's
// warning no longer appears in a session, that path is genuinely ported.

#include <cstdio>
#include <cstdlib>

namespace FluxusGLCompat {

inline void warn(const char *name)
{
	// One warning per distinct call site, kept cheap: the caller passes a
	// literal and each inline function owns its own flag.
	std::fprintf(stderr, "[fluxus] GLES: '%s' has no equivalent — this call did "
	                     "nothing (see ANDROID-SUBSET.md)\n", name);
}

#define FLUXUS_ES_STUB(name) \
	do { static bool warned = false; if (!warned) { warned = true; FluxusGLCompat::warn(name); } } while (0)

} // namespace FluxusGLCompat

// --- enums the engine names but GLES does not define ------------------------
#define GL_QUADS                     0x0007
#define GL_POLYGON                   0x0009
#define GL_MODULATE                  0x2100
#define GL_LIGHTING                  0x0B50
#define GL_LIGHT0                    0x4000
#define GL_COLOR_MATERIAL            0x0B57
#define GL_NORMALIZE                 0x0BA1
#define GL_RESCALE_NORMAL            0x803A
#define GL_LINE_SMOOTH               0x0B20
#define GL_POINT_SMOOTH              0x0B10
#define GL_LINE_STIPPLE              0x0B24
#define GL_VERTEX_ARRAY              0x8074
#define GL_NORMAL_ARRAY              0x8075
#define GL_COLOR_ARRAY               0x8076
#define GL_TEXTURE_COORD_ARRAY       0x8078
#define GL_MODELVIEW                 0x1700
#define GL_PROJECTION                0x1701
#define GL_MODELVIEW_MATRIX          0x0BA6
#define GL_PROJECTION_MATRIX         0x0BA7
#define GL_FILL                      0x1B02
#define GL_LINE                      0x1B01
#define GL_POINT                     0x1B00
#define GL_S                         0x2000
#define GL_T                         0x2001
#define GL_SPHERE_MAP                0x2402
#define GL_TEXTURE_GEN_MODE          0x2500
#define GL_TEXTURE_GEN_S             0x0C60
#define GL_TEXTURE_GEN_T             0x0C61
#define GL_TEXTURE_ENV               0x2300
#define GL_TEXTURE_ENV_MODE          0x2200
#define GL_TEXTURE_ENV_COLOR         0x2201
#define GL_TEXTURE_PRIORITY          0x8066
#define GL_GENERATE_MIPMAP_SGIS      0x8191
#define GL_BGR_EXT                   0x80E0
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#define GL_AMBIENT                   0x1200
#define GL_DIFFUSE                   0x1201
#define GL_SPECULAR                  0x1202
#define GL_EMISSION                  0x1600
#define GL_SHININESS                 0x1601
#define GL_POSITION                  0x1203
#define GL_SPOT_DIRECTION            0x1204
#define GL_SPOT_EXPONENT             0x1205
#define GL_SPOT_CUTOFF               0x1206
#define GL_CONSTANT_ATTENUATION      0x1207
#define GL_LINEAR_ATTENUATION        0x1208
#define GL_QUADRATIC_ATTENUATION     0x1209
#define GL_LIGHT_MODEL_COLOR_CONTROL 0x81F8
#define GL_SEPARATE_SPECULAR_COLOR   0x81FA
#define GL_FOG                       0x0B60
#define GL_FOG_MODE                  0x0B65
#define GL_FOG_DENSITY               0x0B62
#define GL_FOG_START                 0x0B63
#define GL_FOG_END                   0x0B64
#define GL_FOG_COLOR                 0x0B66
#define GL_FOG_HINT                  0x0C54
#define GL_EXP                       0x0800
#define GL_ACCUM_BUFFER_BIT          0x00000200
#define GL_SELECT                    0x1C02
#define GL_RENDER                    0x1C00
#define GL_STEREO                    0x0C33
#define GL_GEOMETRY_VERTICES_OUT_EXT 0x8DDA
#define GL_GEOMETRY_INPUT_TYPE_EXT   0x8DDB
#define GL_GEOMETRY_OUTPUT_TYPE_EXT  0x8DDC

// GLU types survive only as opaque pointer members in headers; nothing on
// Android may call GLU, which does not exist there at all.
typedef struct GLUnurbsObjOpaque   GLUnurbsObj;
typedef struct GLUquadricObjOpaque GLUquadricObj;
typedef struct GLUtesselatorOpaque GLUtesselator;

// Renderer.cpp falls back to the old EXT spelling when GL_POLYGON_OFFSET is
// undefined. On GLES the capability exists under its modern name, so map it
// rather than stub it: polygon offset really works here, and it is what keeps
// a hidden-line wire pass from z-fighting with its own fill.
#define GL_POLYGON_OFFSET GL_POLYGON_OFFSET_FILL

// --- glEnable / glDisable filtering -----------------------------------------
// These two ARE real GLES calls, but the engine passes fixed-function
// capabilities to them (GL_LIGHTING, GL_TEXTURE_2D, the texgen and client-array
// enums). Letting those through sets GL_INVALID_ENUM, which then shows up in
// completely unrelated code as a mysterious error — so filter them here rather
// than debugging the same red herring twice.
namespace FluxusGLCompat {
inline bool isFixedFunctionCap(unsigned cap)
{
	switch (cap)
	{
		case 0x0B50: // GL_LIGHTING
		case 0x4000: // GL_LIGHT0
		case 0x0B57: // GL_COLOR_MATERIAL
		case 0x0BA1: // GL_NORMALIZE
		case 0x803A: // GL_RESCALE_NORMAL
		case 0x0B20: // GL_LINE_SMOOTH
		case 0x0B10: // GL_POINT_SMOOTH
		case 0x0B24: // GL_LINE_STIPPLE
		case 0x0B60: // GL_FOG
		case 0x8074: case 0x8075: case 0x8076: case 0x8078: // client arrays
		case 0x0C60: case 0x0C61: // GL_TEXTURE_GEN_S / _T
		// Texturing is a shader decision on ES: these are valid to BIND with but
		// not to enable, so they would raise GL_INVALID_ENUM too.
		case 0x0DE1: // GL_TEXTURE_2D
		case 0x8513: // GL_TEXTURE_CUBE_MAP
		case 0x8642: // GL_VERTEX_PROGRAM_POINT_SIZE
		case 0x0C33: // GL_STEREO
			return true;
		default:
			return false;
	}
}
} // namespace FluxusGLCompat

// The filter list cannot be complete by inspection — the engine is large and
// some caps arrive through macros. So check right after the call, where the cap
// is still known, and name it. Bisecting a GL_INVALID_ENUM after the fact costs
// far more than this does. (It also swallows a pending error from earlier code,
// which is a fair trade: an unattributed error is worth less than an attributed
// one.)
namespace FluxusGLCompat {
inline void setCap(unsigned cap, bool on, const char* where)
{
	if (isFixedFunctionCap(cap)) return;

	// Diagnostics are OFF unless FLUXUS_GL_DEBUG is set, and deliberately so.
	// Attributing the error correctly means draining the queue first, and a
	// drain on every enable/disable would SWALLOW errors raised anywhere else —
	// the check would end up hiding the very bugs it exists to find.
	static const bool debug = std::getenv("FLUXUS_GL_DEBUG") != 0;
	if (!debug)
	{
		if (on) ::glEnable(cap); else ::glDisable(cap);
		return;
	}

	while (::glGetError() != 0) {}
	if (on) ::glEnable(cap); else ::glDisable(cap);
	if (::glGetError() == 0x0500)
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			std::fprintf(stderr, "[fluxus] GLES: %s(0x%04X) is not a capability here "
			                     "(%s) — add it to isFixedFunctionCap\n",
			             on ? "glEnable" : "glDisable", cap, where);
		}
	}
}
} // namespace FluxusGLCompat

#define glEnable(cap)  FluxusGLCompat::setCap((cap), true,  __FILE__)
#define glDisable(cap) FluxusGLCompat::setCap((cap), false, __FILE__)

// --- immediate mode ---------------------------------------------------------
// The largest group. Every primitive still drawing this way renders NOTHING on
// GLES until it moves behind IRenderBackend.
static inline void glBegin(unsigned)                 { FLUXUS_ES_STUB("glBegin (immediate mode)"); }
static inline void glEnd()                           {}
static inline void glVertex2f(float, float)          {}
static inline void glVertex3f(float, float, float)   {}
static inline void glVertex3fv(const float*)         {}
static inline void glNormal3fv(const float*)         {}
static inline void glTexCoord2f(float, float)        {}
static inline void glColor3f(float, float, float)    {}
static inline void glColor3fv(const float*)          {}
static inline void glColor4f(float, float, float, float) {}
static inline void glColor4fv(const float*)          {}

// --- client arrays ----------------------------------------------------------
static inline void glEnableClientState(unsigned)     { FLUXUS_ES_STUB("glEnableClientState (client arrays)"); }
static inline void glDisableClientState(unsigned)    {}
static inline void glVertexPointer(int, unsigned, int, const void*)   {}
static inline void glNormalPointer(unsigned, int, const void*)        {}
static inline void glColorPointer(int, unsigned, int, const void*)    {}
static inline void glTexCoordPointer(int, unsigned, int, const void*) {}
static inline void glClientActiveTexture(unsigned)   {}

// --- matrix queries ---------------------------------------------------------
// glGetFloatv is a real GLES call, but GL_MODELVIEW_MATRIX / GL_PROJECTION_MATRIX
// are not GLES pnames — passing them through raises GL_INVALID_ENUM and leaves
// the caller reading an uninitialised matrix. SceneGraph already asks the
// backend instead (getModelView/getProjection); Renderer::PreRender does not
// yet, so intercept until it does.
namespace FluxusGLCompat {
inline bool isMatrixPName(unsigned p) { return p == 0x0BA6 || p == 0x0BA7; }
} // namespace FluxusGLCompat
#define glGetFloatv(pname, dst) \
	do { \
		if (FluxusGLCompat::isMatrixPName(pname)) FLUXUS_ES_STUB("glGetFloatv(GL_*_MATRIX) — ask IRenderBackend instead"); \
		else ::glGetFloatv((pname), (dst)); \
	} while (0)

// --- matrix stack -----------------------------------------------------------
// IRenderBackend owns this now; anything still calling it directly is a path
// that has not been ported.
static inline void glMatrixMode(unsigned)            { FLUXUS_ES_STUB("glMatrixMode (fixed-function matrix stack)"); }
static inline void glPushMatrix()                    {}
static inline void glPopMatrix()                     {}
static inline void glLoadIdentity()                  {}
static inline void glLoadMatrixf(const float*)       {}
static inline void glMultMatrixf(const float*)       {}
static inline void glTranslatef(float, float, float) {}
static inline void glFrustum(double, double, double, double, double, double) { FLUXUS_ES_STUB("glFrustum"); }
static inline void glOrtho(double, double, double, double, double, double)   { FLUXUS_ES_STUB("glOrtho"); }

// --- lighting and material --------------------------------------------------
static inline void glLightfv(unsigned, unsigned, const float*)   { FLUXUS_ES_STUB("glLightfv (fixed-function lighting)"); }
static inline void glLightf(unsigned, unsigned, float)           {}
static inline void glLightModeli(unsigned, int)                  {}
static inline void glMaterialfv(unsigned, unsigned, const float*){ FLUXUS_ES_STUB("glMaterialfv (fixed-function material)"); }
static inline void glShadeModel(unsigned)                        {}

// --- texture environment ----------------------------------------------------
static inline void glTexEnvi(unsigned, unsigned, int)            { FLUXUS_ES_STUB("glTexEnvi (texture environment)"); }
static inline void glTexEnvfv(unsigned, unsigned, const float*)  {}
static inline void glTexGeni(unsigned, unsigned, int)            { FLUXUS_ES_STUB("glTexGeni (sphere-map texgen)"); }
static inline void glPrioritizeTextures(int, const unsigned*, const float*) {}
static inline unsigned char glAreTexturesResident(int, const unsigned*, unsigned char*) { return 1; }
static inline void glCompressedTexImage2DARB(unsigned, int, unsigned, int, int, int, int, const void*) { FLUXUS_ES_STUB("glCompressedTexImage2DARB"); }

// --- rasterizer state -------------------------------------------------------
static inline void glPolygonMode(unsigned, unsigned)  { FLUXUS_ES_STUB("glPolygonMode (wireframe/point fill modes)"); }
static inline void glLineStipple(int, unsigned short) { FLUXUS_ES_STUB("glLineStipple (dashed lines)"); }
static inline void glPointSize(float)                 {}
static inline void glFogf(unsigned, float)            { FLUXUS_ES_STUB("glFog* (fixed-function fog)"); }
static inline void glFogfv(unsigned, const float*)    {}
static inline void glFogi(unsigned, int)              {}

// --- picking, accumulation, buffers, text -----------------------------------
static inline void glSelectBuffer(int, unsigned*)     { FLUXUS_ES_STUB("glSelectBuffer (picking)"); }
static inline void glInitNames()                      {}
static inline void glPushName(unsigned)               {}
static inline void glPopName()                        {}
static inline int  glRenderMode(unsigned)             { return 0; }
static inline void glAccum(unsigned, float)           { FLUXUS_ES_STUB("glAccum (accumulation buffer)"); }
static inline void glDrawBuffer(unsigned)             { FLUXUS_ES_STUB("glDrawBuffer"); }
static inline void glRasterPos3f(float, float, float) { FLUXUS_ES_STUB("glRasterPos3f (bitmap text)"); }

// --- GLU --------------------------------------------------------------------
// NURBS is unsupported on Android (GLU does not exist there), but the class has
// to compile and link: SceneGraph -> ShadowVolumeGen dynamic_casts to it, so
// excluding the TU would leave a missing vtable rather than a clean absence.
#define GLU_TRUE                1
#define GLU_FILL                100012
#define GLU_OUTLINE_POLYGON     100240
#define GLU_DISPLAY_MODE        100206
#define GLU_CULLING             100201
#define GLU_SAMPLING_METHOD     100205
#define GLU_DOMAIN_DISTANCE     100217
#define GLU_U_STEP              100206
#define GL_MAP2_VERTEX_3        0x0DB7
#define GL_MAP2_NORMAL          0x0DB2
#define GL_MAP2_COLOR_4         0x0DB0
#define GL_MAP2_TEXTURE_COORD_2 0x0DB4
static inline GLUnurbsObj* gluNewNurbsRenderer() { FLUXUS_ES_STUB("gluNewNurbsRenderer (NURBS unsupported)"); return 0; }
static inline void gluDeleteNurbsRenderer(GLUnurbsObj*) {}
static inline void gluBeginSurface(GLUnurbsObj*) {}
static inline void gluEndSurface(GLUnurbsObj*) {}
static inline void gluNurbsProperty(GLUnurbsObj*, unsigned, float) {}
static inline void gluNurbsSurface(GLUnurbsObj*, int, float*, int, float*, int, int, float*, int, int, unsigned) {}
static inline int  gluBuild2DMipmaps(unsigned, int, int, int, unsigned, unsigned, const void*) { FLUXUS_ES_STUB("gluBuild2DMipmaps"); return 0; }
static inline const char* gluErrorString(unsigned)    { return "gl-error"; }
static inline void gluPickMatrix(double, double, double, double, const int*)  { FLUXUS_ES_STUB("gluPickMatrix (picking)"); }

// --- framebuffer objects: EXT/ARB spellings -> core --------------------------
// These are REAL and fully supported on GLES 3, just under their core names.
// app/FluxusCommandsGpu.cpp (the GPU particle system) uses the EXT/ARB spellings
// that desktop GL 2.1 needed, so map rather than stub — nothing is lost here.
// Renaming them at the source would be the tidier fix, but no golden case covers
// GPU particles, so doing it in the compat layer keeps macOS provably untouched.
#define GL_FRAMEBUFFER_EXT             GL_FRAMEBUFFER
#define GL_COLOR_ATTACHMENT0_EXT       GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT1_EXT       GL_COLOR_ATTACHMENT1
#define GL_FRAMEBUFFER_COMPLETE_EXT    GL_FRAMEBUFFER_COMPLETE
#define GL_RENDERBUFFER_EXT            GL_RENDERBUFFER
#define GL_DEPTH_ATTACHMENT_EXT        GL_DEPTH_ATTACHMENT
#define GL_RGBA32F_ARB                 GL_RGBA32F
#define glGenFramebuffersEXT           glGenFramebuffers
#define glBindFramebufferEXT           glBindFramebuffer
#define glDeleteFramebuffersEXT        glDeleteFramebuffers
#define glFramebufferTexture2DEXT      glFramebufferTexture2D
#define glCheckFramebufferStatusEXT    glCheckFramebufferStatus
#define glGenRenderbuffersEXT          glGenRenderbuffers
#define glBindRenderbufferEXT          glBindRenderbuffer
#define glRenderbufferStorageEXT       glRenderbufferStorage
#define glFramebufferRenderbufferEXT   glFramebufferRenderbuffer

// --- extension spellings ----------------------------------------------------
// GLES 3.2 HAS geometry shaders; it configures the stage with in-shader layout
// qualifiers instead of this call, so the program still links and runs.
static inline void glProgramParameteriEXT(unsigned, unsigned, int) {}

#endif
