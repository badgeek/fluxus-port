// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>
#include <mutex>

// Thread-shared script state. Owned by FluxusComponent (outlives the GL scene),
// written by the message thread (editor) and read/written by the GL thread
// (per-frame eval). Plain std types so it can be included from both the JUCE TU
// and the engine TU.
struct SharedScript {
  std::mutex  m;
  std::string pending;      // latest editor buffer to run each frame
  std::string lastError;    // last eval error ("" = ok), for the console
  bool        dirty = false;
};
