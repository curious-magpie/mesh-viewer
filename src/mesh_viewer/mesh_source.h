// What the viewer draws, as the host describes it.
//
// The viewer knows nothing about how a host stores its meshes. Once a frame it
// asks a MeshSource for the meshes that exist now, and an OverlaySource for
// what the host's computations want marked on them, and makes its GPU copies
// match. Nothing is pushed and nothing is notified: each mesh carries a
// revision, each layer remembers the revision it uploaded, and a mesh that has
// not moved costs one comparison. So a host that moves a vertex has only to
// bump that mesh's revision; the next frame shows it.
//
// Both interfaces are small on purpose. A host implements them as a thin
// adapter over whatever it already has -- mesh-dev's is a few lines over its
// Document -- and the viewer's own example builds them over plain vectors.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <glm/glm.hpp>

#include "mesh_viewer/span.h"

namespace mesh_viewer
{

// One mesh, as it is this frame: a surface, or a volume made of tets.
//
// The spans point into the host's own arrays and are read during the frame
// they were handed over in -- by Scene::sync, and by the renderer when it cuts
// the tets the plane passes through. They must stay valid until the next
// sync. `keep_alive`, if set, is held by the layer showing the mesh, so a host
// that owns its meshes through shared_ptrs can pass one (the aliasing
// constructor makes a shared_ptr<const void> from any of them) and never think
// about which of it and the viewer goes first.
struct MeshView
{
  // Stable for the life of the mesh and never reused: it is how a layer finds
  // its mesh from one frame to the next, and how an overlay channel names the
  // mesh whose vertices it indexes. 0 means "no mesh".
  uint32_t id = 0;

  std::string_view name; // copied by the layer the first time it is seen

  // Must change whenever `positions` does. Only the positions may change over
  // a mesh's life: the triangles and tets are uploaded once, the first time
  // the mesh is seen.
  uint64_t revision = 0;

  Span<glm::dvec3> positions;

  // What is drawn: three indices per triangle. A volume's own boundary hull,
  // or a surface's triangles.
  Span<uint32_t> triangles;

  // Four indices per tet, for the elements the cutting plane passes through.
  // Empty for a surface.
  Span<uint32_t> tets;

  // `closed` gates the cutting plane's cap: the stencil parity trick only
  // means anything on a watertight surface. `oriented` gates backface
  // culling, which is wrong if the triangles do not agree on which side is
  // out. The boundary of a tet mesh is both.
  bool closed = true;
  bool oriented = true;

  std::shared_ptr<const void> keep_alive;

  bool is_volume() const
  {
    return !tets.empty();
  }
};

class MeshSource
{
public:
  virtual ~MeshSource() = default;

  // The meshes that exist now, in the order their layers are first added.
  virtual size_t mesh_count() const = 0;
  virtual MeshView mesh(size_t i) const = 0;
};

// What an overlay channel's indices are: one per point, two per line, three
// per triangle.
enum class OverlayPrim
{
  Points,
  Lines,
  Triangles
};

// A named list of indices into one mesh's vertices, which the viewer draws on
// top of that mesh's layer. The indices address the layer's own vertex buffer,
// so marking a million vertices uploads four megabytes of indices and not one
// position. How a channel looks -- colour, visibility, drawn in front -- is the
// viewer's, and set in its Inspect window; a channel says only what was found.
struct OverlayChannel
{
  std::string_view name; // its identity, and what the checkbox says
  OverlayPrim prim = OverlayPrim::Points;
  uint32_t mesh_id = 0; // whose vertices these index
  Span<uint32_t> indices;
};

class OverlaySource
{
public:
  virtual ~OverlaySource() = default;

  // Changes whenever any channel does. The viewer re-reads the channels only
  // when it sees a number it has not seen, so this is what makes an unchanged
  // overlay free.
  virtual uint64_t revision() const = 0;

  virtual size_t channel_count() const = 0;
  virtual OverlayChannel channel(size_t i) const = 0;
};

} // namespace mesh_viewer
