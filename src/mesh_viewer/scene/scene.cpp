#include "mesh_viewer/scene/scene.h"

#include <algorithm>

#include "mesh_viewer/scene/bounds.h"

namespace mesh_viewer
{

namespace
{

// Layers get distinct colours automatically; the user can recolour any of them
// from the panel afterwards. Assigned here rather than by the loader, because
// this is the only place a layer is born.
const glm::vec3 kPalette[] = {
    {0.55f, 0.65f, 0.80f},
    {0.90f, 0.55f, 0.35f},
    {0.45f, 0.75f, 0.55f},
    {0.85f, 0.75f, 0.40f},
    {0.70f, 0.50f, 0.80f},
    {0.45f, 0.75f, 0.80f},
    {0.85f, 0.50f, 0.60f},
    {0.65f, 0.70f, 0.45f},
};
constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

} // namespace

void Scene::sync(const MeshSource &source)
{
  bool changed = false;

  // Asked once, up front: both steps below need the whole list, and a view is
  // a handful of pointers.
  views_.clear();
  for (size_t i = 0; i < source.mesh_count(); ++i)
    views_.push_back(source.mesh(i));

  // --- 1. drop layers whose mesh is gone ---
  //
  // Ids are never reused, so a layer whose id is missing is showing something
  // that no longer exists. Its GPU buffers go with it, and whatever it kept
  // alive is let go.
  const auto gone = [this](const Layer &l)
  {
    for (const MeshView &v : views_)
      if (v.id == l.mesh_id)
        return false;
    return true;
  };
  const size_t before = layers_.size();
  layers_.erase(std::remove_if(layers_.begin(), layers_.end(), gone),
                layers_.end());
  if (layers_.size() != before)
  {
    rebuild_order();
    changed = true;
  }

  // --- 2. add or refresh one per mesh ---
  for (const MeshView &view : views_)
  {
    // The first mesh fixes the origin for every mesh after it, so that files
    // sharing a coordinate system stay aligned with each other. See Layer for
    // why it exists at all.
    if (!origin_)
    {
      origin_ = mesh_center(view);
    }

    Layer *layer = this->layer(view.id);
    if (!layer)
    {
      layers_.emplace_back();
      layer = &layers_.back();
      layer->color = kPalette[(layers_.size() - 1) % kPaletteSize];
      order_.push_back(int(layers_.size()) - 1);
      changed = true;
    }

    changed = layer->refresh(view, *origin_) || changed;
  }

  // Nothing is held past the sync but what the layers took.
  views_.clear();

  if (changed)
  {
    recompute_bounds();
  }
}

Layer *Scene::layer(uint32_t mesh_id)
{
  for (Layer &l : layers_)
    if (l.mesh_id == mesh_id)
      return &l;
  return nullptr;
}

const Layer *Scene::layer(uint32_t mesh_id) const
{
  for (const Layer &l : layers_)
    if (l.mesh_id == mesh_id)
      return &l;
  return nullptr;
}

// Layer indices shift when one is removed, so the composite order is rebuilt
// from scratch rather than patched. It is a handful of integers.
void Scene::rebuild_order()
{
  order_.resize(layers_.size());
  for (size_t i = 0; i < layers_.size(); ++i)
    order_[i] = int(i);
}

void Scene::clear()
{
  layers_.clear();
  order_.clear();
  bounds_ = SceneBounds{};
  origin_.reset();

  debug_overlay_.release();
}

void Scene::move_earlier(size_t slot)
{
  if (slot > 0 && slot < order_.size())
    std::swap(order_[slot], order_[slot - 1]);
}

void Scene::move_later(size_t slot)
{
  if (slot + 1 < order_.size())
    std::swap(order_[slot], order_[slot + 1]);
}

size_t Scene::visible_triangles() const
{
  size_t total = 0;
  for (const Layer &l : layers_)
    if (l.visible)
      total += l.tri_count;
  return total;
}

void Scene::update_element_slices(const CutPlane &plane)
{
  for (Layer &l : layers_)
    l.update_slice(plane);
}

void Scene::recompute_bounds()
{
  Bounds b;
  for (const Layer &l : layers_)
    b.add(Bounds{l.bbmin, l.bbmax});
  if (b.empty()) // nothing loaded: fall back to a unit box
    b = Bounds{glm::vec3(-1.0f), glm::vec3(1.0f)};

  bounds_.min = b.min;
  bounds_.max = b.max;
  bounds_.center = b.center();
  // A single point, or a plane: every consumer divides by this, so it must not
  // be zero.
  bounds_.radius = b.radius() > 1e-6f ? b.radius() : 1.0f;
}

} // namespace mesh_viewer
