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
        "; Racket + the REAL fluxus building-blocks.ss (with-primitive, pdata-index-map!)\n"
        "(background (vector 0.05 0.05 0.08))\n"
        "(colour (vector 1.0 0.6 0.2))\n"
        "(rotate (vector 0 (* 20 (time)) 0))\n"
        "(with-primitive (build-torus 0.5 1.3 40 40)\n"
        "  (pdata-index-map!\n"
        "    (lambda (i p)\n"
        "      (vadd p (vector (* 0.3 (sin (+ (* i 0.15) (* 3 (time))))) 0 0)))\n"
        "    \"p\")\n"
        "  (recalc-normals))\n";
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
