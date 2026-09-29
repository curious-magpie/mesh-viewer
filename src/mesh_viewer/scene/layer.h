// A Layer is one mesh as the viewer sees it: the display settings the panel
// edits, the GPU buffers, and a view of the mesh being shown.
//
// It does not own the mesh -- the host does, and describes it with a MeshView.
// What a layer owns is the *projection*: the float32, recentred copy of the
// vertices OpenGL needs, the indices of what is drawn (a volume's hull, a
// surface's triangles), and the revision that copy was made from. `refresh`
// compares that revision and re-uploads only what actually changed, which is
// what lets the whole view be rebuilt from the source every frame for nothing.
//
// The layer keeps the view's `keep_alive`, so a host that owns its meshes
// through shared_ptrs has no destruction order to enforce between a view and
// the data it reads.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "mesh_viewer/mesh_source.h"
#include "mesh_viewer/scene/cut_plane.h"
#include "mesh_viewer/scene/draw_objects/element_slice.h"
#include "mesh_viewer/scene/gpu_mesh.h"

namespace mesh_viewer
{

struct Layer
{
  // A layer counts as opaque just short of 1.0, so sliding a slider to the very
  // end is not required to get the opaque (depth-writing) path.
  static constexpr float kOpaqueAlpha = 0.999f;

  uint32_t mesh_id = 0; // which MeshView this shows
  std::string name;     // the MeshView's, copied when the layer was made

  // --- what the user controls, from the panel ---
  bool visible = true;
  float alpha = 1.0f;
  bool cap = false;      // fill in the cutting plane's cross-section
  bool solid_cap = true; // ... opaquely, even when the shell is translucent
  bool show_elements = false; // draw the tets the plane passes through, whole
  glm::vec3 color{0.75f};

  // --- the mesh being shown, as its source last described it ---
  //
  // Taken again on every refresh, whether or not the mesh moved, so they are
  // never older than this frame's sync -- which is as long as a MeshView
  // promises they stay valid.
  Span<glm::dvec3> positions;
  Span<uint32_t> tets; // four per tet; empty for a surface
  bool mesh_closed = true, mesh_oriented = true;
  std::shared_ptr<const void> keep_alive;

  // --- the projection, and what it was made from ---
  glm::vec3 bbmin{0.0f}, bbmax{0.0f}; // recentred scene space
  size_t tri_count = 0; // what is drawn: a volume's hull, or the surface itself
  glm::dvec3 origin{0.0};         // file space minus this is scene space
  bool uploaded = false;          // false until the first upload
  uint64_t uploaded_revision = 0; // the mesh revision last uploaded

  GpuMesh gpu;
  ElementSlice slice;

  // Brings the projection up to date with `mesh`, uploading only what changed:
  // everything on the first call, positions alone once the mesh has moved,
  // nothing at all when it has not. True if anything was uploaded.
  bool refresh(const MeshView &mesh, const glm::dvec3 &scene_origin);

  bool is_opaque() const
  {
    return alpha >= kOpaqueAlpha;
  }

  // A surface mesh has no volume to slice, and no elements to show.
  bool has_volume() const
  {
    return !tets.empty();
  }
  size_t tet_count() const
  {
    return tets.size() / 4;
  }

  // `closed` gates capping and `oriented` gates backface culling; see
  // MeshView. The boundary of a tet complex is both by construction, so a
  // volume is both whatever its source says.
  bool closed() const
  {
    return has_volume() || mesh_closed;
  }
  bool oriented() const
  {
    return has_volume() || mesh_oriented;
  }

  // Whether the cross-section fill should be drawn for this layer at all.
  bool wants_cap() const
  {
    return cap && closed();
  }
  bool wants_elements() const
  {
    return show_elements && has_volume();
  }

  // Brings `slice` up to date with the plane. Cheap to call every frame: the
  // slice rebuilds only when the plane has actually moved. Needs a current GL
  // context.
  void update_slice(const CutPlane &plane);

private:
  std::vector<glm::vec3> staging_;       // the last projection uploaded
  std::vector<uint32_t> drawn_vertices_; // what the drawn triangles use
};

// Centre of a mesh's bounding box, over the drawn vertices only -- a tet mesh
// carries interior nodes we never draw, and an .obj may carry vertices no face
// mentions. Used to pick the scene origin before any layer exists.
glm::dvec3 mesh_center(const MeshView &mesh);

} // namespace mesh_viewer
