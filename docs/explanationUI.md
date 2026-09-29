# Understanding the UI, from a click to pixels

This is a guided reading of the viewer as it was inside mesh-dev, before it became
this project. It assumes you can read basic C++, but does not assume you already know
Dear ImGui or OpenGL. Read `App` as `Viewer`, `Document` as the host's `MeshSource`,
and `app/ui.cpp` as `builtin_windows.cpp`; the note at the top of
`code-walkthrough.md` has the full mapping.

The central idea is:

> **Input changes ordinary C++ state. The frame loop brings the drawable data up
> to date, then draws a picture of that state.**

Keep that sentence in mind whenever a function seems disconnected from what you
see on screen. A checkbox usually does not call the renderer. It changes a field
that the renderer reads later in the same frame.

Some older explanations describe the predecessor, mesh-viewer. In this code,
loading goes through `Document::add`, followed by `Scene::sync`; there is no
`Scene::add`. Mesh positions remain on the CPU in double precision, and a `Layer`
holds read-only mesh references plus GPU data. Use the functions linked here when
an older diagram or source comment names a different path.

## How to use this guide

Read it beside the source, one question at a time. Follow the named functions;
you do not need to understand every function in a file before moving on.

| Reading pass | Sections | What you should be able to explain afterwards |
|---|---|---|
| **1. Understand the application** | 1–6 | Who owns a setting, what runs every frame, and where a mouse drag goes |
| **2. Understand the picture** | 7–11 | How a mesh reaches the GPU, what shaders do, and how the cut is drawn |
| **3. Cross into computation** | 12–15 | How physics and fitting update the picture, and what geometry files to read next |

### Contents

1. [The pieces and their jobs](#1-the-pieces-and-their-jobs)
2. [Where the state lives](#2-where-the-state-lives)
3. [Startup and shutdown](#3-startup-and-shutdown)
4. [One frame, in the actual order](#4-one-frame-in-the-actual-order)
5. [Reading the ImGui code](#5-reading-the-imgui-code)
6. [Mouse input and the camera](#6-mouse-input-and-the-camera)
7. [Loading a mesh and keeping its view current](#7-loading-a-mesh-and-keeping-its-view-current)
8. [OpenGL buffers, explained through GpuMesh](#8-opengl-buffers-explained-through-gpumesh)
9. [From a vertex to a coloured fragment](#9-from-a-vertex-to-a-coloured-fragment)
10. [Reading the renderer as a sequence of passes](#10-reading-the-renderer-as-a-sequence-of-passes)
11. [The cutting plane is several features working together](#11-the-cutting-plane-is-several-features-working-together)
12. [Following UI actions into computation](#12-following-ui-actions-into-computation)
13. [How algorithm results become debug overlays](#13-how-algorithm-results-become-debug-overlays)
14. [The next reading path: geometry](#14-the-next-reading-path-geometry)
15. [Exercises and a navigation cheat sheet](#15-exercises-and-a-navigation-cheat-sheet)

## 1. The pieces and their jobs

“The UI” includes two different kinds of drawing here:

- **The controls:** Model, Inspect, View, buttons, sliders, text. Dear ImGui builds
  these, and its OpenGL backend draws them.
- **The 3D view:** meshes, caps, element slices, and the plane handle. The
  project's `Renderer` draws these using its own OpenGL programs.

They share one window and framebuffer. There is no separate 3D canvas widget in
`ui.cpp`: the scene is drawn across the window, then the panels are drawn over it.

### The libraries are not interchangeable

| Name you see | Its job here | Example |
|---|---|---|
| **GLFW** | Creates the native window and OpenGL context; delivers mouse, keyboard, resize, and file-drop events | `glfwCreateWindow`, `glfwPollEvents` |
| **glad** | Loads OpenGL function pointers from the active driver | `gladLoadGLLoader` |
| **OpenGL** | The graphics API used to create GPU resources and submit rendering commands | `glBufferData`, `glDrawElements` |
| **GLSL** | The language of the small programs that execute on the GPU | `kSurfaceVS`, `kSurfaceFS` |
| **Dear ImGui** | Describes controls, handles their interaction, and produces UI draw data | `ImGui::Checkbox` |
| **ImGui backends** | Connect ImGui to GLFW input and OpenGL drawing | `ImGui_ImplGlfw_*`, `ImGui_ImplOpenGL3_*` |
| **GLM** | C++ vector and matrix mathematics; it does not draw anything | `glm::vec3`, `glm::lookAt` |

The CPU runs the C++ application: input routing, widget code, mesh operations,
and command submission. The GPU executes shaders and graphics operations for
the submitted draws. An OpenGL call returning does not generally mean the GPU
has finished that work.

### The project directories

```text
app/       coordinates the application; owns the window and builds the panels
   |
   +-- core/Document --------> CPU meshes and their identities
   |      ^
   |      +-- physics/ and pipeline/ read or change those meshes
   |
   +-- scene/ ---------------> display settings and drawable mesh projections
   |
   +-- render/ --------------> OpenGL drawing of the scene
```

In particular, `scene/` is not just a collection of mathematical meshes. It is
the **viewer-side representation** of them, including GPU resources.

There are also two meanings of *pipeline*:

- The **conform pipeline** is the CPU algorithm in `src/pipeline/`.
- The **graphics pipeline** is the sequence that transforms triangles into
  framebuffer samples. We explain that in section 9.

## 2. Where the state lives

**Open:** [src/app/app.h](../src/app/app.h).

Start with the member variables, rather than the private function declarations.
They tell you what survives between frames.

```text
App
├── window_          GLFW window, with its OpenGL context
├── document_        authoritative CPU meshes
├── scene_           how those meshes are displayed
│   ├── layers_      one Layer per mesh
│   │   ├── settings: visible, color, alpha, cap, show_elements, ...
│   │   ├── tet/surf: read-only shared references to a CPU mesh
│   │   ├── gpu:     positions and surface indices on the GPU
│   │   └── slice:   selected tetrahedra, for the cutting-plane view
│   ├── order_       order in which layers composite
│   └── debug_overlay_
├── physics_         motion fields, simulation clock, rest snapshots
├── pipeline_        fitting parameters, labels, results, debug channels
├── cam_             viewpoint
├── plane_           cutting-plane equation and toggles
├── plane_drag_      state captured when a plane drag begins
├── renderer_        shader programs and small plane-geometry buffers
└── other UI state   background_, surface_mode_, field_ui_, gesture_, ...
```

The most important distinction is **mesh data versus display settings**:

| Question | Where to look |
|---|---|
| Where are the actual vertex positions? | `TetMesh` or `SurfaceMesh`, owned through `Document` |
| Which mesh is the fitting target? | `ConformPipeline::target_id()` |
| Is that mesh visible? | `Layer::visible` |
| What colour is it? | `Layer::color` |
| Where is the camera looking? | `Camera::target`, `yaw`, `pitch`, `distance` |
| Is the cut enabled? | `CutPlane::enabled` |
| Which physics controls were chosen for a surface? | `App::FieldUi`, keyed by mesh ID |

Hiding a mesh changes its layer, not the document. Fitting can still use it.
Moving the camera also leaves the mesh coordinates untouched.

### A few C++ details that make this easier to read

- `scene_` and `cam_` are members of `App`; the trailing underscore is a naming
  convention. `app.cpp`, `input.cpp`, and `ui.cpp` define methods of the **same
  class**, so all three can use those members.
- `Layer &l = ...` is a reference to an existing layer. Writing `l.alpha` changes
  the actual layer; it does not change a copy.
- `&l.alpha` is the address of that field. Passing it to ImGui lets the widget
  edit the field.
- `shared_ptr<const SurfaceMesh>` keeps the mesh alive while permitting only
  read access through that pointer. The layer does not have an independent
  editable CPU mesh.
- A mesh ID is a stable document identity. A **layer index** is a position in
  `layers_`; a **slot** is a position in `order_`. They are different numbers.
  `scene.at_slot(slot)` performs the slot-to-layer lookup.

## 3. Startup and shutdown

**Open:** [src/main.cpp](../src/main.cpp), then `App::init` in
[src/app/app.cpp](../src/app/app.cpp).

The executable's path is short:

```text
main
  construct App
  app.init(err)
  parse options and collect paths
  app.load(path) for each path
  app.run()
  App destructor when main leaves
```

`init` precedes loading because loading also synchronizes the scene, and scene
synchronization creates OpenGL buffers.

### What is an OpenGL context?

A context holds graphics state and gives access to OpenGL resources. You must
make an appropriate context **current on the calling thread** before using
OpenGL. Constructing a C++ `GpuMesh` with zero handles is harmless; allocating
its GL buffers requires the context.

The relevant initialization sequence is:

1. **Initialize GLFW.** An error callback records useful failure messages.
2. **Request a window configuration:** OpenGL 3.3 core, 24 depth bits, 8 stencil
   bits, and 4 multisamples.
3. **Create the window and make its context current.**
4. **Load GL functions with glad.** GLFW supplies the driver lookup function.
5. **Install input callbacks.**
6. **Create ImGui's context and initialize its backends.** The `false` in
   `ImGui_ImplGlfw_InitForOpenGL(window_, false)` means this application installs
   and forwards callbacks itself.
7. **Initialize the renderer.** Compile/link four shader programs and allocate
   the small buffer used for the plane and its handle.

“Core” means the application uses the modern buffer-and-shader API rather than
legacy OpenGL immediate drawing such as `glBegin`. ImGui's “immediate mode,”
discussed below, is a separate idea.

### What do the framebuffer requests mean?

The **framebuffer** is the destination of rendering. Conceptually it contains:

- **Colour:** the image you will see.
- **Depth:** values used to decide which surface is closer.
- **Stencil:** small integers used as masks; here they identify the cap region.

With **MSAA** (multisample antialiasing), a pixel has multiple coverage/depth/
stencil sample locations. This improves geometric edges such as silhouettes.
It does not mean every shader necessarily runs four times per pixel; ordinary
multisampling separates coverage sampling from shading frequency.

GLFW normally provides a double-buffered window: render into a back buffer,
then `glfwSwapBuffers` presents it. `glfwSwapInterval(1)` requests synchronization
to display refresh.

### Why teardown is explicit

GPU-owning wrappers release their GL objects in destructors. This is **RAII**:
resource lifetime follows the lifetime of its C++ owner. But GL deletion also
needs the context to remain available.

`App::~App` therefore clears the scene and releases the renderer **before**
shutting down ImGui and destroying the window. Otherwise ordinary member
destruction would happen after the destructor body had already removed the
context.

The main buffer/program wrappers are non-copyable: copying a numeric GL handle
would create two apparent owners of the same resource. Move operations transfer
ownership and zero the old handle, which lets layers live in a growing vector.

## 4. One frame, in the actual order

**Open:** `App::run` in [src/app/app.cpp](../src/app/app.cpp).

This is the best function to return to whenever you lose the thread. The central
loop, with only comments shortened, is:

```cpp
glfwPollEvents();

ImGui_ImplOpenGL3_NewFrame();
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();
draw_ui();
ImGui::Render();

if (physics_.running)
  physics_.step(document_);

scene_.sync(document_);
scene_.sync_debug_overlay(pipeline_.debug());

renderer_.draw(scene_, cam_, plane_, background_, surface_mode_);
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

glfwSwapBuffers(window_);
```

Read it as six phases:

1. **Receive events.** GLFW invokes the callbacks in `input.cpp`; they may move
   the camera, change the plane, or load a dropped file.
2. **Evaluate controls.** ImGui starts a frame. `draw_ui()` describes the panels
   and applies interactions to C++ state.
3. **Advance computation.** Running physics takes one step.
4. **Refresh the view's data.** Updated meshes reach their GPU buffers; updated
   debug channels reach their index buffers.
5. **Draw the scene, then the panels.** Both go into the window framebuffer.
6. **Present the frame.** Repeat until the window is asked to close.

**`ImGui::Render()` does not submit the panels to OpenGL.** It finalizes ImGui's
draw data. The later backend call actually renders that data. This separation
allows the panel code to change scene settings before the scene is drawn, while
still placing the panels on top of the result.

### Consequences of this ordering

- A colour or plane edit can affect the scene in the same frame.
- Physics runs **once per rendered loop using `physics_.dt`**, not according to
  measured wall-clock elapsed time. A slower frame rate slows simulation time
  relative to real time.
- A “step once” click calls physics inside `draw_ui`. If `running` is also on,
  the normal step later in the loop runs too.
- Some UI readouts describe the preceding synchronized state. For example,
  element-slice counts are displayed before the renderer updates the slice.
  A new debug channel is synchronized after the panels are built, so its row
  appears on the following UI frame.
- A long operation called from a button blocks this whole loop. OpenMP inside
  a solver does not make the application's frame loop asynchronous.

There is no automatic redraw notification connecting a widget to a renderer.
The application redraws continuously; **expensive data updates** use caches.

## 5. Reading the ImGui code

**Open:** [src/app/ui.cpp](../src/app/ui.cpp), initially at `App::draw_ui` near
the end.

```text
draw_ui()
  model_window()
    meshes_tab() | physics_tab() | fitting_tab()   depending on active tab
  inspect_window()
  view_window()
```

### 5.1 Immediate-mode UI: describe it again every frame

With a retained widget toolkit, you might construct a checkbox object, keep it,
and register an event handler. Here, each frame calls:

```cpp
ImGui::Checkbox("cutting plane", &plane_.enabled);
```

That call places the checkbox, reads its current value, handles interaction,
and can write a new value through the pointer. On the next frame the code calls
it again using the now-current value.

The widget description is repeated; the application state is not recreated.
ImGui also retains its own interaction/layout state internally, identified by
widget IDs.

There are three recurring patterns in `ui.cpp`:

**A. Edit a field directly:**

```cpp
ImGui::SliderFloat("alpha", &l.alpha, 0.0f, 1.0f, "%.2f");
```

The slider owns no opacity value of its own. `l.alpha` is the value.

**B. Edit a temporary representation and write back if it changed:**

```cpp
int mode = int(surface_mode_);
if (ImGui::Combo("surface", &mode, "shaded\0shaded + edges\0x-ray\0"))
  surface_mode_ = SurfaceMode(mode);
```

The enum is represented by an integer for the widget. The embedded `\0`s
separate combo entries. The return value says the selection changed.

**C. Perform an action only when activated:**

```cpp
if (ImGui::SmallButton("step once"))
  physics_.step(document_);
```

The button is described every frame. The step executes only on activation.
This explains why finding the button label is often the fastest route into the
code for a feature.

### 5.2 IDs explain many of the strange-looking strings

There is an “alpha” slider for every layer. How does ImGui tell them apart?

```cpp
ImGui::PushID(int(l.mesh_id));
ImGui::Checkbox("##visible", &l.visible);
ImGui::SliderFloat("alpha", &l.alpha, 0.0f, 1.0f, "%.2f");
ImGui::PopID();
```

The effective identity includes the current window, ID stack, and widget label.
Pushing the mesh ID gives every layer its own namespace. This also keeps widget
identity stable when the user reorders layers.

In `"##visible"`, the part before `##` is empty, so no label is displayed; the
string still identifies the widget. `"##chip"` and `"##color"` follow the same
pattern. They are not special OpenGL names.

### 5.3 Layout calls versus application logic

Read these as layout/grouping instructions, then concentrate on the expressions
inside them:

| ImGui call | Meaning while reading this code |
|---|---|
| `Begin("Model")` / `End()` | Contents of a window |
| `BeginTabItem(...)` | Execute this tab's content block when selected |
| `TreeNode...` / `TreePop()` | A collapsible group; contents run when open |
| `SameLine()` | Put the next item beside the previous one |
| `Separator()` | Visual division |
| `BeginDisabled(condition)` / `EndDisabled()` | Show controls but prevent interaction while the condition holds |
| `Text...` | Display a value; it does not edit it |

Pairing matters: `Begin`/`End` for windows must be paired even when a window's
contents are not visible; tree, tab, combo, and table content blocks in this code
are conditional on their opening call returning true.

`ImGuiCond_FirstUseEver` supplies an initial position/size. It is not a command
to move the window on every frame. ImGui persists window layout in `imgui.ini`;
that does not automatically persist the meshes, fitting parameters, or layer
colours stored by this application.

### 5.4 Read the three windows by what they edit

| Window | Important functions | State they read or change |
|---|---|---|
| **Model / Meshes** | `meshes_tab` | Reads document meshes, counts, and topology; shows domain/target roles |
| **Model / Physics** | `physics_tab`, `field_ui`, `apply_field` | Motion settings, simulation clock, actual surface placement |
| **Model / Fitting** | `fitting_tab` | Selected mesh IDs, `pipeline_.params`, run buttons, reports |
| **Inspect** | `inspect_window` | Plane, global surface mode, per-layer caps/elements, debug-channel appearance |
| **View** | `view_window` | Visibility, colour, alpha, composite order, background, camera framing |

`mesh_chip` is just the little coloured square linking a mesh's appearances in
these windows. `Scene::layer` finds the display layer for a document mesh ID.

### 5.5 First complete trace: changing a layer's colour

```text
view_window(): ColorEdit3(..., &l.color.x, ...)
  -> writes the Layer's RGB values
  -> later: Renderer::draw(...)
  -> draw_layer(...)
  -> draw_surface(...)
  -> surface_.set("uColor", glm::vec4(l.color, l.alpha))
  -> fragment shader uses uColor to colour the triangles
```

No vertex upload is needed. Positions and connectivity did not change. Only a
small shader parameter, called a **uniform**, changes. Keep this trace in mind
when uniforms appear in section 9.

## 6. Mouse input and the camera

**Open:** [src/app/input.cpp](../src/app/input.cpp), then
[src/scene/camera.h](../src/scene/camera.h).

### 6.1 How GLFW reaches an App method

GLFW callbacks receive a `GLFWwindow*`; they do not automatically receive C++'s
`this`. `install_callbacks` stores that missing association:

```cpp
glfwSetWindowUserPointer(window_, this);
```

`app_of(w)` retrieves that pointer and casts it back to `App*`. The small lambdas
registered with GLFW are **trampolines**: they translate a C-style callback into
an ordinary member call.

For example, a scroll event follows:

```text
glfwPollEvents()
  -> registered scroll lambda
     -> ImGui_ImplGlfw_ScrollCallback(...)
     -> if ImGui does not want the mouse: App::on_scroll(dy)
        -> cam_.dolly(dy)
```

Events must reach ImGui so its widgets work. `WantCaptureMouse` and
`WantCaptureKeyboard` then tell the application whether to also handle that
input. This is why using a slider normally does not orbit the scene.

Notice the actual exception: the **cursor-position callback always forwards**
to `on_cursor_pos`. Gesture ownership is decided when the drag starts; movement
then updates that active gesture. The mouse-button, scroll, and key callbacks
have capture checks. Character input is forwarded directly to ImGui.

### 6.2 Who gets a drag?

`on_mouse_button` reads in this order:

```text
release?                    end camera gesture and plane drag
middle button?              start Pan
left button?
  enabled, visible handle hit?
    Ctrl held?              start plane Rotate
    otherwise               start plane Translate
  handle did not claim it?
    Shift held?             start Pan
    otherwise               start Orbit
```

`on_cursor_pos` computes the mouse delta, then updates the plane drag, orbit, or
pan. It changes mathematical state; it makes no mesh draw calls.

The keyboard methods are small for the same reason:

- `P` flips `plane_.enabled`.
- `W` advances `surface_mode_` using `next_mode`.
- `F` calls `refit`, which calls `Camera::fit` with the scene bounds.
- Escape requests window closure.

### 6.3 An orbit camera is a moving viewpoint

The camera stores a target, a distance from it, and yaw/pitch angles:

```text
eye = target + direction(yaw, pitch) * distance
```

- **Orbit:** changes yaw/pitch. Pitch is clamped so the viewing direction does
  not become parallel to the up direction used by `lookAt`.
- **Pan:** moves the target along camera-right and camera-up. The movement scale
  depends on distance, field of view, and viewport height.
- **Dolly / wheel:** multiplies the distance by `0.9^steps`. It moves the eye
  closer or farther; it does not change the field-of-view angle.
- **Fit:** places the target at the scene centre and chooses distance and
  near/far clipping distances from the scene radius. It keeps yaw/pitch.

So the “reset camera” button frames the scene; it does not reset every camera
field to its construction default.

### 6.4 View and projection matrices

`Camera::view()` uses `glm::lookAt` to express scene points relative to the
camera, whose viewing direction in that coordinate system is negative Z.
`Camera::proj()` uses `glm::perspective` to map that view into a perspective
frustum: the truncated pyramid of space visible between near and far planes.

The shader computes:

```text
clip_position = projection * view * vec4(scene_position, 1)
```

The multiplication acts right to left. A position has homogeneous coordinate
`w = 1`, so translations affect it. A direction uses `w = 0`, as you can see
when the plane controller rotates a normal.

`glViewport(0, 0, width, height)` maps the final normalized image into framebuffer
pixels. `on_resize` updates both this viewport and the camera's aspect ratio.

### 6.5 Picking runs the transformation backwards

`Camera::screen_ray` converts a mouse position to normalized device coordinates
(NDC), with X and Y between -1 and 1 over the viewport. It flips Y because mouse
coordinates start at the top, then unprojects a near and a far point using:

```text
inverse(projection * view)
```

After dividing each result by its homogeneous W, their difference gives a ray
direction. This ray is used to hit-test the plane handle; there is no general
mesh-selection tool wired to this function in the current viewer.

For these calculations, mouse and viewport coordinates must use the same scale.
Currently `cam_.vw/vh` come from framebuffer size while GLFW supplies cursor
coordinates in window coordinates. They coincide at 1:1 scaling; on a scaled
display, a framebuffer/window conversion is the place to inspect if picking is
offset.

## 7. Loading a mesh and keeping its view current

**Open:** `App::load`, [src/core/document.cpp](../src/core/document.cpp),
[src/scene/scene.cpp](../src/scene/scene.cpp), and
[src/scene/layer.cpp](../src/scene/layer.cpp).

### 7.1 Follow one file through the representations

```text
file
  |
  | mesh_io::read
  v
MeshData                         temporary CPU arrays, double positions
  |
  | Document::add
  |   optionally Morton-reorder
  |   build TetMesh or SurfaceMesh
  v
MeshEntry in Document            stable ID + authoritative CPU mesh
  |
  | Scene::sync -> Layer::refresh
  v
Layer                            appearance + read-only mesh reference
  |
  | project positions, upload
  v
GpuMesh                          GPU positions + drawable triangle indices
```

`MeshData` is the reader's common output, not a renderer object. If it contains
tets, `Document::add` builds a `TetMesh`; otherwise it builds a `SurfaceMesh`.
The mesh builders derive connectivity/topology information. The scene later
reads those facts rather than deriving them itself.

A directory load collects supported files and sorts them. Dropping files calls
the same `load` path. The viewer defaults to Morton ordering; `--no-morton`
preserves input numbering. That reordering occurs **before** meshes and their
dependent data are built.

### 7.2 A volume is usually drawn only as its boundary

In `layer.cpp`, `drawn_indices` chooses:

- `tet()->boundary_faces()` for a volume;
- `surf()->triangles()` for a surface.

OpenGL does not have a tetrahedron primitive. Drawing the ordinary domain shell
means drawing its boundary triangles. Interior tetrahedra remain in the CPU
mesh and are used by algorithms and the element-slice feature.

All mesh vertices, including interior vertices, are uploaded in mesh order.
Only the **index list used for this draw** is restricted to the boundary. That
lets a slice or debug channel reuse the same positions later.

### 7.3 The coordinate spaces in this program

| Space | Typical type | Used by |
|---|---|---|
| **Mesh / physical coordinates** | `glm::dvec3` | CPU meshes, physics, geometry queries, fitting inputs/outputs |
| **Scene coordinates** | `glm::vec3` | GPU positions, camera, cutting plane, viewer bounds |
| **View coordinates** | GLSL `vec3`/`vec4` | Lighting and projection relative to the camera |
| **Clip coordinates** | GLSL `vec4` | `gl_Position`, before dividing by W |
| **NDC** | X, Y, Z after dividing by W | The standard OpenGL visible cube, -1 to 1 |
| **Window coordinates** | Pixels and depth values | Rasterization into the viewport |

The first mesh synchronized chooses a shared origin from its drawn bounds.
Every layer uses that same origin:

```cpp
verts.push_back(glm::vec3(p - origin));
```

The subtraction happens **in double precision before conversion to float**.
This preserves small differences in meshes whose physical coordinates are
large. It also makes the derivative-based lighting less sensitive to numerical
precision.

For example, mesh X coordinates `1000000.01` and `1000000.02` become `0.01` and
`0.02` relative to an origin of `1000000`, rather than first being rounded as
large float values. The CPU mesh itself is not recentered.

Every mesh shares the origin, so their relative placement remains correct.
There is no per-layer model matrix in the renderer. Manual surface placement
changes actual CPU positions; camera motion changes the view matrix.

The helper named `project` in `layer.cpp` does this recentering/narrowing. It is
not the perspective projection performed by `Camera::proj`.

### 7.4 Why synchronization does not upload everything every frame

Each mesh has a revision counter. Each layer remembers `uploaded_revision`.
`Layer::refresh` has three paths:

| Situation | Work |
|---|---|
| First refresh | Store mesh identity/reference; upload positions and indices; attach the volume's slice to its position buffer |
| Same revision and origin | Return immediately |
| Mesh moved or origin changed | Upload positions, clear the old slice selection, recompute bounds |

`Scene::sync` also adds missing layers and removes layers for missing mesh IDs.
It recomputes aggregate bounds when something changes. Existing layers retain
their display settings when their geometry is refreshed.

The revision increment is explicit in the mesh accessor:

```cpp
std::vector<glm::dvec3> &positions_mut()
{
  ++revision_;
  return pos_;
}
```

The counter changes when writable access is requested, not by detecting writes
to the vector. Writers must request it for each new update; holding a mutable
reference and silently editing it in later frames would bypass freshness
tracking. The normal flow finishes the write before the scene reads it.

Suppose the mesh is at revision 12 and the layer uploaded 12. Moving a surface
requests writable positions, advancing it to 13. On the next sync, `13 != 12`,
so positions are uploaded and the layer remembers 13. A colour change advances
neither revision and requires no position upload.

## 8. OpenGL buffers, explained through GpuMesh

**Open:** [src/scene/gpu_mesh.cpp](../src/scene/gpu_mesh.cpp),
[src/scene/gpu_mesh.h](../src/scene/gpu_mesh.h), and
[src/scene/vertex_layout.h](../src/scene/vertex_layout.h).

### 8.1 Three objects with three different responsibilities

Imagine a square represented by two triangles:

```text
positions:  0:(0,0,0)  1:(1,0,0)  2:(1,1,0)  3:(0,1,0)
indices:    0,1,2,     0,2,3
```

The renderer needs the positions, which positions make each triangle, and how
to interpret the bytes storing those positions.

| Object | Contains | Does not contain |
|---|---|---|
| **VBO**, vertex buffer object | Position bytes | Knowledge of triangles or what “position” means |
| **IBO / EBO**, index/element buffer object | Vertex indices, here 32-bit unsigned integers | Another copy of the positions |
| **VAO**, vertex array object | Attribute-reading configuration and the element-buffer binding | A private copy of the vertex bytes, shader, or camera |

`GLuint vao_`, `vbo_`, and `ibo_` are numeric **handles** identifying GL objects.
They are not CPU pointers to the data.

To **bind** an object is to select it for subsequent OpenGL operations. Binding
does not copy or upload its contents. For example, binding a VAO selects the
vertex-input configuration that the next draw will use.

### 8.2 What upload actually establishes

`GpuMesh::upload`:

1. Creates and binds a VAO.
2. Creates a VBO and uploads the position array with `glBufferData`.
3. Calls `bind_position_attrib` to describe how to read those bytes.
4. Creates an IBO and uploads the indices.
5. Records the number of indices and unbinds the VAO.

This line is the bridge between the C++ memory layout and the vertex shader:

```cpp
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
```

Read its arguments as:

```text
attribute location 0
  contains 3 components
  each component is a float
  no integer-to-normalized conversion
  next vertex begins sizeof(glm::vec3) bytes later
  first value starts at byte offset 0 in the bound VBO
```

`glEnableVertexAttribArray(0)` enables that input. The matching GLSL declaration
is `layout(location = 0) in vec3 aPos`.

A subtle binding detail: `glVertexAttribPointer` records the currently bound
array buffer into the bound VAO's attribute configuration. The element-buffer
binding is also VAO state. Merely binding a different `GL_ARRAY_BUFFER` later
does not redirect every previously configured VAO.

### 8.3 Uploading and drawing are different operations

Uploading stores data for future use. This code submits a draw with:

```cpp
l.gpu.bind(); // glBindVertexArray(vao_)
l.gpu.draw(); // glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr)
```

For the square example, `index_count_` is **6**, not 2. `GL_TRIANGLES` groups
each three indices into one triangle. `nullptr` means byte offset zero into the
bound index buffer; it is not a missing CPU index array.

The same buffers can be drawn many times with different colours, shaders, or
depth settings. This is how the mesh can participate in a stencil pass, a
surface pass, and an edge pass without three geometry uploads.

### 8.4 Updating positions

The initial position allocation uses `GL_DYNAMIC_DRAW`; indices use
`GL_STATIC_DRAW`. These are expected-usage hints to the driver, not automatic
update mechanisms.

`GpuMesh::update_positions` explicitly calls `glBufferSubData` to replace the
position bytes. Connectivity and vertex count are fixed, so the existing
allocation and triangle indices remain usable. Camera motion needs no such
update: a new matrix changes how the existing vertices are projected.

The custom mesh rendering uses position as its only vertex attribute. ImGui's
backend has its own vertex layout, shaders, and textures for panels and text;
it is a separate renderer sharing the same context.

## 9. From a vertex to a coloured fragment

**Open:** [src/render/shaders.h](../src/render/shaders.h), starting with
`kSurfaceVS` and `kSurfaceFS`.

A graphics draw is roughly:

```text
read indexed vertex attributes
  -> vertex shader: transform positions, produce outputs
  -> assemble triangles and clip them
  -> divide clip coordinates by W and map to the viewport
  -> rasterize: find covered framebuffer samples
  -> fragment shader: compute candidate colours
  -> stencil/depth tests and blending
  -> framebuffer
```

This is a conceptual order. Hardware may perform some tests early when legal.
A **fragment** is a candidate contribution to the image, not necessarily a
final pixel: multiple triangles can contribute to the same pixel, and tests
can reject contributions. Multisampling adds sample-level coverage.

### 9.1 Read the vertex shader line by line

The core of `kSurfaceVS` is:

```glsl
vec4 vp = uView * vec4(aPos, 1.0);
vViewPos = vp.xyz;
gl_ClipDistance[0] = dot(uPlane.xyz, aPos) + uPlane.w;
gl_Position = uProj * vp;
```

1. `aPos` is one scene-space position fetched from the VBO.
2. `uView` expresses it relative to the camera.
3. `vViewPos` is an output passed towards the fragment shader.
4. `gl_ClipDistance[0]` says which side of the cutting plane this position is on.
   It affects clipping when the corresponding GL capability is enabled.
5. `gl_Position` is the required clip-space position. The GPU handles the
   subsequent clipping and perspective divide.

For a standard perspective matrix, clip W depends on view-space depth.
Dividing X and Y by that W makes distant objects appear smaller. This is why
`gl_Position` has four components even though the mesh is three-dimensional.

### 9.2 Attribute, uniform, varying: three kinds of data

| Kind | Here | Changes at what scale? |
|---|---|---|
| Vertex attribute | `aPos` | Per vertex |
| Uniform | `uView`, `uProj`, `uPlane`, `uColor` | Set by C++; constant across a draw |
| Vertex output / fragment input, often called a varying | `vViewPos` | Interpolated across each triangle |

The vertex shader supplies three view positions for a triangle. Rasterization
interpolates them for fragments, using perspective-correct interpolation. If
you know barycentric interpolation on triangles, this is the same basic idea,
with the correction needed after perspective projection.

Uniforms belong to a linked program. In
[src/render/shader.cpp](../src/render/shader.cpp), `ShaderProgram::set` looks up
the name's location, caches it, and calls `glUniform*`. Call `use()` first so
the intended program is current. An absent or optimized-out uniform has
location -1, which GL ignores when setting it.

### 9.3 Where the lighting normal comes from

There is no normal attribute in the mesh VBO. `kSurfaceFS` derives one:

```glsl
vec3 g = cross(dFdx(vViewPos), dFdy(vViewPos));
float len = length(g);
vec3 N = (len > 1e-12) ? g / len : vec3(0.0, 0.0, 1.0);
N = faceforward(N, vViewPos, N);
```

`dFdx` and `dFdy` are fragment-shader screen-space derivatives, implemented
using neighbouring fragment invocations. The two position differences lie
along the triangle's surface, so their cross product gives a normal direction.
For a nondegenerate planar triangle this produces flat, per-triangle shading,
subject to floating-point precision.

The length check avoids dividing by a nearly zero vector on degenerate cases.
`faceforward` makes the normal face the eye for lighting. That operation is
distinct from deciding whether a triangle is discarded by face culling.

The remaining lighting is simple:

- `dot(N, L)` gives diffuse brightness according to the angle to the light.
- The ambient term prevents unlit faces from becoming completely black.
- A power of the normal/halfway-vector dot product creates a specular highlight.
- `uColor.a` is carried through as alpha for blending.

The light direction is fixed in **view space**, so it behaves like a light
attached to the camera.

### 9.4 Why there are four shader programs

`Renderer::init` builds these vertex/fragment pairs:

| Program member | Purpose |
|---|---|
| `surface_` | Lit mesh surfaces and filled selected tets |
| `stencil_` | Transform/clip mesh triangles while marking cap parity; colour output is masked off |
| `cap_` | Plane cap and handle, using a supplied normal; also unlit element outlines |
| `overlay_` | Debug points/lines/triangles and the shaded-mode edge overlay |

The surface, stencil, and overlay vertex shaders write clip distance. The cap
vertex shader does not, so draws using it disable that clipping capability.

The shader strings are compiled by the driver at startup, not by the C++
compiler. `ShaderProgram::build` compiles each stage, links the pair, and checks
both compile and link status. A shader error comes back through `Renderer::init`
to `App::init`.

## 10. Reading the renderer as a sequence of passes

**Open:** `Renderer::draw`, then `draw_layer`, in
[src/render/renderer.cpp](../src/render/renderer.cpp). Keep
[src/render/gl_state.h](../src/render/gl_state.h) beside it.

Do not initially read this file as hundreds of unrelated `gl*` calls. Read each
pass as four questions:

1. What geometry is submitted?
2. Which shader is active?
3. Which fragments are allowed to contribute?
4. What does the pass write: colour, depth, stencil?

### 10.1 OpenGL state persists

The draw call does not receive all its settings as arguments. It uses the
currently bound VAO/program and the current graphics state. Enabling blending
or disabling depth writes affects later draws until changed again.

`GlState` packages the relevant switches:

```cpp
GlState s;
s.depth_test = true;
s.depth_write = opaque;
s.blend = !opaque;
s.clip_plane = plane.enabled;
s.apply();
```

Every `apply` sets all the switches represented by that struct, using its
defaults for fields the pass did not override. It is not an automatic
save/restore scope, nor does it cover every possible OpenGL state. Program
selection, VAO binding, uniforms, and point size are managed separately.

### 10.2 Depth testing and depth writing are independent

The depth buffer stores a depth per sample, conventionally cleared to the far
value. With `GL_LESS`, an incoming fragment passes if it is closer than the
stored depth.

- **Depth test:** should the incoming depth be compared?
- **Depth write:** if the fragment passes, should its depth replace the stored
  value?

An opaque surface normally does both. A translucent surface tests against
opaque geometry but does not write depth, so it does not prevent other
translucent surfaces from contributing later.

`GL_LEQUAL` also permits equal depths. The renderer uses it when a drawn edge or
annotation sits on a surface already in the depth buffer.

Perspective depth is nonlinear; near/far choices affect its precision. This is
why `Camera::fit` scales them with scene size, rather than using a fixed pair
for every possible mesh.

### 10.3 Alpha is a compositing weight

With this renderer's blend function, RGB is combined as:

```text
new colour = source colour * source alpha
           + previous framebuffer colour * (1 - source alpha)
```

Changing the order changes the result. Consequently:

1. Opaque layers are drawn first, writing depth.
2. Translucent layers are drawn afterwards in `Scene::order()`.

`Layer::is_opaque()` uses `alpha >= 0.999f`. View's up/down buttons swap entries
in the order array, not mesh IDs or position arrays. They directly affect
translucent compositing order.

For an oriented translucent layer, `draw_surface` draws back-facing triangles
first and front-facing triangles second. This improves shell rendering, but it
is not a general triangle-depth sort. Concave/intersecting/nested transparent
surfaces can still have order-dependent artifacts.

### 10.4 Winding and culling

By default OpenGL calls a projected counter-clockwise triangle front-facing.
**Back-face culling** skips back-facing triangles. `draw_surface` enables
culling when `l.oriented()` is true, using the mesh's topology information.
The viewer treats tet boundaries as oriented; surface flags come from
`SurfaceMesh`.

Culling is independent of transparency and lighting. Flipping a normal in the
fragment shader cannot bring back a triangle already discarded by culling.

### 10.5 The actual frame inside Renderer::draw

```text
compute view/projection matrices
clear colour, depth, stencil
update the plane's small vertex buffer
update element-slice selections when needed
set view/projection uniforms on all four programs

for opaque, then translucent:
  for each layer in the user order:
    if visible and in this opacity group:
      draw_layer(...)

draw plane handle if enabled
draw debug overlays
restore baseline state, unbind VAO and program
```

The **16 plane vertices are rewritten every draw** in the current implementation.
Element slices are cached. They are not the same kind of update.

`draw_layer` first classifies the layer's bounding box against the plane. It
skips a fully removed layer and avoids cap work where the plane misses. For a
crossing layer it can perform:

| Pass | Geometry | Main effect |
|---|---|---|
| `mark_cross_section` | Layer's triangles | Mark stencil, without colour/depth writes |
| `draw_surface` | Same triangles | Draw clipped shell; optionally another edge draw |
| `fill_cross_section` | Large plane quad | Colour the stencil-marked opening |
| `draw_elements` | Selected tets' faces | Draw a filled and outlined slab of whole elements |

Finally, `GlState{}.apply()` resets the custom clip/stencil switches before
ImGui renders. This matters because both renderers share the same context.

### 10.6 Shaded, shaded + edges, and x-ray

| Mode | What changes in the shell draw? | Does the shell fill hide interior geometry? |
|---|---|---|
| `Shaded` | Filled triangles | Yes, when opaque |
| `Edges` | Filled triangles, followed by triangle outlines | Yes, when opaque |
| `Xray` | Triangle rasterization uses `GL_LINE` | Only the lines write shell depth |

`GL_LINE` here is **polygon mode**: OpenGL still receives triangles, but
rasterizes their edges. This differs from `GL_LINES`, where each two indices
describe a line segment.

For `Edges`, the fill is offset slightly away in depth, and the outlines use
`LEQUAL`. This avoids **z-fighting**, unstable competition between nearly equal
depth values. The outline draw uses the overlay shader's flat-colour branch;
a line has no triangle area from which to derive a lighting normal.

The mode controls the shell. Caps and whole-element draws have their own
controls and can still fill parts of an x-ray view.

## 11. The cutting plane is several features working together

**Open:** [src/scene/cut_plane.h](../src/scene/cut_plane.h),
[src/scene/plane_controller.h](../src/scene/plane_controller.h), and the cap/
element methods in `renderer.cpp`.

There are four different objects or operations you may perceive as “the cut”:

1. A mathematical plane used to remove half the rendered shell.
2. A coloured cap that makes the opening look solid.
3. A translucent square handle used for interaction.
4. An optional selection of whole tetrahedra intersected by the plane.

They share the plane equation but are implemented differently.

### 11.1 The equation and its sign

`CutPlane` stores a unit normal `n` and constant `w`:

```text
d(x) = dot(n, x) + w
d(x) < 0: remove this side
d(x) >= 0: keep this side
```

For `n = (1,0,0)` and `w = -2`, the plane is `x = 2` and the kept half is
`x >= 2`. `set_through(p)` chooses `w = -dot(n,p)`. `flip()` negates both `n`
and `w`, preserving the plane's location and swapping the kept side.

`anchor(scene_center)` projects the scene centre onto the plane. The handle is
centred there, and handle rotation pivots there. `basis(u,v)` constructs two
perpendicular directions within the plane, used to build square geometry and
test whether the mouse hits that square.

Inspect's offset control displays `-signed_distance(scene_center)`. For a unit
normal this means the anchor is `scene_center + offset * n`, a more useful
reading than the raw equation constant.

### 11.2 Dragging the handle is CPU geometry

`PlaneController::hit_handle`:

1. Makes a scene-space ray from the mouse.
2. Intersects it with the plane using `ray_plane`.
3. Expresses the hit relative to the anchor along `u` and `v`.
4. Accepts it if both coordinates are within the square's half-width.

The hit test and drawing use the same `handle_half(bounds)` function, so they
agree about the handle's size. The hit test is mathematical; it does not read
the framebuffer or check whether a mesh visually occludes the handle.

For translation, the controller intersects a **helper plane** containing the
normal axis and uses movement along that axis. Intersecting the moving cutting
plane itself would not give a fixed drag reference. For rotation, mouse
displacement rotates the saved normal about camera-up/right and recomputes `w`
through the saved pivot. Both operations refer to the captured drag-start
state rather than accumulating many tiny plane updates.

### 11.3 Clipping happens on the GPU

The surface vertex shader writes `d(x)` to `gl_ClipDistance[0]` and the renderer
enables `GL_CLIP_DISTANCE0`. The graphics pipeline interpolates that value and
clips triangles along its zero crossing before rasterization.

This is not deletion from `TetMesh` or `SurfaceMesh`. Toggle the plane off and
the same GPU buffers draw the entire mesh again. It also does not construct a
CPU cross-section mesh for the fitting algorithms.

### 11.4 The cap uses stencil parity, not a polygon-intersection mesh

Clipping a closed shell leaves an opening. How can the renderer fill an
arbitrarily shaped opening using only a square?

The **stencil buffer** supplies a per-sample mask. The idea is parity: from a
point inside a bounded closed solid, a generic ray crosses the boundary an odd
number of times; from outside, entries and exits occur in pairs. In the
clipped-shell view, this distinguishes samples whose plane intersection is
inside the solid.

**Pass A — mark:** `mark_cross_section` draws the clipped shell with:

- colour writes off;
- depth test and depth writes off, so hidden crossings are counted too;
- culling off, so both face directions count;
- stencil bit 0 inverted for every surviving fragment/sample.

Starting from zero, an odd number of inversions leaves bit 0 equal to one.
An even number leaves it zero. No visible colour has been drawn by this pass.

**Pass B — shell:** `draw_surface` draws the ordinary clipped surface with the
stencil test disabled, preserving the mask.

**Pass C — fill:** `fill_cross_section` draws a large quad lying in the plane,
but permits colour only where stencil bit 0 is one. Most of the square is
rejected; its surviving pixels look like the cross-section. The cap's depth
test still respects other geometry.

The quad is two triangles, not an OpenGL “quad primitive.” `DynamicQuads` uses
`glDrawArrays` over six sequential vertices for it. The smaller handle has
another six vertices and a four-vertex outline.

Important details when reading the state:

- `wants_cap()` requires both the user's cap toggle and `closed()`. Parity needs
  a closed boundary to represent a solid reliably.
- The frame opens the stencil write mask before clearing; clears respect write
  masks.
- The fill zeros the bit on stencil failure, depth failure, and success over
  its covered region, preparing it for subsequent layers.
- Clipping is disabled for the cap: its shader does not write clip distance,
  and its vertices already lie on the plane.
- A slight polygon offset and `LEQUAL` help the coplanar cap avoid flickering.
- “Solid” makes the cap opaque even if the shell is translucent. Cap depth
  writing has an additional exception for ordinary debug channels, explained
  in section 13.

Disabling the stencil test is the convention used for the shell pass. More
generally, setting all stencil operations to `GL_KEEP` also preserves stencil
values; `KEEP` itself is not a write that destroys the mask.

### 11.5 Elements are a CPU selection followed by GPU drawing

**Open:** [src/scene/draw_objects/element_slice.cpp](../src/scene/draw_objects/element_slice.cpp)
and [indexed_draw.h](../src/scene/draw_objects/indexed_draw.h).

The call path is:

```text
Renderer::draw
  -> Scene::update_element_slices
     -> Layer::update_slice
        -> ElementSlice::update
```

The plane is in scene coordinates, but the CPU mesh remains in physical
coordinates. Substituting `x_scene = x_mesh - origin` into the equation gives:

```text
dot(n, x_scene) + w_scene
  = dot(n, x_mesh) + (w_scene - dot(n, origin))

w_mesh = w_scene - dot(n, origin)
```

`Layer::update_slice` performs exactly that conversion. Shifting one equation
is cheaper than making another shifted CPU copy of every vertex.

`ElementSlice::update` then:

1. Records the sign of the plane expression for each CPU vertex.
2. Selects tets having at least one strictly positive and one strictly negative
   corner. Merely touching the plane does not select a tet.
3. Emits all four triangle faces of each selected tet using `TetMesh::kFaces`.
4. Uploads those vertex indices through `IndexedDraw`.

`IndexedDraw` owns a VAO and an index buffer, but **borrows the layer's VBO**.
One selected tet needs 12 indices, or 48 bytes, with no extra positions.
`draw_elements` renders the selected tets filled and outlined with clipping
disabled: the goal is to see whole elements straddling the plane.

The selection remembers its plane equation and reuses the previous indices if
it is unchanged. Mesh movement separately calls `slice.clear()` in
`Layer::refresh`, invalidating the selection even when the plane stayed still.

`IndexedDraw::upload` uses buffer **orphaning**: a null-data `glBufferData`
re-specifies storage before refilling it. This allows the driver to give the
CPU fresh storage while an earlier GPU draw may still be reading the old
contents. The buffer handle, and therefore its VAO association, stays the same.

## 12. Following UI actions into computation

Now you know the return path from changed data to pixels. This is the right
point to follow a button into the mesh code.

### 12.1 Moving a surface with the position widget

In `physics_tab`, the position shown is the surface's **current bounding-box
centre in physical coordinates**. It is measured again instead of remembered
as an independent UI position.

```text
physics_tab: DragFloat3("position", ...)
  -> calculate the user's delta
  -> document_.mutable_surface(mesh_id)
  -> translate(mesh, delta)
     -> mesh.positions_mut(): bump revision
     -> add delta to every CPU position
  -> later in App::run: scene_.sync(document_)
     -> Layer::refresh -> GpuMesh::update_positions
  -> Renderer::draw
```

The widget uses a float display value, so the code applies `shown - before`,
the user's edit, rather than treating the float conversion itself as mesh
motion. The mesh stays double precision.

Notice what this is not doing: it does not store a separate model translation
in the layer. The surface has really moved, so geometry queries and fitting
will see the new positions too.

### 12.2 Choosing a velocity field and pressing running

`FieldUi` remembers controls per mesh: field kind, velocity, axis, rate, period,
and box settings. The fields themselves are immutable objects, so editing those
controls calls `apply_field`, which builds a new field and attaches it to
`SurfacePhysics`. Choosing “none” detaches it without rewinding.

Then each running frame follows:

```text
SurfacePhysics::step(document_)
  -> for each enabled body, request its mutable SurfaceMesh
  -> advance(mesh, field, time, dt, integrator)
     -> integrate each vertex under field.velocity(position, time)
  -> advance the shared clock
```

Read [src/physics/surface_physics.cpp](../src/physics/surface_physics.cpp) before
the fitting solver: `translate` and `advance` are small, complete examples of
an algorithm modifying real mesh data. Their result appears through the same
revision/sync path as a manual position edit.

The driver's stored `rest` positions support rewind. Replacing an attached
field preserves that body's original rest snapshot. The UI “rewind” calls
`rewind`, but does not turn off `running`; the frame can therefore advance
again immediately afterwards.

### 12.3 Clicking conform

In `fitting_tab`, first find:

```cpp
ConformPipeline::Params &p = pipeline_.params;
```

All the parameter widgets below it edit the pipeline's real parameter object.
They do not run the solver. The work starts at:

```cpp
if (ImGui::Button("conform"))
{
  std::string err;
  // error reporting and statistics omitted here
  pipeline_.run(document_, err);
}
```

The mesh pickers select a volume as the **domain** and a surface as the
**target**. Initially `auto_select` chooses the first of each kind. Selecting
them identifies the pair; it does not align their coordinates automatically.

The high-level computation is:

| Stage | Main effect |
|---|---|
| Prepare | Capture the initial domain positions, or restore the existing snapshot; reset labels/debug data |
| Pre-deform | Move domain vertices to concentrate resolution near the target |
| Classify | Assign inside/outside labels to domain tets |
| Repair | Change problematic labels to improve the interface's manifoldness |
| Fit | Move domain vertices so the labelled interface approaches the target |
| Report | Count the result and record timings/diagnostics |

`run` executes the first pass from Prepare through Report. Subsequent passes
start at Classify, keeping the geometry fitted by the preceding pass. The
stage-range controls call `run_stages` directly to work on a portion of this
sequence.

The connection back to drawing has **two branches**:

```text
pipeline_.run(document_, err)
  +-- changed domain positions/revision
  |     -> scene_.sync(document_)
  |     -> refresh the layer's position buffer
  |
  +-- changed named debug index lists
        -> scene_.sync_debug_overlay(pipeline_.debug())
        -> refresh overlay index buffers

both -> renderer_.draw(...)
```

### 12.4 Why you see the result rather than each solver iteration

`pipeline_.run` is called from `draw_ui`, and does not return until its work is
finished. During it, the outer frame loop cannot poll events, synchronize the
scene, draw panels, or swap buffers.

The solver's accepted iterations can bump the mesh revision many times, but a
revision is just a freshness counter. It does not invoke the renderer. All
those updates are observed together when control eventually returns to the
frame loop.

Likewise, running surface physics does not automatically run conform. In this
viewer they are separate operations. If physics is enabled when conform
finishes, the regular physics step later in that same frame can move the
target after the fit used it.

## 13. How algorithm results become debug overlays

**Open:** [src/core/debug_draw.h](../src/core/debug_draw.h),
[src/scene/draw_objects/debug_overlay.cpp](../src/scene/draw_objects/debug_overlay.cpp),
then `Renderer::draw_debug_overlay`.

An algorithm should be able to say “these faces form the interface” without
including OpenGL headers. It publishes a **named channel of indices** instead:

```text
DebugDraw in the pipeline                  DebugOverlay in the scene
------------------------                  -------------------------
name                                      same name
mesh_id                                   source layer's VBO
primitive: points/lines/triangles   --->    GL primitive + IndexedDraw
indices                                   colour, enabled, in_front
```

The producer specifies **what was found**. The scene specifies **how to display
it**. Inspect's controls edit the latter; they do not rerun the algorithm.

### 13.1 Follow the interface channel

In `ConformPipeline::refresh_interface`:

1. `interface_faces` derives `(tet, local face)` pairs from the labels.
2. Each pair is expanded through `TetMesh::kFaces` to three mesh vertex indices.
3. Those indices populate the `"interface"` triangle channel.

The scene resolves its `mesh_id` to a layer, attaches an `IndexedDraw` to that
layer's position buffer, and uploads the index list. No second set of interface
positions is needed. When fitting moves the domain VBO, the interface follows
because it indexes those same vertices.

The index identity is between the **built CPU mesh and its GPU projection**.
With the viewer's default Morton ordering, these are not necessarily the
original file's vertex numbers.

### 13.2 Channel freshness and appearance

`DebugDraw::channel` increments a revision and returns the channel's vector.
For an existing channel, it does **not** empty the vector. A producer replacing
a result must clear or assign it, as `refresh_interface` does. `reset` empties
all channels while keeping their identities.

The overlay sync uses the overall debug revision: if it changed, it walks and
uploads the channels. It is not a separate dirty flag per channel. A frame
with an unchanged debug revision skips this work. Fill channels before the
sync; later edits through an old vector reference alone do not bump a revision.

The same channel name in the same slot retains its viewer colour and toggles
across syncs. Inspect builds its rows by iterating these items; a pipeline
channel needs no separately registered UI control.

### 13.3 What “in front” actually does

Ordinary debug channels draw after the meshes with depth testing/writing on,
`LEQUAL`, no blending, and the cutting plane applied. A hidden layer can still
supply their positions; it does not have to draw its own shell.

Depth still matters. An opaque shell can hide its internal channels. A layer
with an enabled, nonempty ordinary channel draws its **cap** without writing
depth, so that cap alone does not hide the channel behind the cut. This
exception does not disable the shell's depth writing.

For channels marked **in front**, the renderer clears the existing depth
buffer before drawing that group. It preserves the scene's colour image, then
draws the group over it using a fresh depth buffer. The channels can still
occlude each other and themselves. It is a depth clear in the same framebuffer,
not a second offscreen framebuffer and not simply disabling depth tests.

`kOverlayFS` selects shading with `uMode`: flat colour for lines, derivative
lighting for triangles, and a round shaded point sprite for points. Points
use `gl_PointCoord` to discard the square corners. Their “sphere” look is a
shading effect; the code does not draw sphere meshes or compute sphere depth.

For examples of publishing your own debug channels, continue with
[debug-drawing.md](debug-drawing.md).

## 14. The next reading path: geometry

You now have the two interfaces between the viewer and the computation:

1. **Mesh positions and revisions:** the authoritative data and its freshness.
2. **Debug index lists:** subsets of that data that an algorithm wants to show.

Go into geometry from those interfaces, rather than starting with the largest
solver file.

### Step 1: Understand a mesh's vocabulary

Read [src/mesh/surface_mesh.h](../src/mesh/surface_mesh.h) and
[src/mesh/tet_mesh.h](../src/mesh/tet_mesh.h).

Separate these three concepts:

- **Geometry:** vertex positions; these can move.
- **Connectivity:** which vertices make each triangle/tet; fixed for a built
  mesh.
- **Adjacency:** which elements meet across an edge/face, and which elements
  touch a vertex; derived once from connectivity.

Follow `tet(t)`, `neighbor(t,f)`, `boundary_faces`, and `tets_around` before
reading how the adjacency hash table is implemented. A face number is local to
a tet; a vertex index addresses the mesh's position array. `kFaces` is the
conversion you already saw in slices and overlays.

Questions to answer before moving on: why does moving a vertex leave adjacency
valid, and why can that same motion invalidate geometric queries or a slice?

### Step 2: Read one small mesh-changing operation

Read `translate`, then `advance`, in
[src/physics/surface_physics.cpp](../src/physics/surface_physics.cpp), and the
`velocity` interface in
[src/physics/deformation_field.h](../src/physics/deformation_field.h).

You already know how their writes reach the screen. You can now focus on the
numerical operation without carrying UI or GL details through it.

### Step 3: Read the public geometry queries

Read [src/geometry/surface_queries.h](../src/geometry/surface_queries.h), then
its small [implementation](../src/geometry/surface_queries.cpp).

It answers two main questions:

- **Nearest point:** where on the target surface is a query point closest?
- **Winding number / inside:** is a query point inside the target?

`Document::surface_queries(id)` lazily creates this object and brings it up to
date. It uses the same revision idea as the viewer. The two consumers of a moved
surface are now easy to compare:

```text
surface mesh revision changed
  +-- Layer::refresh             update float GPU positions for drawing
  +-- SurfaceQueries::update     update acceleration structures for queries
```

The GPU projection is not used as the input to these geometric algorithms.
Their positions remain in physical coordinates and double precision.

### Step 4: Go below the query interface

Read [src/geometry/surface_bvh.h](../src/geometry/surface_bvh.h) for nearest-point
search. A **BVH**, bounding volume hierarchy, groups triangles into a tree of
boxes. If a box is already farther away than the best candidate, everything
under it can be skipped. Begin with the `Nearest` result and `nearest` method,
then read build/refit and traversal.

Then read [src/geometry/winding_number.h](../src/geometry/winding_number.h).
For an outward-oriented closed surface, the generalized winding number is
ideally one inside and zero outside; hierarchical approximation accelerates
its evaluation. Orientation matters: reversing the surface changes the sign.
This geometric winding number is a different calculation from the screen-space
one-bit stencil parity used for a cap.

`SurfaceQueries::update` refits moved geometry and can rebuild when the
hierarchy's cost has drifted sufficiently. **Refitting a BVH**, **framing the
camera with `App::refit`**, and **fitting the domain to the target** are three
different operations despite the similar names.

### Step 5: Return to the conform button and follow one stage

Read [src/pipeline/conform_pipeline.h](../src/pipeline/conform_pipeline.h), then
`run`, `run_stages`, and `classify` in its
[implementation](../src/pipeline/conform_pipeline.cpp).

Start with Classify because it connects readable inputs and outputs:

```text
TetMesh + current SurfaceQueries + parameters
  -> dual_min_cut
  -> one inside/outside label per tet
  -> interface_faces
  -> debug interface channel
  -> the picture you already understand
```

Read [src/algorithms/dual_min_cut.h](../src/algorithms/dual_min_cut.h) before
its implementation. Then follow Repair into
[src/algorithms/interface_repair.h](../src/algorithms/interface_repair.h), and
the vertex-moving stages into
[src/optimization/mesh_deformer.h](../src/optimization/mesh_deformer.h).

At this point you can ask a precise question of each function: does it change
positions, labels, a derived query structure, or just the view? That distinction
removes much of the apparent complexity.

The build confirms this separation: [CMakeLists.txt](../CMakeLists.txt) puts
physics and the mesh/geometry/fitting code in `mesh_dev_core`. The `mesh-dev`
executable adds `app/`, `scene/`, and `render/`. Core algorithms do not need a
window or OpenGL context.

## 15. Exercises and a navigation cheat sheet

### Small experiments while reading

Use a volume and a surface that you already load in the viewer. These exercises
are intended to connect a visible effect to a short call path.

1. **Change a colour.** Follow `ColorEdit3` to `Layer::color`, then to
   `surface_.set("uColor", ...)`. Explain why `GpuMesh::update_positions` is
   unnecessary.
2. **Orbit, then move the surface's position.** The first changes camera
   matrices; the second changes CPU vertices and the mesh revision. Find those
   two paths and identify where each reaches the GPU.
3. **Enable the plane and disable the cap.** The opening looks hollow. Enable
   the cap again, then enable elements. Identify which of the four cut features
   changed each time.
4. **Make a shell translucent and change layer order.** Read the alpha blend
   expression and explain why the pixels can change without moving a vertex.
5. **Run conform, then hide the domain layer.** Inspect the available channels.
   Their positions still come from the hidden layer's VBO. Compare ordinary
   depth behavior with “in front.”

### Useful debugger stopping points

| What you are tracing | Start here | Then follow |
|---|---|---|
| Entire application | `main`, `App::run` | initialization, then one iteration of the frame loop |
| A panel action | Its `if (ImGui::Button(...))` or widget call | the field or method it changes |
| Camera movement | `App::on_mouse_button`, `App::on_cursor_pos` | `Camera::orbit`, `pan`, `dolly` |
| A moved mesh appearing | `Layer::refresh` | compare `entry.revision()` and `uploaded_revision`, then position upload |
| An unexpected draw result | `Renderer::draw_layer` | classification, chosen passes, `GlState`, shader, VAO |
| A cut element selection | `Layer::update_slice` | coordinate conversion, then `ElementSlice::update` |
| Fitting entering geometry | `ConformPipeline::classify` or `fit` | query acquisition, algorithm arguments, outputs |

A breakpoint inside an ordinary frame function is hit continuously. Use a
condition such as a changed revision or a particular mesh ID to stop at the
interesting update rather than every redraw.

### Where would a change belong?

| Desired change | First place to inspect |
|---|---|
| Different arrangement of controls | `App::model_window`, `inspect_window`, `view_window` |
| Another mouse gesture or key | `App::install_callbacks` and `on_*` in `input.cpp` |
| Different camera behavior | `Camera` |
| A display setting for each mesh | `Layer`, its widget in `ui.cpp`, and its consumer in `Renderer` |
| A fitting parameter | `ConformPipeline::Params`, its widget, and the stage that reads it |
| Highlight some existing mesh vertices/faces | A `DebugDraw` channel |
| Different surface lighting | `kSurfaceFS` |
| A new per-vertex value for a shader | Attribute layout/buffers plus shader inputs; a uniform is not per-vertex |
| Different geometry or connectivity | CPU mesh construction/algorithms, then synchronize the view |

### Questions that often cause confusion

**Why is `draw_ui` allowed to change meshes?** It is an `App` method coordinating
user actions. The geometry operation it invokes lives separately, such as
`translate` or `pipeline_.run`.

**Why is there OpenGL in `scene/` as well as `render/`?** The scene owns GPU
representations and uploads them. The renderer decides how to draw them: shader,
pass order, and graphics state.

**Why does `Renderer::draw` take a non-const scene?** It refreshes element-slice
caches before drawing. Drawing the view does not change the authoritative CPU
mesh positions.

**Why did hiding a mesh not remove its algorithm result?** Layer visibility and
debug-channel visibility are independent. The channel needs the layer's vertex
buffer, not its visible shell.

**Why does a correct revision counter not give a live solver animation?** The
frame loop must regain control to synchronize and render. The current conform
call blocks it until completion.

**What should I remember without reopening this guide?**

```text
App handles input and coordinates actions.
Document owns the real meshes.
Scene holds their current drawable views and display settings.
Renderer turns those views into pixels.

widget/event -> C++ state -> synchronize changed data -> draw -> present
```
