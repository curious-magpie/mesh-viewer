#include "mesh_viewer/viewer.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include <png.h>

namespace mesh_viewer
{

std::string Viewer::save_png()
{
  constexpr int scale = 4;
  GLint limit = 0, viewport_limit[2] = {}, max_samples = 0;
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &limit);
  glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport_limit);
  glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
  if (cam_.vw <= 0 || cam_.vh <= 0 ||
      cam_.vw > std::min(limit, viewport_limit[0]) / scale ||
      cam_.vh > std::min(limit, viewport_limit[1]) / scale)
    throw std::runtime_error(
        "4x image exceeds GPU limits; make the window smaller.");

  Camera camera = cam_;
  camera.vw *= scale;
  camera.vh *= scale;
  const int width = camera.vw, height = camera.vh;
  std::vector<unsigned char> pixels(size_t(width) * height * 4);

  // The normal window render follows this call. Always give it back its
  // framebuffer, even when allocation, rendering or writing fails.
  struct Target
  {
    GLuint fb[2] = {}, rb[3] = {};
    ~Target()
    {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
      glDeleteFramebuffers(2, fb);
      glDeleteRenderbuffers(3, rb);
    }
  } target;
  glGenFramebuffers(2, target.fb);
  glGenRenderbuffers(3, target.rb);
  glBindFramebuffer(GL_FRAMEBUFFER, target.fb[0]);
  const int samples = std::min(4, max_samples);
  glBindRenderbuffer(GL_RENDERBUFFER, target.rb[0]);
  glRenderbufferStorageMultisample(
      GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target.rb[0]);
  // The cap uses stencil, so a depth-only attachment would lose cut faces.
  glBindRenderbuffer(GL_RENDERBUFFER, target.rb[1]);
  glRenderbufferStorageMultisample(
      GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                            GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER,
                            target.rb[1]);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("could not allocate the export framebuffer.");

  glBindFramebuffer(GL_FRAMEBUFFER, target.fb[1]);
  glBindRenderbuffer(GL_RENDERBUFFER, target.rb[2]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target.rb[2]);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("could not allocate the PNG readback buffer.");

  glBindFramebuffer(GL_FRAMEBUFFER, target.fb[0]);
  glViewport(0, 0, width, height);
  CutPlane plane = plane_;
  plane.show_gizmo = false;
  renderer_.draw(
      scene_, camera, plane, view_.background, view_.surface_mode, scale);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fb[1]);
  glBlitFramebuffer(0,
                    0,
                    width,
                    height,
                    0,
                    0,
                    width,
                    height,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fb[1]);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  // Translucent layers blend into the chosen background, not the slide behind it.
  for (size_t i = 3; i < pixels.size(); i += 4)
    pixels[i] = 255;

  // Number files rather than replacing the previous view of a slide.
  char filename[64];
  unsigned number = 0;
  do
    std::snprintf(filename, sizeof(filename), "screenshot-%03u.png", ++number);
  while (std::filesystem::exists(filename));

  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = width;
  image.height = height;
  image.format = PNG_FORMAT_RGBA;
  // OpenGL starts at the bottom; libpng's negative stride flips the rows.
  const bool ok = png_image_write_to_file(
      &image, filename, 0, pixels.data(), -width * 4, nullptr);
  const std::string error = image.message;
  png_image_free(&image);
  if (!ok)
    throw std::runtime_error(error);
  return "Saved " + std::filesystem::absolute(filename).string() + " (" +
         std::to_string(width) + " x " + std::to_string(height) + ")";
}

} // namespace mesh_viewer
