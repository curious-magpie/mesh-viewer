// Dragging the cutting plane with the mouse.
//
// Holds the state captured at the start of a drag, so every update is measured
// against the original plane rather than accumulating rounding frame to frame.
// Keeping it in one object is also what lets the caller ask a single question
// -- "did the plane take this drag?" -- instead of juggling a hit test, a mode
// and four saved values itself.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "mesh_viewer/scene/camera.h"
#include "mesh_viewer/scene/cut_plane.h"

namespace mesh_viewer
{

class PlaneController
{
public:
  enum class Mode
  {
    Translate, // slide along the normal
    Rotate     // turn the normal, pivoting in place
  };

  bool dragging() const
  {
    return dragging_;
  }
  void end()
  {
    dragging_ = false;
  }

  // Starts a drag if the mouse is over the plane's handle, and reports whether
  // it did. A false return means the caller still owns the gesture -- that is
  // how the camera gets everything the handle did not claim.
  bool try_begin(const CutPlane &plane,
                 const Camera &cam,
                 float mx,
                 float my,
                 const glm::vec3 &scene_center,
                 float handle_half,
                 Mode mode)
  {
    glm::vec3 hit;
    if (!hit_handle(plane, cam, mx, my, scene_center, handle_half, hit))
      return false;

    dragging_ = true;
    mode_ = mode;
    hit0_ = hit;
    pivot0_ = plane.anchor(scene_center);
    n0_ = plane.n;
    w0_ = plane.w;
    mx0_ = mx;
    my0_ = my;
    return true;
  }

  void update(CutPlane &plane, const Camera &cam, float mx, float my) const
  {
    if (!dragging_)
      return;
    if (mode_ == Mode::Translate)
      translate(plane, cam, mx, my);
    else
      rotate(plane, cam, mx, my);
  }

  // True when the pixel lands on the square handle centred on the plane.
  static bool hit_handle(const CutPlane &plane,
                         const Camera &cam,
                         float mx,
                         float my,
                         const glm::vec3 &scene_center,
                         float handle_half,
                         glm::vec3 &hit)
  {
    glm::vec3 ro, rd;
    cam.screen_ray(mx, my, ro, rd);
    if (!ray_plane(ro, rd, plane.n, plane.w, hit))
      return false;

    glm::vec3 u, v;
    plane.basis(u, v);
    const glm::vec3 d = hit - plane.anchor(scene_center);
    return std::fabs(glm::dot(d, u)) <= handle_half &&
           std::fabs(glm::dot(d, v)) <= handle_half;
  }

private:
  // Slide along the normal.
  //
  // The mouse moves in 2D, so the drag has to be projected onto the normal
  // axis. The standard trick: intersect the mouse ray with a helper plane that
  // contains the axis and faces the camera as squarely as possible, then take
  // how far along the axis that hit landed.
  //
  // Note this cannot intersect the cutting plane itself -- that plane is the
  // thing moving, so its own surface is no fixed reference.
  void translate(CutPlane &plane, const Camera &cam, float mx, float my) const
  {
    const glm::vec3 axis = n0_;
    const glm::vec3 side = glm::cross(-cam.dir(), axis);
    if (glm::length(side) < 1e-6f)
      return; // looking straight down the axis: no usable helper plane
    const glm::vec3 helper_n = glm::normalize(glm::cross(axis, side));

    glm::vec3 ro, rd, hit;
    cam.screen_ray(mx, my, ro, rd);
    if (!ray_plane(ro, rd, helper_n, -glm::dot(helper_n, hit0_), hit))
      return;

    plane.n = axis;
    plane.w = w0_ - glm::dot(hit - hit0_, axis);
  }

  // Turn the normal with the mouse: horizontal movement spins it about the
  // camera's up axis, vertical about the camera's right axis, so the plane
  // turns the way the cursor pushes it.
  void rotate(CutPlane &plane, const Camera &cam, float mx, float my) const
  {
    const float dx = (mx - mx0_) * 0.006f;
    const float dy = (my - my0_) * 0.006f;

    glm::mat4 r(1.0f);
    r = glm::rotate(r, -dx, cam.up());
    r = glm::rotate(r, -dy, cam.right());
    plane.n = glm::normalize(glm::vec3(r * glm::vec4(n0_, 0.0f)));
    plane.set_through(pivot0_); // spin in place rather than swinging away
  }

  bool dragging_ = false;
  Mode mode_ = Mode::Translate;

  // Captured at the start of the drag.
  glm::vec3 hit0_{0.0f};   // where the ray met the plane
  glm::vec3 pivot0_{0.0f}; // rotation centre, held fixed
  glm::vec3 n0_{0.0f};
  float w0_ = 0.0f;
  float mx0_ = 0.0f, my0_ = 0.0f;
};

} // namespace mesh_viewer
