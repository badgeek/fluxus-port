#include "PostFX.h"

#include "GLSLShader.h"
#include "dada.h"

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

using namespace Fluxus;

// passthrough vertex stage: the quad is already in NDC, just carry the texcoord.
static const char* kVert =
  "varying vec2 uv;\n"
  "void main() { uv = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";

PostFX::~PostFX() { release(); }

void PostFX::release() {
  if (tex)   { glDeleteTextures(1, &tex); tex = 0; }
  if (prev)  { glDeleteTextures(1, &prev); prev = 0; }
  if (depth) { glDeleteRenderbuffersEXT(1, &depth); depth = 0; }
  if (fbo)   { glDeleteFramebuffersEXT(1, &fbo); fbo = 0; }
  if (shader && shader->DecRef()) delete shader;
  shader = nullptr;
  w = h = 0;
  curFrag.clear();
}

bool PostFX::ensure(int W, int H) {
  if (W <= 0 || H <= 0) return false;
  if (fbo && W == w && H == h) return true;

  if (tex)   { glDeleteTextures(1, &tex); tex = 0; }
  if (prev)  { glDeleteTextures(1, &prev); prev = 0; }
  if (depth) { glDeleteRenderbuffersEXT(1, &depth); depth = 0; }
  if (fbo)   { glDeleteFramebuffersEXT(1, &fbo); fbo = 0; }
  w = W; h = H;

  GLuint made[2]; glGenTextures(2, made); tex = made[0]; prev = made[1];
  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, i == 0 ? tex : prev);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenRenderbuffersEXT(1, &depth);
  glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, depth);
  glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, w, h);
  glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);

  glGenFramebuffersEXT(1, &fbo);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
  glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, depth);
  const GLenum st = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  return st == GL_FRAMEBUFFER_COMPLETE_EXT;
}

void PostFX::setFragment(const std::string& frag) {
  if (frag == curFrag && shader) return;
  curFrag = frag;
  if (shader && shader->DecRef()) delete shader;
  shader = nullptr;
  GLSLShader::Init();                       // enable GLSL before compiling
  GLSLShaderPair pair(false, kVert, frag);  // compile from source
  shader = new GLSLShader(pair);
}

void PostFX::begin() {
  if (!fbo) return;
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
  glViewport(0, 0, w, h);
}

void PostFX::end() {
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void PostFX::draw(double timeSeconds, double audio, double feedback) {
  if (!fbo || !shader) return;

  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_TEXTURE_2D);

  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);   // scene
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, prev);  // last output

  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

  shader->Apply();
  shader->SetInt("tex", 0);
  shader->SetInt("prev", 1);
  shader->SetFloat("time", (float) timeSeconds);
  shader->SetFloat("audio", (float) audio);
  shader->SetFloat("feedback", (float) feedback);
  shader->SetVector("resolution", dVector((float) w, (float) h, 0), 2);

  glColor4f(1, 1, 1, 1);
  glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1,  1);
    glTexCoord2f(0, 1); glVertex2f(-1,  1);
  glEnd();

  shader->Unapply();

  // capture this frame's output into `prev` for next-frame feedback
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, prev);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
}
