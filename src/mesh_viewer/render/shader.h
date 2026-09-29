// A linked shader program, with uniforms addressed by name.
//
// Locations are looked up once and cached, so callers write
// `prog.set("uView", view)` instead of carrying a GLint per uniform around.
// Setting a uniform the program does not have is silently ignored, which is
// what lets the renderer's programs share calling code despite having
// different uniform sets.
//
// Owns its GL name and deletes it in the destructor, so it is move-only.
#pragma once

#include <string>
#include <unordered_map>

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace mesh_viewer
{

class ShaderProgram
{
public:
  ShaderProgram() = default;
  ~ShaderProgram()
  {
    destroy();
  }

  ShaderProgram(const ShaderProgram &) = delete;
  ShaderProgram &operator=(const ShaderProgram &) = delete;
  ShaderProgram(ShaderProgram &&other) noexcept
  {
    steal(other);
  }
  ShaderProgram &operator=(ShaderProgram &&other) noexcept
  {
    if (this != &other)
    {
      destroy();
      steal(other);
    }
    return *this;
  }

  // Compiles and links. False with the compiler or linker log in `err`.
  bool
  build(const char *vertex_src, const char *fragment_src, std::string &err);
  void destroy();

  void use() const
  {
    glUseProgram(id_);
  }

  void set(const char *name, const glm::mat4 &m) const;
  void set(const char *name, const glm::vec3 &v) const;
  void set(const char *name, const glm::vec4 &v) const;
  void set(const char *name, float f) const;

private:
  void steal(ShaderProgram &other) noexcept
  {
    id_ = other.id_;
    locations_ = std::move(other.locations_);
    other.id_ = 0;
    other.locations_.clear();
  }

  GLint location(const char *name) const;

  GLuint id_ = 0;
  mutable std::unordered_map<std::string, GLint> locations_;
};

} // namespace mesh_viewer
