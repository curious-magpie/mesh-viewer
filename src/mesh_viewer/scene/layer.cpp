#include "mesh_viewer/scene/layer.h"

#include <algorithm>

#include "mesh_viewer/scene/bounds.h"

namespace mesh_viewer
{

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
    cap = closed(); // capping defaults on wherever it is meaningful
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
  slice.update(
      positions, tets, glm::dvec4(n, double(plane.w) - glm::dot(n, origin)));
}

} // namespace mesh_viewer
