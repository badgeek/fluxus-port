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
    }
    return m;
  }

  void menuItemSelected(int id, int) override {
    if (id >= kAspectBase && id < kViewBase) { setAspect(id - kAspectBase); return; }
    switch (id) {
      case kOpen:   openFile();  break;
      case kSave:   save();      break;
      case kSaveAs: saveAs();    break;
      case kLoadAudio: openAudio(); break;
      case kShowEditor: editorVisible = !editorVisible; if (onShowEditor) onShowEditor(editorVisible); break;
      case kFullWidth:  editorFull    = !editorFull;    if (onFullWidth)  onFullWidth(editorFull);     break;
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
         kShowEditor = kViewBase, kFullWidth };

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

  void setAspect(int i) {
    currentAspect = i;
    const auto& a = kAspects[i];
    if (a.w <= 0 || a.h <= 0) { flux_set_aspect(0.0); return; }   // Free = unlock
    if (win) win->setContentComponentSize(a.w, a.h);             // window -> the AR
    flux_set_aspect((double) a.w / a.h);                         // engine letterbox lock
  }

  void openFile() {
    chooser = std::make_unique<juce::FileChooser>(
        "Open Fluxus script", juce::File(), "*.scm;*.ss;*.scheme");
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
  ToggleFn onShowEditor, onFullWidth;
  bool hasView = false, editorVisible = true, editorFull = false;
  juce::File currentFile;
  juce::DocumentWindow* win = nullptr;
  int currentAspect = 0;               // index into kAspects (0 = Free)
  std::unique_ptr<juce::FileChooser> chooser;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxusMenu)
};
