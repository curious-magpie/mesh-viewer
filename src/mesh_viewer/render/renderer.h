// Draws the scene: the layers, the cutting plane's cross-section fill, and the
// plane's drag handle. See renderer.cpp for why the fill takes three passes.
#pragma once

#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "mesh_viewer/render/dynamic_quads.h"
#include "mesh_viewer/render/shader.h"
#include "mesh_viewer/scene/camera.h"
#include "mesh_viewer/scene/cut_plane.h"
#include "mesh_viewer/scene/scene.h"

namespace mesh_viewer
{

// How a layer's own surface is drawn. `W` cycles these in order.
//
// Xray is not Edges without the fill: it is the *fill* turned into lines, so
// nothing writes depth but the edges themselves and the whole domain goes
// see-through. That is what makes debug geometry buried inside a mesh visible
// without giving every channel "in front" -- which is the job Edges does not
// do, because its fill occludes exactly as a solid should.
enum class SurfaceMode
{
  Shaded, // fill only
  Edges,  // fill, with the triangle edges drawn on top of it
  Xray,   // the triangles as lines, and nothing else
};

inline SurfaceMode next_mode(SurfaceMode m)
{
  switch (m)
  {
  case SurfaceMode::Shaded:
    return SurfaceMode::Edges;
  case SurfaceMode::Edges:
    return SurfaceMode::Xray;
  case SurfaceMode::Xray:
    break;
  }
  return SurfaceMode::Shaded;
}

class Renderer
{
public:
  bool init(std::string &err);

  // Frees the shaders and buffers. Must be called while the GL context is still
  // current; the App destructor sequences this before tearing GLFW down.
  void release();

  // Takes the scene by non-const reference because drawing it refreshes the
  // GPU-side caches derived from the plane -- the element slices, like the
  // plane's own quads. Keeping that inside draw() is what makes it impossible
  // to render a stale slice.
  void draw(Scene &scene,
            const Camera &cam,
            const CutPlane &plane,
            const glm::vec3 &background,
            SurfaceMode mode);

private:
  // The passes that make up one layer; see draw_layer.
  void mark_cross_section(const Layer &l, const CutPlane &plane);
  void draw_surface(const Layer &l, const CutPlane &plane, SurfaceMode mode);
  void fill_cross_section(const Layer &l,
                          const CutPlane &plane,
                          const glm::mat4 &view,
                          bool owns_channel);

  // The tets the plane passes through, drawn as tets.
  void
  draw_elements(const Layer &l, const CutPlane &plane, const glm::mat4 &view);

  void draw_layer(const Layer &l,
                  const CutPlane &plane,
                  const glm::mat4 &view,
                  SurfaceMode mode,
                  bool owns_channel);

  // One group of the debug channels: the ordinary ones, or (`in_front`) the
  // ones drawn over everything.
  void
  draw_debug_overlay(const Scene &scene, const CutPlane &plane, bool in_front);
  void draw_gizmo(const CutPlane &plane, const glm::mat4 &view);

  // Rebuilds the quads that live in the cutting plane. They move whenever the
  // plane does, so they are re-uploaded once per frame.
  void update_plane_geometry(const CutPlane &plane, const SceneBounds &bounds);

  ShaderProgram surface_; // the meshes themselves
  ShaderProgram stencil_; // depth/colour-less pass that marks the cross-section
  ShaderProgram cap_;     // cross-section fill and gizmo
  ShaderProgram overlay_; // every debug channel, whatever it is made of
  DynamicQuads plane_quads_;
};

} // namespace mesh_viewer
