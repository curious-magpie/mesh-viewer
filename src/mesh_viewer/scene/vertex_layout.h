// The one vertex layout this program uses.
//
// Every shader here declares `layout(location = 0) in vec3 aPos` and nothing
// else, and three different objects -- GpuMesh, IndexedDraw and DynamicQuads
// -- have to bind buffers to match it. Stating it once means adding a second
// attribute is one edit rather than a search.
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace mesh_viewer
{

// Points attribute 0 at the currently bound GL_ARRAY_BUFFER as tightly packed
// vec3 positions. Recorded into whichever VAO is bound, so bind that first.
inline void bind_position_attrib()
{
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
}

} // namespace mesh_viewer
