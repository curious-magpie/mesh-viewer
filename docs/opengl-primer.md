# OpenGL, for someone who knows meshes but not graphics

You already know more geometry than this document contains. What is unfamiliar is not
the maths — it is that a GPU is **not a processor you write a program for**. It is a
fixed assembly line that you *configure*, feed buffers into, and then let run. Two small
programs of yours get slotted into two specific stations on that line. Everything else is
switches.

Almost every confusing thing about OpenGL follows from that one fact.

This document explains the pipeline and then points at exactly where each idea lives in
this repository, so you can read the concept and the code together.

**Contents**

1. [The mental model](#1-the-mental-model)
2. [Getting geometry onto the GPU](#2-getting-geometry-onto-the-gpu)
3. [The pipeline, stage by stage](#3-the-pipeline-stage-by-stage)
4. [Coordinate spaces](#4-coordinate-spaces)
5. [Shaders](#5-shaders)
6. [Rasterisation and interpolation](#6-rasterisation-and-interpolation)
7. [The depth buffer](#7-the-depth-buffer)
8. [Winding and culling](#8-winding-and-culling)
9. [Transparency](#9-transparency)
10. [The stencil buffer](#10-the-stencil-buffer)
11. [Clip distances](#11-clip-distances)
12. [Screen-space derivatives](#12-screen-space-derivatives)
13. [Antialiasing](#13-antialiasing)
14. [Where each idea lives in this code](#14-where-each-idea-lives-in-this-code)

---

## 1. The mental model

Three things to internalise.

**OpenGL is a state machine.** There is no `draw(mesh, shader, settings)` call. Instead
you *bind* a shader, *bind* buffers, *enable* depth testing, *set* a blend function — all
into one big global context — and then say "go". The draw call itself carries almost no
information; everything that matters was set beforehand and is still set afterwards.

This is why leaked state is the classic bug: a pass that forgets to turn culling back off
breaks the *next* pass, which looks innocent. It is why `src/render/gl_state.h` exists —
every pass in this renderer fills in a complete `GlState` and applies all of it, rather
than setting only what it thinks has changed.

**Work is per-vertex or per-fragment, and there are vastly more fragments.** A 1080p
window is 2 million pixels. Anything you can compute once per vertex, compute per vertex.

**The GPU runs thousands of invocations in lockstep, with no ordering guarantee you can
rely on.** Two triangles may be shaded simultaneously. You cannot "read what has been
drawn so far" in a shader. Anything that needs to combine results across primitives has
to be done by the fixed-function stations — the depth test, the stencil test, blending —
which *are* ordered and atomic. Several tricks in this codebase exist only because of
that constraint.

---

## 2. Getting geometry onto the GPU

Three objects, and their names are unhelpful. In `src/scene/gpu_mesh.cpp`:

**VBO — Vertex Buffer Object.** A flat array of bytes in GPU memory. It has no idea what
is in it. Here it is `n` consecutive `vec3` positions.

**Vertex attributes.** The description of how to read that array. `glVertexAttribPointer`
says: *attribute 0 is 3 floats, not normalised, stride 12 bytes, starting at offset 0.*
That is what connects the bytes to `layout(location = 0) in vec3 aPos` in the shader.

```
VBO bytes:  [x0 y0 z0][x1 y1 z1][x2 y2 z2] ...
             \_______/
              stride 12, 3 floats -> attribute 0 -> aPos
```

**IBO — Index Buffer Object** (also called EBO). Triangles as indices into the vertex
array rather than repeated vertices. A shared vertex is stored once, and — importantly
for us — the GPU caches shaded vertices, so a shared vertex is often *shaded* once too.
Our 105 k-triangle hull has far fewer than 315 k vertices.

**VAO — Vertex Array Object.** A recording of the above: which VBO, what the attribute
layout is, which IBO. Binding one VAO restores all of it. Think of it as a saved
configuration, not as data.

```cpp
glBindVertexArray(vao);                     // "use this configuration"
glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, nullptr);
```

That second line is the entire draw call. Everything else was set up earlier.

> **In this repo:** `GpuMesh` owns exactly this triple and frees it in its destructor, so
> `Layer` can hold one by value and nothing has to remember to release GL names.

---

## 3. The pipeline, stage by stage

```
  vertex buffer
       |
       v
  [ VERTEX SHADER ]        <- your code, once per vertex
       |                      outputs gl_Position (clip space) + varyings
       v
  primitive assembly       groups vertices into triangles
       |
       v
  clipping                 cut against the view frustum, and against any
       |                   user clip planes you enabled  (section 11)
       v
  perspective divide       xyz /= w     -> normalised device coordinates
       |
       v
  viewport transform       NDC -> pixels, and z -> [0,1] depth
       |
       v
  RASTERISATION            which pixels does this triangle cover?
       |                   for each, interpolate the varyings
       v
  [ FRAGMENT SHADER ]      <- your code, once per covered pixel
       |                      outputs a colour
       v
  stencil test             (section 10)
       |
       v
  depth test               (section 7)
       |
       v
  blending                 (section 9)
       |
       v
  framebuffer
```

A *fragment* is a candidate pixel: one triangle's contribution at one pixel. Several
fragments can land on the same pixel; the tests and blending decide what survives.

Note the fragment shader runs **before** the depth and stencil tests. Hardware usually
cheats and does the depth test early to skip shading, but it may only do so when your
shader cannot change the outcome — which is why `discard` in a fragment shader is
expensive and why this codebase avoids it (section 11).

---

## 4. Coordinate spaces

This is the part worth getting straight once. A vertex passes through these:

| space | what it means | how you leave it |
|---|---|---|
| **object** | coordinates as stored in the file | multiply by the *model* matrix |
| **world** | a common frame for the whole scene | multiply by the *view* matrix |
| **view** (eye) | camera at the origin, looking down **−Z** | multiply by the *projection* matrix |
| **clip** | 4D, pre-divide; this is what `gl_Position` is | divide by `w` (the GPU does it) |
| **NDC** | the cube `[-1,1]³`; everything outside is off-screen | viewport transform (the GPU does it) |
| **window** | pixels, plus depth in `[0,1]` | — |

**This codebase has no model matrix.** Coordinates are baked into world space when a
layer is built, because `build_layer` subtracts a scene-wide origin from every vertex
anyway (see section 12 for why). So the vertex shader is just:

```glsl
vec4 vp = uView * vec4(aPos, 1.0);   // world -> view
gl_Position = uProj * vp;            // view  -> clip
```

**The view matrix** is not a camera position, it is the inverse of one: it moves the
*world* so that the camera ends up at the origin looking down −Z. `glm::lookAt` in
`Camera::view()` builds it.

**The projection matrix** does something sneakier. A perspective projection puts `-z`
into the `w` component. Then the divide by `w` is what produces foreshortening — distant
things have larger `w` and shrink. That is the whole trick of perspective, and it is why
`gl_Position` is 4D and why the divide is a separate stage.

```
clip space:  (x, y, z, w)   with w ≈ distance from the eye
                  |  divide
                  v
NDC:         (x/w, y/w, z/w)      all in [-1, 1] if visible
```

The divide also means **interpolation has to be perspective-corrected** — see section 6.

---

## 5. Shaders

A shader is a small program in GLSL (C-like) compiled by the driver at runtime. Two
stages matter here.

**Vertex shader** — runs once per vertex. Must write `gl_Position`. May write extra
outputs (*varyings*) for the fragment shader.

**Fragment shader** — runs once per covered pixel. Reads the interpolated varyings, must
write a colour.

Three kinds of input:

- `in` in the vertex shader — a **vertex attribute**, different for every vertex, read
  from the VBO. Here: `aPos`.
- `uniform` — constant for the whole draw call, set from C++ before drawing. Here:
  `uView`, `uProj`, `uColor`, `uPlane`.
- `in`/`out` pairs between stages — **varyings**, interpolated across the triangle.
  Here: `vViewPos`.

Our surface fragment shader (`src/render/shaders.h`) is a textbook Blinn–Phong:

```glsl
float diff = max(dot(N, L), 0.0);              // Lambert: brightness ∝ cos(angle to light)
float spec = pow(max(dot(N, H), 0.0), 32.0);   // Blinn-Phong highlight; 32 = tightness
vec3 c = uColor.rgb * (0.30 + 0.70 * diff) + vec3(0.18) * spec;
```

`L` is a fixed direction in **view** space, so the light is attached to the camera — it
turns with you, like a head torch. That is deliberate for an inspection tool: nothing is
ever in shadow just because you orbited. The `0.30` is flat ambient so faces pointing away
are dim rather than black. `H`, the halfway vector between light and view, is the Blinn
half of Blinn–Phong.

---

## 6. Rasterisation and interpolation

The rasteriser finds the pixels a triangle covers, and for each one interpolates every
varying from the three vertex values using **barycentric coordinates** — the same
weights you would use for a P1 finite element shape function on a triangle. Familiar
ground.

One wrinkle: interpolating linearly in *screen* space is wrong under perspective, because
the perspective divide is not affine. The GPU therefore interpolates `value/w` and `1/w`
linearly and divides at the end. This is automatic — but it is the reason `gl_Position`
is kept 4D through rasterisation, and the reason `noperspective` exists as an opt-out.

---

## 7. The depth buffer

One float per pixel, holding the depth of the nearest thing drawn so far. Each fragment
compares its own depth against it:

```
if (fragment_depth < stored_depth) { keep it; store the new depth; }
else                               { throw it away; }
```

That is `glDepthFunc(GL_LESS)`, the default, and it is what makes drawing order not
matter for opaque geometry.

Two controls, and this codebase uses both independently:

- **depth test** — do I compare at all?
- **depth write** (`glDepthMask`) — do I update the buffer if I pass?

Translucent surfaces test but do **not** write, so that a translucent layer does not
prevent a layer behind it from being drawn.

### Precision, and why `znear` is the number that matters

Depth stored in the buffer is *not* linear in distance. After the perspective divide it
is distributed like `1/z`, so resolution is concentrated near the camera:

```
depth buffer resolution

near |################################|         | far
     |<- half your precision is here ->|
     
     znear                                       zfar
```

The consequence is unintuitive but important: **the ratio `zfar/znear` sets your
precision, and pushing `znear` closer to zero destroys it.** A `znear` of 0.001 with a
`zfar` of 1000 is far worse than 1 and 1000.

`Camera::fit()` therefore derives both from the size of what you loaded:

```cpp
znear = radius * 1e-3f;
zfar  = distance + radius * 8.0f;
```

so a 100 m domain and a 1 mm one both get a usable ratio. A fixed `znear = 0.01` would
z-fight badly on small meshes.

### z-fighting and polygon offset

When two surfaces are coplanar, their depths are equal to within rounding and which one
wins flickers per pixel as the camera moves. `glPolygonOffset` nudges a polygon's depth by
a constant plus a term proportional to its slope. This renderer uses it for the
cross-section fill, which is coplanar with the clipped edge of the geometry around it.

> Caveat worth knowing: polygon offset applies only to **polygons**. It does nothing to
> `GL_LINES` primitives. If you ever need to lift lines off a surface, bias them in the
> shader instead.

---

## 8. Winding and culling

After projection, a triangle's vertices go round the screen either clockwise or
counter-clockwise. The GPU computes the signed area and calls counter-clockwise
**front-facing** by default. `glEnable(GL_CULL_FACE)` then throws away the back ones
before shading.

For a closed surface this is free: you can never see the inside, so half the triangles
are wasted work.

**But it depends entirely on the file having consistent winding**, which neither Gmsh nor
OBJ guarantees. If half the triangles are wound backwards, culling punches holes in the
mesh. So this codebase *checks*: `analyse_topology` in `src/scene/layer.cpp` walks every
directed edge, and a surface is `oriented` only if each edge is traversed once in each
direction. Culling is enabled only for layers that pass.

For the boundary of a tetrahedral mesh we do not need the test — the extraction generates
the winding from each tet's own orientation, so it is correct by construction.

---

## 9. Transparency

Blending combines the incoming fragment with what is already in the framebuffer:

```
result = src_colour * src_factor + dst_colour * dst_factor
```

With the usual `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`:

```
result = src * α + dst * (1 - α)
```

**This is not commutative.** Drawing A then B gives a different result from B then A. So
translucent geometry must be drawn back to front, and with depth writes off (or a nearer
translucent surface would stop a farther one from ever being drawn).

Hence the frame order in `Renderer::draw`: all opaque layers first — they write depth and
correctly occlude everything behind them — then the translucent ones.

**Why the panel lets you reorder layers by hand.** The textbook fix is to sort objects
back to front by distance. That fails here, because these meshes *nest*: a domain encloses
the surfaces inside it, so parts of it are in front of them and parts behind. No ordering
of whole objects is correct, and sorting by centroid just flickers as you orbit. A manual
order is both simpler and better behaved. (The real fix, order-independent transparency,
is a much bigger hammer.)

There is one extra trick in `draw_surface`: for a translucent *closed* layer, it draws
back faces first and front faces second, in two draw calls. That resolves the shell
blending against itself, which is the most visible artifact on a closed mesh.

---

## 10. The stencil buffer

An extra integer per pixel — usually 8 bits — that you can test and modify with rules
independent of colour and depth. It is a general-purpose per-pixel scratch pad, and it is
how you compute things that need information *across* primitives, which a shader cannot
do (section 1).

For each fragment you specify:

- a **test**: compare `ref` against the stored value (`GL_EQUAL`, `GL_NOTEQUAL`, …)
- three **actions**, for stencil-fail, depth-fail and pass: keep, replace, increment,
  decrement, zero, or **invert**.

### How this codebase uses it: capping the cut

The cutting plane deletes geometry. That leaves a closed volume looking hollow — you see
through the opening onto the *inside* of its far wall. To make it read as solid, the
opening must be filled in. The problem is knowing the shape of that opening, which is an
arbitrary polygon depending on the mesh and the plane.

The trick is to not compute it at all, but to let the rasteriser answer per pixel:

```
                        cutting plane
                             |
          eye  ------------->|----[==== solid ====]------>   ray A: 1 crossing (odd)
                             |
          eye  ------------->|         [==]                  ray B: 2 crossings (even)
                             |
```

Clipping has already removed everything in front of the plane, and a ray from the eye
crosses the plane exactly once. So for a pixel whose plane-crossing lies **inside** the
solid, the surviving geometry along that ray presents an odd number of surfaces; for a
pixel outside, they pair up into an even number.

Odd/even is one bit, and `GL_INVERT` counts it:

1. Draw the layer with colour and depth writes **off**, stencil op `GL_INVERT`. Depth
   testing must be off too, so that hidden surfaces are still counted.
2. Draw a quad covering the plane, stencil test `GL_EQUAL 1`. It survives only where the
   parity came out odd — exactly the cross-section.

Parity is used rather than the more common winding-number count (increment on back faces,
decrement on front) precisely because it does **not** care about triangle winding or draw
order, neither of which the mesh formats guarantee.

Two details that silently break it, both commented in `renderer.cpp`: `glClear` is masked
by `glStencilMask`, so the mask must be opened before clearing; and pass 2 must *disable*
the stencil test rather than set `GL_KEEP`, because disabling it is what suppresses
stencil writes.

---

## 11. Clip distances

You can cut geometry against an arbitrary plane in two ways.

**`discard` in the fragment shader.** Simple, and wrong for this job: any shader
containing `discard` may prevent the hardware from doing its early depth rejection, for
the whole draw call, because it can no longer know whether a fragment will survive. On a
multi-million-triangle mesh that is a large cost.

**`gl_ClipDistance`.** The vertex shader writes a signed distance; the GPU clips the
geometry **before rasterisation**, at the same station that clips against the view
frustum.

```glsl
gl_ClipDistance[0] = dot(uPlane.xyz, aPos) + uPlane.w;   // negative = cut away
```

plus `glEnable(GL_CLIP_DISTANCE0)` on the C++ side. Because the plane equation is affine
in position, interpolating it across a triangle is exact, so the cut edge is geometrically
exact rather than per-pixel — and MSAA can antialias it, because it is a real edge.

> **Trap:** enabling `GL_CLIP_DISTANCE0` while the bound shader does *not* write
> `gl_ClipDistance[0]` is undefined behaviour. This renderer disables it around the cap
> quad and the gizmo for exactly that reason.
>
> A second trap, found the hard way and commented in the shader: redeclaring
> `out float gl_ClipDistance[1];` is legal GLSL but on Mesa links to a variable the
> clipper never reads. Everything reports success and nothing is clipped.

---

## 12. Screen-space derivatives

This is the one genuinely strange feature, and this renderer leans on it hard.

Fragments are shaded in **2×2 quads**, always, even at a triangle's edge where some of
the four are not actually covered (those run anyway as *helper invocations* and their
output is discarded). Because the hardware has four neighbouring fragments in hand, it
can give you finite differences of any value across them:

```
  dFdx(v) = v(right neighbour) - v(left)
  dFdy(v) = v(above)           - v(below)
```

They exist mainly so textures can pick a mip level. But if you take the derivatives of
**position**, you get two vectors lying in the surface — and their cross product is the
surface normal:

```glsl
vec3 g = cross(dFdx(vViewPos), dFdy(vViewPos));
```

For a planar triangle this is not an approximation; it is exact, and constant across the
triangle. Which means **this viewer stores no normals at all** — no normal attribute, no
normal buffer, 12 bytes per vertex saved on meshes with millions of them, and flat
per-element shading, which is what you want when inspecting an FEM mesh anyway.

Three caveats, all handled in `kSurfaceFS`:

- The result always points toward the eye, for front and back faces alike. That is why
  looking into a cut-open volume still shows lit interior walls. Do **not** try to flip it
  with `gl_FrontFacing`.
- On a sliver triangle the two derivatives can be parallel, giving a zero-length cross
  product and a NaN from `normalize`. Hence the `len > 1e-12` guard.
- **Precision.** `dFdx` of a large coordinate is a difference of two nearly equal large
  numbers — catastrophic cancellation, which shows up as speckled shading. Two defences:
  differentiate the **view-space** position (camera-centred, so magnitudes are small), and
  subtract a scene origin from every coordinate at load time. That is why `build_layer`
  recentres and why `Scene` carries an `origin`.

---

## 13. Antialiasing

`glfwWindowHint(GLFW_SAMPLES, 4)` requests 4× **MSAA**. The framebuffer keeps four depth
and coverage samples per pixel, but the fragment shader still runs **once** per pixel; the
result is written to whichever samples the triangle covered, and averaged at the end. So
you pay for geometric edges only, not for shading — which is why MSAA is the cheap choice
for a mesh viewer, where almost all the visible aliasing is on silhouettes and the cut
edge.

---

## 14. Where each idea lives in this code

| concept | file |
|---|---|
| State machine, one struct per pass | `render/gl_state.h` |
| VAO / VBO / IBO, indexed drawing | `scene/gpu_mesh.{h,cpp}` |
| Shader compilation, uniforms by name | `render/shader.{h,cpp}` |
| All the GLSL | `render/shaders.h` |
| View and projection matrices, near/far choice | `scene/camera.h` |
| The frame: pass order, opaque before translucent | `render/renderer.cpp`, `Renderer::draw` |
| Clip distance | `kSurfaceVS`, and `GlState::clip_plane` |
| Stencil parity cap | `Renderer::mark_cross_section` + `fill_cross_section` |
| Blending, two-pass shell | `Renderer::draw_surface` |
| Winding test that gates culling | `scene/layer.cpp`, `analyse_topology` |
| Screen-space derivative normals | `kSurfaceFS` |
| Recentring for float precision | `scene/layer.cpp`, `build_layer` |

---

## Further reading

- **[Learn OpenGL](https://learnopengl.com/)** — the best tutorial series; the *Getting
  Started* and *Advanced OpenGL* chapters cover everything above. (A copy is already on
  this machine under `~/phd/software/LearnOpenGL/`.)
- **[The OpenGL 3.3 core specification](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)**
  — the per-fragment operations chapter is the authority on the exact order of the tests.
- **[docs.gl](https://docs.gl/)** — per-function reference, much faster to search than the
  spec.
- *Real-Time Rendering* (Akenine-Möller et al.) — the standard textbook, if you want the
  theory properly rather than in an appendix like this one.
