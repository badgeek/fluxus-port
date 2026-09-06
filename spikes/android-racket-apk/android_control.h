// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Where the running sketch comes from on Android: a file on the device and a
// control port, instead of a string baked into the binary.
//
// One background thread serves both — it accepts control connections and polls
// the sketch file's mtime in the same `poll()` wait, so a change from either
// side arrives the same way. It NEVER calls the script engine: it only writes
// the mutex-protected buffer here, and the GL thread picks the new source up in
// nativeDraw. Same rule as the desktop ControlServer and the audio/video hosts.
//
// The wire protocol is the desktop one (app/ControlServer.cpp): one JSON object
// per line, one JSON object back. So the repo's own tooling drives the device:
//
//   adb forward tcp:8020 tcp:8020
//   cli/fluxus load examples/foo.scm
//   cli/fluxus watch examples/foo.scm     # live-code the phone from any editor
//
// Supported: load, eval, get, error. `save` and `screenshot` are not here —
// screenshot needs the PNG writer this build compiles out (FLUXUS_MINIMAL_NO_PNG).
#pragma once

#include <string>

namespace fluxctl {

// Seed the buffer with `source` and start serving. `sketchPath` is watched for
// changes and rewritten whenever a `load` arrives, so a sketch sent over the
// wire survives a restart. Port is bound on 127.0.0.1 only.
void start(int port, const std::string& sketchPath, const std::string& source);

// GL thread. True (and fills `out`) when the source changed since the last call.
bool poll(std::string& out);

// GL thread, after evaluating: what `fluxus error` reports, and what a `load`
// reply carries back.
void setError(const std::string& err);

// The on-device editor (UI thread). `submit` is exactly what a `load` over the
// wire does — same buffer, same write-through to sketchPath — so a sketch typed
// on the phone and one sent from a laptop are indistinguishable downstream.
void        submit(const std::string& source);
std::string source();
std::string error();

}  // namespace fluxctl
