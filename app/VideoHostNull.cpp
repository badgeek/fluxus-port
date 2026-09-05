// SPDX-License-Identifier: AGPL-3.0-or-later
// Null video/webcam host for platforms without a capture backend, and for
// -DFLUXUS_ENABLE_VIDEO=OFF builds. Same shape as HandHostNull: the script
// bindings stay identical everywhere, so a sketch that calls (video-open …)
// loads and runs — it just never gets a texture.
//
// Silence would be the wrong failure here (this project has burned sessions on
// commands that quietly did nothing), so the first open attempt says why on
// stderr. A real Linux backend (V4L2 for the camera, GStreamer/FFmpeg for
// movies) replaces this TU, not its callers.
#include "VideoHost.h"

#include <cstdio>

namespace {
void warnOnce() {
  static bool said = false;
  if (said) return;
  said = true;
  std::fprintf(stderr, "[fluxus] video: no capture backend in this build "
                       "(video-*/camera-* are no-ops)\n");
}
}  // namespace

extern "C" {

int      flux_video_open(const char*)  { warnOnce(); return 0; }
unsigned flux_video_texture(void)      { return 0; }
int      flux_video_width(void)        { return 0; }
int      flux_video_height(void)       { return 0; }
double   flux_video_duration(void)     { return 0.0; }
void     flux_video_play(void)         {}
void     flux_video_pause(void)        {}
void     flux_video_seek(double)       {}
void     flux_video_close(void)        {}

int      flux_camera_open(int)         { warnOnce(); return 0; }
unsigned flux_camera_texture(void)     { return 0; }
int      flux_camera_width(void)       { return 0; }
int      flux_camera_height(void)      { return 0; }
void     flux_camera_close(void)       {}

}  // extern "C"
