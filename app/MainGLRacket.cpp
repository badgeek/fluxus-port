#include <juce_gui_extra/juce_gui_extra.h>
#include "FluxusGLComponent.h"
#include "RacketScriptHost.h"

// Variant app: fluxus's OWN GL text editor (GLEditor/PolyGlyph) driving the
// engine via REAL Racket (CS). Closest to original fluxus: fluxus editor +
// Scheme — but on modern in-process Racket instead of the old embedded mzscheme.
class FluxusGLRacketApplication : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override    { return "FluxusGLRacketApp"; }
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
      setContentOwned(new FluxusGLComponent([] { return std::make_unique<RacketScriptHost>(); }), false);
      centreWithSize(1100, 720);
      setResizable(true, false);
      setVisible(true);
    }
    void closeButtonPressed() override {
      JUCEApplication::getInstance()->systemRequestedQuit();
    }
  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(FluxusGLRacketApplication)
