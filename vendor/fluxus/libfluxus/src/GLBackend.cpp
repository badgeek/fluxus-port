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
}
