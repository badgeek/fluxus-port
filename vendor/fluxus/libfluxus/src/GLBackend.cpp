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
  }
  return GL_TRIANGLES;
}

void GLBackend::pushMatrix()                 { glPushMatrix(); }
void GLBackend::popMatrix()                  { glPopMatrix(); }
void GLBackend::multMatrix(const float* m)   { glMultMatrixf(m); }
void GLBackend::loadMatrix(const float* m)   { glLoadMatrixf(m); }

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
void GLBackend::setFrontFaceCW(bool cw)      { glFrontFace(cw ? GL_CW : GL_CCW); }

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
  if (v.nrm) {
    glEnableClientState(GL_NORMAL_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.nrmVBO);
    glNormalPointer(GL_FLOAT, v.stride, v.nrmVBO ? (void*) 0 : (void*) v.nrm);
  }
  if (v.tex) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, v.texVBO);
    glTexCoordPointer(3, GL_FLOAT, v.stride, v.texVBO ? (void*) 0 : (void*) v.tex);
    // the host GL context (JUCE) can leave a non-identity texture matrix, which
    // collapses our texcoords; reset it so texturing samples correctly.
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
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
  if (v.nrm) { glEnableClientState(GL_NORMAL_ARRAY);        glNormalPointer(GL_FLOAT, v.stride, (void*) v.nrm); }
  if (v.tex) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(3, GL_FLOAT, v.stride, (void*) v.tex);
    // the host GL context (JUCE) can leave a non-identity texture matrix, which
    // collapses our texcoords; reset it so texturing samples correctly.
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
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
