// mesh-viewer-demo: the viewer driven by a host in one file.
//
//   mesh-viewer-demo
//
// Everything a real host does is here, at toy scale:
//
//   the meshes     a tet box and a sphere surface, generated, owned by the host
//   MeshSource     a view of them, which the viewer reads every frame -- with
//                  the box's tets labelled inside or outside the sphere
//   OverlaySource  one channel: the box vertices above a height
//   a Panel        the "Demo" window -- a clock that moves the sphere (update,
//                  animating), a slider whose change is deferred (defer), and
//                  a key of its own (on_key: space starts and stops the clock)
//
// The viewer's own Inspect and View windows come for free; turn the cutting
// plane on (P) and tick "elements" to see the box's tets, coloured by label.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "mesh_viewer/viewer.h"
#include "mesh_viewer/widgets.h"

namespace mv = mesh_viewer;

namespace
{

// A host's mesh, as simple as one can be. The viewer never sees this type,
// only the MeshView the source makes of it.
struct Mesh
{
  uint32_t id = 0;
  std::string name;
  uint64_t revision = 1; // bumped whenever `positions` changes
  std::vector<glm::dvec3> positions;
  std::vector<uint32_t> triangles; // what is drawn
  std::vector<uint32_t> tets;      // empty for a surface
  std::vector<uint8_t> labels;     // one per tet, or none
  uint64_t labels_revision = 1;    // bumped whenever `labels` changes
};

double signed_volume(const std::vector<glm::dvec3> &p,
                     const std::array<uint32_t, 4> &t)
{
  const glm::dvec3 &a = p[t[0]];
  return glm::dot(glm::cross(p[t[1]] - a, p[t[2]] - a), p[t[3]] - a) / 6.0;
}

// The unit cube as n^3 cells of six tets each, and its boundary.
//
// Each cell is cut along its main diagonal into the six tets that walk from
// corner 0 to corner 7 one axis at a time. Every cell is cut the same way, so
// neighbouring cells agree on their shared faces and the tets conform.
Mesh make_box(uint32_t id, int n)
{
  Mesh m;
  m.id = id;
  m.name = "box";

  const auto index = [n](int i, int j, int k)
  { return uint32_t((k * (n + 1) + j) * (n + 1) + i); };
  for (int k = 0; k <= n; ++k)
    for (int j = 0; j <= n; ++j)
      for (int i = 0; i <= n; ++i)
        m.positions.push_back(glm::dvec3(i, j, k) / double(n));

  static const int kAxisOrders[6][3] = {
      {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  for (int k = 0; k < n; ++k)
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i)
      {
        const auto corner = [&](int bits)
        {
          return index(
              i + (bits & 1), j + ((bits >> 1) & 1), k + ((bits >> 2) & 1));
        };
        for (const auto &axes : kAxisOrders)
        {
          const int b1 = 1 << axes[0];
          const int b2 = b1 | (1 << axes[1]);
          std::array<uint32_t, 4> t{
              corner(0), corner(b1), corner(b2), corner(7)};
          if (signed_volume(m.positions, t) < 0.0)
            std::swap(t[2], t[3]); // positive order, so the faces wind outward
          m.tets.insert(m.tets.end(), t.begin(), t.end());
        }
      }

  // The boundary is every face only one tet has. The faces are wound outward
  // for a positive tet, so the hull comes out oriented.
  static const int kFaces[4][3] = {{1, 2, 3}, {0, 3, 2}, {0, 1, 3}, {0, 2, 1}};
  std::map<std::array<uint32_t, 3>, std::pair<int, std::array<uint32_t, 3>>>
      seen;
  for (size_t t = 0; t < m.tets.size(); t += 4)
    for (const auto &f : kFaces)
    {
      const std::array<uint32_t, 3> tri{
          m.tets[t + f[0]], m.tets[t + f[1]], m.tets[t + f[2]]};
      std::array<uint32_t, 3> key = tri;
      std::sort(key.begin(), key.end());
      auto &entry = seen[key];
      ++entry.first;
      entry.second = tri;
    }
  for (const auto &kv : seen)
    if (kv.second.first == 1)
      m.triangles.insert(
          m.triangles.end(), kv.second.second.begin(), kv.second.second.end());
  return m;
}

// An icosahedron, subdivided and pushed out onto a sphere.
Mesh make_sphere(uint32_t id, glm::dvec3 centre, double radius, int levels)
{
  Mesh m;
  m.id = id;
  m.name = "sphere";

  const double g = (1.0 + std::sqrt(5.0)) / 2.0;
  std::vector<glm::dvec3> p = {{-1, g, 0},
                               {1, g, 0},
                               {-1, -g, 0},
                               {1, -g, 0},
                               {0, -1, g},
                               {0, 1, g},
                               {0, -1, -g},
                               {0, 1, -g},
                               {g, 0, -1},
                               {g, 0, 1},
                               {-g, 0, -1},
                               {-g, 0, 1}};
  std::vector<uint32_t> tri = {
      0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
      4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
      6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};

  for (int level = 0; level < levels; ++level)
  {
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoint;
    const auto mid = [&](uint32_t a, uint32_t b)
    {
      const auto key = std::minmax(a, b);
      auto it = midpoint.find(key);
      if (it != midpoint.end())
        return it->second;
      p.push_back((p[a] + p[b]) * 0.5);
      return midpoint[key] = uint32_t(p.size() - 1);
    };
    std::vector<uint32_t> finer;
    for (size_t t = 0; t < tri.size(); t += 3)
    {
      const uint32_t a = tri[t], b = tri[t + 1], c = tri[t + 2];
      const uint32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
      finer.insert(finer.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    tri = std::move(finer);
  }

  for (const glm::dvec3 &q : p)
    m.positions.push_back(centre + radius * glm::normalize(q));

  // Outward, whatever order the table above happens to use: a sphere's
  // outward normal points away from its centre.
  for (size_t t = 0; t < tri.size(); t += 3)
  {
    const glm::dvec3 &a = m.positions[tri[t]], &b = m.positions[tri[t + 1]],
                     &c = m.positions[tri[t + 2]];
    if (glm::dot(glm::cross(b - a, c - a), a - centre) < 0.0)
      std::swap(tri[t + 1], tri[t + 2]);
  }
  m.triangles = std::move(tri);
  return m;
}

// --- what the viewer reads ---

class DemoMeshes : public mv::MeshSource
{
public:
  std::vector<Mesh> meshes;

  size_t mesh_count() const override
  {
    return meshes.size();
  }
  mv::MeshView mesh(size_t i) const override
  {
    const Mesh &m = meshes[i];
    mv::MeshView v;
    v.id = m.id;
    v.name = m.name;
    v.revision = m.revision;
    v.positions = m.positions;
    v.triangles = m.triangles;
    v.tets = m.tets;
    v.tet_labels = m.labels;
    v.labels_revision = m.labels_revision;
    static const std::string_view kNames[] = {"outside the sphere",
                                              "inside the sphere"};
    v.label_names = {kNames, 2};
    // Both meshes are closed and consistently wound, which is the default.
    return v;
  }
};

class DemoMarks : public mv::OverlaySource
{
public:
  uint32_t mesh_id = 0;
  uint64_t revision_ = 1; // bumped whenever `indices` changes
  std::vector<uint32_t> indices;

  uint64_t revision() const override
  {
    return revision_;
  }
  size_t channel_count() const override
  {
    return 1;
  }
  mv::OverlayChannel channel(size_t) const override
  {
    return {"box vertices above h", mv::OverlayPrim::Points, mesh_id, indices};
  }
};

// --- the host's own window ---

class DemoPanel : public mv::Panel
{
public:
  DemoPanel(DemoMeshes &meshes, DemoMarks &marks)
      : meshes_(meshes), marks_(marks)
  {
    rest_ = meshes_.meshes[1].positions;
    mark();
  }

  void ui(mv::Viewer &viewer) override
  {
    for (const Mesh &m : meshes_.meshes)
    {
      ImGui::PushID(int(m.id));
      mv::mesh_chip(viewer.scene().layer(m.id));
      ImGui::Text("%s: %zu vertices, %zu tets",
                  m.name.c_str(),
                  m.positions.size(),
                  m.tets.size() / 4);
      ImGui::PopID();
    }
    ImGui::Separator();

    // Only parameters change here; the sphere itself moves in update().
    ImGui::Checkbox("wobble", &wobble_);
    ImGui::SameLine();
    ImGui::TextDisabled("(space)");
    ImGui::SliderFloat("amplitude", &amplitude_, 0.0f, 0.3f);

    // A change to what is drawn: deferred, like every such change, so it
    // happens between frames rather than while this window describes them.
    if (ImGui::SliderFloat("h", &height_, 0.0f, 1.0f))
      viewer.defer([this] { mark(); });
    ImGui::TextDisabled("%zu vertices marked", marks_.indices.size());
  }

  void update(mv::Viewer &) override
  {
    if (!wobble_)
      return;
    Mesh &sphere = meshes_.meshes[1];
    const double t = glfwGetTime();
    const glm::dvec3 centre(0.5);
    for (size_t v = 0; v < rest_.size(); ++v)
    {
      const glm::dvec3 d = rest_[v] - centre;
      const double s = 1.0 + double(amplitude_) *
                                 std::sin(8.0 * d.z / glm::length(d) + 3.0 * t);
      sphere.positions[v] = centre + d * s;
    }
    ++sphere.revision; // all the viewer needs to know
  }

  bool animating() const override
  {
    return wobble_;
  }

  bool on_key(mv::Viewer &, int key, int) override
  {
    if (key != GLFW_KEY_SPACE)
      return false;
    wobble_ = !wobble_;
    return true;
  }

private:
  void mark()
  {
    const Mesh &box = meshes_.meshes[0];
    marks_.mesh_id = box.id;
    marks_.indices.clear();
    for (size_t v = 0; v < box.positions.size(); ++v)
      if (box.positions[v].z > double(height_))
        marks_.indices.push_back(uint32_t(v));
    ++marks_.revision_;
  }

  DemoMeshes &meshes_;
  DemoMarks &marks_;
  std::vector<glm::dvec3> rest_;
  bool wobble_ = false;
  float amplitude_ = 0.1f;
  float height_ = 0.8f;
};

} // namespace

int main()
{
  // The host's data first, so it outlives the viewer that reads it.
  DemoMeshes meshes;
  meshes.meshes.push_back(make_box(1, 8));
  meshes.meshes.push_back(make_sphere(2, glm::dvec3(0.5), 0.35, 3));

  // Which of the box's tets the sphere (at rest) contains, by centroid: what a
  // host's own classification would hand over.
  Mesh &box = meshes.meshes[0];
  for (size_t t = 0; t < box.tets.size(); t += 4)
  {
    glm::dvec3 c(0.0);
    for (size_t k = 0; k < 4; ++k)
      c += box.positions[box.tets[t + k]] * 0.25;
    box.labels.push_back(uint8_t(glm::length(c - glm::dvec3(0.5)) < 0.35));
  }
  DemoMarks marks;
  DemoPanel panel(meshes, marks);

  mv::Viewer viewer;
  std::string err;
  if (!viewer.init({"mesh-viewer-demo"}, err))
  {
    std::fprintf(stderr, "mesh-viewer-demo: %s\n", err.c_str());
    return 1;
  }
  viewer.set_mesh_source(&meshes);
  viewer.set_overlay_source(&marks);
  viewer.add_window("Demo", panel, {12.0f, 12.0f, 400.0f, 200.0f});
  return viewer.run();
}
