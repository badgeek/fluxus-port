// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Internals shared between the FluxusCommands*.cpp domain TUs. This is NOT part
// of the binding surface: FluxusCommands.h stays the public extern "C" API both
// script hosts bind to, and nothing outside app/FluxusCommands*.cpp includes
// this. Like those TUs it must stay JUCE-free — engine + system GL only (see the
// juce::gl gotcha in CLAUDE.md).
//
// What belongs here: helpers and state that cross a TU boundary. A helper used
// by exactly ONE domain stays file-static in that TU, and file-scope mutable
// state (the pixel/terminal/audio tables) stays in the TU that owns its domain —
// other TUs reach it through the small accessors at the bottom rather than
// externing the raw container.

#include "Renderer.h"
#include "Primitive.h"
#include "PData.h"
#include "State.h"
#include "GLSLShader.h"
#include "dada.h"

#include "GLHeaders.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// ---- build context ----------------------------------------------------------
// The immediate-mode turtle state every command reads. Defined in
// FluxusCommandsCore.cpp.
//
// BuildState is the part that describes the NEXT-BUILT primitive — exactly the
// set addPrim() reads — and it is what (push)/(pop), i.e. (with-state), saves and
// restores as a whole. Upstream fluxus keeps all of it in the State it pushes
// (Parent included: vendor/fluxus/libfluxus/src/State.h:81); the port used to
// push only tx+col, so (parent id), the hints, the wire colour, the texture and
// the shader all LEAKED out of (with-state) onto every prim built afterwards.
struct BuildState {
  Fluxus::dMatrix   tx;
  Fluxus::dColour   col{1, 1, 1, 1};
  int       hints = 0;      // hints turned ON for newly built prims
  int       hintsOff = 0;   // hints turned OFF (e.g. (hint-solid #f) clears the
                            // primitive's default HINT_SOLID — OR alone can't)
  bool      wireColSet = false;           // (wire-colour) outside a grab colours the
  Fluxus::dColour   wireCol{1, 1, 1, 1};  // next-built prims (same asymmetry as hints)
  float     lineWidth = 2.0f;
  Fluxus::GLSLShader* shader = nullptr;   // current shader for newly built prims (not owned)
  int         parent = -1;        // parent id for newly built prims (-1 = root)
  unsigned    texture = 0;        // GL texture id for newly built prims (0 = none)
  int         srcBlend = GL_SRC_ALPHA;           // blend factors for newly built prims
  int         dstBlend = GL_ONE_MINUS_SRC_ALPHA;
  Fluxus::COLOUR_MODE colourMode = Fluxus::MODE_RGB;   // (colour-mode): interpret rgb vs hsv
};

struct BuildCtx : BuildState {
  Fluxus::Renderer* r = nullptr;
  std::vector<BuildState> stack;
  double    time  = 0.0;
  int       frame = 0;
  Fluxus::Primitive* grabbed = nullptr;   // current pdata target
  int        grabbedId = -1;      // its scene-graph id (for scene-graph queries / save)
};
extern BuildCtx g_ctx;

// ---- shared helpers ---------------------------------------------------------
// hand a freshly-built primitive to the renderer, applying the build context.
int addPrim(Fluxus::Primitive* p);                          // FluxusCommandsCore.cpp
// Built-in texturing shader (Apple's GL-on-Metal ignores fixed-function texturing).
Fluxus::GLSLShader* builtinTexShader();                     // FluxusCommandsCore.cpp
// compute + apply this frame's camera view (the orbit survives the scene Clear()).
void applyCamera();                                         // FluxusCommandsInput.cpp

// Inside (with-primitive id ...) the grabbed prim exists, and fluxus state
// commands modify ITS state. Otherwise they set the build context for the
// next-built primitive.
inline Fluxus::State* grabbedState() { return g_ctx.grabbed ? g_ctx.grabbed->GetState() : nullptr; }

// swap a State's shader with correct refcounting (State DecRefs/deletes at 0).
inline void setStateShader(Fluxus::State* s, Fluxus::GLSLShader* sh) {
  if (!s) return;
  if (s->Shader && s->Shader->DecRef()) delete s->Shader;
  s->Shader = sh;
  if (sh) sh->IncRef();
}

// build a colour honouring the active colour-mode (grabbed prim's, else build
// ctx). dColour's mode ctor converts HSV->RGB for us. Lives out here rather than
// in an extern "C" block: a C-linkage function returning dColour warns.
inline Fluxus::dColour makeCol(double r, double g, double b) {
  Fluxus::COLOUR_MODE m = g_ctx.grabbed ? g_ctx.grabbed->GetState()->ColourMode : g_ctx.colourMode;
  return Fluxus::dColour((float) r, (float) g, (float) b, 1, m);
}

// ---- pdata FFI channel cache ------------------------------------------------
// Script pdata access crosses the FFI once per COMPONENT ((pdata-ref "p" i) is
// 3 calls), and each call paid a string alloc + two string-keyed map lookups
// (GetDataInfo + GetData). Cache the resolved TypedPData per channel name for
// the duration of a grab. The PData object pointer survives Resize (only
// AddData/CopyData/RemoveDataVec delete the object), so size is read live from
// the PData; the cache is dropped on grab/ungrab/frame-begin/destroy/clear and
// on structural channel changes (pdata-add / pdata-copy). The cache itself lives
// in FluxusCommandsPdata.cpp; core drops it around grab/frame boundaries.
struct PDataCacheEntry { std::string name; Fluxus::PData* pd = nullptr; char type = 'v'; };

void pdataCacheClear();                                     // FluxusCommandsPdata.cpp
PDataCacheEntry* pdataResolve(const char* name);            // FluxusCommandsPdata.cpp

// Type dispatch for the pdata accessors: every accessor repeats the same
// 'f'/'c'/'v' triplet, so route it through ONE visitor. f gets the typed
// m_Data vector plus the element component count (1 = scalar float channel,
// 3 = vec/colour); elemArr() gives a float* into an element regardless of type
// (valid for ncomp components only).
template <class A> inline float* elemArr(std::vector<float, A>& d, size_t i)            { return &d[i]; }
template <class A> inline float* elemArr(std::vector<Fluxus::dColour, A>& d, size_t i)  { return d[i].arr(); }
template <class A> inline float* elemArr(std::vector<Fluxus::dVector, A>& d, size_t i)  { return d[i].arr(); }
template <class F>
auto withChannel(PDataCacheEntry* c, F&& f) {
  switch (c->type) {
    case 'f': return f(static_cast<Fluxus::TypedPData<float>*>(c->pd)->m_Data, 1);
    case 'c': return f(static_cast<Fluxus::TypedPData<Fluxus::dColour>*>(c->pd)->m_Data, 3);
    default:  return f(static_cast<Fluxus::TypedPData<Fluxus::dVector>*>(c->pd)->m_Data, 3);   // 'v'
  }
}

// ---- cross-domain accessors -------------------------------------------------
// pixels prims own a GL texture, terminals own a libvterm parser — both keyed by
// Primitive* in their own TU. (destroy) has to free those, and (pdata-size) has
// to know a pixels prim's buffer is w*h rather than its vertex count.
void pixelsErase(Fluxus::Primitive* p);       // FluxusCommandsGpu.cpp
int  pixelsCount(Fluxus::Primitive* p);       // w*h, 0 if p is not a pixels prim
void terminalErase(Fluxus::Primitive* p);     // FluxusCommandsTerminal.cpp
// drop last frame's (load-model …) handles in immediate mode (the parsed-scene
// cache is kept) — same reasoning as the per-frame pfunc clear.
void modelsFrameBegin();                      // FluxusCommandsModel.cpp

// snapshot of the audio bands (taken under the audio mutex) for the native
// deformers, which run on the GL thread while the audio host writes.
std::vector<float> audioBands();              // FluxusCommandsInput.cpp
