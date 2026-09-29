// A triangle mesh resident on the GPU: one VAO, a position buffer and an index
// buffer.
//
// Owns its GL names and deletes them in the destructor, so nothing has to
// remember to free them. That makes it move-only: copying would leave two
// objects believing they own the same names, and the second destructor would
// delete buffers still in use.
//
// The usual RAII caveat applies -- glDelete* needs a current context, so a
// GpuMesh must not outlive the GL context. Scene::clear() exists so the
// application can enforce that ordering explicitly.
#pragma once

#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "mesh_viewer/span.h"

namespace mesh_viewer
{

class GpuMesh
{
public:
  GpuMesh() = default;
  ~GpuMesh()
  {
    release();
  }

  GpuMesh(const GpuMesh &) = delete;
  GpuMesh &operator=(const GpuMesh &) = delete;
  GpuMesh(GpuMesh &&other) noexcept
  {
    steal(other);
  }
  GpuMesh &operator=(GpuMesh &&other) noexcept
  {
    if (this != &other)
    {
      release();
      steal(other);
    }
    return *this;
  }

  // Positions only. There is deliberately no normal buffer: the shaders derive
  // a flat normal from screen-space derivatives, which is exact per triangle
  // and saves 12 bytes per vertex on meshes with millions of them.
  void upload(const std::vector<glm::vec3> &verts, Span<uint32_t> indices);

  // Rewrites the positions in place. Connectivity is fixed, so a mesh whose
  // vertices moved needs no new index buffer -- and on a 2.5 M-element domain
  // that is the difference between 5 MB and 6 MB of traffic per update.
  void update_positions(const std::vector<glm::vec3> &verts);

  void release();

  void bind() const
  {
    glBindVertexArray(vao_);
  }
  void draw() const
  {
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  }

  // The position buffer, so an ElementSlice can index into it instead of
  // keeping a second copy of the vertices on the GPU.
  GLuint vertex_buffer() const
  {
    return vbo_;
  }

private:
  void steal(GpuMesh &other) noexcept
  {
    vao_ = other.vao_;
    vbo_ = other.vbo_;
    ibo_ = other.ibo_;
    index_count_ = other.index_count_;
    other.vao_ = other.vbo_ = other.ibo_ = 0;
    other.index_count_ = 0;
  }

  GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
  GLsizei index_count_ = 0;
};

} // namespace mesh_viewer
