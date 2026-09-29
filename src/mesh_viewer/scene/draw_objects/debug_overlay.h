// The GPU side of an OverlaySource: one IndexedDraw per channel, plus how each
// one is shown.
//
// The split is the point. A channel says *what* was found, in mesh indices, and
// is written by whatever found it. An item says how it is drawn -- primitive,
// colour, whether it is on at all -- and belongs to the viewer, so toggling a
// checkbox costs nothing and re-uploads nothing.
//
// Indices address a layer's own vertex buffer, so a channel of a million marked
// vertices is four megabytes of indices and not one copied position.
//
// Owns GL names, so it must not outlive the GL context -- hence `release`, and
// hence `sync` needing a current one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "mesh_viewer/mesh_source.h"
#include "mesh_viewer/scene/draw_objects/indexed_draw.h"

namespace mesh_viewer
{

class Scene;

class DebugOverlay
{
public:
  struct Item
  {
    std::string name; // the channel this shows, and what the checkbox says
    IndexedDraw draw;
    GLenum mode = GL_POINTS;
    uint32_t mesh_id = 0;

    // --- the viewer's, not the producer's ---
    glm::vec3 color{1.0f};

    // Off until ticked. A host may publish channels on every run, and what it
    // found is there to be looked at on request, not drawn over every result.
    bool enabled = false;

    // Draw this channel on top of the whole scene rather than letting what is
    // in front of it hide it.
    //
    // Off by default: a channel is ordinary geometry, and being hidden by the
    // thing in front of it is information -- the visible part of an interface
    // is the part standing outside its target. Turn it on for a channel that
    // has to be findable wherever it is, which is usually a small one: a
    // handful of marked vertices is easy to lose inside a mesh, an interface
    // is not.
    bool in_front = false;
  };

  // Brings the items up to date with `src`, uploading nothing if it has not
  // changed since the last call. Cheap enough to call every frame.
  //
  // `scene` is where the vertex buffer to index comes from; a channel naming a
  // mesh with no layer draws nothing until one appears. Needs a current GL
  // context.
  void sync(const OverlaySource &src, const Scene &scene);

  // Mutable because the checkbox and the colour picker edit them in place.
  const std::vector<Item> &items() const
  {
    return items_;
  }
  std::vector<Item> &items()
  {
    return items_;
  }

  // Requires a current GL context. See the note on GpuMesh.
  void release();

private:
  // Every item holds GL names, so growing this vector moves them. That is
  // correct only because IndexedDraw's move transfers ownership and its copy is
  // deleted -- if it were ever copyable, two items would free the same buffer.
  std::vector<Item> items_;

  uint64_t synced_revision_ = 0;
};

} // namespace mesh_viewer
