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
  void getModelView(float* m16) override;
  void getProjection(float* m16) override;
  void setProjectionMatrix(const float* m16) override;
  void multProjectionMatrix(const float* m16) override;
  void pushPickName(unsigned int id) override;
  void popPickName() override;

  void setColour(float r, float g, float b, float a) override;
  void setMaterial(const float* ambient, const float* emissive,
                   const float* diffuse, const float* specular,
                   float shininess) override;
  void setLineWidth(float w) override;
  void setPointSize(float s) override;
  void setBlend(int srcGL, int dstGL) override;
  void setCull(bool on) override;
  void setNormaliseNormals(bool on) override;
  void setLighting(bool on) override;
  void setFillMode(Fluxus::RFill mode) override;
  void setLightEnabled(int index, bool on) override;
  void setLightColour(int index, Fluxus::RLightColour which, const float* rgba) override;
  void setLightFloat(int index, Fluxus::RLightFloat which, float v) override;
  void setLightPosition(int index, const float* xyzw) override;
  void setLightSpotDirection(int index, const float* xyzw) override;
  void setProgramPointSize(bool on) override;
  void setFrontFaceCW(bool cw) override;

  void drawArrays(RPrim prim, const RVertexArrays& v, int count,
                  const unsigned int* index, int indexCount) override;
};

} // namespace Fluxus

#endif
