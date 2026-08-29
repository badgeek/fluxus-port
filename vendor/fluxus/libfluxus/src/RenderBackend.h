// fluxus->JUCE minimal port: rendering-backend seam.
// The engine draws THROUGH this interface instead of calling raw GL directly,
// so an alternative backend (raylib/rlgl) can be dropped in without touching the
// primitives. The default backend (GLBackend) is a 1:1 passthrough to the same
// fixed-function GL the engine used before — so inserting the seam changes
// nothing visually.
#ifndef FLUXUS_RENDER_BACKEND_H
#define FLUXUS_RENDER_BACKEND_H

namespace Fluxus {

enum class RPrim { Triangles, Quads, TriStrip, TriFan, Polygon, Lines };

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
