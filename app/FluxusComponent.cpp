#include <cstdio>
#include <vector>
#include "FluxusComponent.h"
#include "FluxusScene.h"
#include "IScriptHost.h"
#include "AudioHost.h"
#include "FluxusCommands.h"   // mouse/camera

namespace {
juce::Font monoFont(float h) {
  return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain);
}
const char* kStarter =
  "; empty sketch — write code, then Ctrl+E (or Shift+Enter) to run.\n"
  "; File -> Open… to load an example.\n";
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
  code.setScrollbarsShown(false);   // no scrollbar line over the scene
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
  ctx.setContinuousRepainting(false);   // cap the frame rate via the timer below
                                        // (immediate-mode rebuilds every frame, so
                                        // vsync-continuous pegs a CPU core)
  { juce::OpenGLPixelFormat pf; pf.multisamplingLevel = 4; ctx.setPixelFormat(pf); }
  ctx.setMultisamplingEnabled(true);       // MSAA for smoother edges
  ctx.attachTo(*this);

  pushScript();
  startTimerHz(30);   // drives repaint (see timerCallback) + console/resize poll

  audio = makeJuceAudioHost();   // CoreAudio mic -> FFT bands for (gh n)/(gain)
  audio->start();
}

FluxusComponent::~FluxusComponent() {
  if (audio) audio->stop();
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

void FluxusComponent::loadScript(const juce::String& text) {
  code.setText(text, juce::dontSendNotification);
  pushScript();   // commit + run immediately (same as Ctrl+E)
}

bool FluxusComponent::loadFile(const juce::File& f) {
  if (!f.existsAsFile()) return false;
  loadScript(f.loadFileAsString());
  return true;
}

juce::String FluxusComponent::getScript() const { return code.getText(); }

bool FluxusComponent::loadAudio(const juce::File& f) {
  return audio && f.existsAsFile()
      && audio->loadAudioFile(f.getFullPathName().toRawUTF8());
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
  ctx.triggerRepaint();   // ~30 fps render (enough for typing + slow rotation)

  // apply a script-requested window resize ((set-window-size w h)) — sets the GL
  // content to exactly w x h (e.g. 1080x1920 for a vertical IG frame).
  int rw = 0, rh = 0;
  if (flux_take_window_request(&rw, &rh) && rw > 0 && rh > 0)
    if (auto* win = findParentComponentOfClass<juce::ResizableWindow>())
      win->setContentComponentSize(rw, rh);

  // script-driven editor visibility ((show-editor)/(hide-editor)/(editor-full-width)).
  // Only re-layout when the desired state actually changed.
  int ev = 0, ef = 0;
  if (flux_get_editor(&ev, &ef)) {
    if ((bool) ev != editorVisible)   setEditorVisible(ev != 0);
    if ((bool) ef != editorFullWidth) setEditorFullWidth(ef != 0);
  }

  juce::String err;
  { std::lock_guard<std::mutex> lk(shared.m); err = juce::String(shared.lastError); }
  if (err != lastShown) {
    lastShown = err;
    console.setText(err.isEmpty() ? juce::String("; ok") : err.trimStart(), juce::dontSendNotification);
  }
}

void FluxusComponent::mouseDown(const juce::MouseEvent& e) {
  lastMouse = e.position;
  flux_set_mouse(e.position.x, e.position.y, 1);
}
void FluxusComponent::mouseDrag(const juce::MouseEvent& e) {
  auto d = e.position - lastMouse;
  lastMouse = e.position;
  flux_camera_drag(d.x, -d.y);           // drag to orbit
  flux_set_mouse(e.position.x, e.position.y, 1);
}
void FluxusComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
  flux_camera_zoom(-w.deltaY * 8.0);     // wheel to dolly
}

void FluxusComponent::setEditorVisible(bool v) {
  editorVisible = v;
  code.setVisible(v);
  resized();
}
void FluxusComponent::setEditorFullWidth(bool f) {
  editorFullWidth = f;
  resized();
}

void FluxusComponent::setRecording(bool on) {
  recording = on;
  if (on) {
    // fresh PNG sequence in ~/Movies/fluxus-frames (retina res, i.e. 1080x1920
    // for a 540x960 window). Captures the GL scene only — the editor overlay is
    // a JUCE child painted over GL and is NOT in the framebuffer.
    recDir = juce::File::getSpecialLocation(juce::File::userMoviesDirectory)
               .getChildFile("fluxus-frames");
    recDir.createDirectory();
    for (auto& f : recDir.findChildFiles(juce::File::findFiles, false, "*.png"))
      f.deleteFile();
    flux_set_recording(1, recDir.getFullPathName().toRawUTF8());
    std::fprintf(stderr, "[rec] recording frames -> %s\n",
                 recDir.getFullPathName().toRawUTF8());
  } else {
    flux_set_recording(0, nullptr);
    std::fprintf(stderr,
        "[rec] stopped. Encode to video with:\n"
        "  ffmpeg -framerate 30 -i %s/f%%05d.png -c:v libx264 -crf 12 "
        "-pix_fmt yuv420p deck.mp4\n",
        recDir.getFullPathName().toRawUTF8());
  }
}

void FluxusComponent::setExportAudioFile(const juce::File& f) {
  expAudioFile = f;
  std::fprintf(stderr, "[export] soundtrack = %s\n", f.getFullPathName().toRawUTF8());
}

void FluxusComponent::setExport(bool on) {
  exporting = on;
  const int fps = 60;
  if (on) {
    // Offline, frame-locked render at 60fps piped straight to ffmpeg. The app runs
    // SLOWER than realtime while exporting (each frame waits for the grab+encode),
    // but the output is perfectly smooth at 60fps. Toggle off to finalise the MP4.
    expFile = juce::File::getSpecialLocation(juce::File::userMoviesDirectory)
                .getChildFile("fluxus-export.mp4");
    // freeze the live mic so the exported audio state is deterministic
    if (audio) audio->stop();
    // optional soundtrack: pre-analyse it into per-frame features (reactivity) and
    // mux it into the output. Synced because feature[f] and audio-time f/fps match.
    if (expAudioFile.existsAsFile()) {
      std::vector<float> gains, bands; int nb = 0, nF = 0;
      if (analyzeAudioFileToFrames(expAudioFile.getFullPathName().toRawUTF8(),
                                   fps, gains, bands, nb, nF)) {
        flux_export_audio_load(gains.data(), bands.data(), nF, nb);
        flux_set_export_audio(expAudioFile.getFullPathName().toRawUTF8());
        std::fprintf(stderr, "[export] audio-reactive: analysed %d frames of %s\n",
                     nF, expAudioFile.getFileName().toRawUTF8());
      }
    }
    flux_set_export(1, expFile.getFullPathName().toRawUTF8(), fps);
    std::fprintf(stderr, "[export] rendering (frame-locked %dfps) -> %s\n",
                 fps, expFile.getFullPathName().toRawUTF8());
  } else {
    flux_set_export(0, nullptr, 0);
    flux_export_audio_clear();
    if (audio) audio->start();          // live mic back
    std::fprintf(stderr, "[export] stopped -> %s\n",
                 expFile.getFullPathName().toRawUTF8());
  }
}

void FluxusComponent::resized() {
  auto r = getLocalBounds();
  console.setBounds(r.removeFromBottom(22).reduced(8, 2));
  // editor overlays the left half (or full width) so the 3D stays visible
  const int w = editorFullWidth ? getWidth() : juce::roundToInt(getWidth() * 0.5f);
  code.setBounds(r.removeFromLeft(w).reduced(8, 6));
}
