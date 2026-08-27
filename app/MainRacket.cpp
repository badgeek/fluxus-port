#include <juce_gui_extra/juce_gui_extra.h>
#include "FluxusComponent.h"
#include "AppMenu.h"
#include "RacketScriptHost.h"
#include <cstdlib>

// Variant app: the fluxus engine driven by REAL Racket (CS) via ffi/unsafe.
// Same transparent code-on-scene overlay as FluxusApp, but the script host is
// Racket instead of s7. Ctrl+E / Shift+Enter to run.
class FluxusRacketApplication : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override    { return "FluxusRacketApp"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  bool moreThanOneInstanceAllowed() override          { return true; }

  void initialise(const juce::String&) override {
    mainWindow = std::make_unique<MainWindow>(getApplicationName());
  }
  void shutdown() override { mainWindow = nullptr; }
  void systemRequestedQuit() override { quit(); }

  class MainWindow : public juce::DocumentWindow {
  public:
    explicit MainWindow(const juce::String& name)
      : DocumentWindow(name, juce::Colours::black, DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      // first load: an empty sketch (File -> Open… loads an example). Only the
      // FLUXUS_SCRIPT env var overrides it — no implicit dotfile, so a plain
      // launch always starts empty.
      const char* starter =
        "; empty sketch (real Racket) — write code, then Ctrl+E to run.\n"
        "; File -> Open… to load an example.\n";
      juce::String starterStr(starter);
      if (auto* p = std::getenv("FLUXUS_SCRIPT")) {
        juce::File sf(juce::String::fromUTF8(p));
        if (sf.existsAsFile()) starterStr = sf.loadFileAsString();
      }
      auto* comp = new FluxusComponent([] { return std::make_unique<RacketScriptHost>(); }, starterStr);
      setContentOwned(comp, false);
      menu = std::make_unique<FluxusMenu>(                        // File -> Open / Save
          [comp](const juce::File& f) { comp->loadFile(f); },
          [comp] { return comp->getScript(); });
      menu->setViewCallbacks(                                     // View -> editor
          [comp](bool v) { comp->setEditorVisible(v); },
          [comp](bool f) { comp->setEditorFullWidth(f); },
          comp->isEditorVisible(), comp->isEditorFullWidth());
      menu->setAudioCallback([comp](const juce::File& f) { comp->loadAudio(f); });
      menu->attach(this);
      centreWithSize(1180, 720);
      setResizable(true, false);
      setVisible(true);
    }
    ~MainWindow() override { FluxusMenu::detach(); }
    void closeButtonPressed() override {
      JUCEApplication::getInstance()->systemRequestedQuit();
    }
  private:
    std::unique_ptr<FluxusMenu> menu;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(FluxusRacketApplication)
