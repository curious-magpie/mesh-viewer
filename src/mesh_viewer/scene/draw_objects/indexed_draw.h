// An index buffer over someone else's positions.
//
// This is what the drawable things in a scene have in common once the geometry
// is taken out of them: a VAO, an index buffer of their own, and a vertex
// buffer they borrow. The tets standing in the cutting plane and the marked
// vertices of a selection are both nothing but a list of indices into a layer's
// positions -- so neither copies a vertex, and neither is more than the
// indices it selects.
//
// What it does not know is *what* to select, or when the selection is stale.
// That is the owner's question, and each owner answers it differently: a slice
// remembers the plane it was built for, a debug channel the DebugDraw revision
// it was uploaded from. Nor does it know the primitive: the caller passes that to `draw`,
// the way DynamicQuads does, so one of these can draw triangles and another
// points without being two types.
//
// Owns its GL names like GpuMesh does, with the same caveat -- it must not
// outlive the GL context, which is why `release` is public.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <glad/glad.h>

#include "mesh_viewer/scene/vertex_layout.h"

namespace mesh_viewer
{

class IndexedDraw
{
public:
  IndexedDraw() = default;
  ~IndexedDraw()
  {
    release();
  }

  IndexedDraw(const IndexedDraw &) = delete;
  IndexedDraw &operator=(const IndexedDraw &) = delete;
  IndexedDraw(IndexedDraw &&other) noexcept
  {
    steal(other);
  }
  IndexedDraw &operator=(IndexedDraw &&other) noexcept
  {
    if (this != &other)
    {
      release();
      steal(other);
    }
    return *this;
  }

  // Points this at the vertex buffer its indices refer to, creating the GL
  // objects on the first call. Needs a current GL context, and the buffer must
  // outlive the use of it.
  //
  // Both bindings are recorded into the VAO, so this is the only place either
  // has to be established: the positions come from the borrowed buffer, and
  // only the indices are ours.
  void attach(GLuint vbo)
  {
    if (vao_ == 0)
    {
      glGenVertexArrays(1, &vao_);
      glGenBuffers(1, &ibo_);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    bind_position_attrib();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBindVertexArray(0);
  }

  // Replaces the indices. They address the attached buffer, and nothing is
  // retained: what the GPU holds after this is the whole of it.
  //
  // Uploading none of them draws nothing while keeping the allocation, so an
  // owner that empties itself every frame costs nothing.
  void upload(const uint32_t *indices, size_t count)
  {
    // attach() has to have created the VAO and the index buffer first.
    assert(vao_ != 0 && ibo_ != 0);
    assert(count <= size_t(std::numeric_limits<GLsizei>::max()));

    index_count_ = GLsizei(count);
    if (count == 0)
      return;

    glBindVertexArray(vao_);

    const size_t bytes = count * sizeof(uint32_t);
    if (bytes > capacity_)
    {
      // Grow generously: an owner that re-uploads every frame -- dragging the
      // cutting plane is one -- changes the count every time, and reallocating
      // on each step would stall the pipeline.
      capacity_ = bytes + bytes / 2;
    }
    // Orphan first, unconditionally. Without it the driver has to wait for the
    // last frame's draw to finish reading this buffer before it can be written.
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 GLsizeiptr(capacity_),
                 nullptr,
                 GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(bytes), indices);

    glBindVertexArray(0);
  }

  // Draws nothing until the next upload. Keeps the buffers, so an owner that
  // is emptied and filled again does not churn GL objects -- and unlike
  // uploading nothing, this is valid before the first attach.
  void clear()
  {
    index_count_ = 0;
  }

  // Whether attach() has been called. Uploading before that is a programming
  // error, so an owner that may be asked to update before the layer it draws
  // from exists has to check.
  bool attached() const
  {
    return vao_ != 0;
  }

  bool empty() const
  {
    return index_count_ == 0;
  }
  GLsizei index_count() const
  {
    return index_count_;
  }

  void bind() const
  {
    glBindVertexArray(vao_);
  }
  void draw(GLenum mode) const
  {
    glDrawElements(mode, index_count_, GL_UNSIGNED_INT, nullptr);
  }

  // Requires a current GL context; does not delete the borrowed buffer.
  void release()
  {
    if (ibo_)
      glDeleteBuffers(1, &ibo_);
    if (vao_)
      glDeleteVertexArrays(1, &vao_);
    vao_ = ibo_ = 0;
    index_count_ = 0;
    capacity_ = 0;
  }

private:
  void steal(IndexedDraw &other) noexcept
  {
    vao_ = other.vao_;
    ibo_ = other.ibo_;
    index_count_ = other.index_count_;
    capacity_ = other.capacity_;

    other.vao_ = other.ibo_ = 0;
    other.index_count_ = 0;
    other.capacity_ = 0;
  }

  GLuint vao_ = 0, ibo_ = 0;
  GLsizei index_count_ = 0;
  size_t capacity_ = 0; // bytes currently allocated in ibo_
};

} // namespace mesh_viewer
