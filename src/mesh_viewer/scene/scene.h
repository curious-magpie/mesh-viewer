// The view of a MeshSource: one Layer per mesh, in the order they composite.
//
// Scene owns the layers, the order they composite in, the shared coordinate
// origin and the overall bounding box -- but not the meshes. It is rebuilt from
// the source by `sync`, which is cheap enough to call every frame: a layer
// whose mesh has not moved does nothing at all.
//
// Keeping these together is the point: the draw order must stay a permutation
// of the layer list, and the origin must be fixed once and then applied to
// every later mesh, neither of which survives being spread across call sites.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "mesh_viewer/mesh_source.h"
#include "mesh_viewer/scene/cut_plane.h"
#include "mesh_viewer/scene/draw_objects/debug_overlay.h"
#include "mesh_viewer/scene/layer.h"

namespace mesh_viewer
{

// The bounding box of everything loaded. Sets the camera framing, the cutting
// plane's slider range, and how large the plane geometry has to be drawn.
struct SceneBounds
{
  glm::vec3 min{-1.0f}, max{1.0f};
  glm::vec3 center{0.0f};
  float radius = 1.0f; // half the diagonal, so it bounds the box from outside
};

// Half-width of the cutting plane's draggable handle. Shared by the code that
// draws it and the code that hit-tests it, so the two cannot disagree.
inline float handle_half(const SceneBounds &b)
{
  return b.radius * 0.7f;
}

class Scene
{
public:
  // Makes the view match `source`: a layer appears for every mesh that does
  // not have one, disappears for every mesh that is gone, and refreshes its
  // GPU copy if its mesh has moved.
  //
  // Needs a current GL context.
  void sync(const MeshSource &source);

  // Releases every layer's GPU buffers. Call this while the GL context is still
  // current -- see the note on GpuMesh.
  void clear();

  bool empty() const
  {
    return layers_.empty();
  }
  size_t size() const
  {
    return layers_.size();
  }

  const std::vector<Layer> &layers() const
  {
    return layers_;
  }
  std::vector<Layer> &layers()
  {
    return layers_;
  }

  // Layer indices, back to front. Translucent layers composite in exactly this
  // order, which is why it is the user's to control: these meshes nest, and no
  // automatic per-object sort is correct for nested shells.
  const std::vector<int> &order() const
  {
    return order_;
  }

  // `slot` is a position in order(), not a layer index.
  void move_earlier(size_t slot);
  void move_later(size_t slot);

  // The layer drawn at `slot` in the composite order.
  Layer &at_slot(size_t slot)
  {
    return layers_[size_t(order_[slot])];
  }
  const Layer &at_slot(size_t slot) const
  {
    return layers_[size_t(order_[slot])];
  }

  const SceneBounds &bounds() const
  {
    return bounds_;
  }

  // All coordinates are drawn relative to this, taken from the first mesh
  // synced. Later meshes reuse it, so files sharing a coordinate system stay
  // aligned with each other. See build_layer for why it exists at all.
  glm::dvec3 origin() const
  {
    return origin_.value_or(glm::dvec3(0.0));
  }

  size_t visible_triangles() const;

  // Refreshes every layer's element slice for the current plane. Call once per
  // frame, before drawing; layers whose slice is already current do no work.
  void update_element_slices(const CutPlane &plane);

  // --- the debug overlay ---
  //
  // What a computation has asked to have drawn on top of the meshes. It lives
  // here rather than on the Layers because a channel is a statement about the
  // scene -- often about two meshes at once -- and not a display setting the
  // panel edits per mesh.
  //
  // Call after sync(source), with a current GL context. Nothing is uploaded on
  // a frame where `overlay` has not changed.
  void sync_debug_overlay(const OverlaySource &overlay)
  {
    debug_overlay_.sync(overlay, *this);
  }

  // Mutable because the panel edits each item's colour and whether it is shown.
  const DebugOverlay &debug_overlay() const
  {
    return debug_overlay_;
  }
  DebugOverlay &debug_overlay()
  {
    return debug_overlay_;
  }

  // The layer showing `mesh_id`, or null.
  Layer *layer(uint32_t mesh_id);
  const Layer *layer(uint32_t mesh_id) const;

private:
  void rebuild_order();
  void recompute_bounds();

  std::vector<Layer> layers_;
  std::vector<int> order_;
  SceneBounds bounds_;
  std::optional<glm::dvec3> origin_; // set by the first mesh, then never again
  DebugOverlay debug_overlay_;

  std::vector<MeshView> views_; // sync's scratch, kept for its allocation
};

} // namespace mesh_viewer
