// SPDX-License-Identifier: AGPL-3.0-or-later
// Null process-energy-policy host for platforms that have no equivalent of
// NSProcessInfo activities. Nothing to do: the App Nap / E-core demotion this
// works around is a macOS behaviour, and every other scheduler we target keeps
// a sustained-busy render thread where it is. See AppActivity.h.
#include "AppActivity.h"

void flux_begin_perf_activity() {}
