// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "FluxusComponent.h"
#include "FluxusScene.h"
#include "ImguiOverlay.h"
#include "IScriptHost.h"
#include "AudioHost.h"
#include "MidiHost.h"
#include "OscHost.h"
#include "HandHost.h"
#include "ControlServer.h"
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

  // Also accept keyboard focus at the component level: when a sketch calls
  // (hide-editor), the editor can't receive keys, so the component itself does
  // (feeds script hotkeys via Component::keyPressed -> flux_set_key).
  setWantsKeyboardFocus(true);

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
  midi = makeJuceMidiHost();     // MIDI in -> (midi-cc)/(midi-ccn)/(midi-note)
  midi->start();
  osc = makeJuceOscHost();       // OSC in/out -> (osc-source)/(osc)/(osc-send)
  osc->start();
  hands = makeHandHost();        // hand tracking -> (hand-tracking #t)/(hand h j); opt-in
  hands->start();                // installs the enable bridge only (no camera yet)

  // localhost control server (remote/MCP live-coding). Off unless a port is set:
  // export FLUXUS_CONTROL_PORT=8020 before launch, then point the MCP server at it.
  if (const char* e = std::getenv("FLUXUS_CONTROL_PORT")) {
    const int port = std::atoi(e);
    if (port > 0)
      control = std::make_unique<ControlServer>(
        &shared,
        [this](std::string src) {                       // load+run on the UI thread
          juce::MessageManager::callAsync(
            [this, src] { loadScript(juce::String::fromUTF8(src.c_str())); });
        },
        port);
  }
}

FluxusComponent::~FluxusComponent() {
  if (audio) audio->stop();
  if (midi)  midi->stop();
  if (osc)   osc->stop();
  if (hands) hands->stop();
  stopTimer();
  ctx.detach();
}

void FluxusComponent::newOpenGLContextCreated() {
  scene = std::make_unique<FluxusScene>(&shared, makeHost());
  scene->init();
  tweaks = std::make_unique<ImguiOverlay>();
  tweaks->init();
  tweaks->setVisible(tweaksVisible);
}

void FluxusComponent::openGLContextClosing() {
  tweaks.reset();
  scene.reset();
}

void FluxusComponent::renderOpenGL() {
  if (!scene) return;
  const float s = (float) ctx.getRenderingScale();
  scene->setResolution((int) (getWidth() * s), (int) (getHeight() * s));
  scene->renderFrame();

  // after the scene (and its post/NTSC passes), so the panel isn't fed through
  // the CRT filter or captured by screenshots/exports.
  if (tweaks) {
    tweaks->setDisplay(getWidth(), getHeight(), s);
    tweaks->render();
  }
}

void FluxusComponent::loadScript(const juce::String& text) {
  code.setText(text, juce::dontSendNotification);
  // A different sketch brings its own tweaks. Only here, not in pushScript: a
  // plain Ctrl+E re-eval must KEEP the values you just dialled in.
  flux_tweak_clear();
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

bool FluxusComponent::handleKey(const juce::KeyPress& key) {
  if (isEvalKey(key)) { pushScript(); return true; }   // consume, don't type it
  if (auto c = key.getTextCharacter()) flux_set_key((int) c);  // expose to scripts (key-poll)
  return false;                                        // everything else = normal editing
}

// KeyListener path: the editor `code` has keyboard focus and forwards its keys here.
bool FluxusComponent::keyPressed(const juce::KeyPress& key, juce::Component*) {
  return handleKey(key);
}

// Component path: when the editor is hidden ((hide-editor)) it can't hold keyboard
// focus, so the component grabs focus itself (see setEditorVisible) and receives
// keys here — otherwise script hotkeys like V/R would go nowhere.
bool FluxusComponent::keyPressed(const juce::KeyPress& key) {
  return handleKey(key);
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

  // script-driven tweak-panel visibility ((show-tweaks)/(hide-tweaks))
  int tv = 0;
  if (flux_get_tweaks_visible(&tv) && (bool) tv != tweaksVisible) setTweaksVisible(tv != 0);

  juce::String err;
  { std::lock_guard<std::mutex> lk(shared.m); err = juce::String(shared.lastError); }
  if (err != lastShown) {
    lastShown = err;
    console.setText(err.isEmpty() ? juce::String("; ok") : err.trimStart(), juce::dontSendNotification);
  }
}

// The tweak panel sees every mouse event first; when the pointer is over it, the
// event stops there so dragging a slider doesn't also orbit the camera.
bool FluxusComponent::forwardToTweaks(const juce::MouseEvent& e) {
  if (!tweaks) return false;
  tweaks->onMouseMove(e.position.x, e.position.y);
  return tweaks->wantsMouse();
}

void FluxusComponent::mouseDown(const juce::MouseEvent& e) {
  lastMouse = e.position;
  if (tweaks) tweaks->onMouseButton(0, true);
  if (forwardToTweaks(e)) return;
  flux_set_mouse(e.position.x, e.position.y, 1);
  // Clicking the art area moves keyboard focus HERE. With focus in the code
  // TextEditor, plain letters are consumed as typing before our KeyListener sees
  // them, so script hotkeys ((key-poll) — e.g. R) only fire via this component's
  // own keyPressed. Click canvas = hotkeys; click editor = typing.
  grabKeyboardFocus();
}
void FluxusComponent::mouseUp(const juce::MouseEvent& e) {
  if (tweaks) tweaks->onMouseButton(0, false);
  forwardToTweaks(e);
}
void FluxusComponent::mouseMove(const juce::MouseEvent& e) {
  forwardToTweaks(e);   // keeps hover (and so wantsMouse) live between clicks
}
void FluxusComponent::mouseDrag(const juce::MouseEvent& e) {
  auto d = e.position - lastMouse;
  lastMouse = e.position;
  if (forwardToTweaks(e)) return;
  flux_camera_drag(d.x, -d.y);           // drag to orbit
  flux_set_mouse(e.position.x, e.position.y, 1);
}
void FluxusComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
  if (tweaks) tweaks->onMouseWheel(w.deltaX, w.deltaY);
  if (forwardToTweaks(e)) return;
  flux_camera_zoom(-w.deltaY * 8.0);     // wheel to dolly
}

void FluxusComponent::setTweaksVisible(bool v) {
  tweaksVisible = v;
  if (tweaks) tweaks->setVisible(v);
  // Push it back into the shared state too, so the G key / menu and a script's
  // (show-tweaks)/(hide-tweaks) agree — otherwise the timer poll below would
  // immediately undo whatever the user just pressed.
  flux_set_tweaks_visible(v ? 1 : 0);
}

void FluxusComponent::setEditorVisible(bool v) {
  editorVisible = v;
  code.setVisible(v);
  // Keyboard focus follows visibility: a hidden editor can't hold focus, so the
  // component takes it (its Component::keyPressed then feeds script hotkeys). When
  // shown again, hand focus back to the editor for normal typing.
  if (v) code.grabKeyboardFocus();
  else   grabKeyboardFocus();
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
