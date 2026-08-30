// SPDX-License-Identifier: AGPL-3.0-or-later
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

std::string EditorOverlay::getSelection() {
  std::lock_guard<std::mutex> lk(m);
  return (ed && ed->HasSelection()) ? wstring_to_string(ed->GetText()) : std::string();
}

bool EditorOverlay::hasSelection() {
  std::lock_guard<std::mutex> lk(m);
  return ed && ed->HasSelection();
}

void EditorOverlay::insertText(const std::string& text) {
  std::lock_guard<std::mutex> lk(m);
  if (!ed) return;
  // feed each character to the editor as if typed (replaces the selection on the
  // first char, then inserts the rest); newlines map to the RETURN key.
  for (wchar_t c : string_to_wstring(text))
    ed->Handle(0, (c == L'\n' || c == L'\r') ? 13 : (int) c, 0, 0, 0, 0, 0);
}

void EditorOverlay::cutSelection() {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->Handle(0, 24, 0, 0, 0, 0, 2);   // GLEDITOR_CUT + GLUT_ACTIVE_CTRL
}

void EditorOverlay::selectAll() {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->SelectAll();
}

void EditorOverlay::jumpWord(int dir) {
  std::lock_guard<std::mutex> lk(m);
  if (ed) ed->JumpWord(dir);
}
