// The cutting plane.
//
// Stored as dot(n, x) + w == 0. Points where that expression is negative are
// clipped away, so the half-space that survives is the one the normal points
// toward. Interaction lives separately, in PlaneController.
#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace mesh_viewer
{

// Where a shape sits relative to the plane. `Front` is the half the normal
// points into -- the half that survives clipping.
enum class PlaneSide
{
  Front,
  Back,
  Crossing
};

struct CutPlane
{
  bool enabled = false;
  bool show_gizmo = true;
  glm::vec3 n{1.0f, 0.0f, 0.0f}; // unit normal
  float w = 0.0f;                // plane constant

  glm::vec4 equation() const
  {
    return glm::vec4(n, w);
  }

  // Positive in front of the plane (the kept side), negative behind it.
  float signed_distance(const glm::vec3 &p) const
  {
    return glm::dot(n, p) + w;
  }

  // Which side of the plane an axis-aligned box falls on.
  //
  // Two dot products, and it answers the three questions the renderer asks per
  // layer per frame: is this layer clipped away entirely (Back), does it have a
  // cross-section to cap at all (Crossing only), and is there any point
  // scanning its tets for the elements in the cut (Crossing only).
  PlaneSide classify(const glm::vec3 &bbmin, const glm::vec3 &bbmax) const
  {
    const float center = signed_distance((bbmin + bbmax) * 0.5f);
    const float extent = glm::dot(glm::abs(n), (bbmax - bbmin) * 0.5f);
    if (center > extent)
      return PlaneSide::Front;
    if (center < -extent)
      return PlaneSide::Back;
    return PlaneSide::Crossing;
  }

  // Where the plane crosses the middle of the scene. The drag handle is centred
  // here and rotation pivots about it, so the plane stays under the mouse
  // instead of swinging away.
  glm::vec3 anchor(const glm::vec3 &scene_center) const
  {
    return scene_center - signed_distance(scene_center) * n;
  }

  // Any two perpendicular directions lying in the plane. Only used to build the
  // quads, so which pair it picks does not matter -- just that they are stable.
  void basis(glm::vec3 &u, glm::vec3 &v) const
  {
    // Cross with whichever axis is least parallel to n, so the result never
    // collapses to zero length.
    const glm::vec3 ref =
        std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    u = glm::normalize(glm::cross(ref, n));
    v = glm::cross(n, u);
  }

  // Moves the plane to pass through `p`, keeping its normal.
  void set_through(const glm::vec3 &p)
  {
    w = -glm::dot(n, p);
  }

  // Points the normal along `axis` without letting the plane drift off the cut
  // it was already making.
  void look_along(const glm::vec3 &axis, const glm::vec3 &scene_center)
  {
    const glm::vec3 p = anchor(scene_center);
    n = glm::normalize(axis);
    set_through(p);
  }

  // Swaps which half is kept, leaving the cut itself where it is.
  void flip()
  {
    n = -n;
    w = -w;
  }
};

// Ray/plane intersection. False if the ray is parallel to the plane, or if the
// plane is behind the ray's origin.
inline bool ray_plane(const glm::vec3 &ro,
                      const glm::vec3 &rd,
                      const glm::vec3 &n,
                      float w,
                      glm::vec3 &hit)
{
  const float denom = glm::dot(n, rd);
  if (std::fabs(denom) < 1e-9f)
    return false;
  const float t = -(glm::dot(n, ro) + w) / denom;
  if (t < 0.0f)
    return false;
  hit = ro + rd * t;
  return true;
}

} // namespace mesh_viewer
