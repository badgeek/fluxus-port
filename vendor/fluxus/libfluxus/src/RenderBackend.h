// fluxus->JUCE minimal port: rendering-backend seam.
// The engine draws THROUGH this interface instead of calling raw GL directly,
// so an alternative backend (raylib/rlgl) can be dropped in without touching the
// primitives. The default backend (GLBackend) is a 1:1 passthrough to the same
// fixed-function GL the engine used before — so inserting the seam changes
// nothing visually.
#ifndef FLUXUS_RENDER_BACKEND_H
#define FLUXUS_RENDER_BACKEND_H

namespace Fluxus {

// fluxus->JUCE port: LineStrip added when RibbonPrimitive's wire pass moved
// behind this seam — it draws one connected polyline, which Lines cannot express
// without doubling every interior vertex.
enum class RPrim { Triangles, Quads, TriStrip, TriFan, Polygon, Lines, LineStrip, Points };

// fluxus->JUCE port: how the NEXT draws rasterize their faces. Fixed-function GL
// expressed this as glPolygonMode, which GLES does not have at all — there, a
// backend has to turn the topology into real line or point geometry instead. So
// the seam carries the INTENT ("draw this as wireframe") rather than the
// mechanism, which is the only form both backends can answer.
enum class RFill { Fill, Line, Point };

// fluxus->JUCE port: fixed-function light parameters. Mapped one-to-one from the
// glLight* calls Light.cpp used to make, deliberately: a struct-at-render-time
// design would have moved WHEN the state is applied, and the golden cases do not
// exercise lights, so there would be nothing to catch a regression. A shader
// backend collects these into uniforms; ours currently uses index 0's diffuse
// and ignores the rest (see ANDROID-SUBSET.md — spot and multiple lights are
// listed as unsupported).
enum class RLightColour { Ambient, Diffuse, Specular };
enum class RLightFloat  { SpotCutoff, SpotExponent,
                          ConstantAttenuation, LinearAttenuation, QuadraticAttenuation };

// Raw vertex-array views (contiguous PData arrays). col == nullptr => no per-
// vertex colour. Stride in bytes (fluxus stores dVector = 4 floats).
struct RVertexArrays {
  const float* pos = nullptr;   // xyz
  const float* nrm = nullptr;   // xyz
  const float* tex = nullptr;   // 3 floats
  const float* col = nullptr;   // rgba (nullptr => disabled)
  int          stride = 0;
#ifdef FLUXUS_ENABLE_VBO
  // fluxus->JUCE port: optional per-attribute VBO ids (0 => use client pointer).
  // When set, the backend draws from GPU buffers, skipping the per-call upload of
  // client arrays (a big cost on Apple's Metal-emulated GL for static geometry).
  unsigned int posVBO = 0, nrmVBO = 0, texVBO = 0, colVBO = 0;
#endif
};

struct IRenderBackend {
  virtual ~IRenderBackend() {}

  // matrix stack
  virtual void pushMatrix() = 0;
  virtual void popMatrix()  = 0;
  virtual void multMatrix(const float* m16) = 0;
  virtual void loadMatrix(const float* m16) = 0;
  // fluxus->JUCE port: read the current modelview back. Fixed-function callers
  // asked GL directly (glGetFloatv(GL_MODELVIEW_MATRIX)); a shader backend keeps
  // its own stack and GLES has no such query, so the question comes through here.
  // Used by ParticlePrimitive's depth sort.
  virtual void getModelView(float* m16) = 0;
  // Same reasoning for the projection, which SceneGraph needs to build the
  // frustum planes it culls against.
  virtual void getProjection(float* m16) = 0;
  // fluxus->JUCE port: set the projection explicitly. Fixed-function GL had a
  // second matrix stack selected by glMatrixMode, so Camera::DoProjection could
  // just call glFrustum and rely on the mode being right. GLES has neither the
  // mode nor glFrustum, so the seam has to say WHICH matrix is being set.
  virtual void setProjectionMatrix(const float* m16) = 0;
  // ...and multiply into it. The distinction matters: glFrustum and glOrtho
  // MULTIPLIED, and Renderer::PreRender relies on that — in pick mode it loads
  // identity, multiplies in gluPickMatrix, and only then calls the camera. A
  // load there would silently discard the pick matrix.
  virtual void multProjectionMatrix(const float* m16) = 0;

  // Object identity for picking. Fixed-function GL had a name stack
  // (glPushName/glPopName) feeding a selection buffer; GLES has neither, so a
  // backend there implements these as no-ops (picking is listed as unsupported
  // in ANDROID-SUBSET.md) or, later, as the id a colour-pick pass writes out.
  virtual void pushPickName(unsigned int id) = 0;
  virtual void popPickName() = 0;

  // per-object state
  virtual void setColour(float r, float g, float b, float a) = 0;
  virtual void setMaterial(const float* ambient, const float* emissive,
                           const float* diffuse, const float* specular,
                           float shininess) = 0;
  virtual void setLineWidth(float w) = 0;
  virtual void setPointSize(float s) = 0;
  virtual void setBlend(int srcGL, int dstGL) = 0;   // GLenum values
  virtual void setCull(bool on) = 0;
  virtual void setFrontFaceCW(bool cw) = 0;
  // fluxus->JUCE port: the last two pieces of per-primitive state that were
  // still raw GL enums in State::Apply. Both are fixed-function concepts a
  // shader backend answers differently — it normalises in the vertex shader, and
  // always writes gl_PointSize — so a GLES backend implements them as no-ops.
  virtual void setNormaliseNormals(bool on) = 0;
  virtual void setProgramPointSize(bool on) = 0;
  // fluxus->JUCE port: lighting on/off, which HINT_UNLIT and every wire pass
  // toggle. It was raw glEnable/glDisable(GL_LIGHTING) — the single most common
  // fixed-function enum left in the engine — and on a shader backend it is a
  // uniform, not a capability. Without it on the seam, a GLES backend silently
  // lights everything and HINT_UNLIT does nothing.
  virtual void setLighting(bool on) = 0;
  virtual void setFillMode(RFill mode) = 0;

  // lights
  virtual void setLightEnabled(int index, bool on) = 0;
  virtual void setLightColour(int index, RLightColour which, const float* rgba) = 0;
  virtual void setLightFloat(int index, RLightFloat which, float v) = 0;
  virtual void setLightPosition(int index, const float* xyzw) = 0;
  virtual void setLightSpotDirection(int index, const float* xyzw) = 0;

  // geometry (index/indexCount optional: index==nullptr => glDrawArrays-style)
  virtual void drawArrays(RPrim prim, const RVertexArrays& v, int count,
                          const unsigned int* index, int indexCount) = 0;
};

// Process-global current backend. Defaults to a GLBackend instance so the engine
// works with no host wiring. The JUCE host may SetBackend(...) to swap it.
IRenderBackend* Backend();
void            SetBackend(IRenderBackend* b);

} // namespace Fluxus

#endif
