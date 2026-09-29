// A read-only view of a contiguous array: a pointer and a count.
//
// The viewer is C++17, so this stands in for std::span<const T>. It exists so
// that a host can hand the viewer its own arrays -- a std::vector, a column of
// a numpy array, a raw buffer -- without the viewer naming the host's types or
// copying anything. It owns nothing; see MeshView for how long what it points
// at has to live.
#pragma once

#include <cstddef>
#include <vector>

namespace mesh_viewer
{

template <typename T> class Span
{
public:
  Span() = default;
  Span(const T *data, size_t size) : data_(data), size_(size)
  {
  }
  // Implicit, so a vector can be passed wherever a span is taken.
  Span(const std::vector<T> &v) : data_(v.data()), size_(v.size())
  {
  }

  const T *data() const
  {
    return data_;
  }
  size_t size() const
  {
    return size_;
  }
  bool empty() const
  {
    return size_ == 0;
  }

  const T &operator[](size_t i) const
  {
    return data_[i];
  }
  const T *begin() const
  {
    return data_;
  }
  const T *end() const
  {
    return data_ + size_;
  }

private:
  const T *data_ = nullptr;
  size_t size_ = 0;
};

} // namespace mesh_viewer
