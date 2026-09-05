// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ImguiOverlay.h"
#include "FluxusCommands.h"

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "GLHeaders.h"

#include <chrono>
#include <cmath>

namespace {
// The panel works in PIXELS: the font atlas is rasterised at the retina size so
// the text stays crisp, and the style is scaled to match. That makes ImGui's
// coordinate space identical to the GL framebuffer, so the mouse positions fed
// in from JUCE (which arrive in points) are scaled as they are drained.
constexpr float kFontPoints = 13.0f;

double nowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}

ImguiOverlay::ImguiOverlay()  = default;
ImguiOverlay::~ImguiOverlay() { shutdown(); }

void ImguiOverlay::init() {
  if (ready) return;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr;   // don't drop an imgui.ini beside the .app
  ImGui_ImplOpenGL2_Init();
  ready = true;
}

void ImguiOverlay::shutdown() {
  if (!ready) return;
  ImGui_ImplOpenGL2_Shutdown();
  ImGui::DestroyContext();
  ready = false;
}

void ImguiOverlay::setVisible(bool v) {
  visible.store(v);
  // A hidden panel captures nothing: WantCaptureMouse would otherwise keep the
  // value from the last drawn frame and block the camera for good.
  if (!v) captureMouse.store(false);
}

void ImguiOverlay::setDisplay(int w, int h, float scale) {
  if (!ready || w <= 0 || h <= 0 || scale <= 0.0f) return;
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float) w * scale, (float) h * scale);
  if (std::fabs(scale - uiScale) < 1e-6f) return;

  // Re-rasterise the font at the new pixel size and re-scale the style. Happens
  // on the first frame, and again if the window moves to a display of a
  // different density.
  ImGui::GetStyle() = ImGuiStyle();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImFontConfig cfg;
  cfg.SizePixels = kFontPoints * scale;
  io.Fonts->Clear();
  io.Fonts->AddFontDefault(&cfg);
  ImGui_ImplOpenGL2_DestroyFontsTexture();   // forces a re-upload next frame
  uiScale = scale;
}

// --- message thread: queue only, never touch ImGui --------------------------
void ImguiOverlay::push(const InputEvent& e) {
  std::lock_guard<std::mutex> lk(inputMutex);
  inputQueue.push_back(e);
}
void ImguiOverlay::onMouseMove(float x, float y) {
  push({ InputEvent::Kind::MousePos, x, y });
}
void ImguiOverlay::onMouseButton(int button, bool down) {
  push({ InputEvent::Kind::MouseButton, (float) button, down ? 1.0f : 0.0f });
}
void ImguiOverlay::onMouseWheel(float dx, float dy) {
  push({ InputEvent::Kind::MouseWheel, dx, dy });
}

// --- GL thread ---------------------------------------------------------------
void ImguiOverlay::drainInput() {
  std::vector<InputEvent> events;
  { std::lock_guard<std::mutex> lk(inputMutex); events.swap(inputQueue); }

  ImGuiIO& io = ImGui::GetIO();
  for (const InputEvent& e : events) {
    switch (e.kind) {
      case InputEvent::Kind::MousePos:
        io.AddMousePosEvent(e.a * uiScale, e.b * uiScale);   // points -> pixels
        break;
      case InputEvent::Kind::MouseButton:
        io.AddMouseButtonEvent((int) e.a, e.b != 0.0f);
        break;
      case InputEvent::Kind::MouseWheel:
        io.AddMouseWheelEvent(e.a, e.b);
        break;
    }
  }
}

void ImguiOverlay::render() {
  std::vector<TweakVar> vars;
  if (ready && visible.load()) flux_tweak_list(vars);

  // Nothing to show — the panel is off, or the sketch declares no (tweak …).
  // Skip ImGui entirely: no NewFrame, no GL calls, and release the input gate so
  // the camera keeps working. Also drop whatever the message thread queued, or
  // it would grow without bound while the panel is idle.
  if (vars.empty()) {
    captureMouse.store(false);
    std::lock_guard<std::mutex> lk(inputMutex);
    inputQueue.clear();
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) return;

  const double now = nowSeconds();
  const double dt  = lastFrameTime > 0.0 ? now - lastFrameTime : 0.0;
  io.DeltaTime = dt > 0.0 ? (float) dt : 1.0f / 60.0f;   // ImGui asserts on <= 0
  lastFrameTime = now;

  drainInput();
  ImGui_ImplOpenGL2_NewFrame();
  ImGui::NewFrame();
  drawPanel(vars);
  ImGui::Render();
  captureMouse.store(io.WantCaptureMouse);

  // The OpenGL2 backend is pure fixed-function and, as its own header warns,
  // cannot undo modern-GL state. libfluxus leaves a GLSL program bound for
  // shaded prims and (with FLUXUS_ENABLE_VBO) a vertex buffer bound, either of
  // which would swallow or garble the panel's client-array draws. Clear both,
  // and put texturing back on unit 0, before handing over.
  glUseProgram(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glClientActiveTexture(GL_TEXTURE0);

  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

void ImguiOverlay::drawPanel(const std::vector<TweakVar>& vars) {
  // Top-right: both editors (the JUCE TextEditor and the fluxus GLEditor) live
  // on the left. Flat window, no child regions — every extra clip rect is one
  // more state change on a renderer that is already state-bound.
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f * uiScale, 12.0f * uiScale),
                          ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
  ImGui::Begin("tweaks", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
  for (const TweakVar& v : vars) {
    float f = (float) v.value;
    if (ImGui::SliderFloat(v.name.c_str(), &f, (float) v.lo, (float) v.hi))
      flux_tweak_set(v.name.c_str(), f);
  }
  ImGui::End();
}
