// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A real IRenderBackend implementation on OpenGL ES — the thing an Android port
// would need. Everything the fixed-function pipeline used to do for free is
// explicit here: the matrix stack, lighting, quad splitting, and wireframe.
//
// This is spike code: enough to render and judge a picture, not a finished
// backend. It has no textures, one hard-coded light, and no state sorting.

#include "RenderBackend.h"

#include <vector>

class GLESBackend : public Fluxus::IRenderBackend {
 public:
  bool init();          // compile the program; false on shader failure
  void shutdown();

  // GLES has no projection matrix state, so the app supplies one and the
  // backend folds it into the MVP uniform on every draw.
  void setProjection(const float* m16);
  void setUnlit(bool on) { unlit = on; }

  // IRenderBackend
  void pushMatrix() override;
  void popMatrix() override;
  void multMatrix(const float* m16) override;
  void loadMatrix(const float* m16) override;
  void getModelView(float* m16) override;   // our own stack — GLES has no query
  void getProjection(float* m16) override;
  // Picking is unsupported on GLES (no selection buffer) — see ANDROID-SUBSET.md.
  void pushPickName(unsigned int) override {}
  void popPickName() override {}
  void setColour(float r, float g, float b, float a) override;
  void setMaterial(const float* ambient, const float* emissive,
                   const float* diffuse, const float* specular,
                   float shininess) override;
  void setLineWidth(float w) override;
  void setPointSize(float s) override;
  void setBlend(int srcGL, int dstGL) override;
  void setCull(bool on) override;
  void setFrontFaceCW(bool cw) override;
  // Both are fixed-function concepts: this backend normalises in the vertex
  // shader and always writes gl_PointSize, so there is nothing to toggle.
  void setNormaliseNormals(bool) override {}
  void setProgramPointSize(bool) override {}
  // On a shader backend this is a uniform, which is exactly why it had to reach
  // the seam: as raw glDisable(GL_LIGHTING) it was a no-op here and HINT_UNLIT
  // silently did nothing.
  void setLighting(bool on) override { unlit = !on; }
  void drawArrays(Fluxus::RPrim prim, const Fluxus::RVertexArrays& v, int count,
                  const unsigned int* index, int indexCount) override;

  // Used by the legacy-capture path for the engine's raw wire/points passes.
  void drawRaw(unsigned legacyMode, const void* pos, int posStride,
               const void* nrm, const void* col, int count,
               const unsigned int* index, int indexCount, bool asLines);

 private:
  void applyUniforms();
  // plain unsigned, not GLuint: this header is included by main.cpp, which sees
  // the shim's view of GL rather than the real one.
  unsigned program = 0;
  unsigned vbo = 0, ibo = 0;
  int    aPos = -1, aNrm = -1, aCol = -1;
  int    uMVP = -1, uMV = -1, uColour = -1, uUseVertCol = -1, uUnlit = -1;

  std::vector<float> stack;          // 16 floats per level, top() is current
  float proj[16];
  float colour[4] = {1, 1, 1, 1};
  bool  unlit = false;
  std::vector<unsigned int> scratch; // index expansion buffer
};
