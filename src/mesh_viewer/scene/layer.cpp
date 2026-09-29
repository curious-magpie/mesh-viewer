#include "mesh_viewer/scene/layer.h"

#include <algorithm>

#include "mesh_viewer/scene/bounds.h"

namespace mesh_viewer
{

namespace
{

// The groups' default colours: a cool one for label 0 and warm, saturated ones
// after it. Label 0 is usually the bulk -- the outside, the background
// material -- and the rest what is being looked for. Recolour any of them from
// the Inspect window.
const glm::vec3 kLabelPalette[] = {
    {0.62f, 0.70f, 0.82f},
    {0.98f, 0.60f, 0.25f},
    {0.45f, 0.85f, 0.45f},
    {0.95f, 0.35f, 0.50f},
    {0.95f, 0.85f, 0.30f},
    {0.65f, 0.50f, 0.95f},
};
constexpr size_t kLabelPaletteSize =
    sizeof(kLabelPalette) / sizeof(kLabelPalette[0]);

} // namespace

glm::dvec3 mesh_center(const MeshView &mesh)
{
  // Over the drawn triangles rather than every corner of every tet: interior
  // nodes are inside the hull by definition, so the box is the same one, found
  // from 300 k indices instead of 10 M on a 2.5 M-element mesh.
  DBounds b;
  for (uint32_t i : mesh.triangles)
    b.add(mesh.positions[i]);
  return b.empty() ? glm::dvec3(0.0) : b.center();
}

bool Layer::refresh(const MeshView &mesh, const glm::dvec3 &scene_origin)
{
  // Taken every time, moved or not: the spans are only promised until the next
  // sync, and the slice reads them after this one.
  positions = mesh.positions;
  tets = mesh.tets;
  mesh_closed = mesh.closed;
  mesh_oriented = mesh.oriented;
  keep_alive = mesh.keep_alive;
  refresh_labels(mesh);

  const uint64_t revision = mesh.revision;
  const bool first = !uploaded;
  if (!first && revision == uploaded_revision && scene_origin == origin)
    return false; // the mesh has not moved, and the scene has not shifted

  origin = scene_origin;

  // The float32, recentred copy the GPU draws -- the only place precision is
  // dropped, and dropped for the GPU alone; the mesh keeps the numbers the
  // file stated. Into a buffer kept between refreshes, because while physics
  // runs this is every frame.
  const Span<glm::dvec3> pos = mesh.positions;
  staging_.resize(pos.size());
  for (size_t v = 0; v < pos.size(); ++v)
    staging_[v] = glm::vec3(pos[v] - origin);

  if (first)
  {
    mesh_id = mesh.id;
    name = std::string(mesh.name);

    // Connectivity never changes, so the index buffer is uploaded exactly once
    // for the life of the mesh: a volume draws the boundary hull its source
    // found, a surface its own triangles.
    const Span<uint32_t> indices = mesh.triangles;
    tri_count = indices.size() / 3;
    gpu.upload(staging_, indices);

    // And so is which vertices those triangles use, which is what the bounds
    // are taken over.
    drawn_vertices_.assign(indices.begin(), indices.end());
    std::sort(drawn_vertices_.begin(), drawn_vertices_.end());
    drawn_vertices_.erase(
        std::unique(drawn_vertices_.begin(), drawn_vertices_.end()),
        drawn_vertices_.end());

    if (has_volume())
      slice.attach(gpu.vertex_buffer());
  }
  else
  {
    // Only the vertices moved; the indices still describe them.
    gpu.update_positions(staging_);
    slice.clear(); // that selection was made against the old positions
  }

  // Over the drawn vertices only. A file may carry vertices nothing references
  // -- they are uploaded, because compacting them out would cost a remap pass
  // to save 12 bytes each -- and a tet mesh carries interior ones; neither may
  // drag the camera off the mesh.
  Bounds b;
  for (uint32_t v : drawn_vertices_)
    b.add(staging_[v]);
  bbmin = b.min;
  bbmax = b.max;

  uploaded = true;
  uploaded_revision = revision;
  return true;
}

void Layer::update_slice(const CutPlane &plane)
{
  // Nothing to select: no volume, the panel did not ask, or the plane misses
  // this layer entirely -- the last of which is two dot products and saves a
  // full scan of the tets on every layer the cut does not reach.
  if (!plane.enabled || !wants_elements() ||
      plane.classify(bbmin, bbmax) != PlaneSide::Crossing)
  {
    slice.clear();
    return;
  }

  // The plane lives in scene space and the mesh in the file's own; shifting the
  // plane is four numbers, where shifting the mesh would be a second copy of
  // every vertex.
  const glm::dvec3 n(plane.n);
  slice.update(positions,
               tets,
               tet_labels,
               labels.size(),
               glm::dvec4(n, double(plane.w) - glm::dot(n, origin)));
}

void Layer::refresh_labels(const MeshView &mesh)
{
  // Only a full set means anything: a label per tet, or none.
  const bool usable =
      !mesh.tets.empty() && mesh.tet_labels.size() == mesh.tets.size() / 4;
  const Span<uint8_t> now = usable ? mesh.tet_labels : Span<uint8_t>();

  // The same labels as last frame -- the same array, the same revision -- are
  // nothing to do. Anything else rebuilds the slice, which sorts by them.
  const bool same =
      labels_seen && mesh.labels_revision == seen_labels_revision &&
      now.data() == tet_labels.data() && now.size() == tet_labels.size();
  tet_labels = now;
  if (!same)
  {
    labels_seen = true;
    seen_labels_revision = mesh.labels_revision;
    slice.clear();

    // One group per value up to the largest present. Existing groups keep
    // what the panel did to them; new ones get the next colour.
    size_t count = 0;
    for (uint8_t label : now)
      count = std::max(count, size_t(label) + 1);
    const size_t before = labels.size();
    labels.resize(count);
    for (size_t g = before; g < count; ++g)
      labels[g].color = kLabelPalette[g % kLabelPaletteSize];
  }

  // Names are cheap, and a source may name its labels later than it has them.
  for (size_t g = 0; g < labels.size(); ++g)
    labels[g].name = g < mesh.label_names.size()
                         ? std::string(mesh.label_names[g])
                         : "label " + std::to_string(g);
}

} // namespace mesh_viewer
