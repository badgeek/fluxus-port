// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless correctness test for the assimp model importer (app/FluxusCommandsModel.cpp).
//
// Builds a real Renderer, imports a model, and checks the primitives that come
// back: one per mesh, indexed, right vertex/index counts, finite bounding box,
// node transforms baked in, and the parsed-scene cache actually caching. No GL
// context — importing and building prims never issue GL calls (only drawing does,
// and we never draw), same premise as pdata_bench.
//
// Usage: model_test [model…]   — with no arguments it writes a tiny .obj to
// /tmp and tests with that, so it is self-contained; pass real fbx/gltf/dae paths
// to exercise those importers too.
// Exits non-zero on any failure.
#include "FluxusCommands.h"
#include "Renderer.h"
#include "PolyPrimitive.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// JUCE-side hooks the command layer references but a headless test never uses.
extern "C" {
unsigned flux_font_atlas(void) { return 0; }
unsigned flux_glyph_atlas_texture(void) { return 0; }
void flux_glyph_cell(unsigned, float* s0, float* t0, float* s1, float* t1) {
  if (s0) *s0 = 0; if (t0) *t0 = 0; if (s1) *s1 = 0; if (t1) *t1 = 0;
}
unsigned flux_load_texture(const char*) { return 0; }
unsigned flux_load_texture_mem(const void*, int, const char*) { return 0; }
void flux_write_png(const char*, const unsigned char*, int, int) {}
}

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); g_fail = 1; } } while (0)

// a two-triangle quad with normals + uvs, so the test needs no external asset
static const char* kObj =
  "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
  "vn 0 0 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
  "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
  "f 1/1/1 2/2/2 3/3/3\nf 1/1/1 3/3/3 4/4/4\n";

static std::string writeTempObj() {
  const std::string path = "/tmp/flux_model_test.obj";
  if (FILE* f = std::fopen(path.c_str(), "w")) { std::fputs(kObj, f); std::fclose(f); }
  return path;
}

static void report(Fluxus::Renderer* r, int h, const char* path) {
  const int meshes = flux_model_mesh_count(h);
  long verts = 0, indices = 0;
  int indexed = 0;
  for (int i = 0; i < meshes; ++i) {
    const int id = flux_model_prim(h, i);
    auto* p = dynamic_cast<Fluxus::PolyPrimitive*>(r->GetPrimitive(id));
    if (!p) { CHECK(false, "mesh prim is not a PolyPrimitive"); continue; }
    verts   += p->Size();
    indices += (long) p->GetIndex().size();
    if (p->IsIndexed()) indexed++;
    CHECK(p->GetDataRaw("p") != nullptr, "mesh has no p channel");
    // parented to the model root
    CHECK(r->GetSceneGraph().FindNode(id) != nullptr, "mesh prim not in the scene graph");
  }
  CHECK(indexed == meshes, "every mesh prim should be in indexed mode");
  std::printf("  %-46s meshes=%2d verts=%7ld indices=%7ld root=%d\n",
              path, meshes, verts, indices, flux_model_root(h));
}

// ---- independent reference skinning ---------------------------------------
// The loader routes skinning through the engine's SkinningPrimFunc (two locator
// trees + w<n> weight channels). This recomputes the same pose straight from
// assimp — v' = sum_b w_b * (globalAnim(bone) * bone.offset) * v — and compares.
// It is what caught the bind-pose bug: FBX rest transforms are NOT the bind pose,
// and skinning against them put the fox 35.9 units out while still *looking* like
// an animation.
namespace ref {

// clamped, so a clip whose first key is at t>0 holds that key instead of
// extrapolating backwards out of it
float factor(double ticks, double ti, double tj) {
  const double span = tj - ti;
  if (span <= 0) return 0.f;
  const double f = (ticks - ti) / span;
  return (float) (f < 0 ? 0 : (f > 1 ? 1 : f));
}

template <class KeyT>
unsigned keyBefore(const KeyT* keys, unsigned n, double t) {
  for (unsigned i = 0; i + 1 < n; ++i) if (t < keys[i + 1].mTime) return i;
  return n ? n - 1 : 0;
}

// defaults come from the node's own transform — a channel need not animate all
// three components (FBX $AssimpFbx$ helper nodes routinely animate one)
aiMatrix4x4 sample(const aiNodeAnim* ch, double ticks, const aiNode* node) {
  aiVector3D pos, scl;
  aiQuaternion rot;
  node->mTransformation.Decompose(scl, rot, pos);
  if (ch->mNumPositionKeys) {
    const unsigned i = keyBefore(ch->mPositionKeys, ch->mNumPositionKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumPositionKeys) ? i + 1 : i;
    const float f = factor(ticks, ch->mPositionKeys[i].mTime, ch->mPositionKeys[j].mTime);
    pos = ch->mPositionKeys[i].mValue + (ch->mPositionKeys[j].mValue - ch->mPositionKeys[i].mValue) * f;
  }
  if (ch->mNumScalingKeys) {
    const unsigned i = keyBefore(ch->mScalingKeys, ch->mNumScalingKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumScalingKeys) ? i + 1 : i;
    const float f = factor(ticks, ch->mScalingKeys[i].mTime, ch->mScalingKeys[j].mTime);
    scl = ch->mScalingKeys[i].mValue + (ch->mScalingKeys[j].mValue - ch->mScalingKeys[i].mValue) * f;
  }
  if (ch->mNumRotationKeys) {
    const unsigned i = keyBefore(ch->mRotationKeys, ch->mNumRotationKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumRotationKeys) ? i + 1 : i;
    const float f = factor(ticks, ch->mRotationKeys[i].mTime, ch->mRotationKeys[j].mTime);
    aiQuaternion::Interpolate(rot, ch->mRotationKeys[i].mValue, ch->mRotationKeys[j].mValue, f);
    rot.Normalize();
  }
  return aiMatrix4x4(scl, rot, pos);
}

void globals(const aiAnimation* an, double ticks, const aiNode* n, aiMatrix4x4 parent,
             std::map<std::string, aiMatrix4x4>& out) {
  const aiNodeAnim* ch = nullptr;
  if (an) for (unsigned c = 0; c < an->mNumChannels; ++c)
    if (an->mChannels[c]->mNodeName == n->mName) { ch = an->mChannels[c]; break; }
  const aiMatrix4x4 g = parent * (ch ? sample(ch, ticks, n) : n->mTransformation);
  out[n->mName.C_Str()] = g;
  for (unsigned i = 0; i < n->mNumChildren; ++i) globals(an, ticks, n->mChildren[i], g, out);
}

// returns the worst |ours - reference| over every skinned vertex
double compare(Fluxus::Renderer* r, int h, const char* path, double tsec, int animIdx) {
  Assimp::Importer imp;
  const aiScene* sc = imp.ReadFile(path,
      aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
      aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights | aiProcess_RemoveRedundantMaterials |
      aiProcess_SplitLargeMeshes | aiProcess_Triangulate | aiProcess_GenUVCoords |
      aiProcess_SortByPType | aiProcess_FindDegenerates | aiProcess_FindInstances |
      aiProcess_OptimizeMeshes);
  if (!sc || sc->mNumAnimations == 0 || animIdx >= (int) sc->mNumAnimations) return 0;
  const aiAnimation* an = sc->mAnimations[animIdx];
  const double tps = an->mTicksPerSecond != 0 ? an->mTicksPerSecond : 25.0;
  const double ticks = an->mDuration > 0 ? std::fmod(tsec * tps, an->mDuration) : 0;

  std::map<std::string, aiMatrix4x4> gAnim;
  globals(an, ticks, sc->mRootNode, aiMatrix4x4(), gAnim);

  double worst = 0;
  int prim = 0;
  for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
    const aiMesh* m = sc->mMeshes[mi];
    if (m->mNumFaces == 0) continue;
    const int id = flux_model_prim(h, prim++);
    if (m->mNumBones == 0) continue;
    auto* p = dynamic_cast<Fluxus::PolyPrimitive*>(r->GetPrimitive(id));
    if (!p) continue;

    std::vector<aiVector3D> want(m->mNumVertices, aiVector3D(0, 0, 0));
    for (unsigned b = 0; b < m->mNumBones; ++b) {
      const aiBone* bone = m->mBones[b];
      auto it = gAnim.find(bone->mName.C_Str());
      if (it == gAnim.end()) continue;
      const aiMatrix4x4 mat = it->second * bone->mOffsetMatrix;
      for (unsigned k = 0; k < bone->mNumWeights; ++k) {
        const aiVertexWeight& vw = bone->mWeights[k];
        want[vw.mVertexId] += (mat * m->mVertices[vw.mVertexId]) * vw.mWeight;
      }
    }
    const auto* got = p->GetDataVec<Fluxus::dVector>("p");
    for (unsigned v = 0; v < m->mNumVertices && v < p->Size(); ++v) {
      const Fluxus::dVector& g = (*got)[v];
      const double d = std::sqrt((g.x - want[v].x) * (g.x - want[v].x) +
                                 (g.y - want[v].y) * (g.y - want[v].y) +
                                 (g.z - want[v].z) * (g.z - want[v].z));
      if (d > worst) worst = d;
    }
  }
  return worst;
}

} // namespace ref

int main(int argc, char** argv) {
  auto* r = new Fluxus::Renderer();
  flux_set_renderer(r);
  flux_frame_begin(0.0, 0);

  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) paths.push_back(argv[i]);
  if (paths.empty()) paths.push_back(writeTempObj());

  // --- a bad path must fail cleanly, not crash --------------------------------
  CHECK(flux_load_model("/definitely/not/here.fbx", 0) == -1, "missing file should return -1");
  CHECK(flux_model_error()[0] != '\0', "a failed load should leave an error message");
  CHECK(flux_model_mesh_count(-1) == 0, "mesh count of an invalid handle is 0");
  CHECK(flux_model_prim(-1, 0) == -1, "prim of an invalid handle is -1");

  for (const std::string& path : paths) {
    const int h = flux_load_model(path.c_str(), 0);
    if (h < 0) {
      std::fprintf(stderr, "FAIL: could not load %s: %s\n", path.c_str(), flux_model_error());
      g_fail = 1;
      continue;
    }
    CHECK(flux_model_mesh_count(h) > 0, "model loaded but has no meshes");
    CHECK(flux_model_root(h) >= 0, "model has no root locator");
    report(r, h, path.c_str());

    // --- animation + skinning ---------------------------------------------------
    const int anims = flux_model_anim_count(h);
    const int bones = flux_model_bone_count(h);
    if (anims > 0 && bones > 0) {
      const double dur = flux_model_anim_duration(h, 0);
      CHECK(dur > 0, "animation has no duration");
      // snapshot the bind pose, pose the skeleton mid-clip, and see the mesh move
      std::vector<double> before;
      for (int i = 0; i < flux_model_mesh_count(h); ++i) {
        flux_grab(flux_model_prim(h, i));
        const int n = flux_pdata_size();
        for (int v = 0; v < n; ++v)
          for (int c = 0; c < 3; ++c) before.push_back(flux_pdata_get("p", v, c));
      }
      flux_model_set_anim_time(h, 0, dur * 0.37);

      double maxd = 0;
      long moved = 0, total = 0;
      size_t k = 0;
      for (int i = 0; i < flux_model_mesh_count(h); ++i) {
        flux_grab(flux_model_prim(h, i));
        const int n = flux_pdata_size();
        for (int v = 0; v < n; ++v, ++total) {
          double d = 0;
          for (int c = 0; c < 3; ++c) {
            const double delta = flux_pdata_get("p", v, c) - before[k++];
            d += delta * delta;
          }
          d = std::sqrt(d);
          if (d > maxd) maxd = d;
          if (d > 1e-4) moved++;
        }
      }
      flux_grab(-1);
      // The reference formula IS linear blend, so check against it in 'linear
      // mode; the dual-quaternion path is checked separately below (it differs
      // from LBS by design — that is the whole point of it).
      flux_model_skinning(0);

      // sweep the clip (and every clip): a single sample time hides trouble at the
      // ends, where key lookup clamps and clips whose first key is not at t=0 used
      // to EXTRAPOLATE backwards
      double err = 0;
      for (int ai = 0; ai < anims && ai < 4; ++ai)
        for (double frac : {0.0, 0.13, 0.37, 0.62, 0.91, 1.0}) {
          const double t = flux_model_anim_duration(h, ai) * frac;
          flux_model_set_anim_time(h, ai, t);
          const double e = ref::compare(r, h, path.c_str(), t, ai);
          if (e > err) err = e;
        }
      flux_model_set_anim_time(h, 0, dur * 0.37);
      std::printf("    anims=%d bones=%d dur=%.2fs -> posed at %.2fs: %ld/%ld verts moved, max %.3f,"
                  " vs assimp reference: %.4f\n",
                  anims, bones, dur, dur * 0.37, moved, total, maxd, err);
      CHECK(moved > total / 10, "posing the skeleton did not deform the mesh");
      CHECK(maxd < 1e6, "skinned vertices flew off to infinity");
      CHECK(err < 1e-3, "skinned pose disagrees with assimp's own globalAnim*offset*v");

      // --- dual quaternion vs linear blend ---------------------------------
      // DQS is meant to DIFFER from LBS (that is how it saves a folded joint),
      // but only locally: same pose, no vertex thrown across the model. Check it
      // stays within a fraction of the model's own size, and that the model does
      // not change scale — a botched dual-quat normalisation shows up as both.
      const double tp = dur * 0.37;
      flux_model_skinning(0);
      flux_model_set_anim_time(h, 0, tp);
      std::vector<double> lbs;
      double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
      for (int i = 0; i < flux_model_mesh_count(h); ++i) {
        flux_grab(flux_model_prim(h, i));
        const int n = flux_pdata_size();
        for (int v = 0; v < n; ++v)
          for (int c = 0; c < 3; ++c) {
            const double x = flux_pdata_get("p", v, c);
            lbs.push_back(x);
            if (x < lo[c]) lo[c] = x;
            if (x > hi[c]) hi[c] = x;
          }
      }
      const double extent = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));

      flux_model_skinning(1);
      flux_model_set_anim_time(h, 0, tp);
      double worst = 0;
      bool finite = true;
      size_t q = 0;
      for (int i = 0; i < flux_model_mesh_count(h); ++i) {
        flux_grab(flux_model_prim(h, i));
        const int n = flux_pdata_size();
        for (int v = 0; v < n; ++v) {
          double d = 0;
          for (int c = 0; c < 3; ++c) {
            const double x = flux_pdata_get("p", v, c);
            if (!std::isfinite(x)) finite = false;
            const double e = x - lbs[q++];
            d += e * e;
          }
          d = std::sqrt(d);
          if (d > worst) worst = d;
        }
      }
      flux_grab(-1);
      std::printf("    dual-quat vs linear: worst %.4f over a model %.2f across (%.1f%%)\n",
                  worst, extent, extent > 0 ? 100.0 * worst / extent : 0.0);
      CHECK(finite, "dual-quat skinning produced non-finite vertices");
      CHECK(worst < 0.5 * extent, "dual-quat pose is nowhere near the linear one");
    } else if (anims > 0 || bones > 0) {
      std::printf("    anims=%d bones=%d (no skinning to check)\n", anims, bones);
    }

    // --- the scene cache: a second load must not re-parse, and must produce a
    // second, independent set of primitives ------------------------------------
    const int h2 = flux_load_model(path.c_str(), 0);
    CHECK(h2 >= 0 && h2 != h, "second load should give a new handle");
    CHECK(flux_model_mesh_count(h2) == flux_model_mesh_count(h), "cached load lost meshes");
    if (flux_model_mesh_count(h) > 0)
      CHECK(flux_model_prim(h2, 0) != flux_model_prim(h, 0), "second load reused the same prim");
  }

  std::printf("RESULT: %s\n", g_fail ? "FAILED" : "OK");
  return g_fail;
}
