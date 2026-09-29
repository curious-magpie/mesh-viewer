#include "mesh_viewer/scene/draw_objects/debug_overlay.h"

#include "mesh_viewer/scene/scene.h"

namespace mesh_viewer
{

namespace
{

// Channels get distinct colours automatically, the way layers do; the panel can
// recolour any of them afterwards. Bright and saturated on purpose -- these are
// drawn over the meshes and have to read against any of them.
const glm::vec3 kPalette[] = {
    {1.00f, 0.45f, 0.05f},
    {0.30f, 0.85f, 1.00f},
    {1.00f, 0.25f, 0.45f},
    {0.55f, 1.00f, 0.30f},
    {1.00f, 0.85f, 0.15f},
    {0.70f, 0.45f, 1.00f},
};
constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

GLenum gl_mode(OverlayPrim prim)
{
  switch (prim)
  {
  case OverlayPrim::Lines:
    return GL_LINES;
  case OverlayPrim::Triangles:
    return GL_TRIANGLES;
  case OverlayPrim::Points:
    break;
  }
  return GL_POINTS;
}

} // namespace

void DebugOverlay::sync(const OverlaySource &src, const Scene &scene)
{
  if (src.revision() == synced_revision_)
    return;
  bool complete = true; // every channel found the layer it indexes

  // Items track channels one for one. Shrinking destroys the items that are
  // gone, which frees their GL objects -- which is why this needs a context.
  const size_t count = src.channel_count();
  items_.resize(count);

  for (size_t i = 0; i < count; ++i)
  {
    const OverlayChannel c = src.channel(i);
    Item &item = items_[i];

    // The name is the identity. A different one in this slot is a different
    // channel, so it gets the appearance a new channel gets; the same one keeps
    // whatever the panel has since done to it.
    if (item.name != c.name)
    {
      item.name = std::string(c.name);
      item.color = kPalette[i % kPaletteSize];
      item.enabled = true;
      item.in_front = false;
    }

    item.mode = gl_mode(c.prim);
    item.mesh_id = c.mesh_id;

    // Nothing to index: the mesh this channel describes has no layer, either
    // because it was never loaded or because it has been closed. Keep the
    // channel and its settings, and leave the sync unfinished, so that the
    // next one fills it in if a layer appears.
    const Layer *layer = scene.layer(c.mesh_id);
    if (!layer)
    {
      item.draw.clear();
      complete = false;
      continue;
    }

    // Attached every time rather than once: a channel can move to a different
    // mesh between runs, and re-recording two bindings into a VAO costs less
    // than remembering which buffer each item last saw.
    item.draw.attach(layer->gpu.vertex_buffer());
    item.draw.upload(c.indices.data(), c.indices.size());
  }

  if (complete)
    synced_revision_ = src.revision();
}

void DebugOverlay::release()
{
  items_.clear(); // each item's IndexedDraw frees itself
  synced_revision_ = 0;
}

} // namespace mesh_viewer
