// The tetrahedra the cutting plane passes through, ready to draw.
//
// A slice is nothing but an index buffer: the tets it selects are drawn from
// the layer's own vertex buffer, so nothing is copied per frame and a slice
// costs 48 bytes per selected tet. That buffer, and the GL objects around it,
// are IndexedDraw's -- what is here is the selecting.
//
// It knows which plane it was built for, so `update` can be called every frame
// and does work only when the plane has actually moved. That is the whole
// contract -- there is no way to hold a stale slice, because there is no way to
// ask for one.
#pragma once

#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "mesh_viewer/span.h"
#include "mesh_viewer/scene/draw_objects/indexed_draw.h"

namespace mesh_viewer
{

class ElementSlice
{
public:
  // Copying, moving and destruction are all the member's: the GL names live in
  // `draw_`, which is move-only and releases itself, so there is nothing here
  // to hand-write and nothing to keep in step with the fields below.

  // Points the slice at the vertex buffer its indices refer to. Called once,
  // when the layer is built; a slice indexes one buffer for its whole life.
  void attach(GLuint vbo)
  {
    draw_.attach(vbo);
  }

  // Selects every tet (four indices each into `positions`) with vertices on
  // both sides of `plane`, which is stated in the mesh's own coordinates.
  // Returns immediately if the slice already holds that plane's selection.
  void update(Span<glm::dvec3> positions,
              Span<uint32_t> tets,
              const glm::dvec4 &plane);

  // Draws nothing until the next update. Keeps the buffers, so toggling the
  // slice off and on again does not churn GL objects.
  void clear()
  {
    draw_.clear();
    // A zero normal makes this an equation no CutPlane can produce, so the
    // next update cannot mistake an empty slice for a current one.
    built_for_ = glm::dvec4(0.0);
  }

  bool empty() const
  {
    return draw_.empty();
  }

  // How many tets the plane currently passes through -- four faces each.
  size_t tets() const
  {
    return size_t(draw_.index_count()) / 12;
  }

  void bind() const
  {
    draw_.bind();
  }
  void draw() const
  {
    draw_.draw(GL_TRIANGLES);
  }

  void release()
  {
    draw_.release();
    built_for_ = glm::dvec4(0.0);
  }

private:
  IndexedDraw draw_;
  glm::dvec4 built_for_{0.0}; // the plane `indices_` was selected for

  // Kept between updates so that dragging the plane allocates nothing.
  std::vector<uint32_t> indices_;
  std::vector<int8_t> side_; // which side of the plane each vertex is on
};

} // namespace mesh_viewer
