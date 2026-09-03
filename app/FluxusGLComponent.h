// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_opengl/juce_opengl.h>
#include <memory>
#include <functional>
#include "SharedScript.h"

class FluxusScene;
class EditorOverlay;
class IScriptHost;
struct IAudioHost;
struct IMidiHost;
struct IOscHost;
struct IHandHost;
using ScriptHostFactory = std::function<std::unique_ptr<IScriptHost>()>;

// Variant host that uses fluxus's OWN GL text editor (GLEditor) as the code
// overlay — the code renders through the fluxus PolyGlyph font in GL, exactly
// like the original fluxus scratchpad, instead of a JUCE TextEditor.
class FluxusGLComponent : public juce::Component,
                          private juce::OpenGLRenderer,
                          private juce::Timer {
public:
  explicit FluxusGLComponent(ScriptHostFactory makeHost);
  ~FluxusGLComponent() override;

  // load a script into the GL editor and commit it (File -> Open menu)
  void loadScript(const juce::String& text);
  bool loadFile(const juce::File& f);
  juce::String getScript();                   // current GL editor text (File -> Save)
  bool loadAudio(const juce::File& f);        // File -> Load Audio (play + analyse)

  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;

  bool keyPressed(const juce::KeyPress& k) override;
  void parentHierarchyChanged() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
  void paint(juce::Graphics&) override {}
  void resized() override {}

private:
  void timerCallback() override;   // 30 Hz repaint (cap fps; see .cpp)
  juce::OpenGLContext ctx;
  std::unique_ptr<FluxusScene>   scene;
  std::unique_ptr<EditorOverlay> overlay;
  std::unique_ptr<IAudioHost>    audio;
  std::unique_ptr<IMidiHost>     midi;
  std::unique_ptr<IOscHost>      osc;
  std::unique_ptr<IHandHost>     hands;
  ScriptHostFactory makeHost;
  SharedScript shared;
  juce::Point<float> lastMouse;
  bool fontReady = false;
  bool overlayVisible = true;   // Ctrl+H toggles the code overlay

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxusGLComponent)
};
