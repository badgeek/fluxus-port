#pragma once
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include "PostFX.h"

struct SharedScript;
namespace Fluxus { class Renderer; }
class IScriptHost;

// Engine boundary (JUCE-free TU). Owns the fluxus Renderer + the s7 script host,
// and runs fluxus's per-frame model: clear the scene, eval the current script
// buffer (which rebuilds the scene via build-* commands), then render.
class FluxusScene {
public:
  FluxusScene(SharedScript* shared, std::unique_ptr<IScriptHost> host);
  ~FluxusScene();

  void init();                        // needs a current GL context
  void setResolution(int w, int h);
  void renderFrame();

private:
  std::unique_ptr<Fluxus::Renderer> renderer;
  std::unique_ptr<IScriptHost>      host;
  SharedScript* shared = nullptr;
  std::string   currentScript;
  int           frameCount = 0;
  long long     startMs = 0;
  std::thread::id glThread;   // the thread Racket/s7 was init'd on
  int           resW = 0, resH = 0;
  bool          committedOnce = false;   // retained mode: has the buffer been eval'd yet
  PostFX        postfx;       // optional full-screen post-processing pass
  float         prevVP[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};   // last frame view-proj
  long long     lastRenderMs = 0;

  // offline export: frame-locked time (t = expFrame/expFps, NOT wall clock) piped
  // as raw RGBA to a background ffmpeg process. Output is smooth at expFps no
  // matter how slow the per-frame grab is — it's a render, not a realtime capture.
  std::FILE*    expPipe = nullptr;
  bool          expOn = false;
  long          expFrame = 0;
  int           expFps = 60, expW = 0, expH = 0;
  std::string   expPathStr;
  void          exportBegin(const char* path, int fps);   // GL thread: open ffmpeg pipe
  void          exportWriteFrame();                        // GL thread: grab + pipe one frame
  void          exportEnd();                               // GL thread: close pipe, finalise
};
