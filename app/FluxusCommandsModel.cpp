// SPDX-License-Identifier: AGPL-3.0-or-later
// Model domain of the fluxus command layer: 3D model import via assimp
// (fbx/gltf/glb/dae/ply/stl/3ds/obj…) into fluxus primitives.
//
// One INDEXED PolyPrimitive(TRILIST) per aiMesh, built the same way
// OBJPrimitiveIO::MakePrimitive does (Resize + SetDataRaw + GetIndex +
// SetIndexMode) — indexed is ~4x fewer verts than an unindexed TRILIST on real
// models, and PolyPrimitive::Render draws indexed straight from the VBO cache.
// Each mesh's node transform is baked into its vertices and every prim is
// parented to one locator, so a script moves the whole model by grabbing the root.
//
// The parsed aiScene is CACHED by path+flags: an immediate-mode sketch re-runs its
// whole buffer every frame, so (load-model …) would otherwise re-parse the file at
// 30 Hz. Rebuilding the prims from a cached scene is ~0.1 ms for a 6.5k-vert model;
// re-parsing is 5-6 ms. Retained mode is still the right way to use this.
//
// JUCE-free like every other command TU (CLAUDE.md gotcha 2) — image decoding for
// textures goes through flux_load_texture / flux_load_texture_mem, which live in
// the JUCE-side TextureLoader.cpp.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

#include "PolyPrimitive.h"
#include "LocatorPrimitive.h"
#include "State.h"
#include "dada.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Fluxus;

namespace {

// ---- imported scenes, cached by path+flags ---------------------------------
// Importer owns the aiScene, so the Importer has to outlive every use of it.
struct ImportedScene {
  Assimp::Importer imp;
  const aiScene*   scene = nullptr;
  std::string      dir;        // directory the file came from (texture paths are relative to it)
};

std::map<std::string, std::shared_ptr<ImportedScene>> g_sceneCache;
std::string g_lastError;

// A loaded model instance: the prims built from one import.
//
// Skinned models additionally get TWO locator trees mirroring the aiNode
// hierarchy — the live skeleton and a never-animated bindpose copy — because that
// is the shape the engine's SkinningPrimFunc wants (it computes, per node,
// global(skeleton) * global(bindpose)^-1). Both trees hang off the model root, so
// whatever transform a sketch puts on the root cancels out of that product and the
// skinning stays in model space.
struct SkinnedMesh {
  int primId = -1;
  int pfunc  = -1;      // 'skinning' pfunc, args already set
};

struct Model {
  std::shared_ptr<ImportedScene> src;
  int rootId = -1;
  std::vector<int>         prims;
  std::vector<std::string> names;
  // skeleton, in the SceneGraph::GetNodes order (pre-order DFS) that decides which
  // "w<n>" pdata channel belongs to which node
  std::vector<const aiNode*> nodes;
  std::vector<int>           skelIds;    // live locators, animated
  std::vector<int>           bindIds;    // bindpose locators, left alone
  std::vector<SkinnedMesh>   skinned;
  int skelRoot = -1, bindRoot = -1;
  std::vector<int>           parentIdx;   // parallel to nodes; -1 for the root
  std::vector<aiMatrix4x4>   bindLocal;   // bind-pose local per node (see bindLocals)
  // meshes that carry bones, noted during the walk (with the world transform baked
  // into their vertices) and wired up once the skeleton exists; plus the
  // per-animation channel lookup, filled lazily (one entry per node)
  struct PendingSkin { const aiMesh* mesh; int primId; aiMatrix4x4 meshWorld; };
  std::vector<PendingSkin> pendingSkin;
  std::map<int, std::vector<const aiNodeAnim*>> animChannels;
};

std::vector<std::unique_ptr<Model>> g_models;

Model* model(int h) {
  return (h >= 0 && h < (int) g_models.size()) ? g_models[(size_t) h].get() : nullptr;
}

std::string dirOf(const std::string& path) {
  const size_t cut = path.find_last_of('/');
  return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

// ---- assimp <-> fluxus conversions -----------------------------------------
// assimp's aiMatrix4x4 is row-major with a column-vector convention (v' = M*v);
// fluxus's dMatrix is row-major with a ROW-vector convention (v' = v*M). So the
// element copy is a transpose. dMatrix::operator* is reversed relative to a
// standard matrix product, which is why composition below reads
// `local * parent` rather than `parent * local`.
dMatrix aiToD(const aiMatrix4x4& m) {
  dMatrix o;
  o.m[0][0] = m.a1; o.m[1][0] = m.a2; o.m[2][0] = m.a3; o.m[3][0] = m.a4;
  o.m[0][1] = m.b1; o.m[1][1] = m.b2; o.m[2][1] = m.b3; o.m[3][1] = m.b4;
  o.m[0][2] = m.c1; o.m[1][2] = m.c2; o.m[2][2] = m.c3; o.m[3][2] = m.c4;
  o.m[0][3] = m.d1; o.m[1][3] = m.d2; o.m[2][3] = m.d3; o.m[3][3] = m.d4;
  return o;
}

// The flag set ofxAssimpModelLoader uses, minus aiProcess_ConvertToLeftHanded —
// fluxus is right-handed like GL, so converting would mirror every model.
unsigned importFlags(int mode) {
  unsigned flags = aiProcess_CalcTangentSpace         | aiProcess_GenSmoothNormals |
                   aiProcess_JoinIdenticalVertices    | aiProcess_ImproveCacheLocality |
                   aiProcess_LimitBoneWeights         | aiProcess_RemoveRedundantMaterials |
                   aiProcess_SplitLargeMeshes         | aiProcess_Triangulate |
                   aiProcess_GenUVCoords              | aiProcess_SortByPType |
                   aiProcess_FindDegenerates          | aiProcess_FindInstances |
                   aiProcess_OptimizeMeshes;
  if (mode == 1) flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords;
  if (mode == 2) flags |= aiProcess_OptimizeGraph | aiProcess_ValidateDataStructure;
  return flags;
}

// ---- textures ---------------------------------------------------------------
// Resolve a material's diffuse texture to a GL id: embedded ("*0" in glTF/FBX)
// through the in-memory decoder, otherwise a path relative to the model's folder.
unsigned meshTexture(const ImportedScene& src, const aiMaterial* mtl) {
  aiString texPath;
  if (mtl->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS) return 0;
  std::string p = texPath.C_Str();
  if (p.empty()) return 0;

  if (const aiTexture* emb = src.scene->GetEmbeddedTexture(p.c_str())) {
    // mHeight == 0 => mWidth is a byte count of an encoded (png/jpg) image, which
    // is what glTF/FBX ship. Raw-pixel embeds (mHeight != 0) are rare; skip them.
    if (emb->mHeight == 0 && emb->mWidth > 0)
      return flux_load_texture_mem(emb->pcData, (int) emb->mWidth, (src.dir + "#" + p).c_str());
    return 0;
  }

  if (p.size() > 2 && p[0] == '/' && p[1] == '/') p = p.substr(2);   // Blender's "//rel/path"
  const std::string full = (!p.empty() && p[0] == '/') ? p : src.dir + "/" + p;
  return flux_load_texture(full.c_str());
}

// ---- mesh -> primitive ------------------------------------------------------
// `xf` is the mesh's world transform, baked into the vertices: fluxus has one
// State.Transform per primitive and we want the model to arrive pre-assembled.
PolyPrimitive* makePrim(const aiMesh* m, const dMatrix& xf) {
  PolyPrimitive* prim = new PolyPrimitive(PolyPrimitive::TRILIST);
  const unsigned n = m->mNumVertices;
  prim->Resize(n);

  std::vector<dVector, FLX_ALLOC(dVector)> pos(n);
  for (unsigned i = 0; i < n; ++i)
    pos[i] = xf.transform(dVector(m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z));
  prim->SetDataRaw("p", new TypedPData<dVector>(pos));

  if (m->HasNormals()) {
    std::vector<dVector, FLX_ALLOC(dVector)> nrm(n);
    for (unsigned i = 0; i < n; ++i)
      nrm[i] = xf.transform_no_trans(dVector(m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z));
    prim->SetDataRaw("n", new TypedPData<dVector>(nrm));
  }
  if (m->GetNumUVChannels() > 0) {
    // flux_load_texture uploads images flipped in Y (it matches the font atlas and
    // (load-texture)), so the V coordinate is flipped here rather than in the
    // shared loader.
    std::vector<dVector, FLX_ALLOC(dVector)> tex(n);
    for (unsigned i = 0; i < n; ++i)
      tex[i] = dVector(m->mTextureCoords[0][i].x, 1.0f - m->mTextureCoords[0][i].y, 0);
    prim->SetDataRaw("t", new TypedPData<dVector>(tex));
  }
  if (m->GetNumColorChannels() > 0) {
    std::vector<dColour, FLX_ALLOC(dColour)> col(n);
    for (unsigned i = 0; i < n; ++i) {
      const aiColor4D& c = m->mColors[0][i];
      col[i] = dColour(c.r, c.g, c.b, c.a);
    }
    prim->SetDataRaw("c", new TypedPData<dColour>(col));
  }

  std::vector<unsigned int> idx;
  idx.reserve((size_t) m->mNumFaces * 3);
  for (unsigned f = 0; f < m->mNumFaces; ++f) {
    const aiFace& face = m->mFaces[f];
    if (face.mNumIndices != 3) continue;      // aiProcess_Triangulate guarantees tris
    idx.push_back(face.mIndices[0]);
    idx.push_back(face.mIndices[1]);
    idx.push_back(face.mIndices[2]);
  }
  prim->GetIndex() = idx;
  prim->SetIndexMode(true);
  return prim;
}

// Apply the file's material to a prim's State. Must run AFTER addPrim: the
// renderer copies its own build State over a freshly added primitive's, so
// anything set before is silently discarded (CLAUDE.md, GPU section).
void applyMaterial(const ImportedScene& src, const aiMesh* m, Primitive* p) {
  if (m->mMaterialIndex >= src.scene->mNumMaterials) return;
  aiMaterial* mtl = src.scene->mMaterials[m->mMaterialIndex];
  State* s = p->GetState();
  aiColor4D c;
  if (aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE,  &c) == AI_SUCCESS) s->Colour   = dColour(c.r, c.g, c.b, c.a);
  if (aiGetMaterialColor(mtl, AI_MATKEY_COLOR_SPECULAR, &c) == AI_SUCCESS) s->Specular = dColour(c.r, c.g, c.b, c.a);
  if (aiGetMaterialColor(mtl, AI_MATKEY_COLOR_AMBIENT,  &c) == AI_SUCCESS) s->Ambient  = dColour(c.r, c.g, c.b, c.a);
  if (aiGetMaterialColor(mtl, AI_MATKEY_COLOR_EMISSIVE, &c) == AI_SUCCESS) s->Emissive = dColour(c.r, c.g, c.b, c.a);
  float shininess = 0;
  if (aiGetMaterialFloat(mtl, AI_MATKEY_SHININESS, &shininess) == AI_SUCCESS && shininess > 0)
    s->Shinyness = shininess;
  float opacity = 1;
  if (aiGetMaterialFloat(mtl, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS && opacity < 1.0f)
    s->Opacity = opacity;
  // two-sided materials: don't cull, or the far side of the shell disappears
  int twoSided = 0;
  unsigned max = 1;
  if (aiGetMaterialIntegerArray(mtl, AI_MATKEY_TWOSIDED, &twoSided, &max) == AI_SUCCESS && twoSided)
    s->Hints &= ~HINT_CULL_CCW;

  if (const unsigned tex = meshTexture(src, mtl)) {
    s->Textures[0] = tex;
    // Apple's GL-on-Metal ignores fixed-function texturing, so a textured prim
    // needs the built-in texturing shader — same rule flux_texture follows.
    if (!s->Shader) setStateShader(s, builtinTexShader());
    // an all-white diffuse under a texture is the common case; keep the texture
    // readable rather than multiplying it down by a dark material colour
  }
  if (m->GetNumColorChannels() > 0) s->Hints |= HINT_VERTCOLS;
}

// ---- skeleton ---------------------------------------------------------------
// SkinningPrimFunc reads one float channel per node of the skeleton it is given,
// in SceneGraph::GetNodes order — a PRE-ORDER DFS. So the locator tree is built in
// the same pre-order as the aiNode tree, and channel "w<i>" belongs to nodes[i].
// Nodes that deform nothing still need a (zero) channel.
void collectNodes(const aiNode* n, int parent, std::vector<const aiNode*>& out,
                  std::vector<int>& parentIdx) {
  const int self = (int) out.size();
  out.push_back(n);
  parentIdx.push_back(parent);
  for (unsigned i = 0; i < n->mNumChildren; ++i) collectNodes(n->mChildren[i], self, out, parentIdx);
}

// One locator per node, parented like the node tree and carrying the LOCAL
// transform given in `locals` — the scene graph then reproduces the globals.
// Pre-order guarantees a parent's locator exists before its children's.
void buildLocators(const std::vector<const aiNode*>& nodes, const std::vector<int>& parentIdx,
                   const std::vector<aiMatrix4x4>& locals, int rootParent, std::vector<int>& ids) {
  ids.clear();
  ids.reserve(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    g_ctx.tx     = dMatrix();
    g_ctx.parent = parentIdx[i] < 0 ? rootParent : ids[(size_t) parentIdx[i]];
    const int id = addPrim(new LocatorPrimitive());
    if (id < 0) return;
    if (Primitive* p = g_ctx.r->GetPrimitive(id)) p->GetState()->Transform = aiToD(locals[i]);
    ids.push_back(id);
  }
}

// The BIND POSE is defined by the bones' offset matrices, NOT by the node
// hierarchy's rest transforms — in FBX the two routinely disagree (the rest pose
// is just whatever pose the file was saved in). Skinning against the rest pose
// therefore shears the mesh: measured against assimp's own
// globalAnim*offset*v formula, the fox came out 35.9 units off, astroBoy 0.11.
//
// offset maps mesh space -> bone space in the bind pose, and our vertices already
// have the mesh node's world transform baked in, so the bind global we want is
//   bindGlobal(bone) = meshWorld * offset^-1
// and the local transform to hand a locator is  parentBind^-1 * bindGlobal.
// Nodes no bone references keep their rest transform relative to their parent.
std::vector<aiMatrix4x4> bindLocals(const std::vector<const aiNode*>& nodes,
                                    const std::vector<int>& parentIdx,
                                    const std::vector<std::pair<const aiMesh*, aiMatrix4x4>>& meshes) {
  std::map<std::string, size_t> slot;
  for (size_t i = 0; i < nodes.size(); ++i) slot[nodes[i]->mName.C_Str()] = i;

  std::vector<aiMatrix4x4> global(nodes.size());
  std::vector<char> known(nodes.size(), 0);
  for (const auto& mw : meshes) {
    const aiMesh* m = mw.first;
    for (unsigned b = 0; b < m->mNumBones; ++b) {
      auto it = slot.find(m->mBones[b]->mName.C_Str());
      if (it == slot.end() || known[it->second]) continue;
      aiMatrix4x4 inv = m->mBones[b]->mOffsetMatrix;
      inv.Inverse();
      global[it->second] = mw.second * inv;
      known[it->second] = 1;
    }
  }
  // fill the gaps in pre-order, so a parent's global is settled first
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (known[i]) continue;
    global[i] = parentIdx[i] < 0 ? nodes[i]->mTransformation
                                 : global[(size_t) parentIdx[i]] * nodes[i]->mTransformation;
    known[i] = 1;
  }

  std::vector<aiMatrix4x4> locals(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (parentIdx[i] < 0) { locals[i] = global[i]; continue; }
    aiMatrix4x4 pinv = global[(size_t) parentIdx[i]];
    pinv.Inverse();
    locals[i] = pinv * global[i];
  }
  return locals;
}

// pdata the engine's skinning needs: w<i> per skeleton node, plus pref/nref (the
// bind-pose copies it interpolates from).
void setupSkin(Model& mo, const aiMesh* m, int primId) {
  Primitive* p = g_ctx.r->GetPrimitive(primId);
  if (!p) return;
  const size_t nodes = mo.nodes.size();
  const unsigned nverts = m->mNumVertices;

  std::map<std::string, size_t> slot;
  for (size_t i = 0; i < nodes; ++i) slot[mo.nodes[i]->mName.C_Str()] = i;

  std::vector<std::vector<float, FLX_ALLOC(float)>> w(
      nodes, std::vector<float, FLX_ALLOC(float)>(nverts, 0.0f));
  for (unsigned b = 0; b < m->mNumBones; ++b) {
    const aiBone* bone = m->mBones[b];
    auto it = slot.find(bone->mName.C_Str());
    if (it == slot.end()) continue;               // a bone with no node: nothing to drive it
    for (unsigned k = 0; k < bone->mNumWeights; ++k) {
      const aiVertexWeight& vw = bone->mWeights[k];
      if (vw.mVertexId < nverts) w[it->second][vw.mVertexId] = vw.mWeight;
    }
  }
  for (size_t i = 0; i < nodes; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "w%d", (int) i);
    p->AddData(name, new TypedPData<float>(w[i]));
  }
  if (auto* pos = p->GetDataVec<dVector>("p")) p->AddData("pref", new TypedPData<dVector>(*pos));
  if (auto* nrm = p->GetDataVec<dVector>("n")) p->AddData("nref", new TypedPData<dVector>(*nrm));

  SkinnedMesh sm;
  sm.primId = primId;
  sm.pfunc  = flux_pfunc_make("skinning");
  if (sm.pfunc >= 0) {
    flux_pfunc_set_int(sm.pfunc, "skeleton-root", mo.skelRoot);
    flux_pfunc_set_int(sm.pfunc, "bindpose-root", mo.bindRoot);
    flux_pfunc_set_int(sm.pfunc, "skin-normals", p->GetDataVec<dVector>("n") ? 1 : 0);
  }
  mo.skinned.push_back(sm);
}

// ---- animation --------------------------------------------------------------
// Keyframe sampling, the same shape ofxAssimpAnimation uses: per channel, find the
// key pair around `ticks` and interpolate (slerp for rotation).
template <class KeyT>
unsigned keyBefore(const KeyT* keys, unsigned n, double ticks) {
  for (unsigned i = 0; i + 1 < n; ++i) if (ticks < keys[i + 1].mTime) return i;
  return n ? n - 1 : 0;
}

// Interpolation factor between key i and key j, CLAMPED. Clips routinely start at
// a tick > 0 (glTF especially), and for ticks before the first key the raw factor
// goes negative — which does not "hold the first pose", it EXTRAPOLATES away from
// it, stretching whichever limb that channel drives.
float keyFactor(double ticks, double ti, double tj) {
  const double span = tj - ti;
  if (span <= 0) return 0.0f;
  const double f = (ticks - ti) / span;
  return (float) (f < 0 ? 0 : (f > 1 ? 1 : f));
}

// `node` supplies the defaults. A channel does NOT have to animate all three
// components, and FBX in particular splits a node into $AssimpFbx$ helper nodes
// where a channel often carries ONLY rotation (or only translation) while the
// node's own transform holds a unit-conversion scale. Defaulting the missing
// components to identity throws that scale away — the fox rendered ~50x too big
// the moment the animation started, while the bind pose was fine.
aiMatrix4x4 sampleChannel(const aiNodeAnim* ch, double ticks, const aiNode* node) {
  aiVector3D   pos, scl;
  aiQuaternion rot;
  node->mTransformation.Decompose(scl, rot, pos);
  if (ch->mNumPositionKeys) {
    const unsigned i = keyBefore(ch->mPositionKeys, ch->mNumPositionKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumPositionKeys) ? i + 1 : i;
    const float f = keyFactor(ticks, ch->mPositionKeys[i].mTime, ch->mPositionKeys[j].mTime);
    pos = ch->mPositionKeys[i].mValue + (ch->mPositionKeys[j].mValue - ch->mPositionKeys[i].mValue) * f;
  }
  if (ch->mNumScalingKeys) {
    const unsigned i = keyBefore(ch->mScalingKeys, ch->mNumScalingKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumScalingKeys) ? i + 1 : i;
    const float f = keyFactor(ticks, ch->mScalingKeys[i].mTime, ch->mScalingKeys[j].mTime);
    scl = ch->mScalingKeys[i].mValue + (ch->mScalingKeys[j].mValue - ch->mScalingKeys[i].mValue) * f;
  }
  if (ch->mNumRotationKeys) {
    const unsigned i = keyBefore(ch->mRotationKeys, ch->mNumRotationKeys, ticks);
    const unsigned j = (i + 1 < ch->mNumRotationKeys) ? i + 1 : i;
    const float f = keyFactor(ticks, ch->mRotationKeys[i].mTime, ch->mRotationKeys[j].mTime);
    // assimp's own slerp — the engine's dQuat::slerp is broken (see CLAUDE.md)
    aiQuaternion::Interpolate(rot, ch->mRotationKeys[i].mValue, ch->mRotationKeys[j].mValue, f);
    rot.Normalize();
  }
  aiMatrix4x4 m(scl, rot, pos);
  return m;
}

// walk the node tree, emitting a prim per (node, mesh) pair
void walk(Model& mo, const aiNode* node, const dMatrix& parentXf, const aiMatrix4x4& parentAi) {
  // assimp composes world = parentWorld * local in ITS (column-vector) convention.
  // Transposed into fluxus's row-vector dMatrix that is the standard product
  // local·parentWorld, and the engine's reversed operator* writes it PARENT FIRST.
  // Verified against assimp's own aiMatrix4x4 product over astroBoy's rig: the
  // other order is exact only for a flat hierarchy (which is why a two-node fbx
  // looks right either way) and diverges by up to 42 units on a real skeleton.
  const dMatrix world = parentXf * aiToD(node->mTransformation);
  const aiMatrix4x4 worldAi = parentAi * node->mTransformation;   // same thing, assimp's convention
  for (unsigned i = 0; i < node->mNumMeshes; ++i) {
    const aiMesh* m = mo.src->scene->mMeshes[node->mMeshes[i]];
    if (m->mNumFaces == 0) continue;
    const int id = addPrim(makePrim(m, world));
    if (id < 0) continue;
    applyMaterial(*mo.src, m, g_ctx.r->GetPrimitive(id));
    mo.prims.push_back(id);
    mo.names.push_back(m->mName.length ? m->mName.C_Str() : node->mName.C_Str());
    if (m->mNumBones > 0) mo.pendingSkin.push_back({m, id, worldAi});
  }
  for (unsigned i = 0; i < node->mNumChildren; ++i) walk(mo, node->mChildren[i], world, worldAi);
}

} // namespace

// Immediate mode re-runs the whole buffer every frame, so (load-model …) is called
// again each frame and would pile up handles. The parsed-scene cache is kept (it is
// the expensive part); only the handles go. Retained mode loads once, so keep them.
void modelsFrameBegin() {
  if (!flux_retained_on()) g_models.clear();
}

extern "C" {

int flux_load_model(const char* path, int flags) {
  if (!path || !*path || !g_ctx.r) return -1;
  const std::string key = std::string(path) + "#" + std::to_string(flags);

  auto it = g_sceneCache.find(key);
  if (it == g_sceneCache.end()) {
    auto src = std::make_shared<ImportedScene>();
    src->scene = src->imp.ReadFile(path, importFlags(flags));
    if (!src->scene || !src->scene->mRootNode) {
      g_lastError = src->imp.GetErrorString() ? src->imp.GetErrorString() : "unknown assimp error";
      return -1;
    }
    src->dir = dirOf(path);
    it = g_sceneCache.emplace(key, std::move(src)).first;
  }

  auto mo = std::unique_ptr<Model>(new Model());
  mo->src = it->second;

  // one locator holding the whole model, built under the CURRENT build state so
  // (with-state (translate …) (load-model …)) places it like any other primitive
  mo->rootId = addPrim(new LocatorPrimitive());

  // the meshes themselves are parented to it, with an identity build transform:
  // node transforms are already baked into the vertices
  const BuildState saved = g_ctx;
  g_ctx.tx = dMatrix();
  g_ctx.parent = mo->rootId;
  g_ctx.texture = 0;
  walk(*mo, mo->src->scene->mRootNode, dMatrix(), aiMatrix4x4());

  // skinned meshes: two locator trees mirroring the node hierarchy (live +
  // bindpose), then the per-node weight channels. Both trees hang off the model
  // root, so a transform on the root cancels out of skeleton*bindpose^-1.
  // Both START in the bind pose, so a model with bones but no animation — or one
  // that has not been posed yet — draws exactly as it was imported.
  if (!mo->pendingSkin.empty()) {
    collectNodes(mo->src->scene->mRootNode, -1, mo->nodes, mo->parentIdx);
    std::vector<std::pair<const aiMesh*, aiMatrix4x4>> skinMeshes;
    for (const auto& ps : mo->pendingSkin) skinMeshes.emplace_back(ps.mesh, ps.meshWorld);
    mo->bindLocal = bindLocals(mo->nodes, mo->parentIdx, skinMeshes);
    buildLocators(mo->nodes, mo->parentIdx, mo->bindLocal, mo->rootId, mo->skelIds);
    buildLocators(mo->nodes, mo->parentIdx, mo->bindLocal, mo->rootId, mo->bindIds);
    mo->skelRoot = mo->skelIds.empty() ? -1 : mo->skelIds[0];
    mo->bindRoot = mo->bindIds.empty() ? -1 : mo->bindIds[0];
    if (mo->skelRoot >= 0 && mo->bindRoot >= 0)
      for (const auto& ps : mo->pendingSkin) setupSkin(*mo, ps.mesh, ps.primId);
  }
  static_cast<BuildState&>(g_ctx) = saved;

  g_models.push_back(std::move(mo));
  g_lastError.clear();
  return (int) g_models.size() - 1;
}

int  flux_model_root(int h)       { Model* m = model(h); return m ? m->rootId : -1; }
int  flux_model_mesh_count(int h) { Model* m = model(h); return m ? (int) m->prims.size() : 0; }

int flux_model_prim(int h, int i) {
  Model* m = model(h);
  return (m && i >= 0 && i < (int) m->prims.size()) ? m->prims[(size_t) i] : -1;
}

const char* flux_model_mesh_name(int h, int i) {
  Model* m = model(h);
  return (m && i >= 0 && i < (int) m->names.size()) ? m->names[(size_t) i].c_str() : "";
}

const char* flux_model_error(void) { return g_lastError.c_str(); }

// ---- animation --------------------------------------------------------------
int flux_model_anim_count(int h) {
  Model* m = model(h);
  return (m && m->src && m->src->scene) ? (int) m->src->scene->mNumAnimations : 0;
}

double flux_model_anim_duration(int h, int a) {
  Model* m = model(h);
  if (!m || !m->src || !m->src->scene || a < 0 || a >= (int) m->src->scene->mNumAnimations) return 0;
  const aiAnimation* an = m->src->scene->mAnimations[a];
  const double tps = an->mTicksPerSecond != 0.0 ? an->mTicksPerSecond : 25.0;
  return an->mDuration / tps;                    // seconds
}

// Pose the skeleton at time t (SECONDS, wrapped into the clip) and re-skin every
// skinned mesh. Cheap enough to call once per frame from an every-frame thunk.
void flux_model_set_anim_time(int h, int a, double t) {
  Model* m = model(h);
  if (!m || !m->src || !m->src->scene || !g_ctx.r) return;
  const aiScene* sc = m->src->scene;
  if (a < 0 || a >= (int) sc->mNumAnimations || m->skelIds.size() != m->nodes.size()) return;
  const aiAnimation* an = sc->mAnimations[a];

  // one channel lookup per node, built once per animation
  auto ch = m->animChannels.find(a);
  if (ch == m->animChannels.end()) {
    std::vector<const aiNodeAnim*> byNode(m->nodes.size(), nullptr);
    for (unsigned c = 0; c < an->mNumChannels; ++c) {
      const aiNodeAnim* na = an->mChannels[c];
      for (size_t i = 0; i < m->nodes.size(); ++i)
        if (m->nodes[i]->mName == na->mNodeName) { byNode[i] = na; break; }
    }
    ch = m->animChannels.emplace(a, std::move(byNode)).first;
  }

  const double tps = an->mTicksPerSecond != 0.0 ? an->mTicksPerSecond : 25.0;
  double ticks = an->mDuration > 0 ? std::fmod(t * tps, an->mDuration) : 0.0;
  if (ticks < 0) ticks += an->mDuration;

  // Pose the LIVE skeleton. A node the clip does not animate keeps its REST local
  // (node->mTransformation) — that is what assimp's own globalAnim*offset*v does,
  // and model_test checks us against it. (Falling back to the bind local instead
  // is tempting for the 7 leaf bones the fox clip ignores, but it puts astroBoy
  // 0.0195 off the canonical result for no visible gain.)
  for (size_t i = 0; i < m->nodes.size(); ++i) {
    Primitive* p = g_ctx.r->GetPrimitive(m->skelIds[i]);
    if (!p) continue;
    const aiNodeAnim* na = ch->second[i];
    p->GetState()->Transform =
        aiToD(na ? sampleChannel(na, ticks, m->nodes[i]) : m->nodes[i]->mTransformation);
  }

  // then run the engine's skinning pfunc over each skinned mesh
  const int prevGrab = g_ctx.grabbedId;
  for (const SkinnedMesh& sm : m->skinned) {
    if (sm.pfunc < 0) continue;
    flux_grab(sm.primId);
    flux_pfunc_run(sm.pfunc);
    // the pfunc writes p/n behind the pdata layer's back; without this the VBO
    // cache keeps drawing the bind pose (PolyPrimitive::UpdateVBO is version-keyed)
    if (g_ctx.grabbed) g_ctx.grabbed->BumpPDataVersion();
  }
  flux_grab(prevGrab);
}

int flux_model_bone_count(int h) { Model* m = model(h); return m ? (int) m->skelIds.size() : 0; }

int flux_model_bone(int h, int i) {
  Model* m = model(h);
  return (m && i >= 0 && i < (int) m->skelIds.size()) ? m->skelIds[(size_t) i] : -1;
}

const char* flux_model_bone_name(int h, int i) {
  Model* m = model(h);
  return (m && i >= 0 && i < (int) m->nodes.size()) ? m->nodes[(size_t) i]->mName.C_Str() : "";
}

void flux_model_free(int h) {
  if (Model* m = model(h)) { m->prims.clear(); m->names.clear(); m->src.reset(); m->rootId = -1; }
}

} // extern "C"
