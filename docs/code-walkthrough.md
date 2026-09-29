# A walk through the code

> **Read this first: what has changed since this was written.** This walkthrough, and
> `explanationUI.md`, were written while the viewer was part of mesh-dev. The object
> graph, the frame loop, the draw passes and the GL lifetimes still hold. What has moved
> is where the meshes come from and who owns the window:
>
> - **`App` is split in two.** Its window, frame loop, input, deferral and the Inspect and
>   View windows are `mesh_viewer::Viewer` (`src/mesh_viewer/viewer.*`, `input.cpp`,
>   `builtin_windows.cpp`). Its meshes, pipeline, physics and the Model window stayed in
>   mesh-dev, which is now a *host*: it registers a `Panel` with `Viewer::add_window`.
>   Read `App::` below as `Viewer::`, and `app/ui.cpp` as `builtin_windows.cpp`.
> - **`Document` is a `MeshSource` here.** The viewer asks it once a frame for each mesh
>   as a `MeshView`: positions, the triangles to draw, the tets (if any), a revision,
>   `closed` and `oriented` (`src/mesh_viewer/mesh_source.h`). `Scene::sync(source)`
>   replaces `Scene::sync(document)`, and a `Layer` holds those spans and the view's
>   `keep_alive` where it held `shared_ptr<const TetMesh>`. `TetMesh`, `SurfaceMesh` and
>   `MeshEntry` below are mesh-dev's types, and appear here as what mesh-dev's source hands
>   over.
> - **`DebugDraw` is an `OverlaySource` here.** The viewer reads named index channels
>   through it; mesh-dev's `DebugDraw` is one implementation.
> - **Loading a file is the host's.** The viewer has no readers. §5's load path now ends at
>   the host, and a dropped file goes to the host's `set_drop_handler`.
> - Paths: `scene/` and `render/` are `src/mesh_viewer/scene/` and
>   `src/mesh_viewer/render/`, and everything is in `namespace mesh_viewer`.
>
> `README.md` has the host's side of the API; `examples/demo.cpp` is a whole host in one file.

`opengl-primer.md` explains how a GPU works. This document explains how _this program_
works: which object owns which bytes, what a mesh looks like at each stage, what happens
on one frame, and where to cut in when you want the viewer to show something about your
own mesh-processing code.

It is written for the case you actually have: you have an algorithm that builds or
modifies a mesh, and you want to look at intermediate results instead of guessing.

**Contents** 12. [Invariants worth preserving](#12-invariants-worth-preserving)

1. [The object graph](#1-the-object-graph)
2. [One mesh, three representations](#2-one-mesh-three-representations)
3. [Index conventions — the part to get right](#3-index-conventions--the-part-to-get-right)
4. [Coordinate spaces in this program](#4-coordinate-spaces-in-this-program)
5. [The load path, call by call](#5-the-load-path-call-by-call)
6. [What the GPU holds when nothing is happening](#6-what-the-gpu-holds-when-nothing-is-happening)
7. [One frame, start to finish](#7-one-frame-start-to-finish)
8. [The cutting plane: one vec4, five consumers](#8-the-cutting-plane-one-vec4-five-consumers)
9. [Lifetimes, and the one rule you must not break](#9-lifetimes-and-the-one-rule-you-must-not-break)
10. [Hooking your own algorithms in](#10-hooking-your-own-algorithms-in)
11. [Symptom → cause](#11-symptom--cause)

---

## 1. The object graph

There is no framework here and no dependency injection. One `App` owns everything, by
value, and the ownership tree _is_ the architecture:

```
App                                   app/app.h
├── GLFWwindow*        the window and the GL context
├── Scene              scene/scene.h          ── all loaded geometry
│   └── vector<Layer>  scene/layer.h          ── one per file
│       ├── GpuMesh       VAO + VBO + IBO     (GPU)
│       ├── verts, tets   the volume          (CPU, tet meshes only)
│       └── ElementSlice  VAO + dynamic IBO   (GPU)
├── Camera             scene/camera.h         ── yaw, pitch, distance, target
├── CutPlane           scene/cut_plane.h      ── a unit normal and a constant
├── PlaneController    scene/plane_controller.h ── drag state, nothing else
└── Renderer           render/renderer.h
    ├── 3 ShaderProgram   surface / stencil / cap   (GPU)
    └── DynamicQuads      16 vertices, rewritten every frame (GPU)
```

Two of these are pure mathematics with no state to speak of — `Camera` and `CutPlane` are
header-only structs you can copy, unit-test or print at will. `PlaneController` holds only
what a drag captured at its start. Everything that owns GPU memory is move-only and frees
itself in its destructor.

The dependency direction is strictly one way: `io/` knows nothing about OpenGL, `scene/`
knows nothing about windows or ImGui, `render/` knows nothing about file formats. You can
read and change any one of them without holding the other three in your head.

---

## 2. One mesh, three representations

A mesh exists in three forms, and the difference between them is the single most useful
thing to know when you want to plug your own code in.

```
   file on disk                MeshData                 Layer
   ────────────                ────────                 ─────
   $Nodes / $Elements   ──►    pos   vector<vec3>  ──►  verts  vector<vec3>   (CPU, tets only)
   text, node tags             tris  vector<u32>        tets   vector<u32>    (CPU, tets only)
   arbitrary numbering         tets  vector<u32>        GpuMesh:
                                                          VBO = every vertex, recentred
   lives: during the read      lives: during the load    IBO = boundary triangles only
   dies:  end of App::load     dies:  end of App::load   lives: until the layer is removed
```

### `MeshData` — `io/mesh_data.h`

What every reader produces, and the only thing `io/` and `scene/` agree on:

```cpp
std::vector<glm::vec3> pos;   // dense, 0..n-1
std::vector<uint32_t>  tris;  // 3 indices per triangle
std::vector<uint32_t>  tets;  // 4 indices per tetrahedron; empty for a surface
```

That is the whole format. If your algorithm can produce those three vectors, it can be
displayed — see [recipe 10.1](#101-show-a-mesh-your-own-code-produced).

**It is a temporary.** `App::load` (`app/app.cpp`) creates one `MeshData` per file on
the stack, hands it to `Scene::add`, prints a line, and lets it die at the end of the loop
iteration. Nothing keeps it.

### `Layer` — `scene/layer.h`

One file, turned into something drawable, plus everything the panel edits:

| group              | fields                                                           | notes                                                                                                                             |
| ------------------ | ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| identity           | `name`                                                           | the file stem                                                                                                                     |
| user controls      | `visible`, `alpha`, `color`, `cap`, `solid_cap`, `show_elements` | written directly by ImGui widgets                                                                                                 |
| discovered at load | `closed`, `oriented`, `bbmin/bbmax`, `tri_count`                 | `closed` gates capping, `oriented` gates culling; `bbmin/bbmax` also decide per frame whether the plane touches this layer at all |
| CPU geometry       | `verts`, `tets`                                                  | **tet meshes only**; empty for a surface                                                                                          |
| GPU geometry       | `gpu` (`GpuMesh`), `slice` (`ElementSlice`)                      |                                                                                                                                   |

`closed` and `oriented` are not cosmetic. `closed` comes from `SurfaceMesh::build_adjacency`
(`scene/layer.cpp`) and gates the stencil cap, which is meaningless on a surface with
holes. `oriented` gates backface culling, which punches holes in a mesh whose triangles
disagree about which side is out. A tet mesh gets both for free: the boundary of a tet
complex is closed by construction, and `TetMesh::orient` puts every tet's corners in
positive order at build time so that `collect_boundary` can wind each hull face outward
from the shared `TetMesh::kFaces` table without consulting a volume again.

`tet_count()` and `has_volume()` are methods over `tets` rather than stored fields, so the
count and the volume it describes cannot disagree.

### Why the CPU copy exists at all

Originally it did not. A volume mesh is _drawn_ as its boundary hull — for a 2.5 M-element
mesh that is 105 k triangles instead of 10 M — so `build_layer` extracted the hull,
uploaded it, and dropped the tets on the floor.

Showing the elements the plane cuts needs the volume back, so `build_layer`
(`scene/layer.cpp`) now keeps `verts` and `tets` for tet meshes — moved out of the
`MeshData` rather than copied, since that array is 40 MB on a 2.5 M-element file. The cost
is roughly the file's own size in RAM: 16 bytes per tet plus 12 bytes per vertex. A surface
mesh keeps nothing.

This copy is also the natural place for _your_ data to live if you extend the viewer —
it is already indexed identically to the GPU buffer (next section).

---

## 3. Index conventions — the part to get right

**Within `MeshData`, every index is an index into `pos`.** Nothing else. Not a file tag,
not a 1-based OBJ index, not an element id.

The readers are what make that true:

- **`.msh`** node tags are arbitrary positive integers, routinely sparse — Gmsh renumbers,
  extracting a physical group leaves holes, merging files leaves gaps. `NodeMap`
  (`io/msh_reader.cpp`) maps them to `[0, n)`, choosing a flat array when tags are dense
  and an open-addressing table when they are not. An element referencing an undeclared node
  is dropped rather than guessed at.
- **`.obj`** indices are 1-based, may be negative (relative to the end), and come in four
  notations (`v`, `v/vt`, `v//vn`, `v/vt/vn`). All are resolved to the same dense range;
  out-of-range and zero indices are dropped with a count reported on stderr.

**The GPU vertex buffer holds `pos` as-is** — recentred, but neither compacted nor
reordered. `build_layer` uploads every vertex, including ones no element references,
precisely so that this holds:

> An index that is valid in `MeshData.pos` is valid in the layer's `verts`, and is valid as
> an index into the layer's GPU vertex buffer.

That identity is what makes the cheapest extension hook possible: any subset of vertices,
triangles or tets you can name by index can be drawn _without uploading a single vertex_ —
you upload only indices. `ElementSlice` (`scene/draw_objects/element_slice.cpp`) is exactly that, and is
90 lines including comments.

The one place indices were re-derived used to be the hull extraction here, where a face was
packed into a `uint64` as three indices at 21 bits each -- with a sort fallback above
2²¹ ≈ 2.1 M vertices, where that packing cannot hold an index. **That has moved and the
limit is gone.** `TetMesh::build_adjacency` (`mesh/tet_mesh.cpp`) keys its open-addressing
table on a `FaceKey` -- the three indices sorted, as three full `uint32_t` -- so there is no
packing to overflow, and the hull falls out of the adjacency rather than being extracted
from it. If you write similar packing code elsewhere, the 21-bit limit is still the one to
mind.

---

## 4. Coordinate spaces in this program

The primer covers the standard pipeline spaces. This program adds one of its own, and it
will bite you the first time you print a coordinate:

```
  file space     what your .msh / .obj / algorithm says
      │
      │  minus Scene::origin()     ← chosen once, from the first file loaded
      ▼
  scene space    what Layer::verts, the VBO, the bounds, the camera target
      │          and the cutting plane all use.  ALSO world space:
      │          there is no model matrix in this renderer.
      │  uView
      ▼
  view space     camera at the origin, looking down −Z
      │  uProj
      ▼
  clip space     gl_Position
```

`Scene::origin()` is `mesh_center` (`scene/layer.cpp`) of the **first** mesh added, and
every later file reuses it, so files sharing a coordinate system stay aligned. It exists for
float32 precision: the shaders derive normals by differentiating interpolated position, and
differencing large engineering coordinates cancels away the mantissa and speckles the
shading (primer §12).

Practical consequences:

- To go back to your file's numbers, add `scene_.origin()` — the panel prints it under
  **Scene** for exactly this reason.
- A ray from `Camera::screen_ray` is in scene space, so you can intersect it directly
  against `Layer::verts` without any conversion. That is how the plane handle hit-test
  works (`scene/plane_controller.h`).
- The cutting plane's `w` is in scene space too. The panel deliberately shows you a
  _distance from the scene centre along the normal_ instead (`app/ui.cpp`), because the raw
  constant jumps around whenever the normal turns.

---

## 5. The load path, call by call

`main` builds an `App`, calls `init`, then `load` once per command-line argument, then
`run`. Files dropped on the window take the same path (`app/input.cpp`).

```
App::load(path)                                app/app.cpp
 ├─ directory? collect every supported file, sorted (deterministic layer order)
 └─ for each file:
     ├─ mesh_io::read(file, mesh, err)         io/mesh_reader.cpp
     │   └─ extension_of() → reader_for() → MshReader/ObjReader::read()
     │       ├─ scan::TextFile::load           one fread of the whole file
     │       └─ parse into MeshData            pos / tris / tets
     ├─ Scene::add(std::move(mesh), name, err) scene/scene.cpp   → Layer*
     │   ├─ first file only: origin_ = mesh_center(mesh)
     │   ├─ build_layer(std::move(mesh), …)    scene/layer.cpp
     │   │   ├─ tets present?  TetMesh::build     → orient, adjacency, vertex map,
     │   │   │                                      boundary_faces() = the hull
     │   │   ├─ else           SurfaceMesh::build → adjacency, vertex map,
     │   │   │                                      closed / oriented
     │   │   ├─ recentre every vertex, measure the bounding box of what is drawn
     │   │   ├─ GpuMesh::upload(verts, indices)     ← the only GL call in this path
     │   │   └─ tets present?  move verts + tets onto the Layer, and point its
     │   │                     ElementSlice at the vertex buffer just uploaded
     │   ├─ layers_.push_back, order_.push_back
     │   └─ recompute_bounds()                 camera framing, plane slider range
     └─ assign a palette colour, print the timing line
```

`add` hands back the layer it appended, so the caller never has to guess where it landed.
Failures are per file: a bad file prints to stderr and is skipped, so one broken mesh in a
directory does not stop the rest. Everything up to `GpuMesh::upload` is plain C++ you can
call from a test without a window.

Note what `Scene::add` does _not_ do: it never touches the camera. `App::refit`
(`app/app.cpp`) does that, on `F`, on a drop, and once before the first frame.

---

## 6. What the GPU holds when nothing is happening

Per layer:

| object             | contents                                                                      | size                                           |
| ------------------ | ----------------------------------------------------------------------------- | ---------------------------------------------- |
| `GpuMesh` VBO      | every vertex of the file, recentred, `vec3`                                   | 12 B × vertices                                |
| `GpuMesh` IBO      | the drawn triangles — hull for a tet mesh, the file's triangles for a surface | 12 B × triangles                               |
| `ElementSlice` IBO | 4 faces of each tet the plane currently passes through                        | 48 B × cut tets, only while **elements** is on |

Shared, in `Renderer`:

| object         | contents                                                                  |
| -------------- | ------------------------------------------------------------------------- |
| `surface_`     | the lit program; the only one that writes `gl_ClipDistance`               |
| `stencil_`     | position and clip distance only — it runs over the whole mesh every frame |
| `cap_`         | the cross-section fill, the gizmo, and the element wireframe              |
| `overlay_`     | the debug channels, and the edges a layer draws in **shaded + edges**    |
| `plane_quads_` | 16 vertices: 6 cap quad, 6 gizmo quad, 4 gizmo outline                    |

There is no texture, no framebuffer object, no uniform buffer, no normal buffer and no
per-vertex colour. Attribute **0 is position, and it is the only attribute** in the entire
program. That is worth knowing before you add a second one.

`ElementSlice` deserves one more sentence, because it is the template for most extensions:
its VAO points attribute 0 at _the layer's own VBO_ and binds its own index buffer
(`ElementSlice::attach`, `scene/draw_objects/element_slice.cpp`). Both bindings are VAO state, so it
happens once at load and per-frame updates touch only the indices. Different geometry, zero
duplicated vertices.

The layout itself is stated once, in `scene/vertex_layout.h`, and all three buffer owners
call it — so adding a second attribute is one edit rather than a search.

---

## 7. One frame, start to finish

`App::run` (`app/app.cpp`) is a plain loop. In order:

```cpp
glfwPollEvents();                     // callbacks fire here, mutating cam_ / plane_
ImGui_ImplOpenGL3_NewFrame();
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();
draw_ui();                            // widgets write straight into Layer / App fields
ImGui::Render();                      // builds ImGui's vertex data; draws nothing yet
renderer_.draw(scene_, cam_, plane_, background_, surface_mode_);
ImGui_ImplOpenGL3_RenderDrawData(...) // panel on top
glfwSwapBuffers(window_);
```

No dirty flags, no invalidation, no retained scene graph: every frame re-reads the current
state and redraws from scratch.

The two exceptions are the caches derived from the plane — the quads that lie in it, and
the tets it passes through — and both are refreshed inside `Renderer::draw`, not by the
loop. That is deliberate: a slice you cannot draw without refreshing first is a slice that
cannot go stale. `ElementSlice::update` compares the plane equation it was built for and
returns immediately when nothing moved (`scene/draw_objects/element_slice.cpp`), which is why
`draw` takes the scene by non-const reference.

### Inside `Renderer::draw` (`render/renderer.cpp`)

```
open the stencil mask, clear colour + depth + stencil     ← glClear is masked by
                                                            glStencilMask; not optional
update_plane_geometry(plane, bounds)                      ← 16 vertices, re-uploaded
scene.update_element_slices(plane)                        ← no-op unless the plane moved
set uProj and uView once on each of the three programs    ← neither changes within a frame

for opaque_pass in {true, false}:            ← two sweeps over the same list
    for slot in scene.order():               ← the user's compositing order
        layer = scene.at_slot(slot)
        if layer.visible and layer.is_opaque() == opaque_pass:
            draw_layer(layer, ...)       ← skipped outright if the plane
                                           clips this layer away entirely

if plane.enabled and plane.show_gizmo: draw_gizmo(...)
GlState{}.apply(); glBindVertexArray(0); glUseProgram(0);   ← hand ImGui a clean slate
```

Opaque layers go first so they lay down depth for everything translucent behind them.
Translucent ones then blend in the order shown in the panel — there is deliberately no
automatic back-to-front sort, because these meshes nest and no ordering of whole objects is
correct (primer §9).

### Inside `draw_layer` (`render/renderer.cpp`) — up to four passes

Before any of them, one cheap test: `CutPlane::classify` puts the layer's bounding box in
front of the plane, behind it, or across it (two dot products). A layer behind the plane is
clipped away entirely and returns immediately; a layer not crossing it has no cross-section,
so passes 1, 3 and 4 are skipped.

| #   | pass                 | draws                            | depth                           | stencil                       | clip        | notes                                                                                     |
| --- | -------------------- | -------------------------------- | ------------------------------- | ----------------------------- | ----------- | ----------------------------------------------------------------------------------------- |
| 1   | `mark_cross_section` | the layer, into the stencil only | off / off                       | `INVERT` on 1 bit             | on          | skipped unless the plane crosses this layer and it is closed; colour and depth writes off |
| 2   | `draw_surface`       | the layer's triangles            | test on, write if opaque        | **disabled**                  | if plane on | culls only when `oriented`; translucent closed shells draw back faces then front          |
| 2b  | `draw_surface`       | those same triangles, as edges   | `LEQUAL`, pass 2 offset away    | **disabled**                  | if plane on | **shaded + edges** only; same index buffer under `GL_LINE`, flat colour from `overlay_`  |
| 3   | `fill_cross_section` | the plane quad                   | `LEQUAL`, offset toward the eye | `EQUAL 1`, zeroed as consumed | off         | the cap; zeroing as it draws is its own stencil clear                                     |
| 4   | `draw_elements`      | the cut tets, then their edges   | on, offset away from the eye    | off                           | **off**     | the slab of whole tets; only when **elements** is on and the plane crosses the layer      |

Pass 2 _disables_ the stencil test rather than setting `GL_KEEP`: disabling it is what
suppresses stencil writes, and setting `GL_KEEP` would not protect pass 1's bits. Pass 4
disables clipping on purpose — the whole tet is wanted, both sides of the plane — which is
also why it must not use a program that leaves `gl_ClipDistance` unwritten while
`GL_CLIP_DISTANCE0` is on (primer §11).

`W` cycles three surface modes (`SurfaceMode` in `render/renderer.h`), and the difference
between the last two is the whole reason there are three:

| mode              | pass 2        | pass 2b | the domain is |
| ----------------- | ------------- | ------- | ------------- |
| `shaded`          | `GL_FILL`     | —       | solid         |
| `shaded + edges`  | `GL_FILL`     | yes     | solid         |
| `x-ray`           | `GL_LINE`     | —       | see-through   |

`x-ray` is not `shaded + edges` minus the fill: it is the *fill itself* turned into lines.
That is what makes it see-through — a line writes depth only where it is, so nothing behind
the hull is occluded. `shaded + edges` writes depth across the whole hull, as a solid
should, so debug geometry buried inside a mesh is hidden by it; reach for `x-ray`, the
channel's own **in front** toggle, or hiding the layer (whose channels still draw).

Pass 2b is what **shaded + edges** adds. It is not a polygon-mode flip of pass 2 but a
second draw over the same index buffer: pass 2 keeps filling, offset *away* from the eye,
and 2b re-rasterizes those triangles as their sides so the edges win the depth test along
every shared fragment. It cannot borrow `surface_`, which takes its normal from screen-space
derivatives that degenerate on a primitive with no area; `overlay_`'s flat branch
(`uMode = 0`) is there for exactly this, and clips against the plane as pass 2 does.
Culling carries over from the fill, so an oriented mesh hides the edges on its far side —
which is what makes this read as lines lying *on* a solid rather than a cage seen through
it.

Every pass fills in a complete `GlState` and applies all of it (`render/gl_state.h`) — the
polygon mode and the cull face included, so the edge pass and the two-sided translucent draw
go through the same mechanism as everything else rather than around it. If you add a pass, do
the same: half-set state is the classic renderer bug, and it always breaks some _other_
pass.

---

## 8. The cutting plane: one vec4, five consumers

`CutPlane` is four floats — a unit normal `n` and a constant `w`, with `dot(n, x) + w`
negative on the side that gets cut away.

```
                    CutPlane { n, w }
                           │
      ┌──────────┬─────────┼──────────┬────────────────┐
      ▼          ▼         ▼          ▼                ▼
  uPlane in   the cap    gizmo     handle hit-test   ElementSlice
  the vertex  quad's     quads     (ray/plane, in    (sign test per
  shaders     extent               plane_controller)  tet vertex)
  (clipping)
```

It is mutated from exactly three places — the panel (`app/ui.cpp`), the `P` key
(`app/input.cpp`), and a drag on the handle (`PlaneController::update`) — and read
everywhere else. `anchor()` is what keeps a drag or a rotation under the mouse: it is where
the plane crosses the middle of the scene, so rotation pivots there instead of swinging the
plane out of view.

Adding a second plane would mean a second `gl_ClipDistance` slot, `GL_CLIP_DISTANCE1`, and
a decision about what capping even means with two cuts — which is why there is one.

---

## 9. Lifetimes, and the one rule you must not break

Every object that owns a GL name (`GpuMesh`, `ElementSlice`, `ShaderProgram`,
`DynamicQuads`) deletes it in its destructor and is move-only — copying would leave two
owners of one name and the second destructor would delete a buffer still in use.

**The rule: `glDelete*` needs a current context.** A GL-owning object must not outlive the
GLFW window. That is why `App::~App` (`app/app.cpp`) is written in this order:

```cpp
scene_.clear();       // every layer's buffers, while the context is still current
renderer_.release();  // shaders and the plane quad buffer
...                   // only then ImGui shutdown, glfwDestroyWindow, glfwTerminate
```

If you add anything that holds a GL name, give it the same destructor and make sure it is
reachable from one of those two calls. A leak here is silent; a use-after-context is a
crash on exit that looks like it comes from the driver.

---

## 10. Hooking your own algorithms in

Ordered by how little they cost.

### 10.1 Show a mesh your own code produced

Fill a `MeshData` and hand it to the scene. That is the entire API:

```cpp
MeshData m;
m.pos  = my_points;                 // vector<glm::vec3>
m.tets = my_connectivity;           // 4 indices per tet, into m.pos
std::string err;
// add() consumes the mesh and hands back the layer it made, or null.
if (Layer *layer = scene_.add(std::move(m), "step-3", err))
  layer->color = {0.9f, 0.5f, 0.3f};
else
  std::fprintf(stderr, "%s\n", err.c_str());
```

The hull extraction, watertightness test, recentring and upload all happen inside. If you
are iterating on an algorithm, the cheapest workflow is to have it write a `.msh` per stage
and load the directory — every file becomes its own layer with its own colour, visibility
and alpha, and the cutting plane slices all of them at once.

### 10.2 Highlight a subset of elements

> **Superseded in mesh-dev.** There is machinery for this now — a named channel of indices,
> with a checkbox and a colour, and no renderer change per subset. See
> **`debug-drawing.md`**, which has recipes for vertices, edges, facets and whole tets.
> `ElementSlice` has also moved to `scene/draw_objects/element_slice.cpp` and the face table
> is `TetMesh::kFaces`. What follows is still the right mental model, and still what the
> machinery does underneath.

This is the hook worth learning, because it costs no vertex upload: build a `vector<uint32_t>`
of triangle indices into the layer's existing vertex buffer and draw it with its own index
buffer. `ElementSlice` is the worked example — read `scene/draw_objects/element_slice.cpp`, it is short.

To mark, say, every tet failing a quality test, you would:

1. keep the offending tet indices from your check (or recompute them in a `Layer` method),
2. emit their 4 faces into a `vector<uint32_t>` — `TetMesh::kFaces` (`mesh/tet_mesh.h`) is
   the face table, wound outward, and is the one every part of the program shares,
3. upload with a second `ElementSlice`, and
4. draw it in `draw_layer` with a distinct colour.

Steps 3 and 4 are about fifteen lines, because `ElementSlice` already handles buffer growth
and its own freshness (`attach` once, `update` as often as you like) and `draw_elements`
already handles the state.

### 10.3 Colour by a per-vertex or per-element scalar

This one does need a new attribute, since attribute 0 is the only one that exists today.

- **Per vertex** (a nodal field, a distance, a residual): add a second VBO of one `float`
  per vertex, `glVertexAttribPointer(1, 1, GL_FLOAT, ...)` into the layer's VAO, and a new
  program that maps the interpolated value through a colour ramp. Keep the existing
  `dFdx/dFdy` normal — it is independent of any of this.
- **Per element** (element quality, a material tag): fragments interpolate, so a per-element
  value has to be either flat-qualified (`flat in float vQuality;`) or, more simply,
  duplicated to the element's three vertices — which means an unindexed draw for that layer.
  For a debug overlay on a subset of elements, the duplication is usually cheaper to write
  than to avoid.

The ramp belongs in a new program in `render/shaders.h` next to the three existing ones;
`Renderer::init` builds them from a small table, so adding one is a single row.

### 10.4 Move vertices every frame

`GpuMesh::upload` reallocates with `glBufferData(..., GL_STATIC_DRAW)`. For geometry that
changes every frame — a smoothing iteration animating, say — add an `update_positions`
method that calls `glBufferSubData` on the existing VBO instead, and allocate it
`GL_DYNAMIC_DRAW`. `DynamicQuads` (`render/dynamic_quads.h`) is the same pattern at a
smaller scale, and `ElementSlice::update` shows the orphan-and-refill pattern for a buffer
whose size changes every frame.

Connectivity changes need a full re-upload; positions alone do not.

### 10.5 Read something back out — picking

`Camera::screen_ray(mx, my, origin, dir)` gives you a scene-space ray through a pixel, and
`Layer::verts` / `Layer::tets` are right there on the CPU. A "click a tet and print its
index, its vertices and its quality" probe is maybe thirty lines in `input.cpp`, needs no
GL at all, and is often more useful than any amount of shading.

### 10.6 Support another file format

Write a `MeshReader` subclass in `io/` and add one line to the list in
`io/mesh_reader.cpp`. The loader, the directory scan and the usage text all ask the
registry, so nothing else changes. `scan.h` gives you the byte-level parsing helpers — use
them rather than `istream`, which costs about 20× more on a 100 MB file.

---

## 11. Symptom → cause

The failures you are most likely to hit, and where they come from.

| what you see                                           | likely cause                                                                                                                                                                                                                                      |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| nothing is clipped, but the plane is on                | a program with `GL_CLIP_DISTANCE0` enabled that does not write `gl_ClipDistance[0]` — or the Mesa trap: redeclaring `out float gl_ClipDistance[1];` compiles and silently does nothing                                                            |
| holes in the mesh, whole triangles missing             | backface culling on a mesh with inconsistent winding — check `oriented` in the panel; `SurfaceMesh::build` should have caught it                                                                                                                  |
| the cut face is hollow, you see inside the far wall    | capping off, or the surface is not closed (the checkbox is disabled then) — parity capping is meaningless with holes                                                                                                                              |
| the cap flickers or z-fights                           | two coplanar fills without the polygon offset, or `GL_LESS` where `GL_LEQUAL` is needed — every layer's cap lies in the same plane                                                                                                                |
| speckled, noisy shading                                | precision in the derivative normals: coordinates too large. Check `Scene::origin()` was applied — it is subtracted in `build_layer`, not at draw time                                                                                             |
| stencil effects do nothing                             | `glStencilMask` closed when `glClear` ran, or a pass that set `GL_KEEP` instead of disabling the stencil test                                                                                                                                     |
| a translucent layer hides one behind it                | depth writes left on for a non-opaque layer                                                                                                                                                                                                       |
| everything vanishes after you add a pass               | leaked state — fill in a whole `GlState`, never a subset                                                                                                                                                                                          |
| crash on exit, inside the driver                       | a GL-owning object outliving the context; see §9                                                                                                                                                                                                  |
| a mesh loads with far fewer elements than the file has | `.msh` silently drops elements referencing a node the file never declared, and keeps only types 2 and 4 (triangle, tet) — a mesh of hexes or second-order elements loads as nothing; `.obj` drops malformed faces and reports the count on stderr |

---

## 12. Invariants worth preserving

If you change the code, these are the assumptions the rest of it rests on:

1. **Every index in a `MeshData` is an index into `pos`**, and `pos` is dense. Readers
   guarantee this; nothing downstream re-checks it.
2. **The GPU vertex buffer is `pos`, recentred, in the same order** — so CPU indices remain
   valid GPU indices. Compacting unreferenced vertices would break every extension in §10.
3. **`Scene::order()` is a permutation of the layer list.** Add and remove layers through
   `Scene`, never by touching `layers_` alone.
4. **The origin is fixed by the first mesh** and applied to every later one, or files stop
   lining up with each other.
5. **`closed` gates capping; `oriented` gates culling.** Do not enable either without the
   corresponding flag.
6. **Each pass applies a complete `GlState`** — polygon mode and cull face included.
7. **GL-owning objects are move-only and die before the context.**
8. **Derived facts stay derived.** A count that a vector already answers (`tet_count()`), a
   cache key that belongs to the thing it describes (`ElementSlice`'s plane), a bounding
   box the `Scene` has already measured — none of these get a second copy to fall out of
   step with the first.

---

## Where to go next

- `docs/opengl-primer.md` — the graphics concepts themselves: the pipeline, coordinate
  spaces, depth, stencil, blending, clip distances, derivative normals.
- `README.md` — the five design decisions in one page, and the known limits.
- The code, in this order if you are new to it: `io/mesh_data.h` (20 lines, the whole data
  model), `scene/layer.h`, `render/renderer.cpp`'s header comment, then `Renderer::draw`.
