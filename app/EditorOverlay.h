#pragma once
#include <string>
#include <memory>
#include <mutex>

namespace fluxus { class GLEditor; }

// JUCE-free handle around fluxus's own GL text editor (GLEditor + PolyGlyph).
// Kept in its own TU so the system-GL symbols GLEditor uses don't clash with
// JUCE's juce::gl namespace. Thread-safe: keys arrive on the message thread,
// render() runs on the GL thread.
class EditorOverlay {
public:
  EditorOverlay();
  ~EditorOverlay();

  void init(const std::string& fontPath, const std::string& startText);
  void reshape(int w, int h);
  void render();                                  // GL thread: draws text over the scene
  void handleKey(int key, int special, int mod);  // message thread
  std::string getText();
  void setText(const std::string& text);           // replace editor buffer

  // system-clipboard bridge (message thread)
  std::string getSelection();     // selected text, or "" if nothing selected
  bool        hasSelection();
  void        insertText(const std::string& text); // type text in at the cursor
  void        cutSelection();      // delete the current selection
  void        selectAll();         // Cmd/Ctrl+A
  void        jumpWord(int dir);   // Ctrl+Left/Right (dir <0 left, >=0 right)

private:
  std::unique_ptr<fluxus::GLEditor> ed;
  std::mutex m;
};
