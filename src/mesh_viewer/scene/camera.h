// A trackball camera: the eye looks at a target point from `distance` away,
// turned by a free rotation.
//
// The rotation is a quaternion rather than a yaw and a pitch. A yaw/pitch
// camera has poles: its pitch has to stop short of straight up or down, where
// the view direction meets the world's up vector and lookAt degenerates, so a
// drag that carries on past them just stops; and a sideways drag spins about
// the world's vertical axis, which near a pole turns the view about a point in
// the middle of the screen. Here a drag turns the scene about the screen's own
// axes -- sideways about the screen's vertical, up and down about its
// horizontal -- and any orientation can be reached and left in any direction.
//
// Rotation turns about `pivot`, the scene's centre, which `fit` sets and
// panning leaves alone: pan the meshes into a corner and they still turn about
// themselves. Header-only: it is pure math over a handful of numbers.
#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace mesh_viewer
{

struct Camera
{
  glm::vec3 target{0.0f}; // the point at the centre of the view
  glm::vec3 pivot{0.0f};  // the point rotation turns about: the scene's centre
  float distance = 3.0f;

  // The camera's axes in the world: +X is screen right, +Y screen up, +Z from
  // the target toward the eye. It starts at `default_rotation`.
  glm::quat rotation = default_rotation();

  // Parallel projection: no perspective, so a view along an axis is a true
  // elevation and equal lengths look equal wherever they are -- what you want
  // to judge a film's thickness side-on. It frames what perspective framed at
  // the target, so switching does not jump.
  bool orthographic = false;
  float fovy = glm::radians(45.0f);
  float znear = 0.01f, zfar = 100.0f;
  int vw = 1280, vh = 720;   // viewport, in pixels
  float scene_radius = 1.0f; // what fit() framed, for the depth range

  // Unit vector from the target toward the eye, and the screen's right and up.
  glm::vec3 dir() const
  {
    return rotation * glm::vec3(0, 0, 1);
  }
  glm::vec3 right() const
  {
    return rotation * glm::vec3(1, 0, 0);
  }
  glm::vec3 up() const
  {
    return rotation * glm::vec3(0, 1, 0);
  }
  glm::vec3 eye() const
  {
    return target + dir() * distance;
  }

  // The camera's own up, not the world's: it is never parallel to the view
  // direction, so this holds in every orientation.
  glm::mat4 view() const
  {
    return glm::lookAt(eye(), target, up());
  }
  glm::mat4 proj() const
  {
    const float aspect = vh > 0 ? float(vw) / float(vh) : 1.0f;
    if (!orthographic)
      return glm::perspective(fovy, aspect, znear, zfar);
    // The height perspective shows at the target, so zooming -- which changes
    // `distance` -- zooms here too. Depth runs as far behind the eye as in
    // front: with no perspective nothing needs clipping for being close.
    const float h = distance * std::tan(fovy * 0.5f);
    return glm::ortho(-h * aspect, h * aspect, -h, h, -zfar, zfar);
  }

  // --- orientations ---

  // A little above and to the side: the view a scene opens in.
  static glm::quat default_rotation()
  {
    return glm::angleAxis(0.7f, glm::vec3(0, 1, 0)) *
           glm::angleAxis(-0.5f, glm::vec3(1, 0, 0));
  }

  // Looking at the scene from the + side of world axis `axis` (0, 1, 2 for X,
  // Y, Z), or the - side. Screen up is world +Y, except looking down or up the
  // Y axis, where it is -Z from above and +Z from below; screen right then
  // follows, so every view is right-handed.
  static glm::quat looking_from(int axis, bool negative)
  {
    glm::vec3 back(0.0f);
    back[axis] = negative ? -1.0f : 1.0f;
    const glm::vec3 up = axis != 1  ? glm::vec3(0, 1, 0)
                         : negative ? glm::vec3(0, 0, 1)
                                    : glm::vec3(0, 0, -1);
    return glm::quat_cast(glm::mat3(glm::cross(up, back), up, back));
  }

  // Turns the camera to orientation `to` about the pivot, as a drag would: the
  // pivot keeps its place on screen.
  void turn_to(const glm::quat &to)
  {
    const glm::quat delta = to * glm::inverse(rotation);
    // Renormalised every step, so a long session cannot drift into a scale.
    rotation = glm::normalize(to);
    target = pivot + delta * (target - pivot);
  }

  // --- mouse gestures, all in pixels of movement ---

  // Turns the scene the way the mouse moved: about the screen axis at right
  // angles to the drag, through `pivot`, by 0.005 rad a pixel. The scene
  // turning one way is the camera -- its orientation and its target -- turning
  // the other way about the same axis.
  void orbit(float dx, float dy)
  {
    const float angle = 0.005f * std::sqrt(dx * dx + dy * dy);
    if (angle == 0.0f)
      return;
    // Screen y grows downward, so a drag down turns about +X and brings the
    // near side of the scene down; a drag right turns about +Y and brings it
    // right.
    const glm::vec3 axis = rotation * glm::normalize(glm::vec3(dy, dx, 0.0f));
    turn_to(glm::angleAxis(-angle, axis) * rotation);
  }

  void pan(float dx, float dy)
  {
    // Scaled so a pixel of mouse movement tracks a pixel of the scene: the
    // visible height at the target is 2*distance*tan(fovy/2), spread over vh
    // -- in either projection, since the orthographic one frames the same.
    const float per_pixel =
        distance * std::tan(fovy * 0.5f) * 2.0f / float(std::max(vh, 1));
    target += (-right() * dx + up() * dy) * per_pixel;
  }

  void dolly(float steps)
  {
    // Multiplicative, so zooming feels the same however close you already are.
    distance = std::max(distance * std::pow(0.9f, steps), 1e-4f);
    // The far plane follows, or zooming out past it clips the whole scene.
    zfar = std::max(zfar, distance + scene_radius * 8.0f);
  }

  // Frames a sphere. The caller passes the centre and radius rather than a box
  // because Scene has already measured both, and two floors on a degenerate
  // radius would be two chances to disagree about how big an empty scene is.
  //
  // The orientation is kept: framing is about what is in view, not from where.
  void fit(const glm::vec3 &center, float radius)
  {
    target = center;
    pivot = center;
    distance = radius / std::tan(fovy * 0.5f) * 1.6f;
    // Derived from the scene size rather than fixed, so a 100 m domain and a
    // 1 mm one both get usable depth precision instead of z-fighting.
    znear = radius * 1e-3f;
    zfar = distance + radius * 8.0f;
    scene_radius = radius;
  }

  // The world-space ray through a pixel. `my` is measured from the top of the
  // window, matching GLFW's cursor coordinates.
  void screen_ray(float mx, float my, glm::vec3 &origin, glm::vec3 &dir) const
  {
    const float ndc_x = 2.0f * mx / float(std::max(vw, 1)) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * my / float(std::max(vh, 1));

    // Unproject the pixel on the near and far planes; the ray joins them.
    const glm::mat4 inv = glm::inverse(proj() * view());
    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt = inv * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    near_pt /= near_pt.w;
    far_pt /= far_pt.w;

    origin = glm::vec3(near_pt);
    dir = glm::normalize(glm::vec3(far_pt - near_pt));
  }
};

} // namespace mesh_viewer
