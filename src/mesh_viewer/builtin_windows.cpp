#include "mesh_viewer/builtin_windows.h"

#include <vector>

#include "imgui.h"

#include "mesh_viewer/viewer.h"
#include "mesh_viewer/widgets.h"

namespace mesh_viewer
{

// ---------------------------------------------------------------------------
// Inspect: looking into a result
// ---------------------------------------------------------------------------

void InspectWindow::ui(Viewer &viewer)
{
  Scene &scene = viewer.scene();
  CutPlane &plane = viewer.plane();
  SurfaceMode &surface_mode = viewer.view().surface_mode;

  size_t cut = 0;
  for (const Layer &l : scene.layers())
    cut += l.slice.tets();
  if (cut)
    ImGui::TextDisabled("%zu tets standing in the cut", cut);
  else
    ImGui::TextDisabled("P plane   W surface   F frame the scene");
  ImGui::Separator();

  const SceneBounds &bounds = scene.bounds();

  ImGui::Checkbox("cutting plane", &plane.enabled);
  ImGui::SameLine();
  ImGui::Checkbox("gizmo", &plane.show_gizmo);

  // Kept in the same order `W` cycles them, so the key and the list agree.
  int mode = int(surface_mode);
  if (ImGui::Combo("surface", &mode, "shaded\0shaded + edges\0x-ray\0"))
    surface_mode = SurfaceMode(mode);

  if (ImGui::DragFloat3("normal", &plane.n.x, 0.01f, -1.0f, 1.0f))
    if (glm::length(plane.n) > 1e-6f)
      plane.n = glm::normalize(plane.n);

  // Offset is shown as a distance from the scene centre along the normal, which
  // stays meaningful when the normal is rotated -- unlike the raw plane
  // constant, which would jump around as the normal turns.
  float offset = -plane.signed_distance(bounds.center);
  if (ImGui::SliderFloat(
          "offset", &offset, -bounds.radius, bounds.radius, "%.4f"))
    plane.w = -glm::dot(plane.n, bounds.center) - offset;

  if (ImGui::SmallButton("X"))
    plane.look_along({1, 0, 0}, bounds.center);
  ImGui::SameLine();
  if (ImGui::SmallButton("Y"))
    plane.look_along({0, 1, 0}, bounds.center);
  ImGui::SameLine();
  if (ImGui::SmallButton("Z"))
    plane.look_along({0, 0, 1}, bounds.center);
  ImGui::SameLine();
  if (ImGui::SmallButton("flip"))
    plane.flip();
  ImGui::SameLine();
  if (ImGui::SmallButton("centre"))
    plane.set_through(bounds.center);

  ImGui::TextDisabled("drag gizmo: slide   ctrl+drag: rotate");
  ImGui::Separator();

  // What the cut shows of each mesh. These belong here rather than next to
  // colour and alpha: the cap is what makes a cut read as solid, and the
  // elements are what make it read as a mesh.
  for (size_t slot = 0; slot < scene.order().size(); ++slot)
  {
    Layer &l = scene.at_slot(slot);
    ImGui::PushID(int(l.mesh_id));
    mesh_chip(&l);
    ImGui::Text("%s", l.name.c_str());
    ImGui::SameLine();

    ImGui::BeginDisabled(!l.closed());
    ImGui::Checkbox("cap", &l.cap);
    ImGui::SameLine();
    ImGui::BeginDisabled(!l.cap);
    ImGui::Checkbox("solid", &l.solid_cap);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!l.has_volume());
    ImGui::Checkbox("elements", &l.show_elements);
    ImGui::EndDisabled();

    if (!l.closed())
      ImGui::TextDisabled("   not closed, so no cap");
    else if (l.has_volume() && l.show_elements)
      ImGui::TextDisabled(
          "   %zu of %zu tets cut", l.slice.tets(), l.tet_count());

    // The groups the source labelled the tets with, each with a colour and a
    // checkbox of its own: the way to look at the inside of a labelling on its
    // own is to hide the outside. The counts are of the tets in the cut.
    if (l.has_volume() && l.show_elements)
      for (size_t g = 0; g < l.labels.size(); ++g)
      {
        LabelStyle &style = l.labels[g];
        ImGui::PushID(int(g));
        ImGui::TextDisabled("  ");
        ImGui::SameLine();
        ImGui::ColorEdit3("##label colour",
                          &style.color.x,
                          ImGuiColorEditFlags_NoInputs |
                              ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::Checkbox(style.name.c_str(), &style.visible);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu",
                            g < l.slice.groups() ? l.slice.tets(g) : size_t(0));
        ImGui::PopID();
      }

    ImGui::PopID();
  }

  // What the stages asked to have drawn. Nothing is registered here: a channel
  // written anywhere in the pipeline shows up as a row, and every control on it
  // is the viewer's, so none of them re-upload anything.
  //
  // A channel draws whether or not its mesh is shown, so hiding a layer is how
  // you look at what was found inside it.
  std::vector<DebugOverlay::Item> &debug = scene.debug_overlay().items();
  if (!debug.empty())
  {
    ImGui::Separator();
    for (DebugOverlay::Item &item : debug)
    {
      ImGui::PushID(item.name.c_str());
      ImGui::ColorEdit3("##colour",
                        &item.color.x,
                        ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel);
      ImGui::SameLine();
      ImGui::BeginDisabled(item.draw.empty());
      ImGui::Checkbox(item.name.c_str(), &item.enabled);
      ImGui::SameLine();
      ImGui::Checkbox("in front", &item.in_front);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("draw over everything, rather than being occluded");
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextDisabled("%d", int(item.draw.index_count()));
      ImGui::PopID();
    }
  }
}

// ---------------------------------------------------------------------------
// View: what it looks like
// ---------------------------------------------------------------------------

void ViewWindow::ui(Viewer &viewer)
{
  Scene &scene = viewer.scene();
  glm::vec3 &background = viewer.view().background;

  ImGui::Text("%.1f fps   %zu tris",
              double(ImGui::GetIO().Framerate),
              scene.visible_triangles());
  ImGui::Separator();

  if (ImGui::SmallButton("all on"))
    for (Layer &l : scene.layers())
      l.visible = true;
  ImGui::SameLine();
  if (ImGui::SmallButton("all off"))
    for (Layer &l : scene.layers())
      l.visible = false;
  ImGui::SameLine();
  if (ImGui::SmallButton("opaque"))
    for (Layer &l : scene.layers())
      l.alpha = 1.0f;

  // Walk the composite order, not the layer list, so the panel reads back to
  // front -- the same order the renderer blends in.
  for (size_t slot = 0; slot < scene.order().size(); ++slot)
  {
    Layer &l = scene.at_slot(slot);
    ImGui::PushID(int(l.mesh_id));

    ImGui::Checkbox("##visible", &l.visible);
    ImGui::SameLine();
    ImGui::ColorEdit3("##color",
                      &l.color.x,
                      ImGuiColorEditFlags_NoInputs |
                          ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::Text("%s", l.name.c_str());

    ImGui::SliderFloat("alpha", &l.alpha, 0.0f, 1.0f, "%.2f");

    // Reordering *is* the transparency control: nested shells have no correct
    // automatic back-to-front order, so the user picks one.
    ImGui::BeginDisabled(slot == 0);
    if (ImGui::SmallButton("up"))
      scene.move_earlier(slot);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(slot + 1 >= scene.order().size());
    if (ImGui::SmallButton("down"))
      scene.move_later(slot);
    ImGui::EndDisabled();

    ImGui::PopID();
    ImGui::Separator();
  }

  ImGui::ColorEdit3("background", &background.x, ImGuiColorEditFlags_NoInputs);
  ImGui::SameLine();
  if (ImGui::SmallButton("reset camera"))
    viewer.frame_all();

  // Coordinates on screen are scene-relative; showing the origin lets you
  // recover the file's own numbers by adding it back.
  const glm::dvec3 o = scene.origin();
  ImGui::TextDisabled("origin  %.6g %.6g %.6g", o.x, o.y, o.z);
  ImGui::TextDisabled("radius  %.4g", double(scene.bounds().radius));
}

} // namespace mesh_viewer
