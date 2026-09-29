#include "mesh_viewer/scene/draw_objects/element_slice.h"

#include <algorithm>

namespace mesh_viewer
{

namespace
{

// The four faces of a tet, as corner triples: face i is the one opposite
// corner i, wound outward for a positively oriented tet. An opaque slice does
// not depend on the winding -- it draws with culling off and shades from
// derivatives -- but a translucent one culls to blend its far faces first.
constexpr int kTetFaces[4][3] = {{1, 2, 3}, {0, 3, 2}, {0, 1, 3}, {0, 2, 1}};

} // namespace

void ElementSlice::update(Span<glm::dvec3> positions,
                          Span<uint32_t> tets,
                          Span<uint8_t> labels,
                          size_t groups,
                          const glm::dvec4 &plane)
{
  if (!draw_.attached())
    return; // never attached to a vertex buffer, so there is nothing to index
  if (plane == built_for_)
    return; // the plane has not moved; last update's selection still holds
  built_for_ = plane;

  // --- 1. which side of the plane is each vertex on? ---
  //
  // Done per vertex rather than per tet corner. A tet mesh references each of
  // its vertices about 22 times, so testing them here turns 10 million random
  // gathers into 450 thousand sequential ones on a 2.5 M-element mesh -- and
  // the tet loop below then reads four bytes instead of four vec3s.
  const glm::dvec3 n(plane);
  const Span<glm::dvec3> pos = positions;
  side_.resize(pos.size());
  for (size_t i = 0; i < pos.size(); ++i)
  {
    const double d = glm::dot(n, pos[i]) + plane.w;
    side_[i] = int8_t(d > 0.0) - int8_t(d < 0.0);
  }

  // --- 2. a tet is cut when its corners do not all land on the same side ---
  //
  // Corners sitting exactly on the plane count for neither side, so a tet that
  // merely touches it with a vertex or an edge is left out. Each goes into its
  // label's group, so the groups come out as runs of one buffer.
  const bool labelled = !labels.empty();
  if (!labelled || groups == 0)
    groups = 1;
  by_group_.resize(groups);
  for (std::vector<uint32_t> &g : by_group_)
    g.clear();

  const size_t tet_count = tets.size() / 4;
  for (size_t t = 0; t < tet_count; ++t)
  {
    const uint32_t *v = tets.data() + 4 * t;
    int front = 0, back = 0;
    for (int i = 0; i < 4; ++i)
    {
      front += side_[v[i]] > 0;
      back += side_[v[i]] < 0;
    }
    if (front == 0 || back == 0)
      continue;

    // All four faces, so the tet reads as a solid body standing in the cut.
    // A label past the groups the layer counted -- which it cannot be, if the
    // layer counted them from these labels -- goes into the last group rather
    // than out of bounds.
    std::vector<uint32_t> &out =
        by_group_[labelled ? std::min<size_t>(labels[t], groups - 1) : 0];
    for (const auto &f : kTetFaces)
    {
      out.push_back(v[f[0]]);
      out.push_back(v[f[1]]);
      out.push_back(v[f[2]]);
    }
  }

  // --- 3. join the groups, and hand the indices to the GPU ---
  //
  // A plane that misses every tet uploads nothing and draws nothing, which is
  // the same path as any other count.
  indices_.clear();
  group_first_.assign(1, 0);
  for (const std::vector<uint32_t> &g : by_group_)
  {
    indices_.insert(indices_.end(), g.begin(), g.end());
    group_first_.push_back(GLsizei(indices_.size()));
  }
  draw_.upload(indices_.data(), indices_.size());
}

} // namespace mesh_viewer
