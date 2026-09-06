// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A real IRenderBackend implementation on OpenGL ES — the thing an Android port
// would need. Everything the fixed-function pipeline used to do for free is
// explicit here: the matrix stack, lighting, quad splitting, and wireframe.
//
// This is spike code: enough to render and judge a picture, not a finished
// backend. It has one hard-coded light and no state sorting. It now has a
// texture: one diffuse sampler, modulated with the material/vertex colour —
// the wire and legacy-capture paths never had UV data and stay untextured.

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
  void setProjectionMatrix(const float* m16) override { setProjection(m16); }
  // No pick matrix and no projection stack here, so a multiply into an identity
  // projection is the same as setting it.
  void multProjectionMatrix(const float* m16) override { setProjection(m16); }
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
  // GLES has no glPolygonMode, so the mode is remembered and the topology is
  // turned into line or point geometry at draw time instead.
  void setFillMode(Fluxus::RFill mode) override { fill = mode; }
  // Only light 0's diffuse reaches the shader; spot and multiple lights are
  // listed as unsupported in ANDROID-SUBSET.md.
  void setLightEnabled(int, bool) override {}
  void setLightColour(int index, Fluxus::RLightColour which, const float* rgba) override {
    if (index == 0 && which == Fluxus::RLightColour::Diffuse)
      for (int i = 0; i < 3; ++i) lightColour[i] = rgba[i];
  }
  void setLightFloat(int, Fluxus::RLightFloat, float) override {}
  void setLightPosition(int, const float*) override {}
  void setLightSpotDirection(int, const float*) override {}
  void setTexture(unsigned int id) override { curTex = id; }
  void drawArrays(Fluxus::RPrim prim, const Fluxus::RVertexArrays& v, int count,
                  const unsigned int* index, int indexCount) override;

  // Used by the legacy-capture path for the engine's raw wire/points passes.
  // tex is nullptr there — LegacyGL never carries UV data, and the wire/points
  // passes ignore texture on desktop too (see CLAUDE.md's model-import notes).
  void drawRaw(unsigned legacyMode, const void* pos, int posStride,
               const void* nrm, const void* tex, const void* col, int count,
               const unsigned int* index, int indexCount, bool asLines);

 private:
  void applyUniforms();
  // plain unsigned, not GLuint: this header is included by main.cpp, which sees
  // the shim's view of GL rather than the real one.
  unsigned program = 0;
  unsigned vbo = 0, ibo = 0;
  int    aPos = -1, aNrm = -1, aCol = -1, aTex = -1;
  int    uMVP = -1, uMV = -1, uColour = -1, uUseVertCol = -1, uUnlit = -1;
  int    uSampler = -1, uUseTex = -1;
  unsigned curTex = 0;   // 0 == untextured, set via setTexture from State::Apply

  std::vector<float> stack;          // 16 floats per level, top() is current
  float proj[16];
  float colour[4] = {1, 1, 1, 1};
  bool  unlit = false;
  Fluxus::RFill fill = Fluxus::RFill::Fill;
  float lightColour[3] = {1, 1, 1};
  std::vector<unsigned int> scratch; // index expansion buffer
};
