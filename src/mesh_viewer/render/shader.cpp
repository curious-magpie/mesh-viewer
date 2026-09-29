#include "mesh_viewer/render/shader.h"

#include <glm/gtc/type_ptr.hpp>

namespace mesh_viewer
{

namespace
{

GLuint compile(GLenum type, const char *src, std::string &err)
{
  const GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);

  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[2048];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    err = log;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

} // namespace

bool ShaderProgram::build(const char *vertex_src,
                          const char *fragment_src,
                          std::string &err)
{
  destroy();

  const GLuint vs = compile(GL_VERTEX_SHADER, vertex_src, err);
  if (!vs)
    return false;
  const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src, err);
  if (!fs)
  {
    glDeleteShader(vs);
    return false;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[2048];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    err = log;
    glDeleteProgram(program);
    return false;
  }

  id_ = program;
  return true;
}

void ShaderProgram::destroy()
{
  if (id_)
    glDeleteProgram(id_);
  id_ = 0;
  locations_.clear();
}

GLint ShaderProgram::location(const char *name) const
{
  auto it = locations_.find(name);
  if (it != locations_.end())
    return it->second;
  const GLint loc = glGetUniformLocation(id_, name);
  locations_.emplace(name, loc); // cache misses too: -1 is a valid answer
  return loc;
}

void ShaderProgram::set(const char *name, const glm::mat4 &m) const
{
  glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
}

void ShaderProgram::set(const char *name, const glm::vec3 &v) const
{
  glUniform3fv(location(name), 1, glm::value_ptr(v));
}

void ShaderProgram::set(const char *name, const glm::vec4 &v) const
{
  glUniform4fv(location(name), 1, glm::value_ptr(v));
}

void ShaderProgram::set(const char *name, float f) const
{
  glUniform1f(location(name), f);
}

} // namespace mesh_viewer
