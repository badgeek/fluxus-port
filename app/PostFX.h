#pragma once
#include <string>

namespace Fluxus { class GLSLShader; }

// Full-screen post-processing: render the scene into an FBO colour texture, then
// draw a fullscreen quad through a user fragment shader (passthrough vertex).
// Uniforms provided: sampler2D tex, float time, float audio, vec2 resolution.
// GL only — created/used on the GL thread by FluxusScene.
class PostFX {
public:
  ~PostFX();
  // (re)create the FBO at w*h if needed; returns false if unusable
  bool ensure(int w, int h);
  void setFragment(const std::string& frag);   // recompile if changed
  void begin();                                 // bind FBO (scene renders into it)
  void end();                                   // unbind
  void draw(double timeSeconds, double audio, double feedback);  // fullscreen post pass
  void release();                               // free GL objects

private:
  unsigned int fbo = 0, tex = 0, depth = 0, prev = 0;  // prev = last output (feedback)
  int w = 0, h = 0;
  Fluxus::GLSLShader* shader = nullptr;
  std::string curFrag;
};
