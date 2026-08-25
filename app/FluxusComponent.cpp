#include "FluxusComponent.h"
#include "FluxusScene.h"
#include "IScriptHost.h"

namespace {
juce::Font monoFont(float h) {
  return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain);
}
const char* kStarter =
  "; fluxus-style live coding (s7) - edit, then Ctrl+E (or Shift+Enter) to run\n"
  "(background (vector 0.08 0.09 0.12))\n"
  "\n"
  "(colour (vector 0.9 0.5 0.2))\n"
  "(rotate (vector (* 25 (time)) (* 40 (time)) 0))\n"
  "(build-cube)\n"
  "\n"
  "(with-state\n"
  "  (translate (vector 2.5 0 0))\n"
  "  (colour (vector 0.3 0.7 1.0))\n"
  "  (scale (vector 0.6 0.6 0.6))\n"
  "  (rotate (vector 0 (* -60 (time)) 0))\n"
  "  (build-sphere 16 16))\n";
}

FluxusComponent::FluxusComponent(ScriptHostFactory mh, juce::String starter)
  : makeHost(std::move(mh)) {
  const juce::String starterText = starter.isNotEmpty() ? starter : juce::String(kStarter);
  // transparent overlay editor (fluxus look: green mono text on the live scene)
  code.setMultiLine(true, false);
  code.setReturnKeyStartsNewLine(true);
  code.setFont(monoFont(15.0f));
  code.setColour(juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
  code.setColour(juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
  code.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
  code.setColour(juce::TextEditor::textColourId,           juce::Colour(0xff9dff9d));
  code.setColour(juce::CaretComponent::caretColourId,      juce::Colours::white);
  code.setColour(juce::TextEditor::highlightColourId,      juce::Colour(0x4066ff66));
  code.setOpaque(false);
  code.setText(starterText, juce::dontSendNotification);
  code.addKeyListener(this);   // Ctrl+E / Shift+Enter commits; edits alone don't run
  addAndMakeVisible(code);

  console.setMultiLine(false);
  console.setReadOnly(true);
  console.setFont(monoFont(13.0f));
  console.setColour(juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
  console.setColour(juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
  console.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
  console.setColour(juce::TextEditor::textColourId,           juce::Colour(0xffff8f6b));
  console.setOpaque(false);
  addAndMakeVisible(console);

  ctx.setRenderer(this);
  ctx.setComponentPaintingEnabled(true);   // paint the overlay OVER the GL
  ctx.setContinuousRepainting(true);
  ctx.attachTo(*this);

  pushScript();
  startTimerHz(10);
}

FluxusComponent::~FluxusComponent() {
  stopTimer();
  ctx.detach();
}

void FluxusComponent::newOpenGLContextCreated() {
  scene = std::make_unique<FluxusScene>(&shared, makeHost());
  scene->init();
}

void FluxusComponent::openGLContextClosing() {
  scene.reset();
}

void FluxusComponent::renderOpenGL() {
  if (!scene) return;
  const float s = (float) ctx.getRenderingScale();
  scene->setResolution((int) (getWidth() * s), (int) (getHeight() * s));
  scene->renderFrame();
}

void FluxusComponent::pushScript() {
  std::lock_guard<std::mutex> lk(shared.m);
  shared.pending = code.getText().toStdString();
  shared.dirty = true;
}

bool FluxusComponent::isEvalKey(const juce::KeyPress& key) const {
  auto m = key.getModifiers();
  const int kc = key.getKeyCode();
  const bool ctrlE  = (m.isCtrlDown() || m.isCommandDown()) && (kc == (int) 'E' || kc == (int) 'e');
  const bool shiftEnter = m.isShiftDown() && kc == juce::KeyPress::returnKey;
  return ctrlE || shiftEnter;
}

bool FluxusComponent::keyPressed(const juce::KeyPress& key, juce::Component*) {
  if (isEvalKey(key)) { pushScript(); return true; }   // consume, don't type it
  return false;                                        // everything else = normal editing
}

void FluxusComponent::timerCallback() {
  juce::String err;
  { std::lock_guard<std::mutex> lk(shared.m); err = juce::String(shared.lastError); }
  if (err != lastShown) {
    lastShown = err;
    console.setText(err.isEmpty() ? juce::String("; ok") : err.trimStart(), juce::dontSendNotification);
  }
}

void FluxusComponent::resized() {
  auto r = getLocalBounds();
  console.setBounds(r.removeFromBottom(22).reduced(8, 2));
  // editor overlays the left portion so the 3D stays visible on the right
  code.setBounds(r.removeFromLeft(juce::roundToInt(getWidth() * 0.5f)).reduced(8, 6));
}
