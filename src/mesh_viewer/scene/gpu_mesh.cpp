#include "mesh_viewer/scene/gpu_mesh.h"

#include "mesh_viewer/scene/vertex_layout.h"

namespace mesh_viewer
{

void GpuMesh::upload(const std::vector<glm::vec3> &verts,
                     Span<uint32_t> indices)
{
  release();

  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);

  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               GLsizeiptr(verts.size() * sizeof(glm::vec3)),
               verts.data(),
               GL_DYNAMIC_DRAW); // positions are rewritten as the mesh moves
  bind_position_attrib();

  glGenBuffers(1, &ibo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               GLsizeiptr(indices.size() * sizeof(uint32_t)),
               indices.data(),
               GL_STATIC_DRAW);

  glBindVertexArray(0);
  index_count_ = GLsizei(indices.size());
}

void GpuMesh::update_positions(const std::vector<glm::vec3> &verts)
{
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER,
                  0,
                  GLsizeiptr(verts.size() * sizeof(glm::vec3)),
                  verts.data());
}

void GpuMesh::release()
{
  if (ibo_)
    glDeleteBuffers(1, &ibo_);
  if (vbo_)
    glDeleteBuffers(1, &vbo_);
  if (vao_)
    glDeleteVertexArrays(1, &vao_);
  vao_ = vbo_ = ibo_ = 0;
  index_count_ = 0;
}

} // namespace mesh_viewer
