#include "EditorOverlay.h"

// fluxus editor + system GL only in this TU (no juce_opengl)
#include "GLEditor.h"
#include "Unicode.h"

using namespace fluxus;

EditorOverlay::EditorOverlay()  = default;
EditorOverlay::~EditorOverlay() = default;

void EditorOverlay::init(const std::string& fontPath, const std::string& startText) {
  std::lock_guard<std::mutex> lk(m);
  // fluxus-green text
  GLEditor::m_TextColourRed   = 0.6f;
  GLEditor::m_TextColourGreen = 1.0f;
  GLEditor::m_TextColourBlue  = 0.6f;
  GLEditor::InitFont(string_to_wstring(fontPath));   // shared PolyGlyph
  ed = std::make_unique<GLEditor>();
  ed->SetText(string_to_wstring(startText));
}

void EditorOverlay::reshape(int w, int h) {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->Reshape((unsigned) w, (unsigned) h);
}

void EditorOverlay::render() {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->Render();
}

void EditorOverlay::handleKey(int key, int special, int mod) {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->Handle(0, key, special, 0, 0, 0, mod);
}

std::string EditorOverlay::getText() {
  std::lock_guard<std::mutex> lk(m);
  return ed ? wstring_to_string(ed->GetAllText()) : std::string();
}

void EditorOverlay::setText(const std::string& text) {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->SetText(string_to_wstring(text));
}
