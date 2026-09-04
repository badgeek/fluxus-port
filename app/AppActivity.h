// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Take a process-wide NSProcessInfo activity (UserInitiated + LatencyCritical)
// so macOS never App-Naps / E-core-demotes the app. A LaunchServices-launched
// (`open`, Finder double-click) GUI app gets the default energy policy, and the
// scheduler moves its sustained-busy render thread to an E-core — the SAME
// frame then takes ~2.5-3x the wall time (ntsc filter 3.9 -> ~12 ms/frame,
// whole app ~20% -> ~40% CPU). A terminal-child launch inherits the shell's
// interactive policy and never shows this. Call once at app init; the token is
// held for the process lifetime.
void flux_begin_perf_activity();
