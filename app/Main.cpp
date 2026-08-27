#include <juce_gui_extra/juce_gui_extra.h>
#include "FluxusComponent.h"
#include "AppMenu.h"
#include "S7ScriptHost.h"
#include <cstdlib>

// Phase-0 host: a JUCE app window whose content is the FluxusComponent (GL
// surface driving the libfluxus renderer).
class FluxusApplication : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override    { return "FluxusApp"; }
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
      // optional: FLUXUS_SCRIPT=/path/to.scm loads that file as the starter
      juce::String starter;
      if (auto* p = std::getenv("FLUXUS_SCRIPT")) {
        juce::File f(juce::String::fromUTF8(p));
        if (f.existsAsFile()) starter = f.loadFileAsString();
      }
      auto* comp = new FluxusComponent([] { return std::make_unique<S7ScriptHost>(); }, starter);
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

START_JUCE_APPLICATION(FluxusApplication)
