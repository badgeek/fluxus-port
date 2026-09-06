// SPDX-License-Identifier: AGPL-3.0-or-later
// See GLESBackend.h. Compiled WITHOUT the shim (build.sh only force-includes it
// for engine TUs), so the GL calls below are the real GLES ones.

#include "GLESBackend.h"
#include "LegacyCapture.h"

#include <GLES3/gl32.h>

#include <cstdio>
#include <cstring>

using Fluxus::RPrim;
using Fluxus::RVertexArrays;

namespace {

// GLSL ES 1.00: attribute/varying, works on any ES2+ context.
const char* kVert =
    "#version 100\n"
    "attribute vec3 aPos;\n"
    "attribute vec3 aNrm;\n"
    "attribute vec4 aCol;\n"
    "attribute vec3 aTex;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uMV;\n"
    "varying vec3 vNrm;\n"
    "varying vec4 vCol;\n"
    "varying vec2 vTex;\n"
    "void main() {\n"
    "  vNrm = mat3(uMV[0].xyz, uMV[1].xyz, uMV[2].xyz) * aNrm;\n"
    "  vCol = aCol;\n"
    "  vTex = aTex.xy;\n"
    "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "  gl_PointSize = 4.0;\n"
    "}\n";

// One hard-coded headlight stands in for the whole fixed-function lighting
// block (Light.cpp, 28 errors in the audit) — enough to tell whether normals
// and winding survived the port.
const char* kFrag =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform vec4  uColour;\n"
    "uniform float uUseVertCol;\n"
    "uniform float uUnlit;\n"
    "uniform sampler2D uSampler;\n"
    "uniform float uUseTex;\n"
    "varying vec3  vNrm;\n"
    "varying vec4  vCol;\n"
    "varying vec2  vTex;\n"
    "void main() {\n"
    "  vec4 base = mix(uColour, vCol, uUseVertCol);\n"
    // Modulate, matching desktop's default texture-env (GL_MODULATE): the
    // diffuse image tints the material/vertex colour rather than replacing it.
    "  vec3 texRgb = texture2D(uSampler, vTex).rgb;\n"
    "  base.rgb = mix(base.rgb, base.rgb * texRgb, uUseTex);\n"
    "  vec3 n = normalize(vNrm);\n"
    "  float d = max(dot(n, normalize(vec3(0.3, 0.5, 1.0))), 0.0);\n"
    "  vec3 lit = base.rgb * (0.25 + 0.75 * d);\n"
    "  gl_FragColor = vec4(mix(lit, base.rgb, uUnlit), base.a);\n"
    "}\n";

unsigned compile(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[gles] shader compile failed: %s\n", log);
    return 0;
  }
  return s;
}

void mul4(const float* a, const float* b, float* out) {   // out = a * b, GL order
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
}

GLESBackend* g_backend = nullptr;   // for the legacy-capture entry points

}  // namespace

// --- lifecycle --------------------------------------------------------------

bool GLESBackend::init() {
  unsigned vs = compile(GL_VERTEX_SHADER, kVert);
  unsigned fs = compile(GL_FRAGMENT_SHADER, kFrag);
  if (!vs || !fs) return false;
  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  int ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[gles] link failed: %s\n", log);
    return false;
  }
  aPos = glGetAttribLocation(program, "aPos");
  aNrm = glGetAttribLocation(program, "aNrm");
  aCol = glGetAttribLocation(program, "aCol");
  aTex = glGetAttribLocation(program, "aTex");
  uMVP = glGetUniformLocation(program, "uMVP");
  uMV  = glGetUniformLocation(program, "uMV");
  uColour     = glGetUniformLocation(program, "uColour");
  uUseVertCol = glGetUniformLocation(program, "uUseVertCol");
  uUnlit      = glGetUniformLocation(program, "uUnlit");
  uSampler    = glGetUniformLocation(program, "uSampler");
  uUseTex     = glGetUniformLocation(program, "uUseTex");

  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ibo);

  float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  std::memcpy(proj, ident, sizeof(proj));
  stack.assign(ident, ident + 16);
  g_backend = this;
  return true;
}

void GLESBackend::shutdown() {
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ibo) glDeleteBuffers(1, &ibo);
  if (program) glDeleteProgram(program);
  g_backend = nullptr;
}

// --- matrix stack (the fixed-function one is gone; this is it) --------------

void GLESBackend::setProjection(const float* m) { std::memcpy(proj, m, sizeof(proj)); }

void GLESBackend::pushMatrix() {
  stack.insert(stack.end(), stack.end() - 16, stack.end());
}
void GLESBackend::popMatrix() {
  if (stack.size() > 16) stack.resize(stack.size() - 16);
}
void GLESBackend::loadMatrix(const float* m) {
  std::memcpy(&stack[stack.size() - 16], m, 16 * sizeof(float));
}
// The fixed-function pipeline let callers ask GL for this
// (glGetFloatv(GL_MODELVIEW_MATRIX)); here the stack IS the answer.
void GLESBackend::getModelView(float* m) {
  std::memcpy(m, &stack[stack.size() - 16], 16 * sizeof(float));
}

void GLESBackend::getProjection(float* m) { std::memcpy(m, proj, sizeof(proj)); }

void GLESBackend::multMatrix(const float* m) {
  float out[16];
  mul4(&stack[stack.size() - 16], m, out);
  std::memcpy(&stack[stack.size() - 16], out, sizeof(out));
}

// --- state ------------------------------------------------------------------

void GLESBackend::setColour(float r, float g, float b, float a) {
  colour[0] = r; colour[1] = g; colour[2] = b; colour[3] = a;
}
void GLESBackend::setMaterial(const float*, const float* emissive,
                              const float* diffuse, const float*, float) {
  // No material model in this spike: take diffuse as the base colour, and let a
  // non-black emissive win, which is what the unlit-ish prims rely on.
  if (diffuse) { for (int i = 0; i < 4; ++i) colour[i] = diffuse[i]; }
  if (emissive && (emissive[0] + emissive[1] + emissive[2]) > 0.01f)
    for (int i = 0; i < 3; ++i) colour[i] = emissive[i];
}
void GLESBackend::setLineWidth(float w) { glLineWidth(w); }
void GLESBackend::setPointSize(float)   {}   // written by the vertex shader
void GLESBackend::setBlend(int src, int dst) { glBlendFunc((GLenum) src, (GLenum) dst); }
void GLESBackend::setCull(bool on)      { if (on) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void GLESBackend::setFrontFaceCW(bool cw) { glFrontFace(cw ? GL_CW : GL_CCW); }

void GLESBackend::applyUniforms() {
  glUseProgram(program);
  const float* mv = &stack[stack.size() - 16];
  float mvp[16];
  mul4(proj, mv, mvp);
  glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
  glUniformMatrix4fv(uMV,  1, GL_FALSE, mv);
  glUniform4fv(uColour, 1, colour);
  glUniform1f(uUnlit, unlit ? 1.0f : 0.0f);
  glUniform1f(uUseTex, curTex ? 1.0f : 0.0f);
  if (curTex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, curTex);
    glUniform1i(uSampler, 0);
  }
}

// --- geometry ---------------------------------------------------------------

void GLESBackend::drawArrays(RPrim prim, const RVertexArrays& v, int count,
                             const unsigned int* index, int indexCount) {
  // This is where GLES pays for having no glPolygonMode: a wireframe request
  // becomes real edge geometry, built from the same topology.
  const bool asLines = (fill == Fluxus::RFill::Line);
  unsigned legacy = fluxspike::kTriangles;
  switch (prim) {
    case RPrim::Triangles: legacy = fluxspike::kTriangles; break;
    case RPrim::Quads:     legacy = fluxspike::kQuads;     break;
    case RPrim::TriStrip:  legacy = fluxspike::kTriStrip;  break;
    case RPrim::TriFan:    legacy = fluxspike::kTriFan;    break;
    case RPrim::Polygon:   legacy = fluxspike::kPolygon;   break;
    case RPrim::Lines:     legacy = fluxspike::kLines;     break;
  }
  // No uniform writes here: nothing has bound the program yet, and a uniform
  // set with no current program is GL_INVALID_OPERATION. drawRaw sets it right
  // after applyUniforms().
  drawRaw(legacy, v.pos, v.stride, v.nrm, v.tex, v.col, count, index, indexCount, asLines);
}

// The one routine that matters. GLES has neither GL_QUADS/GL_POLYGON nor
// glPolygonMode, so both are resolved here into index buffers.
void GLESBackend::drawRaw(unsigned legacyMode, const void* pos, int posStride,
                          const void* nrm, const void* tex, const void* col, int count,
                          const unsigned int* index, int indexCount,
                          bool asLines) {
  if (!pos) return;
  const int stride = posStride ? posStride : 16;

  // Vertex count: given directly for array draws, derived from the indices for
  // indexed ones (the engine passes count == 0 there).
  int verts = count;
  if (index && indexCount > 0) {
    unsigned hi = 0;
    for (int i = 0; i < indexCount; ++i) if (index[i] > hi) hi = index[i];
    verts = (int) hi + 1;
  }
  if (verts <= 0) return;

  applyUniforms();
  glUniform1f(uUseVertCol, col ? 1.0f : 0.0f);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) verts * stride, pos, GL_STREAM_DRAW);
  glEnableVertexAttribArray(aPos);
  glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, stride, (void*) 0);

  // Normals, texcoords and colours live in their own arrays; upload each into
  // its own region rather than juggling several buffers.
  static unsigned nrmVbo = 0, colVbo = 0, texVbo = 0;
  if (!nrmVbo) { glGenBuffers(1, &nrmVbo); glGenBuffers(1, &colVbo); glGenBuffers(1, &texVbo); }
  if (nrm) {
    glBindBuffer(GL_ARRAY_BUFFER, nrmVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) verts * stride, nrm, GL_STREAM_DRAW);
    glEnableVertexAttribArray(aNrm);
    glVertexAttribPointer(aNrm, 3, GL_FLOAT, GL_FALSE, stride, (void*) 0);
  } else {
    glDisableVertexAttribArray(aNrm);
    glVertexAttrib3f(aNrm, 0, 0, 1);
  }
  if (tex && aTex >= 0) {
    glBindBuffer(GL_ARRAY_BUFFER, texVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) verts * stride, tex, GL_STREAM_DRAW);
    glEnableVertexAttribArray(aTex);
    glVertexAttribPointer(aTex, 3, GL_FLOAT, GL_FALSE, stride, (void*) 0);
  } else if (aTex >= 0) {
    glDisableVertexAttribArray(aTex);
    glVertexAttrib3f(aTex, 0, 0, 0);
  }
  if (col) {
    glBindBuffer(GL_ARRAY_BUFFER, colVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) verts * stride, col, GL_STREAM_DRAW);
    glEnableVertexAttribArray(aCol);
    glVertexAttribPointer(aCol, 4, GL_FLOAT, GL_FALSE, stride, (void*) 0);
  } else {
    glDisableVertexAttribArray(aCol);
    glVertexAttrib4f(aCol, colour[0], colour[1], colour[2], colour[3]);
  }

  // Build the index list: source indices (or 0..n-1), then either triangulate
  // or convert to edges.
  scratch.clear();
  const int n = (index && indexCount > 0) ? indexCount : verts;
  auto src = [&](int i) -> unsigned { return index ? index[i] : (unsigned) i; };

  auto tri = [&](unsigned a, unsigned b, unsigned c) {
    if (asLines) {
      scratch.push_back(a); scratch.push_back(b);
      scratch.push_back(b); scratch.push_back(c);
      scratch.push_back(c); scratch.push_back(a);
    } else {
      scratch.push_back(a); scratch.push_back(b); scratch.push_back(c);
    }
  };

  GLenum mode = asLines ? GL_LINES : GL_TRIANGLES;
  switch (legacyMode) {
    case fluxspike::kQuads:
      for (int i = 0; i + 3 < n; i += 4) {
        if (asLines) {   // the four real edges, not two triangles' worth of six
          unsigned q[4] = {src(i), src(i + 1), src(i + 2), src(i + 3)};
          for (int e = 0; e < 4; ++e) {
            scratch.push_back(q[e]); scratch.push_back(q[(e + 1) % 4]);
          }
        } else {
          tri(src(i), src(i + 1), src(i + 2));
          tri(src(i), src(i + 2), src(i + 3));
        }
      }
      break;
    case fluxspike::kTriangles:
      for (int i = 0; i + 2 < n; i += 3) tri(src(i), src(i + 1), src(i + 2));
      break;
    case fluxspike::kTriStrip:
      for (int i = 0; i + 2 < n; ++i)
        (i & 1) ? tri(src(i + 1), src(i), src(i + 2))
                : tri(src(i), src(i + 1), src(i + 2));
      break;
    case fluxspike::kTriFan:
    case fluxspike::kPolygon:
      for (int i = 1; i + 1 < n; ++i) tri(src(0), src(i), src(i + 1));
      break;
    case fluxspike::kLines:
      for (int i = 0; i < n; ++i) scratch.push_back(src(i));
      mode = GL_LINES;
      break;
    case fluxspike::kPoints:
      for (int i = 0; i < n; ++i) scratch.push_back(src(i));
      mode = GL_POINTS;
      break;
    default:
      for (int i = 0; i < n; ++i) scratch.push_back(src(i));
      break;
  }
  if (scratch.empty()) return;

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr) scratch.size() * sizeof(unsigned), scratch.data(),
               GL_STREAM_DRAW);
  glDrawElements(mode, (GLsizei) scratch.size(), GL_UNSIGNED_INT, (void*) 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// --- legacy capture entry points (called from the shim) ---------------------

namespace fluxspike {

LegacyGL& legacy() { static LegacyGL g; return g; }

bool enumIsLegacy(unsigned cap) {
  switch (cap) {
    case 0x0B50:  // GL_LIGHTING
    case 0x4000:  // GL_LIGHT0
    case 0x0B57:  // GL_COLOR_MATERIAL
    case 0x0BA1:  // GL_NORMALIZE
    case 0x803A:  // GL_RESCALE_NORMAL
    case 0x0B20:  // GL_LINE_SMOOTH
    case 0x0B10:  // GL_POINT_SMOOTH
    case 0x0B24:  // GL_LINE_STIPPLE
    case 0x8074: case 0x8075: case 0x8076: case 0x8078:  // client arrays
    case 0x0C60: case 0x0C61:  // GL_TEXTURE_GEN_S/T
    // Texturing is a shader decision on ES, not a capability: these are valid
    // enums to BIND with but not to enable/disable, so passing them through
    // raises GL_INVALID_ENUM. TexturePainter toggles both every ApplyState.
    case 0x0DE1:  // GL_TEXTURE_2D
    case 0x8513:  // GL_TEXTURE_CUBE_MAP
    case 0x8642:  // GL_VERTEX_PROGRAM_POINT_SIZE
      return true;
    default:
      return false;
  }
}

void legacyDraw(unsigned mode, int first, int count) {
  if (!g_backend || first != 0) return;
  const LegacyGL& g = legacy();
  g_backend->setColour(g.colour[0], g.colour[1], g.colour[2], g.colour[3]);
  g_backend->drawRaw(mode, g.pos, g.posStride, g.nrm, nullptr, g.col, count, nullptr, 0,
                     g.polygonMode == kLineMode);
}

void legacyDrawElements(unsigned mode, int count, unsigned, const void* idx) {
  if (!g_backend) return;
  const LegacyGL& g = legacy();
  g_backend->setColour(g.colour[0], g.colour[1], g.colour[2], g.colour[3]);
  g_backend->drawRaw(mode, g.pos, g.posStride, g.nrm, nullptr, g.col, 0,
                     (const unsigned int*) idx, count,
                     g.polygonMode == kLineMode);
}

}  // namespace fluxspike
