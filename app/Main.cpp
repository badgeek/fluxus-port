#include <juce_gui_extra/juce_gui_extra.h>
#include "FluxusComponent.h"
#include "S7ScriptHost.h"

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
      setContentOwned(new FluxusComponent([] { return std::make_unique<S7ScriptHost>(); }), false);
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

START_JUCE_APPLICATION(FluxusApplication)
