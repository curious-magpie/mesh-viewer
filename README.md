# mesh-viewer

A small OpenGL 3.3 / Dear ImGui viewer for tetrahedral and surface meshes, made to be the
front end of mesh-processing research code. The research code (the *host*) keeps its meshes
and its algorithms. The viewer draws them and gives the host windows of its own to put
controls in. Neither side knows the other's types: the host describes its meshes through two
small interfaces, and the host's windows are `Panel`s it registers.

mesh-dev is the first host. It pulls this repository in as a submodule at `ext/mesh-viewer`.

What it draws:
- every mesh as a layer, with colour, alpha and a composite order you choose (nested
  translucent shells have no correct automatic order)
- a cutting plane with a gizmo: slide it, ctrl-drag to rotate it
- the tets the plane passes through, drawn whole and with the layer's transparency, on
  by default. If the host labels its tets (inside/outside, materials, …), one **by label**
  checkbox colours them by label
- a stencil-filled cap on closed meshes, off by default
- three surface modes: shaded, shaded with edges, x-ray
- overlay channels: named lists of vertex indices a host wants marked, as points, lines or
  triangles

## Building

```
cmake -S . -B build          # GLFW and GLM from the system; glad and ImGui are in ext/
cmake --build build -j
build/mesh-viewer-demo
```

`MESH_VIEWER_BUILD_EXAMPLES` (on when this is the top-level project) builds the demo. As a
subproject, `add_subdirectory(path/to/mesh-viewer)` then link `mesh_viewer::mesh_viewer`.
The link is public for glad, ImGui, GLFW, GL and glm, because a host's panels are ImGui code
and its sources hand over glm vectors. The viewer is C++17.

Mouse: drag to orbit, shift-drag or middle-drag to pan, wheel to zoom. With the cutting plane
on, drag its handle to slide it and ctrl-drag to rotate it. Keys: P plane, W surface mode,
F frame the scene, Esc quit. Other keys go to the host's panels.

## Writing a host

`examples/demo.cpp` is a complete host in about 300 lines, and the thing to copy. The parts:

**1. Describe your meshes** (`src/mesh_viewer/mesh_source.h`). Once a frame the viewer asks
a `MeshSource` for each mesh as a `MeshView`:

```cpp
struct MeshView {
  uint32_t id;                    // stable, never reused; 0 = none
  std::string_view name;
  uint64_t revision;              // change it whenever the positions change
  Span<glm::dvec3> positions;
  Span<uint32_t> triangles;       // what is drawn: a volume's hull, or the surface
  Span<uint32_t> tets;            // 4 per tet, positive order; empty for a surface
  Span<uint8_t> tet_labels;       // optional: a group per tet, for the cut's colours
  uint64_t labels_revision;       // change it whenever the labels change
  Span<std::string_view> label_names;  // optional: "inside", "outside", ...
  bool closed, oriented;          // gate the cap and backface culling
  std::shared_ptr<const void> keep_alive;
};
```

Nothing is pushed and nothing is notified. The viewer compares each revision with the one it
uploaded, so moving a mesh means writing its positions and bumping its revision. The
connectivity is uploaded once, the first time a mesh is seen. Labels have their own
revision, because a classification can change them without moving a vertex. The spans must stay valid until
the next frame's sync. `keep_alive` is held by the layer if you want the viewer to share
ownership.

**2. Optionally, mark things on them.** An `OverlaySource` hands over named channels of
indices into one mesh's vertices, plus one revision for the whole set. The Inspect window
lists them with a colour, a checkbox (off until you tick it) and "in front". Appearance is the viewer's; a channel
says only what was found.

**3. Add your windows** (`src/mesh_viewer/panel.h`):

```cpp
class FittingPanel : public mesh_viewer::Panel {
  void ui(mesh_viewer::Viewer &v) override;     // widgets; mesh changes via v.defer(...)
  void update(mesh_viewer::Viewer &v) override; // every frame, after deferred work
  bool animating() const override;              // true = don't wait for input
  bool on_key(mesh_viewer::Viewer &v, int key, int mods) override;
};

viewer.add_window("Fitting", fitting_panel, {12, 12, 400, 520});
```

The viewer draws the window frame, puts it in the Windows menu, and remembers where it was
left (in `imgui.ini`, keyed by title). The placement is only the first-use position; a
negative x anchors the window to the right edge. A panel reaches the scene, camera, cutting
plane and view settings through the `Viewer&` it is handed. `mesh_viewer::mesh_chip(layer)`
(`widgets.h`) draws a mesh's colour swatch the way the built-in windows do.

**4. Run it:**

```cpp
mesh_viewer::Viewer viewer;
viewer.init({"my-program"}, err);
viewer.set_mesh_source(&meshes);
viewer.set_overlay_source(&overlay);      // optional
viewer.set_drop_handler([&](const auto &paths) { /* load into your source */ });
viewer.add_window("Fitting", fitting_panel);
return viewer.run();
```

Declare your data before the viewer, so it outlives the viewer's GPU copies.

### The frame, in order

```
wait for input (or poll, while a panel is animating() or a drag is on)
ImGui frame: the Windows menu, then each open window's Panel::ui
run what the panels deferred
Panel::update on every panel, open or not
Scene::sync(mesh source), then the overlay's
Renderer::draw, then ImGui's draw data, then swap
```

Changes to the meshes are deferred to between frames. Done inside `ui`, they would change the
meshes halfway through drawing the windows that describe them, and a long computation would
run inside an open ImGui window.

## Layout

```
src/mesh_viewer/
  viewer.h/.cpp        the window, the frame loop, the Windows menu, deferral
  input.cpp            GLFW callbacks; who gets a drag
  builtin_windows.*    Inspect and View, which are ordinary Panels
  panel.h              the extension point
  mesh_source.h        MeshView, MeshSource, OverlayChannel, OverlaySource
  span.h, widgets.*    Span<T>; mesh_chip
  scene/               Scene (one Layer per mesh), Layer, GpuMesh, ElementSlice,
                       DebugOverlay, Camera, CutPlane, PlaneController
  render/              Renderer (the draw passes), shaders, GL state
examples/demo.cpp      a whole host in one file
docs/                  opengl-primer.md (how a GPU works), code-walkthrough.md and
                       explanationUI.md (how this one is put together)
```

`render/` depends only on `scene/`, and `scene/` only on `mesh_source.h`. The renderer can be
read without knowing where a mesh came from.
