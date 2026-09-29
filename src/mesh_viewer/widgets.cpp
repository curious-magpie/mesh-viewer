#include "mesh_viewer/widgets.h"

#include "imgui.h"

namespace mesh_viewer
{

void mesh_chip(const Layer *l)
{
  const ImVec4 c = l ? ImVec4(l->color.r, l->color.g, l->color.b, 1.0f)
                     : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
  ImGui::ColorButton("##chip",
                     c,
                     ImGuiColorEditFlags_NoTooltip |
                         ImGuiColorEditFlags_NoDragDrop,
                     ImVec2(12, 12));
  ImGui::SameLine();
}

} // namespace mesh_viewer
