// SPDX-License-Identifier: AGPL-3.0-or-later
// Host-state domain of the fluxus command layer: every external input (audio,
// MIDI, OSC, hands, mouse, keys) plus the camera, the persistent script store and
// the window/editor/recording/export/screenshot/tweak state the app polls.
//
// CRITICAL (CLAUDE.md gotcha 1): the setters here run on OTHER threads — audio,
// MIDI, the message thread — while scripts read on the GL thread. That is why
// each block carries its own mutex (or an atomic) and why nothing in this file
// may touch a script engine.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine + system GL only
#include "Renderer.h"
#include "Camera.h"
#include "LocatorPrimitive.h"
#include "SceneGraph.h"
#include "State.h"
#include "dada.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Fluxus;

namespace {
std::mutex  g_errMutex;
std::string g_err;

std::mutex         g_audioMutex;
std::vector<float> g_bands;
double             g_gain = 0.0;

// MIDI input state (MidiHost writes on a JUCE thread; scripts read on GL thread)
std::mutex g_midiMutex;
int  g_midiCC[16][128] = {};     // last CC value per channel/controller (0 default)
int  g_midiNotePitch = -1;       // last note-on pitch (-1 = none yet)
int  g_midiNoteVel   = 0;

// OSC state: latest args per received address + the send transport bridge
std::mutex g_oscMutex;
std::map<std::string, std::vector<double>> g_oscMsgs;
std::string g_oscLastAddr;
FluxOscBridge g_oscBridge;

// Hand-tracking state: nHands x landmarksPerHand x 3 (x,y,z), pushed by HandHost
std::mutex         g_handMutex;
int                g_handCount = 0;
int                g_handPer   = 21;
std::vector<float> g_handXYZ;
std::mutex         g_handBridgeMutex;
FluxHandBridge     g_handBridge;

// persistent script state (GL thread only, but guard anyway)
std::mutex                                  g_stateMutex;
std::map<std::string, std::vector<double>>  g_state;

// mouse + orbit camera state (persists across frames)
struct CamState { double yaw = 0.3, pitch = 0.3, dist = 10.0; };
CamState g_cam;
double   g_mouseX = 0, g_mouseY = 0;
int      g_mouseButton = 0;

// script-driven camera: an optional transform override (scripts set it via
// set-camera-transform) that suppresses the mouse orbit, plus the last matrix
// actually applied (returned by get-camera-transform) and the pixel resolution
// the host feeds each frame (get-screen-size).
bool    g_camOverride = false;
dMatrix g_camOverrideMat;
dMatrix g_camAppliedMat;
int     g_screenW = 720, g_screenH = 576;
// camera-as-node (ofCamera : ofNode parity). g_camAttach: scene-graph node the
// camera rides (0 = none), applied via Camera::LockCamera each frame since Clear()
// recreates camera[0]. g_camNode*: an invisible locator whose world transform tracks
// the inverse view so its children render in eye space (HUD). g_camViewInv is the
// inverse of the full modelview base (view, incl. any follow-cam attach) recomputed
// each frame in applyCamera(); g_camNodeId is invalidated on the immediate-mode
// per-frame Clear() and refreshed in place in retained mode.
int     g_camAttach = 0;
double  g_camLag = 0.0;
int     g_camNodeId = -1;
dMatrix g_camViewInv;
// script-requested window size (scheme (set-window-size w h)); the message-thread
// component polls flux_take_window_request and resizes its window content. Defs
// live in the extern "C" block below so they export C symbols.
std::mutex g_winMutex;
int  g_winReqW = 0, g_winReqH = 0;
bool g_winReqPending = false;
// script-requested code-editor visibility ((show-editor)/(hide-editor)/(editor-
// full-width b)). The message-thread component polls flux_get_editor and toggles
// the overlay editor. g_edSet stays false until a script speaks, so sketches that
// never call these keep the default (editor shown, left half).
std::mutex g_edMutex;
bool g_edSet = false;
int  g_edVisible = 1, g_edFull = 0;
// script-requested tweak-panel visibility ((show-tweaks)/(hide-tweaks)). Same
// deal: polled on the message thread, untouched until a script asks.
std::mutex g_tweakVisMutex;
bool g_tweakVisSet = false;
int  g_tweakVis = 1;
// frame-recording state: while on, the scene grabs each rendered frame to
// g_recDir/fNNNNN.png (g_recFrame auto-increments on the GL thread).
std::mutex g_recMutex;
bool g_recOn = false;
std::string g_recDir;
long g_recFrame = 0;
// offline export desired-state (the scene owns the ffmpeg pipe on the GL thread)
std::mutex g_expMutex;
bool g_expOn = false;
std::string g_expPath;
int g_expFps = 60;
// audio-reactive export: mux path + pre-analysed per-frame feature table
std::mutex g_expAudMutex;
std::string g_expAudPath;
std::vector<float> g_expAudGains;
std::vector<float> g_expAudBands;
int  g_expAudNBands = 0;
long g_expAudFrames = 0;
// one-shot screenshot state
std::mutex g_shotMutex;
std::string g_shotPending;
std::set<std::string> g_shotDone;
// aspect-ratio lock (0 = auto/off). When >0 the frustum is built for this w/h
// and the viewport is letterboxed (bars) so content keeps the AR as the window
// resizes. g_lastVfov remembers the vertical fov so a resize can rebuild the
// frustum even for scripts that don't call (set-fov) every frame.
double  g_aspectLock = 0.0, g_prevAspectLock = 0.0;
double  g_lastVfov   = 73.7397;   // = 2*atan(0.75) deg, the default frustum fov

Camera* cam0() {
  if (!g_ctx.r) return nullptr;
  auto& cams = g_ctx.r->GetCameraVec();
  return cams.empty() ? nullptr : &cams[0];
}

// follow-cam smoothing state. We bake the follow ourselves (NOT engine LockCamera)
// so we know the EXACT applied view and can invert it precisely for the camera-node
// HUD anchor at any lag. g_followInit guards the first frame (snap, no blend).
dMatrix g_followSmoothed;
bool    g_followInit = false;

// Compute + apply the camera view for this frame, and derive the camera-node anchor
// (inverse of the applied view). doFollowBlend advances the follow smoothing exactly
// once per frame — true only from flux_camera_finalize() (after the script has moved
// the followed node); applyCamera() passes false for its provisional frame-begin set.
void computeAndApplyCamera(bool doFollowBlend) {
  Camera* c = cam0();
  if (!c) return;
  dMatrix orbit;
  if (g_camOverride) {
    orbit = g_camOverrideMat;
  } else {
    dMatrix rot;  rot.rotxyz((float) g_cam.pitch, (float) g_cam.yaw, 0);
    dMatrix back; back.translate(0, 0, (float) -g_cam.dist);
    orbit = back * rot;               // rotate world, then push back from eye
  }
  dMatrix applied = orbit;
  if (g_camAttach && g_ctx.r) {
    dMatrix worldmat = g_ctx.r->GetGlobalTransform(g_camAttach).inverse();
    if (doFollowBlend) {
      if (!g_followInit || g_camLag <= 0.0) { g_followSmoothed = worldmat; g_followInit = true; }
      else g_followSmoothed.blend(worldmat, (float) g_camLag);
    }
    applied = g_followSmoothed * orbit;   // ride the node, keep orbit as an offset
  }
  c->LockCamera(0);                    // we bake the follow ourselves
  c->SetMatrix(applied);
  g_camAppliedMat = applied;
  g_camViewInv    = applied.inverse(); // eye-space anchor for the camera-node
}

// build the frustum for a vertical fov + aspect (w/h) on the given camera
void applyFrustum(Camera* c, double vfovDeg, double aspect) {
  const double front = 1.0;                                   // near clip
  const double t = front * std::tan(vfovDeg * 0.5 * 3.14159265358979323846 / 180.0);
  const double r = t * aspect;
  c->SetFrustum((float) -r, (float) r, (float) -t, (float) t);
}
// centre a viewport of target AR inside a window of the current pixel size,
// adding letterbox/pillarbox bars so content isn't stretched.
void applyLetterbox(Camera* c, double targetAR) {
  const double winAR = (g_screenH > 0) ? (double) g_screenW / g_screenH : targetAR;
  double vx = 0, vy = 0, vw = 1, vh = 1;
  if (winAR > targetAR) { vw = targetAR / winAR; vx = (1.0 - vw) * 0.5; }  // pillarbox
  else                  { vh = winAR / targetAR; vy = (1.0 - vh) * 0.5; }  // letterbox
  c->SetViewport((float) vx, (float) vy, (float) vw, (float) vh);
}

// key channel: the app pushes the last-pressed char; a script polls it once
// ((key-poll) consumes it, returning 0 when nothing new). Feeds simple hotkeys.
std::atomic<int> g_key{0};

// held-key channel: unlike the consume-once key-poll above, this mirrors the LIVE
// physical up/down state of each key (indexed by char code). The app's message
// thread polls the OS every frame and writes here; a script reads (key-down? c)
// every frame for smooth hold-to-move FPS controls. JUCE-free: just an atomic array.
std::array<std::atomic<uint8_t>, 256> g_keyDown{};
} // namespace

void applyCamera() {
  computeAndApplyCamera(false);        // provisional (frame begin, node not yet moved)
  if (g_camAttach == 0) g_followInit = false;
  if (!flux_retained_on()) g_camNodeId = -1;   // immediate mode Clear()ed the graph
}

// snapshot for the native deformers in FluxusCommandsPdata.cpp (GL thread) while
// the audio host writes on its own thread.
std::vector<float> audioBands() {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  return g_bands;
}

extern "C" {

void flux_set_audio(const float* bands, int n, double gain) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  g_bands.assign(bands, bands + (n > 0 ? n : 0));
  g_gain = gain;
}
double flux_audio_harmonic(int n) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  if (g_bands.empty()) return 0.0;
  if (n < 0) n = -n;
  return g_bands[(size_t) (n % (int) g_bands.size())];   // fluxus: gh(h) = bars[h % numBars]
}
double flux_audio_gain(void) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  return g_gain;
}

// ---- MIDI input -------------------------------------------------------------
void flux_set_midi_cc(int chan, int ctrl, int val) {
  if (chan < 0 || chan >= 16 || ctrl < 0 || ctrl >= 128) return;
  std::lock_guard<std::mutex> lk(g_midiMutex);
  g_midiCC[chan][ctrl] = val;
}
void flux_set_midi_note(int pitch, int vel) {
  std::lock_guard<std::mutex> lk(g_midiMutex);
  g_midiNotePitch = pitch; g_midiNoteVel = vel;
}
int flux_midi_cc(int chan, int ctrl) {
  if (chan < 0 || chan >= 16 || ctrl < 0 || ctrl >= 128) return 0;
  std::lock_guard<std::mutex> lk(g_midiMutex);
  return g_midiCC[chan][ctrl];
}
double flux_midi_ccn(int chan, int ctrl) { return flux_midi_cc(chan, ctrl) / 127.0; }
int flux_midi_note(void)          { std::lock_guard<std::mutex> lk(g_midiMutex); return g_midiNotePitch; }
int flux_midi_note_velocity(void) { std::lock_guard<std::mutex> lk(g_midiMutex); return g_midiNoteVel; }

// ---- OSC --------------------------------------------------------------------
void flux_set_osc(const char* addr, const double* args, int n) {
  if (!addr) return;
  std::lock_guard<std::mutex> lk(g_oscMutex);
  g_oscLastAddr = addr;
  g_oscMsgs[addr].assign(args, args + (n > 0 ? n : 0));
}
double flux_osc_get(const char* addr, int index) {
  if (!addr) return 0.0;
  std::lock_guard<std::mutex> lk(g_oscMutex);
  auto it = g_oscMsgs.find(addr);
  if (it == g_oscMsgs.end() || index < 0 || index >= (int) it->second.size()) return 0.0;
  return it->second[(size_t) index];
}
int flux_osc_msg(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_oscMutex);
  int len = (int) g_oscLastAddr.size();
  if (out && cap > 0) { int c = len < cap - 1 ? len : cap - 1; memcpy(out, g_oscLastAddr.data(), (size_t) c); out[c] = 0; }
  return len;
}
void flux_osc_source(int port)                        { if (g_oscBridge.openSource) g_oscBridge.openSource(port); }
void flux_osc_destination(const char* host, int port) { if (g_oscBridge.setDestination) g_oscBridge.setDestination(host ? host : "", port); }
void flux_osc_send(const char* addr, const double* args, int n) {
  if (g_oscBridge.send) g_oscBridge.send(addr ? addr : "", std::vector<double>(args, args + (n > 0 ? n : 0)));
}

// ---- Hand tracking ----------------------------------------------------------
void flux_set_hands(int nHands, const float* xyz, int per) {
  std::lock_guard<std::mutex> lk(g_handMutex);
  if (nHands < 0) nHands = 0;
  if (per <= 0)   per = g_handPer;
  g_handCount = nHands; g_handPer = per;
  size_t n = (size_t) nHands * per * 3;
  if (xyz && n) g_handXYZ.assign(xyz, xyz + n); else g_handXYZ.clear();
}
int flux_hand_count(void) { std::lock_guard<std::mutex> lk(g_handMutex); return g_handCount; }
double flux_hand_joint(int hand, int joint, int axis) {
  std::lock_guard<std::mutex> lk(g_handMutex);
  if (hand < 0 || hand >= g_handCount || joint < 0 || joint >= g_handPer || axis < 0 || axis > 2) return 0.0;
  size_t idx = (((size_t) hand * g_handPer) + joint) * 3 + axis;
  return idx < g_handXYZ.size() ? (double) g_handXYZ[idx] : 0.0;
}
double flux_hand_pinch(int hand) {
  std::lock_guard<std::mutex> lk(g_handMutex);
  if (hand < 0 || hand >= g_handCount) return 0.0;
  auto at = [&](int j, int a) -> float {
    size_t i = (((size_t) hand * g_handPer) + j) * 3 + a;
    return i < g_handXYZ.size() ? g_handXYZ[i] : 0.f;
  };
  float dx = at(4,0) - at(8,0), dy = at(4,1) - at(8,1), dz = at(4,2) - at(8,2);
  return std::sqrt(dx*dx + dy*dy + dz*dz);
}
void flux_hand_tracking(int on) {
  std::function<void(bool)> f;
  { std::lock_guard<std::mutex> lk(g_handBridgeMutex); f = g_handBridge.enable; }
  if (f) f(on != 0);
}

void flux_set_mouse(double x, double y, int button) { g_mouseX = x; g_mouseY = y; g_mouseButton = button; }
double flux_mouse_x(void)     { return g_mouseX; }
double flux_mouse_y(void)     { return g_mouseY; }
int    flux_mouse_button(void){ return g_mouseButton; }

void flux_set_key(int c) { g_key = c; }
int  flux_get_key(void)  { return g_key.exchange(0); }

void flux_set_key_down(int code, int down) { if (code >= 0 && code < 256) g_keyDown[(size_t) code] = down ? 1 : 0; }
int  flux_key_is_down(int code)            { return (code >= 0 && code < 256) ? (int) g_keyDown[(size_t) code].load() : 0; }
void flux_clear_keys_down(void)            { for (auto& k : g_keyDown) k = 0; }
void flux_camera_drag(double dx, double dy) {
  g_cam.yaw   += dx * 0.5;
  g_cam.pitch += dy * 0.5;
  if (g_cam.pitch >  89.0) g_cam.pitch =  89.0;
  if (g_cam.pitch < -89.0) g_cam.pitch = -89.0;
}
void flux_camera_zoom(double d) {
  g_cam.dist += d;
  if (g_cam.dist < 2.0)  g_cam.dist = 2.0;
  if (g_cam.dist > 80.0) g_cam.dist = 80.0;
}
double flux_camera_dist(void)  { return g_cam.dist; }
double flux_camera_yaw(void)   { return g_cam.yaw; }
double flux_camera_pitch(void) { return g_cam.pitch; }

// ---- script-driven camera --------------------------------------------------
void flux_set_camera_transform(const double* m) {
  if (!m) return;
  float* a = g_camOverrideMat.arr();
  for (int i = 0; i < 16; ++i) a[i] = (float) m[i];
  g_camOverride = true;
  g_camAppliedMat = g_camOverrideMat;
  if (Camera* c = cam0()) c->SetMatrix(g_camOverrideMat);   // apply this frame too
}
void flux_get_camera_transform(double* out) {
  if (!out) return;
  const float* a = g_camAppliedMat.arr();
  for (int i = 0; i < 16; ++i) out[i] = a[i];
}
void flux_set_camera_position(double x, double y, double z) {
  // set the eye position: translate part of the camera (view) matrix
  dMatrix m; m.translate((float) -x, (float) -y, (float) -z);
  float* a = g_camOverrideMat.arr();
  for (int i = 0; i < 16; ++i) a[i] = m.arr()[i];
  g_camOverride = true;
  g_camAppliedMat = g_camOverrideMat;
  if (Camera* c = cam0()) c->SetMatrix(g_camOverrideMat);
}
void flux_camera_reset(void) {
  g_camOverride = false;   // back to the mouse orbit...
  g_cam = CamState();      // ...at its default yaw/pitch/dist (undo drag+zoom)
  g_camAttach = 0;         // ...and detach any follow-cam
  g_camLag = 0.0;
  g_followInit = false;
}

// ---- camera as a scene-graph node ------------------------------------------
void flux_camera_parent(int id) { g_camAttach = id; g_followInit = false; }   // 0 = detach
void flux_camera_lag(double amt) {
  g_camLag = amt < 0.0 ? 0.0 : (amt > 1.0 ? 1.0 : amt);
}
// Return an invisible locator whose world transform tracks the inverse view;
// parenting prims to it puts them in eye space (HUD/billboard). Created lazily so
// sketches that never ask pay nothing. applyCamera() refreshes its transform each
// frame (retained) or invalidates the id after the per-frame Clear() (immediate).
int flux_camera_node(void) {
  if (!g_ctx.r) return -1;
  // g_camViewInv is the provisional (frame-begin) anchor; flux_camera_finalize()
  // refreshes it exactly before Render, so mid-thunk creation here is fine.
  // reuse the existing node if it's still in the graph this frame
  if (g_camNodeId >= 0 && g_ctx.r->GetSceneGraph().FindNode(g_camNodeId)) {
    if (SceneNode* n = (SceneNode*) g_ctx.r->GetSceneGraph().FindNode(g_camNodeId))
      n->Prim->GetState()->Transform = g_camViewInv;
    return g_camNodeId;
  }
  // create fresh, with a clean build context so an active (parent …) / grab / tx
  // doesn't leak onto the camera-node.
  int savedParent = g_ctx.parent; dMatrix savedTx = g_ctx.tx;
  g_ctx.parent = -1; g_ctx.tx = dMatrix();
  LocatorPrimitive* loc = new LocatorPrimitive();
  loc->SetBoundingBoxRadius(0);      // never skew scene AABB / frustum
  int id = addPrim(loc);
  g_ctx.parent = savedParent; g_ctx.tx = savedTx;
  if (id >= 0) loc->GetState()->Transform = g_camViewInv;   // eye-space anchor
  g_camNodeId = id;
  return id;
}
// Called by the render loop AFTER the script eval and BEFORE Render(): the attached
// node's transform is now final, so recompute the camera-node anchor and refresh the
// node in place. This is what makes HUD prims pin exactly (see the ordering note).
void flux_camera_finalize(void) {
  computeAndApplyCamera(true);   // followed node is now final -> exact view + anchor
  if (g_camNodeId >= 0 && g_ctx.r) {
    if (SceneNode* n = (SceneNode*) g_ctx.r->GetSceneGraph().FindNode(g_camNodeId))
      n->Prim->GetState()->Transform = g_camViewInv;
    else g_camNodeId = -1;   // destroyed under us
  }
}

void flux_set_fov(double vfovDeg) {
  Camera* c = cam0();
  if (!c) return;
  g_lastVfov = vfovDeg;
  const double aspect = (g_aspectLock > 0.0) ? g_aspectLock
                      : (g_screenH > 0) ? (double) g_screenW / g_screenH : 4.0 / 3.0;
  applyFrustum(c, vfovDeg, aspect);
}

// lock the render aspect ratio (w/h); ratio<=0 restores auto (fill window).
void flux_set_aspect(double ratio) { g_aspectLock = (ratio > 0.0) ? ratio : 0.0; }

void flux_request_window_size(int w, int h) {
  std::lock_guard<std::mutex> lk(g_winMutex);
  g_winReqW = w; g_winReqH = h; g_winReqPending = true;
}
int flux_take_window_request(int* w, int* h) {
  std::lock_guard<std::mutex> lk(g_winMutex);
  if (!g_winReqPending) return 0;
  if (w) *w = g_winReqW; if (h) *h = g_winReqH;
  g_winReqPending = false; return 1;
}

// editor visibility: the script sets a desired state; the component reads it every
// tick and only re-lays-out when it actually changed (so calling this every frame
// in an every-frame thunk is cheap).
void flux_set_editor_visible(int visible) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  g_edVisible = visible ? 1 : 0; g_edSet = true;
}
void flux_set_editor_full_width(int full) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  g_edFull = full ? 1 : 0; g_edSet = true;
}
int flux_get_editor(int* visible, int* full) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  if (!g_edSet) return 0;
  if (visible) *visible = g_edVisible;
  if (full)    *full    = g_edFull;
  return 1;
}
void flux_set_tweaks_visible(int visible) {
  std::lock_guard<std::mutex> lk(g_tweakVisMutex);
  g_tweakVis = visible ? 1 : 0; g_tweakVisSet = true;
}
int flux_get_tweaks_visible(int* visible) {
  std::lock_guard<std::mutex> lk(g_tweakVisMutex);
  if (!g_tweakVisSet) return 0;
  if (visible) *visible = g_tweakVis;
  return 1;
}

void flux_set_recording(int on, const char* dir) {
  std::lock_guard<std::mutex> lk(g_recMutex);
  if (on) { g_recDir = dir ? dir : "."; g_recFrame = 0; g_recOn = true; }
  else    { g_recOn = false; }
}
int flux_recording_next(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_recMutex);
  if (!g_recOn || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s/f%05ld.png", g_recDir.c_str(), g_recFrame++);
  return 1;
}

void flux_set_export(int on, const char* path, int fps) {
  std::lock_guard<std::mutex> lk(g_expMutex);
  if (on) { g_expPath = path ? path : "export.mp4"; g_expFps = fps > 0 ? fps : 60; g_expOn = true; }
  else    { g_expOn = false; }
}
int flux_export_state(char* pathOut, int cap, int* fps) {
  std::lock_guard<std::mutex> lk(g_expMutex);
  if (!g_expOn) return 0;
  if (pathOut && cap > 0) std::snprintf(pathOut, (size_t) cap, "%s", g_expPath.c_str());
  if (fps) *fps = g_expFps;
  return 1;
}

void flux_set_export_audio(const char* wavPath) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  g_expAudPath = wavPath ? wavPath : "";
}
int flux_export_audio_path(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  if (g_expAudPath.empty() || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s", g_expAudPath.c_str());
  return 1;
}
void flux_export_audio_load(const float* gains, const float* bands, int nFrames, int nBands) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  if (!gains || !bands || nFrames <= 0 || nBands <= 0) { g_expAudFrames = 0; return; }
  g_expAudGains.assign(gains, gains + nFrames);
  g_expAudBands.assign(bands, bands + (size_t) nFrames * nBands);
  g_expAudNBands = nBands; g_expAudFrames = nFrames;
}
void flux_export_audio_apply(long frame) {
  float gain; const float* bands; int n;
  {
    std::lock_guard<std::mutex> lk(g_expAudMutex);
    if (g_expAudFrames <= 0) return;
    long f = frame; if (f < 0) f = 0; if (f >= g_expAudFrames) f = g_expAudFrames - 1;  // clamp
    gain = g_expAudGains[(size_t) f];
    bands = &g_expAudBands[(size_t) f * g_expAudNBands];
    n = g_expAudNBands;
  }
  flux_set_audio(bands, n, gain);   // overwrite this frame's audio state
}
void flux_export_audio_clear(void) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  g_expAudGains.clear(); g_expAudBands.clear(); g_expAudFrames = 0; g_expAudNBands = 0;
  g_expAudPath.clear();
}

// one-shot screenshot request. Captured once per unique path, so a script may
// call (screenshot p) unconditionally every frame — only the first is written.
void flux_screenshot(const char* path) {
  if (!path) return;
  std::lock_guard<std::mutex> lk(g_shotMutex);
  if (g_shotDone.count(path)) return;          // already captured this path
  g_shotPending = path;
}
int flux_take_screenshot(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_shotMutex);
  if (g_shotPending.empty() || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s", g_shotPending.c_str());
  g_shotDone.insert(g_shotPending);
  g_shotPending.clear();
  return 1;
}
void flux_set_frustum(double l, double r, double b, double t) {
  if (Camera* c = cam0()) c->SetFrustum((float) l, (float) r, (float) b, (float) t);
}
void flux_set_ortho(int on)         { if (Camera* c = cam0()) c->SetOrtho(on != 0); }
void flux_set_ortho_zoom(double z)  { if (Camera* c = cam0()) c->SetOrthoZoom((float) z); }
void flux_set_clip(double f, double b) { if (Camera* c = cam0()) c->SetClip((float) f, (float) b); }
void flux_set_viewport(double x, double y, double w, double h) {
  if (Camera* c = cam0()) c->SetViewport((float) x, (float) y, (float) w, (float) h);
}
void flux_set_resolution(int w, int h) {
  g_screenW = w; g_screenH = h;
  // apply the aspect lock every frame (runs on the GL thread) so the render
  // tracks window resizes: locked -> rebuild frustum + letterbox; just-unlocked
  // -> restore the full viewport + a window-aspect frustum once.
  if (Camera* c = cam0()) {
    if (g_aspectLock > 0.0) {
      applyFrustum(c, g_lastVfov, g_aspectLock);
      applyLetterbox(c, g_aspectLock);
    } else if (g_prevAspectLock > 0.0) {
      c->SetViewport(0.0f, 0.0f, 1.0f, 1.0f);
      applyFrustum(c, g_lastVfov, (h > 0) ? (double) w / h : 4.0 / 3.0);
    }
  }
  g_prevAspectLock = g_aspectLock;
}
void flux_get_screen_size(double* out) { if (out) { out[0] = g_screenW; out[1] = g_screenH; } }

int flux_state_get(const char* key, double* out, int n) {
  if (!key || !out || n <= 0) return 0;
  std::lock_guard<std::mutex> lk(g_stateMutex);
  auto it = g_state.find(key);
  if (it == g_state.end()) return 0;
  const auto& v = it->second;
  for (int i = 0; i < n; ++i) out[i] = (i < (int) v.size()) ? v[(size_t) i] : 0.0;
  return 1;
}
void flux_state_set(const char* key, const double* v, int n) {
  if (!key || !v || n < 0) return;
  std::lock_guard<std::mutex> lk(g_stateMutex);
  g_state[key].assign(v, v + n);
}
void flux_state_clear(void) {
  std::lock_guard<std::mutex> lk(g_stateMutex);
  g_state.clear();
}

void flux_report_error(const char* msg) {
  std::lock_guard<std::mutex> lk(g_errMutex);
  g_err = msg ? msg : "";
}

} // extern "C"

// OSC send transport bridge (C++ linkage — takes FluxOscBridge, so it lives
// outside the extern "C" block). OscHost installs its lambdas here at start.
void flux_osc_install_bridge(const FluxOscBridge& b) { g_oscBridge = b; }
void flux_hand_install_bridge(const FluxHandBridge& b) {
  std::lock_guard<std::mutex> lk(g_handBridgeMutex); g_handBridge = b;
}

// ---- live tweak registry (script reads, tweak panel writes) ----------------
// A flat vector rather than a map: a sketch declares tens of tweaks at most, and
// this keeps them in declaration order, which is the order the panel lists them
// in. State lives at file scope so slider values survive every re-eval.
namespace {
std::mutex            g_tweakMutex;
std::vector<TweakVar> g_tweaks;

TweakVar* findTweak(const char* name) {
  for (auto& t : g_tweaks)
    if (t.name == name) return &t;
  return nullptr;
}
inline double clampTweak(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
}
extern "C" double flux_tweak(const char* name, double def, double lo, double hi) {
  if (!name) return def;
  if (hi < lo) std::swap(lo, hi);
  std::lock_guard<std::mutex> lk(g_tweakMutex);
  if (TweakVar* t = findTweak(name)) {
    t->lo = lo; t->hi = hi;                      // the script owns the range: re-apply it
    t->value = clampTweak(t->value, lo, hi);     // (a narrowed range pulls the value in)
    return t->value;
  }
  g_tweaks.push_back({ name, clampTweak(def, lo, hi), lo, hi });
  return g_tweaks.back().value;
}
void flux_tweak_list(std::vector<TweakVar>& out) {
  std::lock_guard<std::mutex> lk(g_tweakMutex);
  out = g_tweaks;
}
void flux_tweak_set(const char* name, double value) {
  if (!name) return;
  std::lock_guard<std::mutex> lk(g_tweakMutex);
  if (TweakVar* t = findTweak(name)) t->value = clampTweak(value, t->lo, t->hi);
}
void flux_tweak_clear() {
  std::lock_guard<std::mutex> lk(g_tweakMutex);
  g_tweaks.clear();
}

std::string flux_last_error() {
  std::lock_guard<std::mutex> lk(g_errMutex);
  return g_err;
}
