#include "mesh_viewer/scene/draw_objects/element_slice.h"

namespace mesh_viewer
{

namespace
{

// The four faces of a tet, as corner triples: face i is the one opposite
// corner i, wound outward for a positively oriented tet. The draw does not
// depend on the winding -- it runs with culling off and shades from
// derivatives -- so a host whose tets are the other way round loses nothing.
constexpr int kTetFaces[4][3] = {{1, 2, 3}, {0, 3, 2}, {0, 1, 3}, {0, 2, 1}};

} // namespace

void ElementSlice::update(Span<glm::dvec3> positions,
                          Span<uint32_t> tets,
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
  // merely touches it with a vertex or an edge is left out.
  indices_.clear();
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
    for (const auto &f : kTetFaces)
    {
      indices_.push_back(v[f[0]]);
      indices_.push_back(v[f[1]]);
      indices_.push_back(v[f[2]]);
    }
  }

  // --- 3. hand the indices to the GPU ---
  //
  // A plane that misses every tet uploads nothing and draws nothing, which is
  // the same path as any other count.
  draw_.upload(indices_.data(), indices_.size());
}

} // namespace mesh_viewer
