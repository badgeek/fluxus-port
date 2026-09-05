// SPDX-License-Identifier: AGPL-3.0-or-later
// Whole-frame output state of the fluxus command layer: the post-FX fragment
// shader, the final-stage NTSC filter knobs, anti-aliasing and the retained-mode
// flag. Scripts push here on the GL thread; FluxusScene (PostFX / NTSCEffect)
// reads it each frame — so every block is mutex- or atomic-guarded and this TU
// touches no engine state at all.
#include "FluxusCommands.h"

#include <atomic>
#include <mutex>
#include <string>

// ---- post-FX state (read by FluxusScene's PostFX) --------------------------
namespace {
std::mutex  g_postMutex;
bool        g_postEnabled = false;
std::string g_postFrag;
double      g_postFeedback = 0.0;
bool        g_postDirty   = false;

// built-in feedback motion-blur fragment: blend the current frame over a decayed
// copy of the previous output (max keeps bright trails that fade each frame).
const char* kBlurFrag =
  "uniform sampler2D tex;\n"
  "uniform sampler2D prev;\n"
  "uniform float feedback;\n"
  "varying vec2 uv;\n"
  "void main() {\n"
  "  vec3 c = texture2D(tex, uv).rgb;\n"
  "  vec3 p = texture2D(prev, uv).rgb * feedback;\n"
  "  gl_FragColor = vec4(max(c, p), 1.0);\n"
  "}\n";
}
extern "C" void flux_post_shader(const char* frag) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  std::string f = frag ? frag : "";
  if (f != g_postFrag) { g_postFrag = f; g_postDirty = true; }
  g_postEnabled = true;
}
extern "C" void flux_post_off(void) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  g_postEnabled = false;
}

std::atomic<bool> g_antialias{false};
extern "C" void flux_set_antialias(int on) { g_antialias = (on != 0); }
bool flux_antialias_on() { return g_antialias.load(); }

std::atomic<bool> g_retained{false};
extern "C" void flux_set_retained(int on) { g_retained = (on != 0); }
bool flux_retained_on() { return g_retained.load(); }
extern "C" void flux_blur(double amount) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  if (g_postFrag != kBlurFrag) { g_postFrag = kBlurFrag; g_postDirty = true; }
  g_postFeedback = amount < 0 ? 0 : (amount > 0.97 ? 0.97 : amount);
  g_postEnabled = true;
}
bool flux_post_state(std::string& frag, double& feedback, bool& dirty) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  frag = g_postFrag;
  feedback = g_postFeedback;
  dirty = g_postDirty;
  g_postDirty = false;
  return g_postEnabled;
}

// ---- final-stage NTSC/CRT filter state (read by FluxusScene's NTSCEffect) --
// Scripts push monitor knobs here from the GL thread; FluxusScene reads them
// each frame. A plain mutex mirrors the post-FX block above.
namespace {
std::mutex  g_ntscMutex;
bool        g_ntscEnabled = false;
NtscParams  g_ntsc;   // defaults in the struct match crt_reset()
}
extern "C" void flux_ntsc(int on) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntscEnabled = (on != 0);
}
// The setters that feed the ntsc-rs settings bump presetRev ONLY on an actual
// value change: an immediate-mode sketch re-runs its top-level (ntsc-noise …)/
// (ntsc-preset …) calls EVERY frame, and an unconditional bump made NTSCEffect
// re-parse the JSON and rebuild the whole effect per frame (real CPU, and it
// resets the filter state ntsc-rs caches internally).
extern "C" void flux_ntsc_noise(int n) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  n = n < 0 ? 0 : n;
  if (g_ntsc.noise == n) return;
  g_ntsc.noise = n;
  g_ntsc.presetRev++;              // noise feeds the ntsc-rs settings -> reload
}
extern "C" void flux_ntsc_hue(int deg) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  deg = ((deg % 360) + 360) % 360;
  if (g_ntsc.hue == deg) return;
  g_ntsc.hue = deg;
  g_ntsc.presetRev++;              // hue feeds the ntsc-rs settings -> reload
}
extern "C" void flux_ntsc_saturation(int s) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.saturation = s < 0 ? 0 : s;
}
extern "C" void flux_ntsc_brightness(int b) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.brightness = b;
}
extern "C" void flux_ntsc_contrast(int c) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.contrast = c < 0 ? 0 : c;
}
extern "C" void flux_ntsc_scanlines(int on) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.scanlines = (on != 0);
}
extern "C" void flux_ntsc_monochrome(int on) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.monochrome = (on != 0);
}
extern "C" void flux_ntsc_blend(int on) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  g_ntsc.blend = (on != 0);
}
extern "C" void flux_ntsc_preset(const char* json) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  const char* s = json ? json : "";
  if (g_ntsc.preset == s) return;
  g_ntsc.preset = s;
  g_ntsc.presetRev++;
}
bool flux_ntsc_state(NtscParams& out) {
  std::lock_guard<std::mutex> lk(g_ntscMutex);
  out = g_ntsc;
  return g_ntscEnabled;
}
