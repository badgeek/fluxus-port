#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include <memory>
#include <functional>
#include "SharedScript.h"

class FluxusScene;
class IScriptHost;
struct IAudioHost;
using ScriptHostFactory = std::function<std::unique_ptr<IScriptHost>()>;

// fluxus-style host: the code floats as a TRANSPARENT overlay directly on top of
// the live 3D (like the original fluxus scratchpad), not in a side panel. The
// editor + console are children of this GL component, so JUCE paints them over
// the OpenGLContext. Editing re-runs the script live every frame.
class FluxusComponent : public juce::Component,
                        private juce::OpenGLRenderer,
                        private juce::Timer,
                        private juce::KeyListener {
public:
  explicit FluxusComponent(ScriptHostFactory makeHost, juce::String starter = {});
  ~FluxusComponent() override;

  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;

  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
  void timerCallback() override;
  bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;  // Ctrl+E / Shift+Enter = eval
  bool isEvalKey(const juce::KeyPress& key) const;
  void pushScript();

  juce::OpenGLContext ctx;
  std::unique_ptr<FluxusScene> scene;
  std::unique_ptr<IAudioHost>  audio;
  ScriptHostFactory makeHost;
  SharedScript shared;

  juce::Point<float> lastMouse;
  juce::TextEditor code;      // transparent overlay editor
  juce::TextEditor console;   // transparent status line
  juce::String lastShown { juce::String::charToString(0xffff) };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxusComponent)
};
