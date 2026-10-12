#include "mesh_viewer/viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "mesh_viewer/builtin_windows.h"

namespace mesh_viewer
{

namespace
{

// The prefix of every message, which is the host's title. File scope because
// GLFW's error callback is a free function with no way to reach a Viewer.
std::string g_prefix = "mesh-viewer";

// The last thing GLFW complained about.
//
// GLFW reports errors *only* through a callback, and only to one installed
// before the call that fails. Without it every failure is a bare false, which
// is how "glfwInit failed" once meant a stale WAYLAND_DISPLAY pointing at a
// socket that no longer existed -- something GLFW says in as many words, to
// nobody.
std::string g_glfw_error;

void on_glfw_error(int code, const char *description)
{
  g_glfw_error = description ? description : "(no description)";
  std::fprintf(stderr,
               "%s: GLFW error %d: %s\n",
               g_prefix.c_str(),
               code,
               g_glfw_error.c_str());
}

// `what` plus whatever GLFW had to say about it, if anything.
std::string with_glfw_error(const char *what)
{
  return g_glfw_error.empty() ? std::string(what)
                              : std::string(what) + ": " + g_glfw_error;
}

// What the scene syncs against when the host has not named a source: nothing.
struct NoMeshes : MeshSource
{
  size_t mesh_count() const override
  {
    return 0;
  }
  MeshView mesh(size_t) const override
  {
    return {};
  }
};

struct NoOverlay : OverlaySource
{
  uint64_t revision() const override
  {
    return 0; // what a fresh overlay has already "seen", so nothing happens
  }
  size_t channel_count() const override
  {
    return 0;
  }
  OverlayChannel channel(size_t) const override
  {
    return {};
  }
};

const NoMeshes kNoMeshes;
const NoOverlay kNoOverlay;

} // namespace

Viewer::Viewer() = default;

Viewer::~Viewer()
{
  // Order matters: the GPU-owning objects must release their names while the
  // context is still current, so they are torn down before GLFW is.
  // The layers hold GL names, so they must go while the context is current.
  // The meshes they point at are the host's and can go whenever.
  scene_.clear();
  renderer_.release();

  if (window_)
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}

bool Viewer::init(const Options &options, std::string &err)
{
  title_ = options.title;
  g_prefix = options.title;

  // Before glfwInit, or the reason for its failure is discarded.
  glfwSetErrorCallback(on_glfw_error);

  if (!glfwInit())
  {
    err = with_glfw_error("glfwInit failed");
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DEPTH_BITS, 24);
  glfwWindowHint(GLFW_STENCIL_BITS, 8); // the cross-section fill lives here
  glfwWindowHint(GLFW_SAMPLES, 4);

  window_ = glfwCreateWindow(
      options.width, options.height, title_.c_str(), nullptr, nullptr);
  if (!window_)
  {
    // The usual causes are all in the hints above: no GL 3.3 core, or no
    // pixel format with 24-bit depth, 8-bit stencil and 4x multisampling.
    err = with_glfw_error("could not create a GL 3.3 core window");
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);

  // Loading through glfwGetProcAddress keeps this correct on Wayland, X11 and
  // XWayland alike -- GLEW's GLX-based lookup is not.
  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
  {
    err = "failed to load OpenGL functions";
    return false;
  }

  GLint stencil_bits = 0;
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
                                        GL_STENCIL,
                                        GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
                                        &stencil_bits);
  if (stencil_bits < 1)
    std::fprintf(stderr,
                 "%s: warning -- no stencil buffer, "
                 "cut faces will look hollow\n",
                 g_prefix.c_str());

  install_callbacks();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  // false: we install the GLFW callbacks ourselves and chain into ImGui, so it
  // must not install its own on top.
  ImGui_ImplGlfw_InitForOpenGL(window_, false);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  // The viewer's own windows, through the same door a host's come in by.
  if (options.inspect_window)
  {
    builtin_.push_back(std::make_unique<InspectWindow>());
    add_window("Inspect", *builtin_.back(), InspectWindow::kPlacement);
    windows_.back().builtin = true;
  }
  if (options.view_window)
  {
    builtin_.push_back(std::make_unique<ViewWindow>());
    add_window("View", *builtin_.back(), ViewWindow::kPlacement);
    windows_.back().builtin = true;
  }

  return renderer_.init(err);
}

void Viewer::add_window(std::string title, Panel &panel, WindowPlacement where)
{
  windows_.push_back(Window{std::move(title), &panel, where, true});
}

void Viewer::sync()
{
  // The view is a function of the sources: every frame it is brought up to
  // date, and every frame where nothing moved that costs one comparison per
  // mesh.
  scene_.sync(meshes_ ? *meshes_ : kNoMeshes);
  scene_.sync_debug_overlay(overlay_ ? *overlay_ : kNoOverlay);
}

void Viewer::frame_all()
{
  const SceneBounds &b = scene_.bounds();
  cam_.fit(b.center, b.radius);
}

void Viewer::look_from(int axis, bool negative)
{
  glm::quat to = Camera::looking_from(axis, negative);
  // Already looking from there: the other side, so a second press turns the
  // scene round.
  if (glm::dot(cam_.dir(), to * glm::vec3(0, 0, 1)) > 0.999f)
    to = Camera::looking_from(axis, !negative);
  turn_to(to);
}

void Viewer::look_default()
{
  turn_to(Camera::default_rotation());
}

void Viewer::turn_to(const glm::quat &to)
{
  snap_.active = true;
  snap_.from = cam_.rotation;
  // The short way round: q and -q are the same orientation, and slerp between
  // them the long way would spin the view through a full turn first.
  snap_.to = glm::dot(snap_.from, to) < 0.0f ? -to : to;
  snap_.start = glfwGetTime();
}

void Viewer::step_snap()
{
  if (!snap_.active)
    return;
  // A drag takes the camera back; the snap gives way rather than fight it.
  if (gesture_ != Gesture::None)
  {
    snap_.active = false;
    return;
  }
  const double t = std::min((glfwGetTime() - snap_.start) / kSnapSeconds, 1.0);
  const float eased = float(t * t * (3.0 - 2.0 * t)); // smoothstep
  cam_.turn_to(glm::slerp(snap_.from, snap_.to, eased));
  if (t >= 1.0)
    snap_.active = false;
}

void Viewer::draw_axes()
{
  // In the bottom-right corner of the scene, clear of the windows. An ImGui
  // window of its own, without decoration, so a click on it is ImGui's and
  // does not also start a drag of the camera.
  constexpr float kSize = 112.0f, kLength = 38.0f, kTip = 9.0f;
  ImGui::SetNextWindowPos(
      ImVec2(scene_rect_.x1 - kSize - 8.0f, scene_rect_.y1 - kSize - 8.0f));
  ImGui::SetNextWindowSize(ImVec2(kSize, kSize));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!ImGui::Begin("##axes", nullptr, flags))
  {
    ImGui::End();
    return;
  }

  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 corner = ImGui::GetWindowPos();
  const ImVec2 centre(corner.x + kSize * 0.5f, corner.y + kSize * 0.5f);

  // The six ends, each world axis seen through the camera: right is screen x,
  // up is screen -y. Drawn far to near, so the ones toward the viewer are on
  // top, which is also the order a click should prefer.
  struct End
  {
    ImVec2 at;
    float depth; // toward the viewer is positive
    int axis;
    bool negative;
  };
  End ends[6];
  for (int i = 0; i < 6; ++i)
  {
    glm::vec3 e(0.0f);
    e[i / 2] = (i % 2) ? -1.0f : 1.0f;
    ends[i] = End{ImVec2(centre.x + glm::dot(e, cam_.right()) * kLength,
                         centre.y - glm::dot(e, cam_.up()) * kLength),
                  glm::dot(e, cam_.dir()),
                  i / 2,
                  (i % 2) != 0};
  }
  std::sort(std::begin(ends),
            std::end(ends),
            [](const End &a, const End &b) { return a.depth < b.depth; });

  // X red, Y green, Z blue, as everywhere else.
  static const ImU32 kColour[3] = {IM_COL32(232, 84, 84, 255),
                                   IM_COL32(118, 204, 92, 255),
                                   IM_COL32(88, 142, 240, 255)};
  static const char *kName[3] = {"X", "Y", "Z"};

  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool hovered = ImGui::IsWindowHovered();
  const End *picked = nullptr;
  for (const End &end : ends)
  {
    const float dx = mouse.x - end.at.x, dy = mouse.y - end.at.y;
    if (hovered && dx * dx + dy * dy <= kTip * kTip)
      picked = &end; // the last hit is the nearest
  }

  for (const End &end : ends)
  {
    const ImU32 colour = kColour[end.axis];
    const bool lit = picked == &end;
    if (!end.negative)
    {
      draw->AddLine(centre, end.at, colour, 2.0f);
      draw->AddCircleFilled(end.at, lit ? kTip + 2.0f : kTip, colour);
      const ImVec2 text = ImGui::CalcTextSize(kName[end.axis]);
      draw->AddText(ImVec2(end.at.x - text.x * 0.5f, end.at.y - text.y * 0.5f),
                    IM_COL32(20, 20, 24, 255),
                    kName[end.axis]);
    }
    else
    {
      draw->AddCircleFilled(end.at, kTip - 2.0f, IM_COL32(30, 32, 36, 200));
      draw->AddCircle(end.at, lit ? kTip : kTip - 2.0f, colour, 0, 1.5f);
    }
  }

  if (picked)
  {
    ImGui::SetTooltip("view from %s%s\nkey %s, shift+%s from the - side;\n"
                      "again to turn round",
                      picked->negative ? "-" : "+",
                      kName[picked->axis],
                      kName[picked->axis],
                      kName[picked->axis]);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      look_from(picked->axis, picked->negative);
  }
  ImGui::End();
}

void Viewer::close()
{
  if (window_)
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Viewer::run_deferred()
{
  // Moved out first: an action may itself defer another, which then runs on
  // the next frame rather than invalidating this loop.
  std::vector<std::function<void()>> actions;
  actions.swap(deferred_);
  for (const std::function<void()> &action : actions)
    action();
}

bool Viewer::animating() const
{
  if (gesture_ != Gesture::None || plane_drag_.dragging() || snap_.active)
    return true;
  for (const Window &w : windows_)
    if (w.panel->animating())
      return true;
  return false;
}

float Viewer::draw_menu_bar()
{
  float height = 0.0f;
  if (ImGui::BeginMainMenuBar())
  {
    // Every window, host's and viewer's alike, can be closed from its title
    // bar; this is how it comes back.
    if (ImGui::BeginMenu("Windows"))
    {
      for (Window &w : windows_)
        ImGui::MenuItem(w.title.c_str(), nullptr, &w.open);
      ImGui::Separator();
      ImGui::MenuItem("docked layout", nullptr, &view_.docked);
      ImGui::EndMenu();
    }
    height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
  }
  return height;
}

void Viewer::draw_windows()
{
  // Placements are stated as if there were no menu bar, so that a host does
  // not have to know how tall it is.
  const float top = draw_menu_bar();
  if (view_.docked)
    draw_docked(top);
  else
    draw_floating(top);
}

void Viewer::draw_floating(float top)
{
  // ImGui lays out in window units, not framebuffer pixels -- which differ on
  // a scaled display -- so the right edge is its DisplaySize, not the camera's
  // viewport.
  const ImVec2 display = ImGui::GetIO().DisplaySize;

  for (Window &w : windows_)
  {
    if (!w.open)
      continue;

    const WindowPlacement &p = w.where;
    const float x = p.x < 0.0f ? std::max(display.x + p.x, 12.0f) : p.x;
    ImGui::SetNextWindowPos(ImVec2(x, p.y + top), ImGuiCond_FirstUseEver);
    if (p.width > 0.0f && p.height > 0.0f)
      ImGui::SetNextWindowSize(ImVec2(p.width, p.height),
                               ImGuiCond_FirstUseEver);

    // A collapsed window draws nothing, so its panel is not asked to.
    if (ImGui::Begin(w.title.c_str(), &w.open))
      w.panel->ui(*this);
    ImGui::End();
  }

  // Floating windows sit over the scene, which keeps the whole window.
  scene_rect_ = Rect{0.0f, 0.0f, display.x, display.y};
}

void Viewer::draw_docked(float top)
{
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float height = std::max(display.y - top, 1.0f);
  float used[2] = {0.0f, 0.0f};

  // The viewer's own windows on the left, the host's on the right: what
  // looks at the scene on one side, what acts on the meshes on the other.
  // Within a column, in the order they were added.
  for (int side = 0; side < 2; ++side)
  {
    std::vector<Window *> column;
    for (Window &w : windows_)
      if (w.open && w.builtin == (side == 0))
        column.push_back(&w);
    if (column.empty())
      continue;

    // As wide as the widest placement asked, the first time; after that what
    // the user dragged it to. Never so wide the scene has nowhere to go.
    float &width = column_width_[side];
    if (width <= 0.0f)
      for (const Window *w : column)
        width = std::max(width, std::max(w->where.width, 280.0f));
    const float widest = std::max(display.x * 0.45f, 220.0f);
    width = std::clamp(width, 220.0f, widest);

    // Pinned to its edge and the full height; only the inner edge can be
    // dragged, which the size constraint enforces by fixing the height.
    ImGui::SetNextWindowPos(ImVec2(side == 0 ? 0.0f : display.x - width, top),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, height),
                                        ImVec2(widest, height));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(
            side == 0 ? "##left column" : "##right column", nullptr, flags))
    {
      // Each window is a section that folds, and closes from its header like
      // a floating window from its title bar; the Windows menu brings it back.
      for (Window *w : column)
        if (ImGui::CollapsingHeader(
                w->title.c_str(), &w->open, ImGuiTreeNodeFlags_DefaultOpen))
        {
          ImGui::PushID(w->title.c_str());
          w->panel->ui(*this);
          ImGui::PopID();
          ImGui::Spacing();
        }
    }
    // A drag of the inner edge resizes the window; that is the new width.
    width = ImGui::GetWindowWidth();
    used[side] = width;
    ImGui::End();
  }

  scene_rect_ = Rect{used[0], top, display.x - used[1], display.y};
}

void Viewer::apply_viewport()
{
  int fw = 0, fh = 0;
  glfwGetFramebufferSize(window_, &fw, &fh);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float sx = display.x > 0.0f ? float(fw) / display.x : 1.0f;
  const float sy = display.y > 0.0f ? float(fh) / display.y : 1.0f;

  // From window units to framebuffer pixels, and from a top-left origin to
  // GL's bottom-left one.
  const int x0 = int(std::lround(scene_rect_.x0 * sx));
  const int y0 = int(std::lround(scene_rect_.y0 * sy));
  const int x1 = std::max(int(std::lround(scene_rect_.x1 * sx)), x0 + 1);
  const int y1 = std::max(int(std::lround(scene_rect_.y1 * sy)), y0 + 1);
  scene_x_ = x0;
  scene_y_ = y0;
  cam_.vw = x1 - x0;
  cam_.vh = y1 - y0;
  glViewport(x0, fh - y1, cam_.vw, cam_.vh);
}

int Viewer::run()
{
  // Synced before the first frame, so the camera is framed on something that
  // exists.
  sync();
  frame_all();
  plane_.set_through(scene_.bounds().center);
  glfwGetFramebufferSize(window_, &cam_.vw, &cam_.vh);
  glViewport(0, 0, cam_.vw, cam_.vh);

  while (!glfwWindowShouldClose(window_))
  {
    // With nothing moving the picture only changes when the user does
    // something, so wait for that -- or for a tenth of a second, so the panel
    // still settles after an input -- instead of redrawing at the refresh rate.
    if (animating())
      glfwPollEvents();
    else
      glfwWaitEventsTimeout(0.1);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_windows();
    draw_axes();
    ImGui::Render();

    // What the panels asked to have done to the meshes, now that they have
    // finished describing them.
    run_deferred();

    // Then whatever runs on its own clock. It writes, and the view reads:
    // nothing has to tell the view that a mesh moved, because the revision it
    // bumped is the whole notification.
    for (Window &w : windows_)
      w.panel->update(*this);

    sync();
    step_snap();

    // The scene into the rectangle the windows left free; ImGui sets its own
    // viewport, the whole window, when it draws on top.
    apply_viewport();
    renderer_.draw(scene_, cam_, plane_, view_.background, view_.surface_mode);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window_);
  }
  return 0;
}

} // namespace mesh_viewer
