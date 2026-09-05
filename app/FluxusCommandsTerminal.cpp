// SPDX-License-Identifier: AGPL-3.0-or-later
// Terminal domain of the fluxus command layer: the (build-terminal …) primitive,
// its libvterm parser/screen and the glyph-grid mesh rebuilt from it. The vterm
// table lives here; core reaches it through terminalErase (on destroy) and the
// public flux_free_terminals (before a scene wipe).
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine + system GL only
#include "PolyPrimitive.h"
#include "State.h"
#include "dada.h"

#include <vterm.h>       // VT/ANSI parser + screen model for (build-terminal …)

#include <cmath>
#include <cstring>
#include <map>

using namespace Fluxus;

// ---- terminal (libvterm) ---------------------------------------------------
// A terminal prim is one PolyPrimitive(QUADS) with HINT_VERTCOLS so per-cell colour
// reaches builtinTexShader (texture2D * gl_Color). The mesh is a grid of quads: a
// background quad per cell (samples the atlas' reserved white cell 0 -> colour is the
// per-vertex bg), then a foreground glyph quad per non-blank cell (samples its glyph
// cell -> fg colour * coverage), nudged toward the camera so it draws over the bg.
// One draw call. Rebuilt from the libvterm screen on every (terminal-draw).
namespace {
// terminal primitives: grid prim -> its libvterm parser + screen for (build-terminal)
// & friends. The prim renders a glyph-atlas quad grid rebuilt from the screen state.
struct TerminalState { VTerm* vt = nullptr; VTermScreen* vs = nullptr; int cols = 0, rows = 0;
                       int shape = 0; float radius = 8.0f;      // shape: 0 flat, 1 sphere
                       float bgAlpha = 1.0f; };                  // <=0 skips bg (see-through)
std::map<Primitive*, TerminalState> g_terminals;

const float kTermPitchX = 0.5f, kTermPitchY = 0.9f;

TerminalState* grabbedTerminal() {
  auto it = g_terminals.find(g_ctx.grabbed);
  return it == g_terminals.end() ? nullptr : &it->second;
}

void terminalRebuild(PolyPrimitive* p, TerminalState& ts) {
  p->Clear();
  const float PX = kTermPitchX, PY = kTermPitchY;
  const float TAU = 6.2831853f, PI = 3.14159265f;
  float bs0, bt0, bs1, bt1;
  flux_glyph_cell(0, &bs0, &bt0, &bs1, &bt1);           // reserved opaque-white cell

  // place a grid corner (fractional col cf, row rf) with an outward extrude ex.
  // shape 0 = flat plane; shape 1 = wrap the grid onto a sphere (col->longitude,
  // row->latitude, radial normal) so the terminal reads as a globe/CRT ball.
  auto corner = [&](float cf, float rf, float ex, dVector& pos, dVector& nrm) {
    if (ts.shape == 1) {
      // negate longitude so increasing column runs screen-LEFT->RIGHT on the near
      // face (otherwise the parametrization mirrors the text horizontally).
      const float th = -(cf / ts.cols) * TAU, ph = (rf / ts.rows) * PI;
      const float sp = std::sin(ph), cph = std::cos(ph), sth = std::sin(th), cth = std::cos(th);
      nrm = dVector(sp * cth, cph, sp * sth);
      const float r = ts.radius + ex;
      pos = dVector(r * sp * cth, r * cph, r * sp * sth);
    } else {
      nrm = dVector(0, 0, 1);
      pos = dVector(cf * PX, -rf * PY, ex);
    }
  };
  auto emitCell = [&](int col, int row, int w, float ex, const dColour& c,
                      float s0, float t0, float s1, float t1) {
    const float cl = (float) col, cr = (float) (col + w), rt = (float) row, rb = (float) (row + 1);
    dVector p0, n0, p1, n1, p2, n2, p3, n3;
    corner(cl, rb, ex, p0, n0);  corner(cr, rb, ex, p1, n1);
    corner(cr, rt, ex, p2, n2);  corner(cl, rt, ex, p3, n3);
    p->AddVertex(dVertex(p0, n0, c, s0, t1));   // bottom-left
    p->AddVertex(dVertex(p1, n1, c, s1, t1));   // bottom-right
    p->AddVertex(dVertex(p2, n2, c, s1, t0));   // top-right
    p->AddVertex(dVertex(p3, n3, c, s0, t0));   // top-left
  };
  auto toRGB = [&](VTermColor c) {
    vterm_screen_convert_color_to_rgb(ts.vs, &c);
    return dColour(c.rgb.red / 255.f, c.rgb.green / 255.f, c.rgb.blue / 255.f, 1.f);
  };
  const float fgEx = ts.shape == 1 ? ts.radius * 0.006f : 0.001f;   // lift glyphs off the bg

  // pass A: background quads (skipped entirely when bgAlpha<=0 so you can see THROUGH
  // the mesh — e.g. through a sphere to the glyphs on its far side).
  if (ts.bgAlpha > 0.004f)
    for (int row = 0; row < ts.rows; ++row)
      for (int col = 0; col < ts.cols; ) {
        VTermPos pos; pos.row = row; pos.col = col;
        VTermScreenCell cell; vterm_screen_get_cell(ts.vs, pos, &cell);
        const int w = cell.width > 0 ? cell.width : 1;
        VTermColor bg = cell.attrs.reverse ? cell.fg : cell.bg;
        dColour bgc = toRGB(bg); bgc.a = ts.bgAlpha;
        emitCell(col, row, w, 0.0f, bgc, bs0, bt0, bs1, bt1);
        col += w;
      }
  // pass B: foreground glyph quads (lifted off the bg toward the viewer/outward)
  for (int row = 0; row < ts.rows; ++row)
    for (int col = 0; col < ts.cols; ) {
      VTermPos pos; pos.row = row; pos.col = col;
      VTermScreenCell cell; vterm_screen_get_cell(ts.vs, pos, &cell);
      const int w = cell.width > 0 ? cell.width : 1;
      const uint32_t cp = cell.chars[0];
      if (cp != 0 && cp != 32) {
        VTermColor fg = cell.attrs.reverse ? cell.bg : cell.fg;
        float s0, t0, s1, t1; flux_glyph_cell(cp, &s0, &t0, &s1, &t1);
        emitCell(col, row, w, fgEx, toRGB(fg), s0, t0, s1, t1);
      }
      col += w;
    }
  p->BumpPDataVersion();   // vertex count changed -> re-upload the VBO
}
} // namespace

// (destroy id) on a terminal prim: free its parser and drop the entry, else the
// vterm leaks and a stale Primitive* key survives.
void terminalErase(Primitive* p) {
  auto it = g_terminals.find(p);
  if (it == g_terminals.end()) return;
  if (it->second.vt) vterm_free(it->second.vt);
  g_terminals.erase(it);
}

extern "C" {

int flux_build_terminal(int cols, int rows) {
  if (cols < 1) cols = 1; if (rows < 1) rows = 1;
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  int id = addPrim(p);
  State* s = p->GetState();
  s->Textures[0] = flux_glyph_atlas_texture();
  setStateShader(s, builtinTexShader());
  s->Hints |= HINT_VERTCOLS | HINT_UNLIT;
  s->Cull = false;   // double-sided: a sphere-wrapped grid needs its near hemisphere
                     // to render (State defaults Cull=true, which culls it)

  VTerm* vt = vterm_new(rows, cols);              // NOTE: (rows, cols)
  vterm_set_utf8(vt, 1);
  VTermScreen* vs = vterm_obtain_screen(vt);
  VTermState*  st = vterm_obtain_state(vt);
  VTermColor fg, bg;
  vterm_color_rgb(&fg, 220, 220, 220);
  vterm_color_rgb(&bg, 0, 0, 0);
  vterm_state_set_default_colors(st, &fg, &bg);
  vterm_screen_reset(vs, 1);                       // hard reset — required before use

  TerminalState& ts = (g_terminals[p] = TerminalState{ vt, vs, cols, rows });
  terminalRebuild(p, ts);                          // initial (empty) grid
  return id;
}

void flux_terminal_write(const char* bytes) {
  TerminalState* ts = grabbedTerminal();
  if (ts && bytes) vterm_input_write(ts->vt, bytes, strlen(bytes));
}
void flux_terminal_clear(void) {
  TerminalState* ts = grabbedTerminal();
  if (ts) vterm_screen_reset(ts->vs, 1);
}
void flux_terminal_draw(void) {
  TerminalState* ts = grabbedTerminal();
  if (ts) terminalRebuild((PolyPrimitive*) g_ctx.grabbed, *ts);
}
int flux_terminal_cols(void) { TerminalState* ts = grabbedTerminal(); return ts ? ts->cols : 0; }
int flux_terminal_rows(void) { TerminalState* ts = grabbedTerminal(); return ts ? ts->rows : 0; }
// map the grabbed terminal onto a shape: mode 0 = flat plane, 1 = sphere. radius<=0
// picks a default so the sphere's circumference matches the flat grid width.
void flux_terminal_shape(int mode, double radius) {
  TerminalState* ts = grabbedTerminal();
  if (!ts) return;
  ts->shape = mode;
  ts->radius = radius > 0 ? (float) radius : ts->cols * kTermPitchX / 6.2831853f;
  terminalRebuild((PolyPrimitive*) g_ctx.grabbed, *ts);
}
// set the grabbed terminal's background opacity: 1 = opaque (default), 0 = skip the
// bg quads entirely (see-through — glyphs float on the surface), in between = tint.
void flux_terminal_bg_alpha(double a) {
  TerminalState* ts = grabbedTerminal();
  if (!ts) return;
  ts->bgAlpha = (float) a;
  terminalRebuild((PolyPrimitive*) g_ctx.grabbed, *ts);
}

// Free every terminal's vterm and drop the map. Called before a full scene wipe
// (immediate-mode Clear in FluxusScene, and the (clear) command) so the parsers
// don't leak and no stale Primitive* key survives into the next frame. Retained
// sketches keep their terminal because they build it once and never wipe the scene.
void flux_free_terminals(void) {
  for (auto& kv : g_terminals) if (kv.second.vt) vterm_free(kv.second.vt);
  g_terminals.clear();
}

} // extern "C"
