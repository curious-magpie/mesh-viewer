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
// ask for one. (What else can make it stale -- the mesh moving, its labels
// changing -- the layer answers by calling `clear`.)
//
// Tets with labels are grouped by label: the index buffer holds each group's
// faces in one run, label 0 first, so a group is drawn -- in its own colour,
// or not at all -- by drawing its range. Without labels there is one group.
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
  //
  // `labels` is one per tet, each below `groups`, or empty for one group.
  void update(Span<glm::dvec3> positions,
              Span<uint32_t> tets,
              Span<uint8_t> labels,
              size_t groups,
              const glm::dvec4 &plane);

  // Draws nothing until the next update. Keeps the buffers, so toggling the
  // slice off and on again does not churn GL objects.
  void clear()
  {
    draw_.clear();
    group_first_.clear();
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

  // The groups the last update sorted the tets into, and how many of the cut
  // tets are in each.
  size_t groups() const
  {
    return group_first_.empty() ? 0 : group_first_.size() - 1;
  }
  size_t tets(size_t group) const
  {
    return size_t(group_first_[group + 1] - group_first_[group]) / 12;
  }

  void bind() const
  {
    draw_.bind();
  }
  // One group's tets, after bind().
  void draw(size_t group) const
  {
    const GLsizei first = group_first_[group];
    draw_.draw(GL_TRIANGLES, first, group_first_[group + 1] - first);
  }

  void release()
  {
    draw_.release();
    built_for_ = glm::dvec4(0.0);
    group_first_.clear();
  }

private:
  IndexedDraw draw_;
  glm::dvec4 built_for_{0.0}; // the plane `indices_` was selected for

  // Where each group's run of indices starts, plus one past the last: group g
  // is [group_first_[g], group_first_[g + 1]).
  std::vector<GLsizei> group_first_;

  // Kept between updates so that dragging the plane allocates nothing.
  std::vector<uint32_t> indices_;
  std::vector<std::vector<uint32_t>> by_group_; // each group's, before joining
  std::vector<int8_t> side_; // which side of the plane each vertex is on
};

} // namespace mesh_viewer
