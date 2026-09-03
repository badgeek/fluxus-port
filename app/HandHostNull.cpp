// SPDX-License-Identifier: AGPL-3.0-or-later
// Null hand-tracking host for platforms without a backend yet. Keeps the build +
// script bindings identical everywhere; (hand-tracking #t) is a no-op and
// (hand-count) stays 0. A future Linux/Windows backend replaces this TU with a
// real makeHandHost() (e.g. V4L2/MediaFoundation + a landmark model).
#include "HandHost.h"

namespace {
struct NullHandHost : IHandHost {
  void start() override {}
  void stop()  override {}
};
}

std::unique_ptr<IHandHost> makeHandHost() { return std::make_unique<NullHandHost>(); }
