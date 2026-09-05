// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Shared between shim/OpenGL.h (which the engine sees) and GLESBackend.cpp
// (which must NOT see the shim, or its macros would hijack the real GLES calls
// the backend needs to make). Keeping the declarations here is what lets those
// two compile against completely different views of "OpenGL".

namespace fluxspike {

// Fixed-function draw modes, spelled as plain constants so the backend can read
// them without pulling in the shim's macros.
enum {
  kPoints    = 0x0000,
  kLines     = 0x0001,
  kTriangles = 0x0004,
  kTriStrip  = 0x0005,
  kTriFan    = 0x0006,
  kQuads     = 0x0007,
  kPolygon   = 0x0009,
  kFillMode  = 0x1B02,
  kLineMode  = 0x1B01,
  kPointMode = 0x1B00,
};

// What the shim records on the engine's behalf. The engine's WIRE and POINTS
// passes set client-array pointers and a polygon mode and then call
// glDrawArrays directly — bypassing IRenderBackend — so without capturing this
// there is nothing for a GLES backend to draw.
struct LegacyGL {
  const void* pos = nullptr; int posStride = 0;
  const void* nrm = nullptr; int nrmStride = 0;
  const void* col = nullptr; int colStride = 0;
  float colour[4] = {1, 1, 1, 1};
  unsigned polygonMode = kFillMode;
};

LegacyGL& legacy();

// Draw the captured arrays. polygonMode == kLineMode means "wireframe", which
// GLES cannot express as a rasterizer state — the backend turns the topology
// into real line geometry instead. That substitution is the main thing this
// spike is testing.
void legacyDraw(unsigned mode, int first, int count);
void legacyDrawElements(unsigned mode, int count, unsigned type, const void* idx);

// True for enums that only exist in fixed-function GL, so the shim can keep
// them away from the real glEnable/glDisable and out of the GL error state.
bool enumIsLegacy(unsigned cap);

}  // namespace fluxspike
