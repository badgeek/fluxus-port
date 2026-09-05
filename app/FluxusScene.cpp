// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include "FluxusScene.h"
#include "SharedScript.h"
#include "IScriptHost.h"
#include "FluxusCommands.h"   // feed pixel resolution for (get-screen-size)

// engine + system GL only
#include "Renderer.h"
#include "dada.h"
#include <OpenGL/gl.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace Fluxus;

static long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

FluxusScene::FluxusScene(SharedScript* s, std::unique_ptr<IScriptHost> h)
  : host(std::move(h)), shared(s) {}
FluxusScene::~FluxusScene() { if (expPipe) pclose(expPipe); }

// --- offline export (frame-locked render -> ffmpeg via a pipe) ---------------
void FluxusScene::exportBegin(const char* path, int fps) {
  if (expPipe || resW <= 0 || resH <= 0) return;
  expW = resW; expH = resH; expFps = fps > 0 ? fps : 60; expFrame = 0;
  // pad the (possibly retina) frame up to a clean 9:16 (invisible on the black bg)
  int targetH = (int) std::lround(expW * 16.0 / 9.0);
  if (targetH % 2) ++targetH;
  char vf[256];
  if (targetH > expH) std::snprintf(vf, sizeof vf, "vflip,pad=%d:%d:0:%d:color=black",
                                    expW, targetH, (targetH - expH) / 2);
  else                std::snprintf(vf, sizeof vf, "vflip");
  // optional soundtrack: mux it as a second input (trim to the video with
  // -shortest). The visuals already react to it via flux_export_audio_apply.
  char aud[1200] = {0};
  { char apath[1024];
    if (flux_export_audio_path(apath, (int) sizeof apath))
      std::snprintf(aud, sizeof aud, "-i \"%s\" -map 0:v -map 1:a -c:a aac -b:a 256k -shortest ", apath);
    else
      std::snprintf(aud, sizeof aud, "-map 0:v ");
  }
  // raw RGBA in, hardware H.264 out. glReadPixels is bottom-up, hence vflip.
  char cmd[3072];
  std::snprintf(cmd, sizeof cmd,
    "ffmpeg -y -f rawvideo -pixel_format rgba -video_size %dx%d -framerate %d -i - "
    "%s-vf \"%s\" -c:v h264_videotoolbox -b:v 25M -pix_fmt yuv420p -r %d \"%s\" "
    "2>/tmp/fluxus-export.log",
    expW, expH, expFps, aud, vf, expFps, path);
  expPipe = popen(cmd, "w");
  std::fprintf(stderr, "[export] %s @ %dfps -> %s\n",
               expPipe ? "started" : "FAILED (popen)", expFps, path);
}
void FluxusScene::exportWriteFrame() {
  if (!expPipe || expW <= 0 || expH <= 0) return;
  static std::vector<unsigned char> px;
  px.resize((size_t) expW * expH * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, expW, expH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  std::fwrite(px.data(), 1, px.size(), expPipe);
  ++expFrame;
}
void FluxusScene::exportEnd() {
  if (expPipe) { pclose(expPipe); expPipe = nullptr; }
  std::fprintf(stderr, "[export] finished (%ld frames)\n", expFrame);
}

void FluxusScene::init() {
  renderer = std::make_unique<Renderer>();
  host->init();          // host was injected (s7 or racket)
  glThread = std::this_thread::get_id();   // script engine is bound to this thread
  startMs = nowMs();
}

void FluxusScene::setResolution(int w, int h) {
  if (renderer) renderer->SetResolution(w, h);
  flux_set_resolution(w, h);
  resW = w; resH = h;
}

// --- per-frame phases (called in order by renderFrame) -----------------------

void FluxusScene::updateExportToggle() {
  // offline export toggle (message thread sets desired state; we own the pipe).
  char epath[1024]; int efps = 60;
  const bool want = flux_export_state(epath, (int) sizeof epath, &efps);
  if (want && !expOn) { expOn = true; expPathStr = epath; expFps = efps; }   // pipe opens lazily below
  else if (!want && expOn) { expOn = false; exportEnd(); }
}

void FluxusScene::clearFullViewport() {
  // Paint the WHOLE window opaque-black first. With an aspect lock the camera
  // renders into a letterbox sub-rect; the renderer's clear/scissor only covers
  // that rect, so without this the bars stay uncleared and the transparent JUCE
  // window shows the desktop through them. Full viewport + no scissor here.
  if (resW > 0 && resH > 0) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, resW, resH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
}

void FluxusScene::pullScript(bool& isDirty) {
  // pull the latest editor buffer + dirty flag (message thread writes them)
  isDirty = false;
  if (shared) {
    std::lock_guard<std::mutex> lk(shared->m);
    currentScript = shared->pending;
    isDirty = shared->dirty;
    shared->dirty = false;
  }
}

void FluxusScene::runScript(double t, bool isDirty, std::string& err) {
  // Two models:
  //  - immediate (default): wipe + re-eval the whole buffer every frame.
  //  - retained ((retained) opt-in): eval the buffer ONCE (build persistent
  //    geometry + register the every-frame thunk), then per frame run only that
  //    thunk — no Clear, no rebuild. Fast for heavy static meshes.
  const bool commit = isDirty || !committedOnce;
  if (commit) {
    flux_free_terminals();                // free any terminal parsers before the wipe
    renderer->Clear();
    renderer->SetBGColour(dColour(0.08f, 0.09f, 0.12f, 1.0f));
    flux_set_retained(0);                 // script re-declares (retained) if it wants it
    host->setFrameInfo(t, frameCount);
    if (!currentScript.empty()) host->eval(currentScript, err);
    committedOnce = true;
  } else if (flux_retained_on()) {
    host->setFrameInfo(t, frameCount);    // update time + camera, keep the scene
    host->runFrame(err);
  } else {
    flux_free_terminals();                // free any terminal parsers before the wipe
    renderer->Clear();
    renderer->SetBGColour(dColour(0.08f, 0.09f, 0.12f, 1.0f));
    host->setFrameInfo(t, frameCount);
    if (!currentScript.empty()) host->eval(currentScript, err);
  }
}

void FluxusScene::applyRenderState() {
  // the script just moved any follow-cam target; anchor the camera-node to the FINAL
  // view before rendering so HUD prims parented to it pin exactly (see FluxusCommands).
  flux_camera_finalize();

  glEnable(GL_BLEND);   // per-prim blend factors (blend-mode) drive the result

  // optional line/polygon smoothing (anti-alias) for the wireframe look
  if (flux_antialias_on()) {
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  } else {
    glDisable(GL_LINE_SMOOTH);
  }
}

void FluxusScene::renderWithPostFX(double t) {
  // the script (just eval'd) may have installed a post-processing shader.
  std::string frag; bool dirty = false; double feedback = 0.0;
  const bool post = flux_post_state(frag, feedback, dirty) && resW > 0 && resH > 0
                    && postfx.ensure(resW, resH);
  if (post) {
    if (dirty) postfx.setFragment(frag);
    postfx.begin();                 // render scene into the FBO texture
    renderer->Render();
    // capture view + projection for the reprojection blur. The GL modelview at
    // this point is camera*lastPrim (prims multiply onto it), so take the pure
    // view from the engine camera; the projection isn't touched by prims.
    float mv[16], pr[16];
    { double v[16]; flux_get_camera_transform(v); for (int i=0;i<16;++i) mv[i]=(float)v[i]; }
    glGetFloatv(GL_PROJECTION_MATRIX, pr);
    postfx.end();

    // view-projection this frame + its inverse via engine dMatrix. A GL
    // column-major float[16] memcpy'd into dMatrix storage holds the transpose,
    // and dMatrix's a*b is the storage product b.a — so dPr*dMv IS pr*mv in
    // column-major, and memcpy'ing the inverse back out is the correct
    // column-major inverse (inverse of transpose = transpose of inverse).
    // Requires the det!=1 inverse() fix in dada.h (see comment there).
    float vp[16], vpInv[16];
    dMatrix dPr, dMv;
    std::memcpy(&dPr.m[0][0], pr, sizeof pr);
    std::memcpy(&dMv.m[0][0], mv, sizeof mv);
    dMatrix dVP = dPr * dMv;
    std::memcpy(vp, &dVP.m[0][0], sizeof vp);
    if (std::fabs(dVP.determinant()) < 1e-20f) {
      for (int i=0;i<16;++i) vpInv[i] = (i%5==0)?1.0f:0.0f;   // singular: identity fallback
    } else {
      dMatrix dInv = dVP.inverse();
      std::memcpy(vpInv, &dInv.m[0][0], sizeof vpInv);
    }
    const long long now = nowMs();
    float dt = expOn ? (1.0f / (float) expFps)
                     : ((lastRenderMs > 0) ? (float) ((now - lastRenderMs) / 1000.0) : 0.016f);
    lastRenderMs = now;
    postfx.draw(t, flux_audio_gain(), feedback, vpInv, prevVP, dt);   // outer t = frame-locked in export
    for (int i = 0; i < 16; ++i) prevVP[i] = vp[i];   // remember for next frame
  } else {
    renderer->Render();
  }
}

void FluxusScene::captureOutputs() {
  // final stage: run the whole finished frame (scene + post pass) through the
  // software NTSC/CRT filter, in place on the default framebuffer. Kept BEFORE
  // the grab below so screenshots/recordings/exports capture the filtered image.
  NtscParams np;
  if (flux_ntsc_state(np) && resW > 0 && resH > 0) ntsc.apply(resW, resH, np);

  // grab the finished default framebuffer (post pass included, i.e. exactly what's
  // on screen) to a PNG. Used by both the one-shot (screenshot …) and recording.
  if (resW > 0 && resH > 0) {
    auto grab = [&](const char* path) {
      std::vector<unsigned char> px((size_t) resW * resH * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, resW, resH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      flux_write_png(path, px.data(), resW, resH);
    };
    char shotPath[1024];
    if (flux_take_screenshot(shotPath, (int) sizeof(shotPath))) grab(shotPath);
    char recPath[1024];
    if (flux_recording_next(recPath, (int) sizeof(recPath))) grab(recPath);   // Record Frames

    // offline export: open the pipe lazily (once the resolution is known), then
    // stream this frame's pixels to ffmpeg.
    if (expOn) {
      if (!expPipe) exportBegin(expPathStr.c_str(), expFps);
      if (expPipe)  exportWriteFrame();
    }
  }
}

void FluxusScene::renderFrame() {
  if (!renderer || !host) return;

  // JUCE renders synchronously on the MESSAGE thread during move/resize/fullscreen.
  // The script engine (Racket CS / s7) is bound to the GL thread and is NOT
  // thread-safe — calling it from another thread crashes. Skip those frames.
  if (std::this_thread::get_id() != glThread) return;

  ++frameCount;

  updateExportToggle();

  // Time source: wall clock normally, but frame-locked while exporting so motion
  // advances exactly 1/fps per RENDERED frame — the output is smooth at expFps no
  // matter how slow each grab is (a deterministic render, not a realtime capture).
  const double t = expOn ? (double) expFrame / (double) expFps
                         : (nowMs() - startMs) / 1000.0;
  // audio-reactive export: feed THIS frame's pre-analysed features so (gain)/(gh)
  // react to the soundtrack deterministically, synced to the frame-locked time.
  if (expOn) flux_export_audio_apply(expFrame);
  host->setRenderer(renderer.get());

  clearFullViewport();

  bool isDirty = false;
  pullScript(isDirty);

  std::string err;
  runScript(t, isDirty, err);
  if (shared) {
    std::lock_guard<std::mutex> lk(shared->m);
    shared->lastError = err;   // "" = ok
  }

  applyRenderState();
  renderWithPostFX(t);
  captureOutputs();
}
