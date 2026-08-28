#pragma once
// Video texture seam. AVFoundation decodes a movie file; each rendered frame the
// GL thread pulls the current CVPixelBuffer and uploads it to a GL texture that
// scripts bind like any other texture: (texture (video-texture)).
//
// Threading: AVFoundation decodes on its own internal threads, but WE only ever
// pull a frame + touch GL from flux_video_texture(), which scripts call on the GL
// thread during eval — so it never crosses into the script engine (gotcha #1).
#ifdef __cplusplus
extern "C" {
#endif

// Open (or replace) the current movie and start playing it, looped. Returns 1 on
// success, 0 on failure (missing file / undecodable). GL-thread only.
int      flux_video_open(const char* path);
// Pull the latest decoded frame into a GL texture and return its id (0 until the
// first frame is ready). Safe to call every frame. GL-thread only.
unsigned flux_video_texture(void);
int      flux_video_width(void);
int      flux_video_height(void);
double   flux_video_duration(void);   // seconds (0 if unknown)
void     flux_video_play(void);
void     flux_video_pause(void);
void     flux_video_seek(double seconds);
void     flux_video_close(void);

// Webcam capture as a live GL texture. flux_camera_open(0) = default device (the
// first launch triggers the macOS camera-permission prompt). flux_camera_texture()
// returns the latest frame's GL texture id (0 until the first frame). GL-thread only.
int      flux_camera_open(int device_index);
unsigned flux_camera_texture(void);
int      flux_camera_width(void);
int      flux_camera_height(void);
void     flux_camera_close(void);

#ifdef __cplusplus
}
#endif
