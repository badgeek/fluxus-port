// AVFoundation-backed video texture (Objective-C++). Compiled with -fobjc-arc
// (see CMakeLists). See VideoHost.h for the seam + threading contract.
#import "VideoHost.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>   // CACurrentMediaTime
#import <OpenGL/gl.h>
#include <mutex>

// One player + video output + the GL texture we keep re-uploading into. Held in a
// singleton ObjC object so ARC keeps the strong refs alive across C calls.
@interface FluxVideoState : NSObject
@property(nonatomic, strong) AVPlayer* player;
@property(nonatomic, strong) AVPlayerItem* item;
@property(nonatomic, strong) AVPlayerItemVideoOutput* output;
@property(nonatomic, strong) id endObserver;
@property(nonatomic, assign) GLuint tex;
@property(nonatomic, assign) int w;
@property(nonatomic, assign) int h;
@property(nonatomic, assign) double duration;
@property(nonatomic, copy)   NSString* path;
@end

@implementation FluxVideoState
@end

static FluxVideoState* g_vs = nil;

static void flux_video_close_impl(void) {
  if (!g_vs) return;
  if (g_vs.player) [g_vs.player pause];
  if (g_vs.endObserver)
    [[NSNotificationCenter defaultCenter] removeObserver:g_vs.endObserver];
  if (g_vs.tex) { GLuint t = g_vs.tex; glDeleteTextures(1, &t); g_vs.tex = 0; }
  g_vs = nil;   // ARC releases player/item/output
}

int flux_video_open(const char* path) {
  if (!path || !*path) return 0;
  @autoreleasepool {
    NSString* p = [NSString stringWithUTF8String:path];
    // idempotent: calling (video-open "same") every frame (immediate mode) must NOT
    // tear down + rebuild the player — that would reset it before it can ever
    // decode a frame. Only (re)open when the path actually changes.
    if (g_vs && [g_vs.path isEqualToString:p]) return 1;
    flux_video_close_impl();
    NSURL* url = [NSURL fileURLWithPath:p];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    NSArray<AVAssetTrack*>* vtracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    if (vtracks.count == 0) return 0;

    FluxVideoState* vs = [FluxVideoState new];
    vs.item = [AVPlayerItem playerItemWithAsset:asset];

    // request BGRA, GL-compatible pixel buffers (fast glTexSubImage2D path)
    NSDictionary* attrs = @{
      (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (id)kCVPixelBufferOpenGLCompatibilityKey : @YES,
    };
    vs.output = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];
    [vs.item addOutput:vs.output];

    vs.player = [AVPlayer playerWithPlayerItem:vs.item];
    vs.player.actionAtItemEnd = AVPlayerActionAtItemEndNone;   // we loop manually

    AVAssetTrack* vt = vtracks.firstObject;
    CGSize sz = vt.naturalSize;
    vs.w = (int) llround(fabs(sz.width));
    vs.h = (int) llround(fabs(sz.height));
    CMTime d = asset.duration;
    vs.duration = CMTIME_IS_NUMERIC(d) ? CMTimeGetSeconds(d) : 0.0;

    // loop: on end, seek back to the start and keep playing (seamless enough)
    __weak FluxVideoState* weakVs = vs;
    vs.endObserver =
      [[NSNotificationCenter defaultCenter]
        addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                    object:vs.item
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                  FluxVideoState* s = weakVs;
                  if (s.player) { [s.player seekToTime:kCMTimeZero]; [s.player play]; }
                }];

    vs.path = p;
    g_vs = vs;
    [vs.player play];
    return 1;
  }
}

// Upload a 32BGRA CVPixelBuffer into the GL texture *tex (created/resized as
// needed); updates *tex/*w/*h. GL-thread only. Shared by video + camera.
static void blitCVBuffer(CVPixelBufferRef pb, GLuint* tex, int* w, int* h) {
  CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
  const int pw = (int) CVPixelBufferGetWidth(pb);
  const int ph = (int) CVPixelBufferGetHeight(pb);
  const size_t bpr = CVPixelBufferGetBytesPerRow(pb);
  void* base = CVPixelBufferGetBaseAddress(pb);
  if (base && pw > 0 && ph > 0) {
    if (!*tex || pw != *w || ph != *h) {
      if (*tex) glDeleteTextures(1, tex);
      GLuint id = 0; glGenTextures(1, &id);
      glBindTexture(GL_TEXTURE_2D, id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pw, ph, 0,
                   GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      *tex = id; *w = pw; *h = ph;
    } else {
      glBindTexture(GL_TEXTURE_2D, *tex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) (bpr / 4));   // handle row padding
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph,
                    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, base);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
}

unsigned flux_video_texture(void) {
  if (!g_vs || !g_vs.output) return 0;
  AVPlayerItemVideoOutput* out = g_vs.output;
  CMTime t = [out itemTimeForHostTime:CACurrentMediaTime()];
  if (![out hasNewPixelBufferForItemTime:t]) return g_vs.tex;   // reuse last upload
  CVPixelBufferRef pb = [out copyPixelBufferForItemTime:t itemTimeForDisplay:nullptr];
  if (!pb) return g_vs.tex;
  GLuint tex = g_vs.tex; int w = g_vs.w, h = g_vs.h;
  blitCVBuffer(pb, &tex, &w, &h);
  g_vs.tex = tex; g_vs.w = w; g_vs.h = h;
  CVBufferRelease(pb);
  return g_vs.tex;
}

int    flux_video_width(void)    { return g_vs ? g_vs.w : 0; }
int    flux_video_height(void)   { return g_vs ? g_vs.h : 0; }
double flux_video_duration(void) { return g_vs ? g_vs.duration : 0.0; }
void   flux_video_play(void)     { if (g_vs.player) [g_vs.player play]; }
void   flux_video_pause(void)    { if (g_vs.player) [g_vs.player pause]; }
void   flux_video_seek(double s) { if (g_vs.player)
  [g_vs.player seekToTime:CMTimeMakeWithSeconds(s, 600)]; }
void   flux_video_close(void)    { flux_video_close_impl(); }

// ============================================================================
// Webcam capture -> GL texture. AVCaptureSession delivers frames on a capture
// queue (NOT the GL thread), so the CVPixelBuffer is handed off under a mutex;
// the GL thread pulls the latest and uploads it (same blitCVBuffer path). This
// mirrors the AudioHost external-input pattern (gotcha #1): the callback thread
// only touches mutex-protected state, never GL or the script engine.
// ============================================================================
@interface FluxCamState : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureSession* session;
@property(nonatomic, assign) GLuint tex;
@property(nonatomic, assign) int w;
@property(nonatomic, assign) int h;
@end

// latest frame handed GL-thread <- capture-thread (retained; mutex-guarded)
static std::mutex        g_camMutex;
static CVPixelBufferRef  g_camLatest = nullptr;

@implementation FluxCamState
- (void)captureOutput:(AVCaptureOutput*)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection*)connection {
  CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pb) return;
  CVBufferRetain(pb);
  CVPixelBufferRef old = nullptr;
  { std::lock_guard<std::mutex> lk(g_camMutex); old = g_camLatest; g_camLatest = pb; }
  if (old) CVBufferRelease(old);   // drop the frame the GL thread never consumed
}
@end

static FluxCamState* g_cam = nil;

static void flux_camera_close_impl(void) {
  if (g_cam) {
    if (g_cam.session) [g_cam.session stopRunning];
    if (g_cam.tex) { GLuint t = g_cam.tex; glDeleteTextures(1, &t); g_cam.tex = 0; }
    g_cam = nil;   // ARC releases session
  }
  std::lock_guard<std::mutex> lk(g_camMutex);
  if (g_camLatest) { CVBufferRelease(g_camLatest); g_camLatest = nullptr; }
}

int flux_camera_open(int device_index) {
  @autoreleasepool {
    if (g_cam) return 1;   // already capturing (idempotent for per-frame calls)

    // ask permission (async; first launch prompts). If denied, no frames arrive.
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL){}];

    AVCaptureDeviceDiscoverySession* disc = [AVCaptureDeviceDiscoverySession
      discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera ]
                            mediaType:AVMediaTypeVideo
                             position:AVCaptureDevicePositionUnspecified];
    NSArray<AVCaptureDevice*>* devs = disc.devices;
    AVCaptureDevice* dev = nil;
    if (device_index >= 0 && device_index < (int) devs.count)
      dev = devs[(NSUInteger) device_index];
    if (!dev) dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!dev) return 0;

    NSError* err = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if (!input) return 0;

    AVCaptureSession* session = [AVCaptureSession new];
    session.sessionPreset = AVCaptureSessionPreset1280x720;
    if ([session canAddInput:input]) [session addInput:input]; else return 0;

    AVCaptureVideoDataOutput* out = [AVCaptureVideoDataOutput new];
    out.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
    out.alwaysDiscardsLateVideoFrames = YES;

    FluxCamState* cam = [FluxCamState new];
    dispatch_queue_t q = dispatch_queue_create("flux.camera", DISPATCH_QUEUE_SERIAL);
    [out setSampleBufferDelegate:cam queue:q];
    if ([session canAddOutput:out]) [session addOutput:out]; else return 0;

    cam.session = session;
    g_cam = cam;
    [session startRunning];
    return 1;
  }
}

unsigned flux_camera_texture(void) {
  if (!g_cam) return 0;
  CVPixelBufferRef pb = nullptr;
  { std::lock_guard<std::mutex> lk(g_camMutex); pb = g_camLatest; g_camLatest = nullptr; }
  if (!pb) return g_cam.tex;              // no new frame since last upload
  GLuint tex = g_cam.tex; int w = g_cam.w, h = g_cam.h;
  blitCVBuffer(pb, &tex, &w, &h);
  g_cam.tex = tex; g_cam.w = w; g_cam.h = h;
  CVBufferRelease(pb);
  return g_cam.tex;
}

int  flux_camera_width(void)  { return g_cam ? g_cam.w : 0; }
int  flux_camera_height(void) { return g_cam ? g_cam.h : 0; }
void flux_camera_close(void)  { flux_camera_close_impl(); }
