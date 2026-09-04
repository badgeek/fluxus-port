// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless pdata FFI-layer benchmark + correctness test.
//
// Exercises flux_pdata_get/set exactly as the Scheme pdata-ref/pdata-set!
// wrappers do (3 component calls per vec3), against a real Renderer + sphere
// primitive, with NO GL context — building prims and touching pdata never
// issue GL calls (draw does, and we never draw).
//
// Usage: pdata_bench [iters]   (default 200 passes over all verts)
// Exits non-zero on any correctness failure, so it doubles as a test.
#include "FluxusCommands.h"
#include "Renderer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Glyph-atlas hooks normally provided by TextureLoader/GlyphAtlas (JUCE TUs).
// The bench never builds text/terminal prims, so stubs satisfy the linker.
extern "C" {
unsigned flux_font_atlas(void) { return 0; }
unsigned flux_glyph_atlas_texture(void) { return 0; }
void flux_glyph_cell(unsigned, float* s0, float* t0, float* s1, float* t1) {
  if (s0) *s0 = 0; if (t0) *t0 = 0; if (s1) *s1 = 0; if (t1) *t1 = 0;
}
}

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); g_fail = 1; } } while (0)

static double nowMs() {
  using namespace std::chrono;
  return (double) duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
}

int main(int argc, char** argv) {
  const int iters = argc > 1 ? std::atoi(argv[1]) : 200;

  auto* r = new Fluxus::Renderer();
  flux_set_renderer(r);
  flux_frame_begin(0.0, 0);

  const int id = flux_build_sphere(64, 64);
  CHECK(id > 0, "build_sphere returned no id");
  flux_grab(id);
  const int n = flux_pdata_size();
  std::fprintf(stderr, "sphere verts: %d, iters: %d\n", n, iters);
  CHECK(n > 1000, "sphere unexpectedly small");

  // --- correctness -----------------------------------------------------------
  // roundtrip on "p"
  flux_pdata_set("p", 7, 0, 1.25); flux_pdata_set("p", 7, 1, -2.5); flux_pdata_set("p", 7, 2, 3.75);
  CHECK(flux_pdata_get("p", 7, 0) == 1.25,  "p roundtrip x");
  CHECK(flux_pdata_get("p", 7, 1) == -2.5,  "p roundtrip y");
  CHECK(flux_pdata_get("p", 7, 2) == 3.75,  "p roundtrip z");
  // interleaved channels ("p" + "n") — exercises the multi-entry cache
  const double nx = flux_pdata_get("n", 7, 0);
  flux_pdata_set("p", 8, 0, 9.0);
  CHECK(flux_pdata_get("n", 7, 0) == nx, "n unchanged after p set");
  CHECK(flux_pdata_get("p", 8, 0) == 9.0, "p set with n interleaved");
  // out-of-range / bad channel are safe zeros
  CHECK(flux_pdata_get("p", -1, 0) == 0.0, "negative index");
  CHECK(flux_pdata_get("p", n + 5, 0) == 0.0, "index past size");
  CHECK(flux_pdata_get("nope", 0, 0) == 0.0, "unknown channel");
  flux_pdata_set("nope", 0, 0, 1.0);   // must not crash
  // pdata-add invalidates + new channel usable
  flux_pdata_add("bench", "f");
  flux_pdata_set("bench", 3, 0, 0.5);
  CHECK(flux_pdata_get("bench", 3, 0) == 0.5, "added float channel roundtrip");
  // regrab another prim: cache must not leak the old prim's channels
  const int id2 = flux_build_cube();
  flux_grab(id2);
  const int n2 = flux_pdata_size();
  CHECK(n2 > 0 && n2 != n, "cube pdata size");
  CHECK(flux_pdata_get("bench", 3, 0) == 0.0, "cube has no 'bench' channel");
  flux_pdata_set("p", 0, 0, 4.0);
  CHECK(flux_pdata_get("p", 0, 0) == 4.0, "cube p set after regrab");
  flux_grab(id);
  CHECK(flux_pdata_get("bench", 3, 0) == 0.5, "sphere 'bench' intact after regrab");

  // --- bulk get3/set3 correctness --------------------------------------------
  { double v[3] = {0, 0, 0};
    CHECK(flux_pdata_get3("p", 7, v) == 3, "get3 ncomp vec");
    CHECK(v[0] == 1.25 && v[1] == -2.5 && v[2] == 3.75, "get3 matches per-component sets");
    flux_pdata_set3("p", 7, 5.0, 6.0, 7.0);
    CHECK(flux_pdata_get("p", 7, 0) == 5.0 && flux_pdata_get("p", 7, 2) == 7.0, "set3 visible via per-component get");
    CHECK(flux_pdata_get3("bench", 3, v) == 1, "get3 ncomp float channel");
    CHECK(v[0] == 0.5, "get3 float value");
    flux_pdata_set3("bench", 3, 1.0, 2.0, 9.5);   // float channel: last comp wins (old per-comp quirk)
    CHECK(flux_pdata_get("bench", 3, 0) == 9.5, "set3 float channel keeps z");
    CHECK(flux_pdata_get3("p", n + 5, v) == 0, "get3 out of range");
    CHECK(flux_pdata_get3("nope", 0, v) == 0, "get3 unknown channel");
    flux_pdata_set3("nope", 0, 1, 2, 3);   // must not crash
  }

  // --- benchmark: simulate (pdata-map! (lambda (p n) ...) "p" "n") ----------
  // per vert: 3 gets on "p", 3 gets on "n", 3 sets on "p" — the exact FFI call
  // pattern the Scheme wrappers emit.
  const double t0 = nowMs();
  double sink = 0.0;
  for (int it = 0; it < iters; ++it) {
    for (int i = 0; i < n; ++i) {
      const double px = flux_pdata_get("p", i, 0);
      const double py = flux_pdata_get("p", i, 1);
      const double pz = flux_pdata_get("p", i, 2);
      const double nxx = flux_pdata_get("n", i, 0);
      const double nyy = flux_pdata_get("n", i, 1);
      const double nzz = flux_pdata_get("n", i, 2);
      flux_pdata_set("p", i, 0, px + nxx * 0.0001);
      flux_pdata_set("p", i, 1, py + nyy * 0.0001);
      flux_pdata_set("p", i, 2, pz + nzz * 0.0001);
      sink += px;
    }
  }
  const double t1 = nowMs();
  const double totalMs = t1 - t0;
  const long long calls = 9LL * n * iters;
  std::fprintf(stderr, "pdata bench: %lld calls in %.1f ms  (%.1f ns/call, %.3f ms per %d-vert map pass)\n",
               calls, totalMs, totalMs * 1e6 / (double) calls, totalMs / iters, n);
  std::fprintf(stderr, "sink=%f\n", sink);   // defeat dead-code elimination

  // same map pass through the bulk calls: 3 crossings per vert instead of 9
  const double t2 = nowMs();
  double sink2 = 0.0;
  for (int it = 0; it < iters; ++it) {
    for (int i = 0; i < n; ++i) {
      double p[3], nn[3];
      flux_pdata_get3("p", i, p);
      flux_pdata_get3("n", i, nn);
      flux_pdata_set3("p", i, p[0] + nn[0] * 0.0001, p[1] + nn[1] * 0.0001, p[2] + nn[2] * 0.0001);
      sink2 += p[0];
    }
  }
  const double t3 = nowMs();
  const double bulkMs = t3 - t2;
  const long long bulkCalls = 3LL * n * iters;
  std::fprintf(stderr, "bulk bench:  %lld calls in %.1f ms  (%.1f ns/call, %.3f ms per %d-vert map pass)\n",
               bulkCalls, bulkMs, bulkMs * 1e6 / (double) bulkCalls, bulkMs / iters, n);
  std::fprintf(stderr, "sink2=%f\n", sink2);

  std::fprintf(stderr, g_fail ? "RESULT: FAIL\n" : "RESULT: OK\n");
  return g_fail;
}
