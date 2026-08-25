#include "FluxusGLComponent.h"
#include "FluxusScene.h"
#include "EditorOverlay.h"
#include "IScriptHost.h"

#ifndef FLUXUS_FONT_PATH
#define FLUXUS_FONT_PATH ""
#endif

namespace {
const char* kStarter =
  "; fluxus GLEditor - edit, then Ctrl+E (or Shift+Enter) to run\n"
  "(background (vector 0.08 0.09 0.12))\n"
  "(colour (vector 0.9 0.5 0.2))\n"
  "(rotate (vector (* 25 (time)) (* 40 (time)) 0))\n"
  "(build-cube)\n"
  "\n"
  "(with-state\n"
  "  (translate (vector 2.5 0 0))\n"
  "  (colour (vector 0.3 0.7 1.0))\n"
  "  (scale (vector 0.6 0.6 0.6))\n"
  "  (build-sphere 16 16))\n";

// GLUT special-key codes (freeglut), matched by GLEditor::Handle
enum { K_LEFT=100, K_UP=101, K_RIGHT=102, K_DOWN=103,
       K_PGUP=104, K_PGDN=105, K_HOME=106, K_END=107 };
}

FluxusGLComponent::FluxusGLComponent(ScriptHostFactory mh) : makeHost(std::move(mh)) {
  setWantsKeyboardFocus(true);
  ctx.setRenderer(this);
  ctx.setComponentPaintingEnabled(true);
  ctx.setContinuousRepainting(true);
  ctx.attachTo(*this);
}

FluxusGLComponent::~FluxusGLComponent() {
  ctx.detach();
}

void FluxusGLComponent::newOpenGLContextCreated() {
  scene = std::make_unique<FluxusScene>(&shared, makeHost());
  scene->init();
  overlay = std::make_unique<EditorOverlay>();
  overlay->init(std::string(FLUXUS_FONT_PATH), kStarter);
  fontReady = true;
  // commit the starter program once (Ctrl+E / Shift+Enter re-commits after edits)
  { std::lock_guard<std::mutex> lk(shared.m); shared.pending = kStarter; }
}

void FluxusGLComponent::openGLContextClosing() {
  overlay.reset();
  scene.reset();
  fontReady = false;
}

void FluxusGLComponent::renderOpenGL() {
  if (!scene || !overlay) return;
  const float s = (float) ctx.getRenderingScale();
  const int pw = (int) (getWidth() * s), ph = (int) (getHeight() * s);

  // NOTE: the scene evals the COMMITTED script (shared.pending), not the live
  // editor text — edits only take effect on Ctrl+E / Shift+Enter (see keyPressed).
  scene->setResolution(pw, ph);
  scene->renderFrame();          // eval script + render 3D

  overlay->reshape(pw, ph);
  overlay->render();             // fluxus GL text over the scene
}

void FluxusGLComponent::parentHierarchyChanged() {
  if (isShowing()) grabKeyboardFocus();
}

bool FluxusGLComponent::keyPressed(const juce::KeyPress& k) {
  if (!overlay) return false;
  auto mods = k.getModifiers();
  int mod = 0;
  if (mods.isShiftDown())                        mod |= 1;
  if (mods.isCtrlDown() || mods.isCommandDown()) mod |= 2;
  if (mods.isAltDown())                          mod |= 4;

  const int kc = k.getKeyCode();

  // fluxus eval: Ctrl+E (or Cmd+E / Shift+Enter) commits the buffer to the scene
  const bool ctrlE = (mods.isCtrlDown() || mods.isCommandDown()) && (kc == (int) 'E' || kc == (int) 'e');
  const bool shiftEnter = mods.isShiftDown() && kc == juce::KeyPress::returnKey;
  if (ctrlE || shiftEnter) {
    std::string t = overlay->getText();
    std::lock_guard<std::mutex> lk(shared.m);
    shared.pending = t;
    return true;   // don't forward to the editor
  }

  int key = 0, special = 0;
  if      (kc == juce::KeyPress::leftKey)      special = K_LEFT;
  else if (kc == juce::KeyPress::rightKey)     special = K_RIGHT;
  else if (kc == juce::KeyPress::upKey)        special = K_UP;
  else if (kc == juce::KeyPress::downKey)      special = K_DOWN;
  else if (kc == juce::KeyPress::pageUpKey)    special = K_PGUP;
  else if (kc == juce::KeyPress::pageDownKey)  special = K_PGDN;
  else if (kc == juce::KeyPress::homeKey)      special = K_HOME;
  else if (kc == juce::KeyPress::endKey)       special = K_END;
  else if (kc == juce::KeyPress::returnKey)    key = 13;   // GLEDITOR_RETURN
  else if (kc == juce::KeyPress::backspaceKey) key = 127;  // GLEDITOR_BACKSPACE (apple)
  else if (kc == juce::KeyPress::deleteKey)    key = 8;    // GLEDITOR_DELETE (apple)
  else if (kc == juce::KeyPress::tabKey)       key = 9;
  else if (kc == juce::KeyPress::escapeKey)    key = 27;
  else {
    auto tc = k.getTextCharacter();
    if (tc > 0 && tc < 128) key = (int) tc;
  }

  if (key == 0 && special == 0) return false;
  overlay->handleKey(key, special, mod);
  return true;
}
