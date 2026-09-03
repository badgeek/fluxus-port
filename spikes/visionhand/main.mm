// Vision hand-pose spike (path 2): Apple Vision VNDetectHumanHandPoseRequest,
// GPU/Neural-Engine accelerated, camera -> 21 hand landmarks -> OSC -> fluxus.
//
// Proves the native macOS route works with NO mediapipe / no Python and no JUCE.
// Streams the SAME OSC schema as tools/mediapipe_tracker.py so it drives
// examples/hand-reactive.scm unchanged:
//   /hands <n>, /hand/0 = 63 floats (21 landmarks x,y,z), /pinch/0 <dist>
// Vision joints are remapped into MediaPipe landmark order; Vision is 2D so z=0.
//
// Build (see spikes/visionhand/BUILD.md):
//   clang++ -fobjc-arc -O2 -std=c++17 spikes/visionhand/main.mm -o spikes/build/vision_hand_probe \
//     -framework Foundation -framework AVFoundation -framework Vision \
//     -framework CoreMedia -framework CoreVideo -framework CoreGraphics
// Run:  ./spikes/build/vision_hand_probe [--mirror] [--secs N] [--host H] [--port P]
//
// Camera TCC: run from a terminal that has camera permission (System Settings ->
// Privacy & Security -> Camera). First run may prompt.

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <atomic>

// ---- tiny UDP OSC sender ----------------------------------------------------
static int      g_sock = -1;
static sockaddr_in g_dst;

static void osc_pad(std::vector<uint8_t>& b) { while (b.size() % 4) b.push_back(0); }
static void osc_send(const char* addr, const float* args, int n) {
    std::vector<uint8_t> m;
    for (const char* p = addr; *p; ++p) m.push_back((uint8_t)*p);
    m.push_back(0); osc_pad(m);
    m.push_back(','); for (int i = 0; i < n; ++i) m.push_back('f');
    m.push_back(0); osc_pad(m);
    for (int i = 0; i < n; ++i) {
        uint32_t v; float f = args[i]; std::memcpy(&v, &f, 4); v = htonl(v);
        uint8_t* q = (uint8_t*)&v; m.insert(m.end(), q, q + 4);
    }
    sendto(g_sock, m.data(), m.size(), 0, (sockaddr*)&g_dst, sizeof(g_dst));
}

// 21 Vision joints in MediaPipe landmark order (0 wrist, thumb, index, ...)
static NSArray<VNHumanHandPoseObservationJointName>* mpJointOrder() {
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

static std::atomic<long> g_frames{0}, g_seen{0};
static bool g_mirror = false;

// ---- capture delegate: run Vision per frame, emit OSC -----------------------
@interface Cam : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, strong) VNDetectHumanHandPoseRequest* req;
@property (nonatomic, strong) NSArray* order;
@end

@implementation Cam
- (instancetype)init {
    if ((self = [super init])) {
        _req = [[VNDetectHumanHandPoseRequest alloc] init];
        _req.maximumHandCount = 2;                 // Vision picks GPU/ANE automatically
        _order = mpJointOrder();
    }
    return self;
}
- (void)captureOutput:(AVCaptureOutput*)out
  didOutputSampleBuffer:(CMSampleBufferRef)sbuf
         fromConnection:(AVCaptureConnection*)conn {
    CVPixelBufferRef px = CMSampleBufferGetImageBuffer(sbuf);
    if (!px) return;
    VNImageRequestHandler* h =
        [[VNImageRequestHandler alloc] initWithCVPixelBuffer:px
                                                 orientation:kCGImagePropertyOrientationUp
                                                     options:@{}];
    NSError* err = nil;
    [h performRequests:@[self.req] error:&err];
    NSArray<VNHumanHandPoseObservation*>* obs = self.req.results ?: @[];

    float count = (float)obs.count;
    osc_send("/hands", &count, 1);
    g_frames++;
    if (obs.count) g_seen++;

    int hi = 0;
    for (VNHumanHandPoseObservation* o in obs) {
        float flat[63]; float sumc = 0;
        float tipx[2] = {0,0}, tipy[2] = {0,0}; int ti = 0;   // thumb tip(4), index tip(8)
        for (int i = 0; i < 21; ++i) {
            NSError* e = nil;
            VNRecognizedPoint* p = [o recognizedPointForJointName:self.order[i] error:&e];
            float x = p ? (float)p.location.x : 0.f;
            float y = p ? (float)p.location.y : 0.f;
            float c = p ? (float)p.confidence : 0.f;
            if (g_mirror) x = 1.f - x;
            float ymp = 1.f - y;                 // Vision origin bottom-left -> MediaPipe top-left
            flat[i*3+0] = x; flat[i*3+1] = ymp; flat[i*3+2] = 0.f;   // Vision hand pose is 2D
            sumc += c;
            if (i == 4) { tipx[0]=x; tipy[0]=ymp; }
            if (i == 8) { tipx[1]=x; tipy[1]=ymp; }
        }
        if (sumc / 21.f < 0.15f) continue;       // skip a low-confidence phantom hand
        char addr[32]; std::snprintf(addr, sizeof addr, "/hand/%d", hi);
        osc_send(addr, flat, 63);
        float pinch = std::hypot(tipx[0]-tipx[1], tipy[0]-tipy[1]);
        char paddr[32]; std::snprintf(paddr, sizeof paddr, "/pinch/%d", hi);
        osc_send(paddr, &pinch, 1);
        hi++;
    }
}
@end

int main(int argc, const char** argv) {
    @autoreleasepool {
        const char* host = "127.0.0.1"; int port = 8000; double secs = 0;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if      (a == "--mirror") g_mirror = true;
            else if (a == "--host" && i+1 < argc) host = argv[++i];
            else if (a == "--port" && i+1 < argc) port = atoi(argv[++i]);
            else if (a == "--secs" && i+1 < argc) secs = atof(argv[++i]);
        }
        g_sock = socket(AF_INET, SOCK_DGRAM, 0);
        std::memset(&g_dst, 0, sizeof g_dst);
        g_dst.sin_family = AF_INET; g_dst.sin_port = htons(port);
        inet_pton(AF_INET, host, &g_dst.sin_addr);

        AVCaptureDevice* dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (!dev) { fprintf(stderr, "no camera device\n"); return 1; }
        NSError* err = nil;
        AVCaptureDeviceInput* in = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
        if (!in) { fprintf(stderr, "camera input failed: %s\n", err.localizedDescription.UTF8String); return 1; }

        AVCaptureSession* s = [[AVCaptureSession alloc] init];
        s.sessionPreset = AVCaptureSessionPreset640x480;
        [s addInput:in];
        AVCaptureVideoDataOutput* outp = [[AVCaptureVideoDataOutput alloc] init];
        outp.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey:
                                    @(kCVPixelFormatType_32BGRA) };
        outp.alwaysDiscardsLateVideoFrames = YES;
        Cam* cam = [[Cam alloc] init];
        dispatch_queue_t q = dispatch_queue_create("vision.cam", DISPATCH_QUEUE_SERIAL);
        [outp setSampleBufferDelegate:cam queue:q];
        [s addOutput:outp];

        [s startRunning];
        fprintf(stderr, "vision_hand_probe: camera -> osc udp://%s:%d %s (ctrl-c to stop)\n",
                host, port, g_mirror ? "[mirror]" : "");

        // periodic status + optional auto-stop
        __block int ticks = 0;
        NSTimer* t = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer* tm){
            long f = g_frames.exchange(0), sn = g_seen.exchange(0);
            fprintf(stderr, "  %2d fps, %ld with-hand\n", (int)f, sn);
            if (secs > 0 && ++ticks >= (int)secs) { [s stopRunning]; CFRunLoopStop(CFRunLoopGetMain()); }
        }];
        (void)t;
        CFRunLoopRun();
        [s stopRunning];
        fprintf(stderr, "stopped\n");
    }
    return 0;
}
