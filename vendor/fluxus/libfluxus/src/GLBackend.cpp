#include <cstdio>
// fluxus->JUCE minimal port: GLBackend implementation (raw fixed-function GL).
#include "GLBackend.h"
#include "OpenGL.h"
#include "dada.h"

using namespace Fluxus;

static GLenum toGL(RPrim p) {
  switch (p) {
    case RPrim::Triangles: return GL_TRIANGLES;
    case RPrim::Quads:     return GL_QUADS;
    case RPrim::TriStrip:  return GL_TRIANGLE_STRIP;
    case RPrim::TriFan:    return GL_TRIANGLE_FAN;
    case RPrim::Polygon:   return GL_POLYGON;
    case RPrim::Lines:     return GL_LINES;
    case RPrim::LineStrip: return GL_LINE_STRIP;
    case RPrim::Points:    return GL_POINTS;
  }
  return GL_TRIANGLES;
}

void GLBackend::pushMatrix()                 { glPushMatrix(); }
void GLBackend::popMatrix()                  { glPopMatrix(); }
void GLBackend::multMatrix(const float* m)   { glMultMatrixf(m); }
void GLBackend::loadMatrix(const float* m)   { glLoadMatrixf(m); }
void GLBackend::getModelView(float* m)       { glGetFloatv(GL_MODELVIEW_MATRIX, m); }
void GLBackend::getProjection(float* m)      { glGetFloatv(GL_PROJECTION_MATRIX, m); }

// Select the projection stack, load, and put the mode back — callers expect to
// be left in GL_MODELVIEW, which is what the surrounding engine code assumes.
void GLBackend::setProjectionMatrix(const float* m) {
  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(m);
  glMatrixMode(GL_MODELVIEW);
}
void GLBackend::pushPickName(unsigned int id){ glPushName(id); }
void GLBackend::popPickName()                { glPopName(); }

void GLBackend::setColour(float r, float g, float b, float a) { glColor4f(r, g, b, a); }

void GLBackend::setMaterial(const float* ambient, const float* emissive,
                            const float* diffuse, const float* specular,
                            float shininess) {
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,   ambient);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION,  emissive);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,   diffuse);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  specular);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, &shininess);
}

void GLBackend::setLineWidth(float w)        { glLineWidth(w); }
void GLBackend::setPointSize(float s)        { glPointSize(s); }
void GLBackend::setBlend(int src, int dst)   { glBlendFunc((GLenum) src, (GLenum) dst); }
void GLBackend::setCull(bool on)             { if (on) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void GLBackend::setNormaliseNormals(bool on) { if (on) glEnable(GL_NORMALIZE); else glDisable(GL_NORMALIZE); }
void GLBackend::setLighting(bool on)         { if (on) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING); }

// Desktop GL has the rasterizer state the seam is describing, so this stays a
// direct translation — the same glPolygonMode call the primitives used to make.
void GLBackend::setFillMode(RFill mode) {
  GLenum m = GL_FILL;
  switch (mode) {
    case RFill::Fill:  m = GL_FILL;  break;
    case RFill::Line:  m = GL_LINE;  break;
    case RFill::Point: m = GL_POINT; break;
  }
  glPolygonMode(GL_FRONT_AND_BACK, m);
}
void GLBackend::setProgramPointSize(bool on) { if (on) glEnable(GL_VERTEX_PROGRAM_POINT_SIZE); else glDisable(GL_VERTEX_PROGRAM_POINT_SIZE); }
void GLBackend::setFrontFaceCW(bool cw)      { glFrontFace(cw ? GL_CW : GL_CCW); }

// One-to-one with the glLight* calls Light.cpp used to make.
void GLBackend::setLightEnabled(int index, bool on) {
  if (on) glEnable(GL_LIGHT0 + index); else glDisable(GL_LIGHT0 + index);
}

void GLBackend::setLightColour(int index, RLightColour which, const float* rgba) {
  GLenum p = GL_AMBIENT;
  switch (which) {
    case RLightColour::Ambient:  p = GL_AMBIENT;  break;
    case RLightColour::Diffuse:  p = GL_DIFFUSE;  break;
    case RLightColour::Specular: p = GL_SPECULAR; break;
  }
  glLightfv(GL_LIGHT0 + index, p, rgba);
}

void GLBackend::setLightFloat(int index, RLightFloat which, float v) {
  GLenum p = GL_SPOT_CUTOFF;
  switch (which) {
    case RLightFloat::SpotCutoff:           p = GL_SPOT_CUTOFF;           break;
    case RLightFloat::SpotExponent:         p = GL_SPOT_EXPONENT;         break;
    case RLightFloat::ConstantAttenuation:  p = GL_CONSTANT_ATTENUATION;  break;
    case RLightFloat::LinearAttenuation:    p = GL_LINEAR_ATTENUATION;    break;
    case RLightFloat::QuadraticAttenuation: p = GL_QUADRATIC_ATTENUATION; break;
  }
  glLightf(GL_LIGHT0 + index, p, v);
}

void GLBackend::setLightPosition(int index, const float* xyzw) {
  glLightfv(GL_LIGHT0 + index, GL_POSITION, xyzw);
}

void GLBackend::setLightSpotDirection(int index, const float* xyzw) {
  glLightfv(GL_LIGHT0 + index, GL_SPOT_DIRECTION, xyzw);
}

void GLBackend::drawArrays(RPrim prim, const RVertexArrays& v, int count,
                           const unsigned int* index, int indexCount) {
  const GLenum type = toGL(prim);
  // enable client arrays per-draw: the host GL context (JUCE) resets them between
  // frames, so relying on the renderer's one-time PreRender enable breaks texturing.
#ifdef FLUXUS_ENABLE_VBO
  // fluxus->JUCE port: when an attribute has a VBO id, bind it and use a 0 offset
  // (the pointer arg becomes a byte offset into the bound buffer); otherwise fall
  // back to the client pointer. Each attribute lives in its own buffer.
  if (v.pos) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.posVBO);
    glVertexPointer(3, GL_FLOAT, v.stride, v.posVBO ? (void*) 0 : (void*) v.pos);
  }
  // fluxus->JUCE port: DISABLE an array that this draw does not supply, the way
  // the colour array below always has. Primitives that pass only positions and
  // colours (particles as points, ribbon wire) would otherwise inherit whatever
  // array the previous draw left enabled — a stale pointer, not just stale data.
  if (v.nrm) {
    glEnableClientState(GL_NORMAL_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.nrmVBO);
    glNormalPointer(GL_FLOAT, v.stride, v.nrmVBO ? (void*) 0 : (void*) v.nrm);
  } else {
    glDisableClientState(GL_NORMAL_ARRAY);
  }
  if (v.tex) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.texVBO);
    glTexCoordPointer(3, GL_FLOAT, v.stride, v.texVBO ? (void*) 0 : (void*) v.tex);
    // the host GL context (JUCE) can leave a non-identity texture matrix, which
    // collapses our texcoords; reset it so texturing samples correctly.
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
  } else {
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  }

  if (v.col) {
    glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.colVBO);
    glColorPointer(4, GL_FLOAT, v.stride, v.colVBO ? (void*) 0 : (void*) v.col);
  } else {
    glDisableClientState(GL_COLOR_ARRAY);
  }

  if (index) glDrawElements(type, indexCount, GL_UNSIGNED_INT, index);
  else       glDrawArrays(type, 0, count);

  glBindBuffer(GL_ARRAY_BUFFER, 0);   // leave client-array state clean for others
#else
  if (v.pos) { glEnableClientState(GL_VERTEX_ARRAY);        glVertexPointer(3, GL_FLOAT, v.stride, (void*) v.pos); }
  // else-disable: see the note in the VBO path above.
  if (v.nrm) { glEnableClientState(GL_NORMAL_ARRAY);        glNormalPointer(GL_FLOAT, v.stride, (void*) v.nrm); }
  else       { glDisableClientState(GL_NORMAL_ARRAY); }
  if (v.tex) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(3, GL_FLOAT, v.stride, (void*) v.tex);
    // the host GL context (JUCE) can leave a non-identity texture matrix, which
    // collapses our texcoords; reset it so texturing samples correctly.
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
  } else {
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  }

  if (v.col) {
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_FLOAT, v.stride, (void*) v.col);
  } else {
    glDisableClientState(GL_COLOR_ARRAY);
  }

  if (index) glDrawElements(type, indexCount, GL_UNSIGNED_INT, index);
  else       glDrawArrays(type, 0, count);
#endif
}
