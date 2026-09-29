// A small vertex buffer whose contents are rewritten every frame.
//
// Used for the geometry that lives in the cutting plane: the fill quad and the
// drag handle both move whenever the plane does, so there is nothing to cache.
// Owns its GL names and deletes them in the destructor, like GpuMesh, and
// carries the same caveat -- it must not outlive the GL context.
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "mesh_viewer/scene/vertex_layout.h"

namespace mesh_viewer
{

class DynamicQuads
{
public:
  DynamicQuads() = default;
  ~DynamicQuads()
  {
    release();
  }

  DynamicQuads(const DynamicQuads &) = delete;
  DynamicQuads &operator=(const DynamicQuads &) = delete;

  void allocate(GLsizei vertex_count)
  {
    release();
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 vertex_count * GLsizeiptr(sizeof(glm::vec3)),
                 nullptr,
                 GL_DYNAMIC_DRAW);
    bind_position_attrib();
    glBindVertexArray(0);
  }

  void update(const glm::vec3 *verts, GLsizei count)
  {
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, count * GLsizeiptr(sizeof(glm::vec3)), verts);
  }

  void bind() const
  {
    glBindVertexArray(vao_);
  }
  void draw(GLenum mode, GLint first, GLsizei count) const
  {
    glDrawArrays(mode, first, count);
  }

  void release()
  {
    if (vbo_)
      glDeleteBuffers(1, &vbo_);
    if (vao_)
      glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = 0;
  }

private:
  GLuint vao_ = 0, vbo_ = 0;
};

} // namespace mesh_viewer
