// An axis-aligned bounding box, accumulated a point at a time.
//
// Shared so that "empty" means one thing in this program: a box whose minimum
// has not fallen below its maximum has had nothing added to it.
#pragma once

#include <limits>

#include <glm/glm.hpp>

namespace mesh_viewer
{

template <typename V, typename T> struct BoundsT
{
  V min{std::numeric_limits<T>::infinity()};
  V max{-std::numeric_limits<T>::infinity()};

  void add(const V &p)
  {
    min = glm::min(min, p);
    max = glm::max(max, p);
  }
  void add(const BoundsT &b)
  {
    min = glm::min(min, b.min);
    max = glm::max(max, b.max);
  }
  bool empty() const
  {
    return min.x > max.x;
  }
  V center() const
  {
    return (min + max) * T(0.5);
  }
  // Half the diagonal, so it bounds the box from outside.
  T radius() const
  {
    return glm::length(max - min) * T(0.5);
  }
};

// Scene space is float, because that is what the GPU takes; mesh space is
// double, because that is what the mesh keeps.
using Bounds = BoundsT<glm::vec3, float>;
using DBounds = BoundsT<glm::dvec3, double>;

} // namespace mesh_viewer
