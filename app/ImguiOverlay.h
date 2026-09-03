// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <mutex>
#include <vector>

// On-screen tweak panel (Dear ImGui) drawn over the finished scene. Scripts
// register variables with (tweak name default lo hi) — see flux_tweak in
// FluxusCommands — and this panel turns each one into a slider, writing edits
// straight back into the registry. A sketch with no tweaks draws nothing at all,
// so the panel only appears when there is something to turn.
//
// JUCE-free TU: it drives system GL through ImGui's OpenGL2 backend, so it must
// never see juce_opengl's juce::gl symbols. The JUCE components hold it through
// this forward-declared interface and hand-feed it input, exactly as they do for
// EditorOverlay's GLEditor.
//
// Threading follows the same rule as the script hosts: the ImGui context belongs
// to the GL thread and is NOT thread-safe. The on* methods are called from the
// message thread, so they only append to a mutex-protected queue that render()
// drains; nothing outside render()/init()/shutdown() touches ImGui itself.
class ImguiOverlay {
public:
  ImguiOverlay();
  ~ImguiOverlay();

  void init();        // GL thread, once the context exists
  void shutdown();    // GL thread, before the context goes away

  // GL thread. Frame geometry, mirroring how the components size the scene:
  // `w`/`h` are logical points (the space JUCE mouse events arrive in) and
  // `scale` is the retina factor.
  void setDisplay(int w, int h, float scale);

  // GL thread: drain input, build and draw the panel. No-op — and no GL calls at
  // all — while hidden or while the sketch has no tweaks.
  void render();

  // --- message thread ------------------------------------------------------
  void onMouseMove(float x, float y);          // in points
  void onMouseButton(int button, bool down);   // 0 left, 1 right, 2 middle
  void onMouseWheel(float dx, float dy);

  // True while the pointer is over the panel. The components check this BEFORE
  // orbiting the camera so a slider drag doesn't also spin the scene. It reflects
  // the last rendered frame — a frame of lag, which is fine for a capture gate.
  bool wantsMouse() const { return captureMouse.load(); }

  void setVisible(bool v);
  bool isVisible() const { return visible.load(); }

private:
  struct InputEvent {
    enum class Kind { MousePos, MouseButton, MouseWheel };
    Kind  kind;
    float a = 0.0f, b = 0.0f;   // pos: x,y (points) · button: index,down · wheel: dx,dy
  };

  void drainInput();   // GL thread: replay the queued events into ImGui
  void drawPanel(const std::vector<struct TweakVar>& vars);   // GL thread: the sliders
  void push(const InputEvent& e);

  bool   ready         = false;
  float  uiScale       = 0.0f;   // 0 = fonts not built yet (see setDisplay)
  double lastFrameTime = 0.0;    // for ImGui's DeltaTime

  std::atomic<bool> visible { true };
  std::atomic<bool> captureMouse { false };   // mirror of io.WantCaptureMouse
  std::mutex               inputMutex;
  std::vector<InputEvent>  inputQueue;
};
