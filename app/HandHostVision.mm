// SPDX-License-Identifier: AGPL-3.0-or-later
// Apple Vision hand-tracking host (Objective-C++, ARC — see CMakeLists).
// AVCaptureSession -> VNDetectHumanHandPoseRequest (GPU/Neural-Engine) -> 21
// landmarks/hand pushed into FluxusCommands (flux_set_hands). Capture runs on a
// serial dispatch queue; it only touches the mutex-protected FluxusCommands state,
// never GL or the script engine (gotcha #1). Opt-in: nothing opens the camera
// until a script calls (hand-tracking #t), which flows through the enable bridge.
//
// This is the in-app, GPU-accelerated equivalent of spikes/visionhand + the
// mediapipe OSC bridge — no Python, no OSC hop, no mediapipe.
#include "HandHost.h"
#include "FluxusCommands.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <vector>

// Vision hand joints in MediaPipe landmark order (0 wrist, thumb 1-4, index 5-8,
// middle 9-12, ring 13-16, pinky 17-20) so downstream bone topology lines up.
static NSArray<VNHumanHandPoseObservationJointName>* mpJointOrder(void) {
  return @[
    VNHumanHandPoseObservationJointNameWrist,
    VNHumanHandPoseObservationJointNameThumbCMC, VNHumanHandPoseObservationJointNameThumbMP,
    VNHumanHandPoseObservationJointNameThumbIP,  VNHumanHandPoseObservationJointNameThumbTip,
    VNHumanHandPoseObservationJointNameIndexMCP, VNHumanHandPoseObservationJointNameIndexPIP,
    VNHumanHandPoseObservationJointNameIndexDIP, VNHumanHandPoseObservationJointNameIndexTip,
    VNHumanHandPoseObservationJointNameMiddleMCP,VNHumanHandPoseObservationJointNameMiddlePIP,
    VNHumanHandPoseObservationJointNameMiddleDIP,VNHumanHandPoseObservationJointNameMiddleTip,
    VNHumanHandPoseObservationJointNameRingMCP,  VNHumanHandPoseObservationJointNameRingPIP,
    VNHumanHandPoseObservationJointNameRingDIP,  VNHumanHandPoseObservationJointNameRingTip,
    VNHumanHandPoseObservationJointNameLittleMCP,VNHumanHandPoseObservationJointNameLittlePIP,
    VNHumanHandPoseObservationJointNameLittleDIP,VNHumanHandPoseObservationJointNameLittleTip,
  ];
}

@interface FluxHandCam : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureSession* session;
@property(nonatomic, strong) VNDetectHumanHandPoseRequest* req;
@property(nonatomic, strong) NSArray* order;
@end

@implementation FluxHandCam
- (instancetype)init {
  if ((self = [super init])) {
    _req = [[VNDetectHumanHandPoseRequest alloc] init];
    _req.maximumHandCount = 2;                 // Vision auto-selects GPU/ANE
    _order = mpJointOrder();
  }
  return self;
}
- (void)captureOutput:(AVCaptureOutput*)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection*)connection {
  CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pb) return;
  VNImageRequestHandler* h =
    [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pb
                                             orientation:kCGImagePropertyOrientationUp
                                                 options:@{}];
  NSError* err = nil;
  [h performRequests:@[self.req] error:&err];
  NSArray<VNHumanHandPoseObservation*>* obs = self.req.results ?: @[];

  std::vector<float> flat;              // nHands * 21 * 3
  int nHands = 0;
  for (VNHumanHandPoseObservation* o in obs) {
    float pts[63]; float sumc = 0;
    for (int i = 0; i < 21; ++i) {
      NSError* e = nil;
      VNRecognizedPoint* p = [o recognizedPointForJointName:self.order[i] error:&e];
      float x = p ? (float) p.location.x : 0.f;
      float y = p ? (float) p.location.y : 0.f;
      sumc += p ? (float) p.confidence : 0.f;
      pts[i*3+0] = 1.f - x;             // mirror (selfie view)
      pts[i*3+1] = 1.f - y;             // Vision origin bottom-left -> top-left (MediaPipe)
      pts[i*3+2] = 0.f;                 // Vision hand pose is 2D
    }
    if (sumc / 21.f < 0.15f) continue;  // drop a low-confidence phantom hand
    flat.insert(flat.end(), pts, pts + 63);
    ++nHands;
  }
  flux_set_hands(nHands, nHands ? flat.data() : nullptr, 21);
}
@end

static FluxHandCam* g_hcam = nil;

static void closeHandCamera(void) {
  if (g_hcam) {
    if (g_hcam.session) [g_hcam.session stopRunning];
    g_hcam = nil;   // ARC releases the session
  }
  flux_set_hands(0, nullptr, 21);
}

static bool openHandCamera(void) {
  @autoreleasepool {
    if (g_hcam) return true;   // idempotent

    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL){}];   // first run prompts

    AVCaptureDevice* dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!dev) { fprintf(stderr, "[hand] no camera device\n"); return false; }
    NSError* err = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if (!input) { fprintf(stderr, "[hand] camera input failed\n"); return false; }

    AVCaptureSession* session = [AVCaptureSession new];
    session.sessionPreset = AVCaptureSessionPreset640x480;   // tracking res; light on CPU/GPU
    if (![session canAddInput:input]) return false;
    [session addInput:input];

    AVCaptureVideoDataOutput* out = [AVCaptureVideoDataOutput new];
    out.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
    out.alwaysDiscardsLateVideoFrames = YES;

    FluxHandCam* cam = [FluxHandCam new];
    dispatch_queue_t q = dispatch_queue_create("flux.hand", DISPATCH_QUEUE_SERIAL);
    [out setSampleBufferDelegate:cam queue:q];
    if (![session canAddOutput:out]) return false;
    [session addOutput:out];

    cam.session = session;
    g_hcam = cam;
    [session startRunning];
    fprintf(stderr, "[hand] Vision hand tracking on (GPU/ANE)\n");
    return true;
  }
}

namespace {
struct VisionHandHost : IHandHost {
  void start() override {
    FluxHandBridge b;
    b.enable = [](bool on) { if (on) openHandCamera(); else closeHandCamera(); };
    flux_hand_install_bridge(b);
  }
  void stop() override {
    flux_hand_install_bridge(FluxHandBridge{});   // detach first
    closeHandCamera();
  }
};
}

std::unique_ptr<IHandHost> makeHandHost() { return std::make_unique<VisionHandHost>(); }
