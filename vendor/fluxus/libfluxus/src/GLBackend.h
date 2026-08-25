// fluxus->JUCE minimal port: default rendering backend — 1:1 fixed-function GL.
// This is exactly the GL the primitives used to call inline, moved behind the
// IRenderBackend seam. Passthrough => zero visual change vs pre-seam engine.
#ifndef FLUXUS_GL_BACKEND_H
#define FLUXUS_GL_BACKEND_H

#include "RenderBackend.h"

namespace Fluxus {

struct GLBackend : public IRenderBackend {
  void pushMatrix() override;
  void popMatrix()  override;
  void multMatrix(const float* m16) override;
  void loadMatrix(const float* m16) override;

  void setColour(float r, float g, float b, float a) override;
  void setMaterial(const float* ambient, const float* emissive,
                   const float* diffuse, const float* specular,
                   float shininess) override;
  void setLineWidth(float w) override;
  void setPointSize(float s) override;
  void setBlend(int srcGL, int dstGL) override;
  void setCull(bool on) override;
  void setFrontFaceCW(bool cw) override;

  void drawArrays(RPrim prim, const RVertexArrays& v, int count,
                  const unsigned int* index, int indexCount) override;
};

} // namespace Fluxus

#endif
