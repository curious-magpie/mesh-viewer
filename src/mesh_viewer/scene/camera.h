// Orbit camera: the eye sits on a sphere around a target point.
//
// Three numbers describe it -- `yaw` and `pitch` place the eye on the sphere,
// `distance` is its radius -- and the three mouse gestures each move one of
// them, with panning sliding the target instead. Header-only: it is pure math
// over a handful of floats.
#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mesh_viewer
{

struct Camera
{
  glm::vec3 target{0.0f}; // the point we orbit around and look at
  float distance = 3.0f;
  float yaw = 0.7f;   // radians, around world +Y
  float pitch = 0.5f; // radians, clamped short of straight up or down
  float fovy = glm::radians(45.0f);
  float znear = 0.01f, zfar = 100.0f;
  int vw = 1280, vh = 720;   // viewport, in pixels
  float scene_radius = 1.0f; // what fit() framed, for the depth range

  // Unit vector from the target toward the eye.
  glm::vec3 dir() const
  {
    const float cp = std::cos(pitch);
    return glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
  }
  glm::vec3 eye() const
  {
    return target + dir() * distance;
  }
  glm::vec3 right() const
  {
    return glm::normalize(glm::cross(-dir(), glm::vec3(0, 1, 0)));
  }
  glm::vec3 up() const
  {
    return glm::normalize(glm::cross(right(), -dir()));
  }

  glm::mat4 view() const
  {
    return glm::lookAt(eye(), target, glm::vec3(0, 1, 0));
  }
  glm::mat4 proj() const
  {
    const float aspect = vh > 0 ? float(vw) / float(vh) : 1.0f;
    return glm::perspective(fovy, aspect, znear, zfar);
  }

  // --- mouse gestures, all in pixels of movement ---

  void orbit(float dx, float dy)
  {
    // Just short of a right angle: at exactly straight up the world-up vector
    // and the view direction become parallel and lookAt degenerates.
    constexpr float kPitchLimit = 1.5533f;
    yaw -= dx * 0.005f;
    pitch = std::clamp(pitch + dy * 0.005f, -kPitchLimit, kPitchLimit);
  }

  void pan(float dx, float dy)
  {
    // Scaled so a pixel of mouse movement tracks a pixel of the scene: the
    // visible height at the target is 2*distance*tan(fovy/2), spread over vh.
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
  void fit(const glm::vec3 &center, float radius)
  {
    target = center;
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
