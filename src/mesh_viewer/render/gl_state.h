// The pipeline switches every draw pass needs to agree on.
//
// A pass fills one of these in and applies it, setting *all* of it
// unconditionally. The passes here each need a different half of these
// switches, and tracking which ones already hold the right value is exactly how
// this kind of renderer quietly breaks. A few dozen redundant GL calls per
// frame cost nothing.
#pragma once

#include <glad/glad.h>

namespace mesh_viewer
{

struct StencilState
{
  bool enabled = false;
  GLenum func = GL_ALWAYS; // comparison against `ref`
  GLint ref = 0;
  GLuint read_mask = 0xFFu;
  GLuint write_mask = 0xFFu;
  GLenum on_fail = GL_KEEP;       // stencil test failed
  GLenum on_depth_fail = GL_KEEP; // stencil passed, depth failed
  GLenum on_pass = GL_KEEP;       // both passed
};

struct GlState
{
  bool depth_test = true;
  GLenum depth_func = GL_LESS;
  bool depth_write = true;
  bool cull = false;
  GLenum cull_face = GL_BACK;
  bool blend = false;
  bool color_write = true;
  GLenum polygon_mode = GL_FILL; // GL_LINE draws the same geometry as wireframe
  bool clip_plane = false;       // GL_CLIP_DISTANCE0
  // Nudges coplanar fills along z. The default pulls toward the eye, which is
  // what the cross-section fill needs; a positive pair pushes away instead, so
  // a fill can sit behind its own wireframe.
  bool polygon_offset = false;
  GLfloat offset_factor = -1.0f, offset_units = -1.0f;
  StencilState stencil;

  void apply() const;
};

inline void gl_toggle(GLenum cap, bool on)
{
  if (on)
    glEnable(cap);
  else
    glDisable(cap);
}

inline void GlState::apply() const
{
  gl_toggle(GL_DEPTH_TEST, depth_test);
  glDepthFunc(depth_func);
  glDepthMask(depth_write ? GL_TRUE : GL_FALSE);

  gl_toggle(GL_CULL_FACE, cull);
  glCullFace(cull_face);

  gl_toggle(GL_BLEND, blend);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);

  const GLboolean cw = color_write ? GL_TRUE : GL_FALSE;
  glColorMask(cw, cw, cw, cw);

  glPolygonMode(GL_FRONT_AND_BACK, polygon_mode);

  gl_toggle(GL_STENCIL_TEST, stencil.enabled);
  glStencilFunc(stencil.func, stencil.ref, stencil.read_mask);
  glStencilOp(stencil.on_fail, stencil.on_depth_fail, stencil.on_pass);
  glStencilMask(stencil.write_mask);

  gl_toggle(GL_CLIP_DISTANCE0, clip_plane);
  glPolygonOffset(offset_factor, offset_units);
  gl_toggle(GL_POLYGON_OFFSET_FILL, polygon_offset);
}

} // namespace mesh_viewer
