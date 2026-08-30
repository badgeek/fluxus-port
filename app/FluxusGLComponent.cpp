#include "FluxusGLComponent.h"
#include "FluxusScene.h"
#include "EditorOverlay.h"
#include "IScriptHost.h"
#include "AudioHost.h"
#include "MidiHost.h"
#include "OscHost.h"
#include "FluxusCommands.h"   // mouse/camera
#include <juce_gui_basics/juce_gui_basics.h>   // SystemClipboard

#ifndef FLUXUS_FONT_PATH
#define FLUXUS_FONT_PATH ""
#endif

namespace {
const char* kStarter =
  "; fluxus GLEditor - edit, then Ctrl+E (or Shift+Enter) to run\n"
  "; retained: the Racket host compiles this once and per frame runs only the\n"
  "; thunk (low CPU); (clear) wipes the scene each frame so it rebuilds cleanly.\n"
  "(retained)\n"
  "(every-frame\n"
  "  (clear)\n"
  "  (background (vector 0.08 0.09 0.12))\n"
  "  (colour (vector 0.9 0.5 0.2))\n"
  "  (rotate (vector (* 25 (time)) (* 40 (time)) 0))\n"
  "  (build-cube)\n"
  "  (with-state\n"
  "    (translate (vector 2.5 0 0))\n"
  "    (colour (vector 0.3 0.7 1.0))\n"
  "    (scale (vector 0.6 0.6 0.6))\n"
  "    (build-sphere 16 16)))\n";

// GLUT special-key codes (freeglut), matched by GLEditor::Handle
enum { K_LEFT=100, K_UP=101, K_RIGHT=102, K_DOWN=103,
       K_PGUP=104, K_PGDN=105, K_HOME=106, K_END=107 };
}

FluxusGLComponent::FluxusGLComponent(ScriptHostFactory mh) : makeHost(std::move(mh)) {
  setWantsKeyboardFocus(true);
  ctx.setRenderer(this);
  ctx.setComponentPaintingEnabled(true);
  ctx.setContinuousRepainting(false);      // cap fps via the 30 Hz timer below
  { juce::OpenGLPixelFormat pf; pf.multisamplingLevel = 4; ctx.setPixelFormat(pf); }
  ctx.setMultisamplingEnabled(true);       // MSAA for smoother edges
  ctx.attachTo(*this);

  audio = makeJuceAudioHost();
  audio->start();
  midi = makeJuceMidiHost();
  midi->start();
  osc = makeJuceOscHost();
  osc->start();
  startTimerHz(30);                        // ~30 fps is plenty; halves render CPU
}

FluxusGLComponent::~FluxusGLComponent() {
  stopTimer();
  if (audio) audio->stop();
  if (midi)  midi->stop();
  if (osc)   osc->stop();
  ctx.detach();
}

void FluxusGLComponent::timerCallback() { ctx.triggerRepaint(); }

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

  if (overlayVisible) {
    overlay->reshape(pw, ph);
    overlay->render();           // fluxus GL text over the scene
  }
}

void FluxusGLComponent::loadScript(const juce::String& text) {
  const std::string t = text.toStdString();
  if (overlay) overlay->setText(t);          // show it in the GL editor
  std::lock_guard<std::mutex> lk(shared.m);  // commit + run (like Ctrl+E)
  shared.pending = t;
  shared.dirty = true;                       // force a re-eval (retained mode re-commits)
}

bool FluxusGLComponent::loadFile(const juce::File& f) {
  if (!f.existsAsFile()) return false;
  loadScript(f.loadFileAsString());
  return true;
}

juce::String FluxusGLComponent::getScript() {
  return overlay ? juce::String(overlay->getText()) : juce::String();
}

bool FluxusGLComponent::loadAudio(const juce::File& f) {
  return audio && f.existsAsFile()
      && audio->loadAudioFile(f.getFullPathName().toRawUTF8());
}

void FluxusGLComponent::parentHierarchyChanged() {
  if (isShowing()) grabKeyboardFocus();
}

void FluxusGLComponent::mouseDown(const juce::MouseEvent& e) {
  lastMouse = e.position;
  flux_set_mouse(e.position.x, e.position.y, 1);
}
void FluxusGLComponent::mouseDrag(const juce::MouseEvent& e) {
  auto d = e.position - lastMouse;
  lastMouse = e.position;
  flux_camera_drag(d.x, -d.y);
  flux_set_mouse(e.position.x, e.position.y, 1);
}
void FluxusGLComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
  flux_camera_zoom(-w.deltaY * 8.0);
}

bool FluxusGLComponent::keyPressed(const juce::KeyPress& k) {
  if (!overlay) return false;
  auto mods = k.getModifiers();
  int mod = 0;
  if (mods.isShiftDown())                        mod |= 1;
  if (mods.isCtrlDown() || mods.isCommandDown()) mod |= 2;
  if (mods.isAltDown())                          mod |= 4;

  const int kc = k.getKeyCode();

  // system-clipboard bridge: Cmd/Ctrl + C / X / V (GLEditor only has its own
  // internal buffer, and JUCE delivers these as plain letters, so bridge here).
  const bool cmd = (mods.isCommandDown() || mods.isCtrlDown()) && !mods.isShiftDown();
  if (cmd && overlay) {
    const int u = juce::CharacterFunctions::toUpperCase((juce::juce_wchar) kc);
    if (u == 'C') {
      const auto s = overlay->getSelection();
      if (!s.empty()) juce::SystemClipboard::copyTextToClipboard(juce::String::fromUTF8(s.c_str()));
      return true;
    }
    if (u == 'X') {
      const auto s = overlay->getSelection();
      if (!s.empty()) {
        juce::SystemClipboard::copyTextToClipboard(juce::String::fromUTF8(s.c_str()));
        overlay->cutSelection();
      }
      return true;
    }
    if (u == 'V') {
      const auto t = juce::SystemClipboard::getTextFromClipboard();
      if (t.isNotEmpty()) overlay->insertText(t.toStdString());
      return true;
    }
    if (u == 'A') { overlay->selectAll(); return true; }        // select all
    if (u == 'H') { overlayVisible = !overlayVisible; return true; } // toggle editor
  }

  // Ctrl/Cmd + Left/Right: jump by word
  if ((mods.isCtrlDown() || mods.isCommandDown()) && overlay) {
    if (kc == juce::KeyPress::leftKey)  { overlay->jumpWord(-1); return true; }
    if (kc == juce::KeyPress::rightKey) { overlay->jumpWord(1);  return true; }
  }

  // fluxus eval: Ctrl+E (or Cmd+E / Shift+Enter) commits the buffer to the scene
  const bool ctrlE = (mods.isCtrlDown() || mods.isCommandDown()) && (kc == (int) 'E' || kc == (int) 'e');
  const bool shiftEnter = mods.isShiftDown() && kc == juce::KeyPress::returnKey;
  if (ctrlE || shiftEnter) {
    std::string t = overlay->getText();
    std::lock_guard<std::mutex> lk(shared.m);
    shared.pending = t;
    shared.dirty = true;   // mark for re-eval — without this the scene only commits
                           // the FIRST frame (commit = isDirty || !committedOnce), so
                           // Ctrl+E edits were silently ignored in retained mode
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
