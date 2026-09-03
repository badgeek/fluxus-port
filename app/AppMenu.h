// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include "FluxusCommands.h"   // flux_set_aspect

// Shared File + Aspect menus for the apps. File: Open… loads a script via the
// load callback; Save / Save As… write the current editor text back to disk.
// Aspect: presets resize the window's content to a target aspect ratio (IG Story
// 9:16, Square, …) and lock the engine's render AR so it letterboxes on further
// resize. Add a preset by adding one row to kAspects below. macOS: native menu.
class FluxusMenu : public juce::MenuBarModel {
public:
  using LoadFn   = std::function<void(const juce::File&)>; // set editor text + run
  using TextFn   = std::function<juce::String()>;          // current editor text
  using ToggleFn = std::function<void(bool)>;              // view toggles
  FluxusMenu(LoadFn onOpen, TextFn getText)
    : load(std::move(onOpen)), text(std::move(getText)) {}

  // opt-in: File -> Load Audio… (play an ogg/wav/… and drive the visuals with it)
  void setAudioCallback(LoadFn onOpenAudio) { audioLoad = std::move(onOpenAudio); }

  // opt-in View menu (JUCE-editor apps only): show/hide + full-width editor.
  void setViewCallbacks(ToggleFn showEditor, ToggleFn fullWidth,
                        bool initVisible, bool initFullWidth) {
    onShowEditor = std::move(showEditor);
    onFullWidth  = std::move(fullWidth);
    editorVisible = initVisible; editorFull = initFullWidth;
    hasView = true;
  }

  // opt-in View -> Record Frames: dump the GL scene as a PNG sequence for video.
  void setRecordCallback(ToggleFn onRec, bool init = false) {
    onRecord = std::move(onRec); recording = init; hasView = true;
  }

  // opt-in View -> Export: offline frame-locked 60fps render straight to MP4.
  void setExportCallback(ToggleFn onExp, bool init = false) {
    onExport = std::move(onExp); exporting = init; hasView = true;
  }
  // opt-in View -> Set Export Audio…: pick a soundtrack the export reacts to + muxes.
  void setExportAudioCallback(LoadFn onPick) { onSetExportAudio = std::move(onPick); hasView = true; }

  // opt-in View -> Show Tweaks: the ImGui slider panel over any (tweak …) vars.
  // `isOn` reads the live state rather than caching it — the G key and a script's
  // (show-tweaks) can flip the panel behind the menu's back.
  using StateFn = std::function<bool()>;
  void setTweaksCallback(ToggleFn onShow, StateFn isOn) {
    onShowTweaks = std::move(onShow); tweaksState = std::move(isOn); hasView = true;
  }

  // --- aspect-ratio presets: label + the content pixel size to resize to.
  //     ratio<=0 (the first row) unlocks / restores free resizing. -------------
  struct Aspect { const char* name; int w, h; };
  static constexpr Aspect kAspects[] = {
    { "Free (unlock)",       0,    0   },
    { "Story  9:16",         506,  900 },
    { "Portrait  4:5",       720,  900 },
    { "Square  1:1",         800,  800 },
    { "Landscape  16:9",     1152, 648 },
    { "Classic  4:3",        900,  675 },
  };

  juce::StringArray getMenuBarNames() override {
    juce::StringArray n { "File", "Aspect" };
    if (hasView) n.add("View");
    return n;
  }

  juce::PopupMenu getMenuForIndex(int, const juce::String& name) override {
    juce::PopupMenu m;
    if (name == "File") {
      m.addItem(kOpen,   "Open…");
      if (! examples().isEmpty()) m.addSubMenu("Examples", examplesMenu());
      m.addSeparator();
      m.addItem(kSave,   "Save",   currentFile != juce::File());
      m.addItem(kSaveAs, "Save As…");
      if (audioLoad) { m.addSeparator(); m.addItem(kLoadAudio, "Load Audio…"); }
    } else if (name == "Aspect") {
      for (int i = 0; i < (int) (sizeof(kAspects) / sizeof(kAspects[0])); ++i)
        m.addItem(kAspectBase + i, kAspects[i].name, true, i == currentAspect);
    } else if (name == "View") {
      m.addItem(kShowEditor, "Show Editor",       true, editorVisible);
      m.addItem(kFullWidth,  "Editor Full Width", true, editorFull);
      if (onShowTweaks) m.addItem(kShowTweaks, "Show Tweaks", true, tweaksOn());
      if (onRecord) {
        m.addSeparator();
        m.addItem(kRecord, recording ? "Stop Recording" : "Record Frames", true, recording);
      }
      if (onSetExportAudio) m.addItem(kSetExportAudio, "Set Export Audio…");
      if (onExport) {
        m.addItem(kExport, exporting ? "Stop Export" : "Export 60fps (offline)", true, exporting);
      }
    }
    return m;
  }

  void menuItemSelected(int id, int) override {
    if (id >= kExampleBase) {
      const auto& e = examples();
      const int i = id - kExampleBase;
      if (load && i < e.size() && e[i].existsAsFile()) { currentFile = e[i]; load(e[i]); }
      return;
    }
    if (id >= kAspectBase && id < kViewBase) { setAspect(id - kAspectBase); return; }
    switch (id) {
      case kOpen:   openFile();  break;
      case kSave:   save();      break;
      case kSaveAs: saveAs();    break;
      case kLoadAudio: openAudio(); break;
      case kShowEditor: editorVisible = !editorVisible; if (onShowEditor) onShowEditor(editorVisible); break;
      case kFullWidth:  editorFull    = !editorFull;    if (onFullWidth)  onFullWidth(editorFull);     break;
      case kShowTweaks: if (onShowTweaks) onShowTweaks(!tweaksOn()); break;
      case kRecord:     recording     = !recording;     if (onRecord)     onRecord(recording);         break;
      case kExport:     exporting     = !exporting;     if (onExport)     onExport(exporting);         break;
      case kSetExportAudio: pickExportAudio(); break;
      default: break;
    }
  }

  void attach(juce::DocumentWindow* w) {
    win = w;
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
   #else
    if (win) win->setMenuBar(this);
   #endif
  }
  static void detach() {
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
   #endif
  }

private:
  enum { kOpen = 1, kSave, kSaveAs, kLoadAudio, kAspectBase = 100, kViewBase = 200,
         kShowEditor = kViewBase, kFullWidth, kRecord, kExport, kSetExportAudio,
         kShowTweaks,
         kExampleBase = 1000 };

  // --- bundled examples ------------------------------------------------------
  // A packaged .app carries the upstream fluxus sketches in
  //   <App>.app/Contents/Resources/examples/
  // (see cmake/bundle_examples.cmake). Dev builds have no Resources/examples, so
  // the menu falls back to the repo's vendor/fluxus/examples when the binary is
  // still inside the build tree — and simply hides the submenu if neither exists.
  static juce::File examplesDir() {
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    // <App>.app/Contents/MacOS/<exe> -> <App>.app/Contents/Resources/examples
    const auto bundled = exe.getParentDirectory().getSiblingFile("Resources")
                            .getChildFile("examples");
    if (bundled.isDirectory()) return bundled;
    // dev: walk up out of build/<Target>_artefacts/<Config>/<App>.app/Contents/MacOS
    for (auto d = exe.getParentDirectory(); d != juce::File() && d.getParentDirectory() != d;
         d = d.getParentDirectory()) {
      const auto v = d.getChildFile("vendor/fluxus/examples");
      if (v.isDirectory()) return v;
    }
    return {};
  }

  // Scanned once — the set cannot change while the app runs.
  static const juce::Array<juce::File>& examples() {
    static const juce::Array<juce::File> found = [] {
      juce::Array<juce::File> a;
      const auto dir = examplesDir();
      if (dir.isDirectory()) {
        dir.findChildFiles(a, juce::File::findFiles, false, "*.scm");
        a.sort();   // by full path == by name within one dir
      }
      return a;
    }();
    return found;
  }

  // Flat list is long (~60), so break it into alphabetical chunks.
  juce::PopupMenu examplesMenu() const {
    const auto& e = examples();
    juce::PopupMenu m;
    constexpr int kChunk = 20;
    if (e.size() <= kChunk) {
      for (int i = 0; i < e.size(); ++i)
        m.addItem(kExampleBase + i, e[i].getFileNameWithoutExtension());
      return m;
    }
    for (int start = 0; start < e.size(); start += kChunk) {
      const int end = juce::jmin(start + kChunk, e.size());
      juce::PopupMenu sub;
      for (int i = start; i < end; ++i)
        sub.addItem(kExampleBase + i, e[i].getFileNameWithoutExtension());
      m.addSubMenu(e[start].getFileNameWithoutExtension().substring(0, 1).toUpperCase()
                     + " – " + e[end - 1].getFileNameWithoutExtension().substring(0, 1).toUpperCase(),
                   sub);
    }
    return m;
  }

  void openAudio() {
    chooser = std::make_unique<juce::FileChooser>(
        "Load audio for the visuals", juce::File(), "*.ogg;*.wav;*.mp3;*.flac;*.aif;*.aiff");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
      const auto f = fc.getResult();
      if (audioLoad && f.existsAsFile()) audioLoad(f);
    });
  }

  void pickExportAudio() {
    chooser = std::make_unique<juce::FileChooser>(
        "Soundtrack the export reacts to", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
      const auto f = fc.getResult();
      if (onSetExportAudio && f.existsAsFile()) onSetExportAudio(f);
    });
  }

  void setAspect(int i) {
    currentAspect = i;
    const auto& a = kAspects[i];
    if (a.w <= 0 || a.h <= 0) { flux_set_aspect(0.0); return; }   // Free = unlock
    if (win) win->setContentComponentSize(a.w, a.h);             // window -> the AR
    flux_set_aspect((double) a.w / a.h);                         // engine letterbox lock
  }

  void openFile() {
    chooser = std::make_unique<juce::FileChooser>(
        "Open Fluxus script",
        currentFile != juce::File() ? currentFile.getParentDirectory() : examplesDir(),
        "*.scm;*.ss;*.scheme");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
      const auto f = fc.getResult();
      if (load && f.existsAsFile()) { currentFile = f; load(f); }
    });
  }

  void save() {
    if (currentFile == juce::File()) { saveAs(); return; }
    writeTo(currentFile);
  }

  void saveAs() {
    chooser = std::make_unique<juce::FileChooser>(
        "Save Fluxus script",
        currentFile != juce::File() ? currentFile
                                    : juce::File::getSpecialLocation(
                                          juce::File::userHomeDirectory).getChildFile("sketch.scm"),
        "*.scm;*.ss;*.scheme");
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
      auto f = fc.getResult();
      if (f == juce::File()) return;                       // cancelled
      if (f.getFileExtension().isEmpty()) f = f.withFileExtension("scm");
      writeTo(f);
    });
  }

  void writeTo(const juce::File& f) {
    if (!text) return;
    if (f.replaceWithText(text())) currentFile = f;         // remember for next Save
  }

  LoadFn load;
  TextFn text;
  LoadFn audioLoad;
  bool tweaksOn() const { return tweaksState && tweaksState(); }

  ToggleFn onShowEditor, onFullWidth, onRecord, onExport, onShowTweaks;
  StateFn  tweaksState;
  LoadFn   onSetExportAudio;
  bool hasView = false, editorVisible = true, editorFull = false, recording = false, exporting = false;
  juce::File currentFile;
  juce::DocumentWindow* win = nullptr;
  int currentAspect = 0;               // index into kAspects (0 = Free)
  std::unique_ptr<juce::FileChooser> chooser;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxusMenu)
};
