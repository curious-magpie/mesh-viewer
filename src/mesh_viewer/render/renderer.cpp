// How a layer is drawn, and why it takes three passes.
//
// The cutting plane removes geometry with gl_ClipDistance, which leaves a
// closed volume looking hollow: you see straight through the opening into the
// inside of its far wall. To make it read as a solid cross-section, the opening
// has to be filled in. That fill is the "cap", and finding its shape is the
// whole trick:
//
//   1. mark_cross_section   draw the layer into the stencil buffer only, with
//                           GL_INVERT, so each pixel ends up flagged if the
//                           plane crosses the inside of the solid there
//   2. draw_surface         the visible geometry, clipped
//   3. fill_cross_section   a quad lying in the plane, drawn only where step 1
//                           left the flag set
//
// Why step 1 works: clipping has already removed everything in front of the
// plane, and a ray from the eye crosses the plane exactly once. So for a pixel
// whose plane-crossing lies inside the solid, the remaining geometry along that
// ray has one unmatched exit surface -- an odd number of crossings. For a pixel
// outside the solid, crossings pair up into an even number. GL_INVERT counts
// that parity in a single bit.
//
// Parity is used rather than the more common winding-number count because it
// does not care about triangle winding or draw order, neither of which the mesh
// formats guarantee.
#include "mesh_viewer/render/renderer.h"

#include <array>

#include "mesh_viewer/render/gl_state.h"
#include "mesh_viewer/render/shaders.h"

namespace mesh_viewer
{

namespace
{

// The single stencil bit the cap uses.
constexpr GLuint kCapBit = 0x01u;

// Layout of the vertex buffer rebuilt each frame by update_plane_geometry.
constexpr GLint kCapQuadFirst = 0;    // 6 verts, covers plane-intersect-scene
constexpr GLint kGizmoQuadFirst = 6;  // 6 verts, the translucent drag handle
constexpr GLint kGizmoLineFirst = 12; // 4 verts, its outline
constexpr GLsizei kPlaneVertexCount = 16;

// How large a debug overlay's point channels are drawn, in pixels.
constexpr float kPointSize = 9.0f;

} // namespace

bool Renderer::init(std::string &err)
{
  const struct
  {
    ShaderProgram *program;
    const char *vs;
    const char *fs;
    const char *name;
  } programs[] = {
      {&surface_, kSurfaceVS, kSurfaceFS, "surface"},
      {&stencil_, kStencilVS, kStencilFS, "stencil"},
      {&cap_, kCapVS, kCapFS, "cap"},
      {&overlay_, kOverlayVS, kOverlayFS, "overlay"},
  };

  for (const auto &entry : programs)
  {
    std::string log;
    if (!entry.program->build(entry.vs, entry.fs, log))
    {
      err = std::string(entry.name) + " shader: " + log;
      return false;
    }
  }

  plane_quads_.allocate(kPlaneVertexCount);
  return true;
}

void Renderer::release()
{
  surface_.destroy();
  stencil_.destroy();
  cap_.destroy();
  overlay_.destroy();
  plane_quads_.release();
}

void Renderer::update_plane_geometry(const CutPlane &plane,
                                     const SceneBounds &bounds)
{
  const glm::vec3 anchor = plane.anchor(bounds.center);
  glm::vec3 u, v;
  plane.basis(u, v);

  // The cap only ever needs to cover where the plane meets the scene, and the
  // handle is deliberately smaller so it does not swamp the view.
  const float cap_half = bounds.radius * 1.05f;
  const float handle = handle_half(bounds);

  // The gizmo's outline has to trace its own fill exactly, so both are built
  // from one set of corners rather than two copies of the same arithmetic.
  const auto corners = [&](float h)
  {
    return std::array<glm::vec3, 4>{anchor - u * h - v * h,
                                    anchor + u * h - v * h,
                                    anchor + u * h + v * h,
                                    anchor - u * h + v * h};
  };
  const auto quad = [](glm::vec3 *dst, const std::array<glm::vec3, 4> &c)
  {
    dst[0] = c[0];
    dst[1] = c[1];
    dst[2] = c[2];
    dst[3] = c[0];
    dst[4] = c[2];
    dst[5] = c[3];
  };

  glm::vec3 verts[kPlaneVertexCount];
  const std::array<glm::vec3, 4> handle_corners = corners(handle);
  quad(&verts[kCapQuadFirst], corners(cap_half));
  quad(&verts[kGizmoQuadFirst], handle_corners);
  for (int i = 0; i < 4; ++i)
    verts[kGizmoLineFirst + i] = handle_corners[size_t(i)];

  plane_quads_.update(verts, kPlaneVertexCount);
}

// Pass 1: flag the pixels where the plane cuts through the inside of this
// layer. Writes neither colour nor depth -- only the stencil bit.
void Renderer::mark_cross_section(const Layer &l, const CutPlane &plane)
{
  GlState s;
  // Depth testing must be off: every surface along the ray has to be counted,
  // including ones hidden behind others. Occlusion is settled later, by the cap
  // quad's own depth test.
  s.depth_test = false;
  s.depth_write = false;
  s.cull = false; // both facings must flip the bit
  s.color_write = false;
  s.clip_plane = true;
  s.stencil.enabled = true;
  s.stencil.func = GL_ALWAYS;
  s.stencil.read_mask = kCapBit;
  s.stencil.write_mask = kCapBit;
  s.stencil.on_pass = GL_INVERT;
  s.apply();

  stencil_.use();
  stencil_.set("uPlane", plane.equation());
  l.gpu.bind();
  l.gpu.draw();
}

// Pass 2: the layer's own triangles, drawn as the mode asks -- and in Edges,
// their edges on top of them. Those edges are the same index buffer a second
// time under GL_LINE, so they cost no geometry: every triangle already on the
// GPU is re-rasterized as its three sides. Shared edges are drawn twice, which
// is invisible and cheaper than deduplicating them into an edge list would be.
void Renderer::draw_surface(const Layer &l,
                            const CutPlane &plane,
                            SurfaceMode mode)
{
  const bool opaque = l.is_opaque();
  const bool edges = mode == SurfaceMode::Edges;
  const bool xray = mode == SurfaceMode::Xray;

  GlState s;
  s.depth_test = true;
  s.depth_write = opaque; // translucent layers must not occlude each other
  s.blend = !opaque;
  s.clip_plane = plane.enabled;
  // Leaving the stencil test *disabled* rather than setting GL_KEEP is what
  // preserves pass 1's bits: the spec suppresses stencil writes entirely when
  // the test is off, so the two sub-draws below cannot clobber them.
  s.stencil.enabled = false;
  // Culling is only valid once the winding is known to be consistent.
  s.cull = l.oriented();
  s.cull_face = GL_BACK;
  // Xray turns *this* draw into lines, which is what makes the layer
  // see-through: a line writes depth only where it is, so nothing behind the
  // hull is occluded by it.
  s.polygon_mode = xray ? GL_LINE : GL_FILL;
  // A positive pair pushes the fill away from the eye, leaving the edge pass
  // below a sliver of depth to win by. Without it the two agree exactly on
  // every shared fragment and the edges dissolve into z-fighting.
  s.polygon_offset = edges;
  s.offset_factor = 1.0f;
  s.offset_units = 1.0f;
  s.apply();

  surface_.use();
  surface_.set("uPlane", plane.equation());
  surface_.set("uColor", glm::vec4(l.color, l.alpha));
  l.gpu.bind();

  // A translucent closed shell blends against itself, which is the most visible
  // transparency artifact on this kind of mesh. Drawing the far side first and
  // the near side second resolves it -- but only when the winding is known, so
  // that "far" and "near" mean anything.
  if (!opaque && l.oriented())
  {
    s.cull_face = GL_FRONT;
    s.apply();
    l.gpu.draw();
    s.cull_face = GL_BACK;
    s.apply();
  }
  l.gpu.draw();

  if (!edges)
    return;

  // The edges themselves. GL_LEQUAL rather than GL_LESS: the offset above put
  // the fill behind them, but a line and the triangle it bounds still meet at
  // equal depth along the silhouette.
  s.polygon_offset = false;
  s.depth_func = GL_LEQUAL;
  s.polygon_mode = GL_LINE;
  // Culling stays as the fill left it, so an oriented mesh hides the edges on
  // its far side -- which is what makes this read as lines lying *on* a solid
  // object rather than a cage seen through it.
  s.apply();

  // Not the surface program: it takes its normal from screen-space derivatives,
  // which degenerate on a primitive with no area. The overlay program clips
  // against the plane like the surface does and has a flat branch for exactly
  // this.
  overlay_.use();
  overlay_.set("uPlane", plane.equation());
  overlay_.set("uMode", 0.0f); // flat: a line has no surface to shade
  // Darkened from the layer rather than a fixed near-black, so the edges stay
  // visible whatever colour the layer has been set to.
  overlay_.set("uColor", glm::vec4(l.color * 0.35f, 1.0f));
  l.gpu.draw();
}

// Pass 3: fill in the cross-section, wherever pass 1 left the bit set.
void Renderer::fill_cross_section(const Layer &l,
                                  const CutPlane &plane,
                                  const glm::mat4 &view,
                                  bool owns_channel)
{
  const bool solid = l.is_opaque() || l.solid_cap;

  GlState s;
  s.depth_test = true;
  // Every layer's cap lies in the same plane, so with GL_LESS the first one
  // drawn would hide all the others. GL_LEQUAL plus a polygon offset toward
  // the eye lets them coexist, and kills the speckling along the cut
  // silhouette where the cap meets the clipped geometry around it.
  s.depth_func = GL_LEQUAL;
  // A solid cap writes depth so that the cut reads as a surface rather than a
  // hole. But it lies *at* the plane, and everything this mesh's own debug
  // channels show is behind it -- so a cap that wrote depth would hide all of
  // them, whatever the rest of the scene is doing. A channel is not occluded by
  // the mesh it was found in. The cap still fills in colour; it only stops
  // claiming to be in front.
  s.depth_write = solid && !owns_channel;
  s.cull = false;
  s.blend = !solid;
  s.clip_plane = false; // the cap program writes no clip distance
  s.polygon_offset = true;
  s.stencil.enabled = true;
  s.stencil.func = GL_EQUAL;
  s.stencil.ref = GLint(kCapBit);
  s.stencil.read_mask = kCapBit;
  s.stencil.write_mask = kCapBit;
  // Zeroing the bit as we consume it makes this pass its own stencil clear, so
  // layers need no glClear between them.
  s.stencil.on_fail = GL_ZERO;
  s.stencil.on_depth_fail = GL_ZERO;
  s.stencil.on_pass = GL_ZERO;
  s.apply();

  cap_.use();
  cap_.set("uNormalView", glm::mat3(view) * plane.n);
  cap_.set("uUnlit", 0.0f);
  // Slightly darker than the shell, so the cut face reads as a cut.
  cap_.set("uColor", glm::vec4(l.color * 0.85f, solid ? 1.0f : l.alpha));
  plane_quads_.bind();
  plane_quads_.draw(GL_TRIANGLES, kCapQuadFirst, 6);
}

// The tetrahedra the plane passes through, drawn whole and unclipped: a slab
// one element thick standing in the cut, so the mesh's elements are visible as
// elements rather than as a flat painted face.
//
// One draw per group -- per label, if the source labels its tets -- each in its
// own colour, and none for a group the panel has hidden. Filled first and then
// outlined, both from the same index buffer. The fill is pushed away from the
// eye so the lines sit on top of it instead of fighting it.
//
// The slab has the shell's alpha. Opaque, it writes depth and needs no order.
// Translucent, it writes none -- so it hides neither itself nor what is behind
// it -- and draws the far faces of every tet before the near ones, as the shell
// does, which is right for each tet on its own. Where cut tets overlap each
// other on screen their order is the buffer's, and at that point this is
// ordinary unsorted blending.
void Renderer::draw_elements(const Layer &l,
                             const CutPlane &plane,
                             const glm::mat4 &view)
{
  const bool opaque = l.is_opaque();

  GlState s;
  s.depth_test = true;
  s.depth_write = opaque;
  s.blend = !opaque;
  s.cull = false;
  s.clip_plane = false; // the whole tet is wanted, both sides of the plane
  s.polygon_offset = true;
  s.offset_factor = 1.0f;
  s.offset_units = 1.0f;

  // Without labels the slab is one group, slightly brighter than the shell it
  // came out of, so it reads as something standing in front of the cut rather
  // than part of it.
  const auto color_of = [&l](size_t group)
  {
    return l.labels.empty() ? glm::min(l.color * 1.25f, glm::vec3(1.0f))
                            : l.labels[group].color;
  };
  const auto shown = [&l](size_t group)
  { return l.labels.empty() || l.labels[group].visible; };

  surface_.use();
  surface_.set("uView", view);
  l.slice.bind();

  // Far faces, then near: two passes when translucent, one with culling off
  // when not.
  const GLenum culls[] = {GL_FRONT, GL_BACK};
  const size_t passes = opaque ? 1 : 2;
  for (size_t pass = 0; pass < passes; ++pass)
  {
    s.cull = !opaque;
    s.cull_face = culls[pass];
    s.apply();
    for (size_t g = 0; g < l.slice.groups(); ++g)
      if (shown(g) && l.slice.tets(g))
      {
        surface_.set("uColor", glm::vec4(color_of(g), l.alpha));
        l.slice.draw(g);
      }
  }

  s.cull = false;
  s.polygon_offset = false;
  s.depth_func = GL_LEQUAL;
  s.polygon_mode = GL_LINE;
  s.apply();

  cap_.use();
  // uNormalView is not dead: the fragment shader mixes the lit colour out
  // rather than branching, and NaN * 0.0 is NaN.
  cap_.set("uNormalView", glm::mat3(view) * plane.n);
  cap_.set("uUnlit", 1.0f);
  cap_.set("uColor", glm::vec4(0.05f, 0.05f, 0.06f, l.alpha));
  for (size_t g = 0; g < l.slice.groups(); ++g)
    if (shown(g) && l.slice.tets(g))
      l.slice.draw(g);
}

void Renderer::draw_layer(const Layer &l,
                          const CutPlane &plane,
                          const glm::mat4 &view,
                          SurfaceMode mode,
                          bool owns_channel)
{
  // Where this layer sits relative to the plane decides how much of the work
  // below is worth doing at all. The cheap test is worth it: the stencil pass
  // submits the entire hull, and a layer the plane misses would contribute
  // nothing but an all-zero stencil.
  const PlaneSide side =
      plane.enabled ? plane.classify(l.bbmin, l.bbmax) : PlaneSide::Front;
  if (side == PlaneSide::Back)
    return; // entirely on the clipped-away side

  // Capping is meaningless on a surface with holes: the parity count would be
  // arbitrary and the fill would flicker.
  const bool capping = side == PlaneSide::Crossing && l.wants_cap();

  if (capping)
    mark_cross_section(l, plane);

  draw_surface(l, plane, mode);

  // The cap goes last, not first: it is the nearest surface of its own layer,
  // and back-to-front compositing needs the nearest thing drawn last.
  if (capping)
    fill_cross_section(l, plane, view, owns_channel);

  // The elements stand in front of the cap, so they go after it.
  if (l.wants_elements() && side == PlaneSide::Crossing)
    draw_elements(l, plane, view);
}

// One group of the debug channels, drawn like any other opaque geometry: an
// ordinary depth test, no blending, so what is in front of a channel hides it.
// A domain you want to see inside is one you have made translucent or hidden,
// neither of which writes depth; a channel that must be seen regardless says so
// with `in_front`.
//
// GL_LEQUAL, not GL_LESS: a channel sits exactly on its mesh constantly -- a
// points channel indexes the mesh's own vertices, an interface channel is read
// off its faces -- and a marked vertex must not be hidden by the surface it is
// marked on.
//
// The cutting plane applies, so slicing a mesh open reveals the channels inside
// it. One program for all of it; what changes between channels is the colour
// and how a fragment is shaded, which follows from the primitive.
void Renderer::draw_debug_overlay(const Scene &scene,
                                  const CutPlane &plane,
                                  bool in_front)
{
  const DebugOverlay &overlay = scene.debug_overlay();

  // A channel needs its layer's vertex buffer, and nothing else from the layer
  // -- so a *hidden* layer still draws its channels. That is the point: taking
  // the domain away to look at what was found inside it is the whole reason to
  // have found it, and the channel has its own checkbox for when it is not
  // wanted. Only a layer that is gone takes its channels with it, because the
  // buffer the indices address went with it.
  const auto showing = [&scene](const DebugOverlay::Item &item)
  {
    return item.enabled && !item.draw.empty() &&
           scene.layer(item.mesh_id) != nullptr;
  };

  bool any = false;
  for (const DebugOverlay::Item &item : overlay.items())
    any = any || (showing(item) && item.in_front == in_front);
  if (!any)
    return;

  GlState s;
  s.depth_test = true;
  s.depth_func = GL_LEQUAL;
  s.depth_write = true;
  s.cull = false; // an overlay triangle faces wherever it faces
  s.clip_plane = plane.enabled;
  s.apply();

  // The channels that asked to ignore what is in front of them get a depth
  // buffer of their own. Clearing rather than switching the depth test off is
  // what leaves them still occluding *themselves*, so a point cloud does not
  // show its far side through its near one.
  if (in_front)
    glClear(GL_DEPTH_BUFFER_BIT);

  overlay_.use();
  overlay_.set("uPlane", plane.equation());
  glPointSize(kPointSize);

  for (const DebugOverlay::Item &item : overlay.items())
  {
    if (!showing(item) || item.in_front != in_front)
      continue;

    // 0 flat, 1 shaded surface, 2 round point -- see kOverlayFS.
    const float shading = item.mode == GL_POINTS  ? 2.0f
                          : item.mode == GL_LINES ? 0.0f
                                                  : 1.0f;
    overlay_.set("uColor", glm::vec4(item.color, 1.0f));
    overlay_.set("uMode", shading);
    item.draw.bind();
    item.draw.draw(item.mode);
  }

  glPointSize(1.0f);
}

void Renderer::draw_gizmo(const CutPlane &plane, const glm::mat4 &view)
{
  GlState s;
  s.depth_test = true;
  s.depth_write = false;
  s.blend = true;
  s.apply();

  cap_.use();
  cap_.set("uNormalView", glm::mat3(view) * plane.n);
  cap_.set("uUnlit", 1.0f);
  plane_quads_.bind();

  cap_.set("uColor", glm::vec4(1.0f, 0.72f, 0.20f, 0.10f)); // faint fill
  plane_quads_.draw(GL_TRIANGLES, kGizmoQuadFirst, 6);
  cap_.set("uColor", glm::vec4(1.0f, 0.72f, 0.20f, 0.90f)); // bright outline
  plane_quads_.draw(GL_LINE_LOOP, kGizmoLineFirst, 4);
}

void Renderer::draw(Scene &scene,
                    const Camera &cam,
                    const CutPlane &plane,
                    const glm::vec3 &background,
                    SurfaceMode mode)
{
  const glm::mat4 view = cam.view();
  const glm::mat4 proj = cam.proj();

  // glClear is masked by glStencilMask, so opening the mask is not optional.
  glStencilMask(0xFFu);
  glClearColor(background.r, background.g, background.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  if (scene.empty())
    return;

  // Both caches derived from the plane are refreshed here, together: the quads
  // that lie in it, re-uploaded every frame (they are twelve vertices), and
  // the tets it passes through, rebuilt only when the plane has moved.
  update_plane_geometry(plane, scene.bounds());
  scene.update_element_slices(plane);

  // Neither matrix changes within a frame, so set both once per program.
  for (ShaderProgram *p : {&surface_, &stencil_, &cap_, &overlay_})
  {
    p->use();
    p->set("uProj", proj);
    p->set("uView", view);
  }

  // Whether this layer's own debug channels are going to be depth-tested
  // against it -- see fill_cross_section. A channel drawn in front of
  // everything needs nothing from this, because it clears depth anyway.
  const auto owns_channel = [&scene](uint32_t mesh_id)
  {
    for (const DebugOverlay::Item &item : scene.debug_overlay().items())
      if (item.mesh_id == mesh_id && item.enabled && !item.in_front &&
          !item.draw.empty())
        return true;
    return false;
  };

  // Two sweeps over the same list: opaque layers first, so they lay down depth
  // for everything translucent behind them, then the translucent ones blending
  // in the user's chosen order.
  for (const bool opaque_pass : {true, false})
    for (size_t slot = 0; slot < scene.order().size(); ++slot)
    {
      const Layer &l = scene.at_slot(slot);
      if (l.visible && l.is_opaque() == opaque_pass)
        draw_layer(l, plane, view, mode, owns_channel(l.mesh_id));
    }

  // The ordinary debug channels are opaque, so they go before the gizmo,
  // which is translucent and has to blend over whatever is behind it; the
  // in-front ones clear the depth buffer, so they go after it, or the gizmo
  // would draw over everything too.
  draw_debug_overlay(scene, plane, false);
  if (plane.enabled && plane.show_gizmo)
    draw_gizmo(plane, view);
  draw_debug_overlay(scene, plane, true);

  // ImGui's backend saves and restores blend, cull and depth, but knows nothing
  // about clip distances or our stencil setup, so hand it a clean slate.
  GlState{}.apply();
  glBindVertexArray(0);
  glUseProgram(0);
}

} // namespace mesh_viewer
