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
        "; audio-reactive normal-displaced sphere (make noise!)\n"
        "(clear)\n"
        "(start-audio \"system:capture_1\" 512 44100)\n"
        "(define x (build-nurbs-sphere 10 30))\n"
        "(with-primitive x\n"
        "  (scale (vector 2 2 2))\n"
        "  (pdata-add \"ori\" \"v\")\n"
        "  (pdata-copy \"p\" \"ori\"))\n"
        "(define (vertex_1 y)\n"
        "  (with-primitive x\n"
        "    (line-width 2)\n"
        "    (hint-wire)\n"
        "    (backfacecull 1)\n"
        "    (opacity (* (gh 5) 1000))\n"
        "    (wire-opacity (* (gh 3) 100))\n"
        "    (wire-colour (vector 1 0.5 0))\n"
        "    (colour (vector 1 0 0))\n"
        "    (rotate (vector 0 (* (delta) y) 0))\n"
        "    (pdata-index-map!\n"
        "      (lambda (index val)\n"
        "        (vadd (pdata-ref \"ori\" index)\n"
        "              (vmul (pdata-ref \"n\" index) (* (gh index) 10))))\n"
        "      \"p\")))\n"
        "(define (renderchain) (vertex_1 50))\n"
        "(every-frame (renderchain))\n";
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
