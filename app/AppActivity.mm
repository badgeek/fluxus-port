// SPDX-License-Identifier: AGPL-3.0-or-later
#import "AppActivity.h"
#import <Foundation/Foundation.h>
// See AppActivity.h. NSActivity (UserInitiated + LatencyCritical) disables
// App Nap and timer coalescing for the process lifetime (token leaks
// deliberately).
//
// KNOWN LIMIT (measured, don't chase it again): a LaunchServices-launched app
// (`open`/Finder) runs with darwin spawn role `ui`, and on Apple Silicon the
// performance controller then biases sustained-busy worker threads onto
// E-cores — the ntsc filter takes ~2.5x the CPU TIME there (app ~44% vs ~21%
// when launched as a terminal child, which has no role) at IDENTICAL fps.
// Nothing app-side breaks through the role policy: USER_INTERACTIVE QoS,
// a real-time time-constraint policy, this NSActivity, `taskpolicy -B`, and
// clearing PRIO_DARWIN_ROLE were all tried; none moved the number. It is an
// energy decision, not a performance problem — frame budget is met either
// way, and %CPU across launch paths measures core speed, not work. Compare
// fps/frame-time, not top.
void flux_begin_perf_activity() {
  static id token = nil;
  if (token) return;
  token = [[NSProcessInfo processInfo]
      beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                        reason:@"fluxus real-time rendering"];
  [token retain];
}
