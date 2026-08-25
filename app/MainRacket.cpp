#include <juce_gui_extra/juce_gui_extra.h>
#include "FluxusComponent.h"
#include "RacketScriptHost.h"

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
      // starter uses build-circle-points — a REAL fluxus shapes.ss function,
      // loaded from the fluxus .ss library and driving the engine via FFI.
      const char* starter =
        "; Racket + real fluxus .ss library (randomness.ss: rndf, crndf)\n"
        "(random-seed 7)                 ; stable scatter each frame\n"
        "(background (vector 0.05 0.05 0.09))\n"
        "(rotate (vector 0 (* 12 (time)) 0))\n"
        "(for ((i (in-range 70)))\n"
        "  (with-state\n"
        "    (translate (vector (* 4 (crndf)) (* 4 (crndf)) (* 4 (crndf))))\n"
        "    (colour (vector (rndf) (rndf) (rndf)))\n"
        "    (scale (vector 0.22 0.22 0.22))\n"
        "    (build-cube)))\n";
      setContentOwned(new FluxusComponent([] { return std::make_unique<RacketScriptHost>(); }, starter), false);
      centreWithSize(1180, 720);
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

START_JUCE_APPLICATION(FluxusRacketApplication)
