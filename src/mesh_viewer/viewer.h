// The viewer: a window, a scene built from a MeshSource, and the panels drawn
// over it.
//
// A host uses it in five steps:
//
//   mesh_viewer::Viewer viewer;
//   viewer.init({"my-program"}, err);          // window, GL, ImGui, shaders
//   viewer.set_mesh_source(&meshes);           // what to draw (mesh_source.h)
//   viewer.add_window("Fitting", fitting_panel, {12, 12, 400, 520});
//   return viewer.run();                       // until the window closes
//
// Everything a host adds is a Panel (panel.h). The viewer's own two windows,
// Inspect (the cutting plane and what it shows) and View (colour, order,
// camera), are Panels too and are registered the same way, so what a host can
// do with a window is exactly what the viewer does with its own.
//
// Split across files by concern:
//   viewer.cpp           lifetime -- window creation, the frame loop, windows
//   input.cpp            the GLFW callbacks and how a gesture is routed
//   builtin_windows.cpp  the Inspect and View windows
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glad/glad.h> // must precede GLFW
#include <GLFW/glfw3.h>

#include "mesh_viewer/mesh_source.h"
#include "mesh_viewer/panel.h"
#include "mesh_viewer/render/renderer.h"
#include "mesh_viewer/scene/camera.h"
#include "mesh_viewer/scene/cut_plane.h"
#include "mesh_viewer/scene/plane_controller.h"
#include "mesh_viewer/scene/scene.h"

namespace mesh_viewer
{

// How the whole picture is drawn, as opposed to any one layer.
struct ViewSettings
{
  glm::vec3 background{0.13f, 0.14f, 0.16f};
  SurfaceMode surface_mode = SurfaceMode::Shaded;
};

class Viewer
{
public:
  struct Options
  {
    // The window's title, and the prefix of every message the viewer prints.
    std::string title = "mesh-viewer";
    int width = 1280, height = 800;

    // The built-in windows. Off means not registered at all, so not in the
    // Windows menu either.
    bool inspect_window = true;
    bool view_window = true;
  };

  using DropHandler =
      std::function<void(const std::vector<std::filesystem::path> &)>;

  Viewer();
  ~Viewer();

  Viewer(const Viewer &) = delete;
  Viewer &operator=(const Viewer &) = delete;

  // Creates the window, loads GL, starts ImGui and builds the shaders.
  bool init(const Options &options, std::string &err);

  // What to draw. Borrowed: each must outlive run(). Null draws nothing.
  void set_mesh_source(const MeshSource *source)
  {
    meshes_ = source;
  }
  void set_overlay_source(const OverlaySource *source)
  {
    overlay_ = source;
  }

  // Adds a window whose contents are `panel`, listed in the Windows menu under
  // `title`. The title is also the window's ImGui identity -- where it was
  // left is remembered under it -- so it must be unique. Borrowed: the panel
  // must outlive run(). Windows are drawn in the order they were added.
  void add_window(std::string title, Panel &panel, WindowPlacement where = {});

  // Called with the paths of files dropped on the window. The scene is synced
  // and the camera framed on it afterwards, so a handler that adds meshes to
  // its source has nothing else to do.
  void set_drop_handler(DropHandler handler)
  {
    drop_handler_ = std::move(handler);
  }

  // Work a panel asks for that changes the meshes -- a fit, a physics step,
  // moving a surface -- queued rather than done on the spot, and run by the
  // frame loop once the frame's UI is built. Done inside the panel, it would
  // change the meshes halfway through drawing the windows that describe them,
  // run a long fit inside an open ImGui window (so an exception there leaves
  // ImGui's window stack unbalanced), and could step a simulation twice in
  // one frame -- once for a button, once for its own clock.
  void defer(std::function<void()> action)
  {
    deferred_.push_back(std::move(action));
  }

  // Brings the scene up to date with the sources now, rather than at the end
  // of the frame. Needs the context init() made.
  void sync();

  // Frames the camera on everything in the scene, as it was last synced.
  void frame_all();

  // Asks the frame loop to stop after this frame.
  void close();

  // Runs until the window closes. Returns the process exit code.
  int run();

  // --- for panels ---
  //
  // Mutable on purpose: a panel is free to change what the viewer shows --
  // recolour a layer, move the plane, frame the camera -- because that is all
  // display state. What it must not do from ui() is change the meshes; see
  // defer.
  Scene &scene()
  {
    return scene_;
  }
  Camera &camera()
  {
    return cam_;
  }
  CutPlane &plane()
  {
    return plane_;
  }
  ViewSettings &view()
  {
    return view_;
  }
  const std::string &title() const
  {
    return title_;
  }

private:
  struct Window
  {
    std::string title;
    Panel *panel = nullptr;
    WindowPlacement where;
    bool open = true;
  };

  // --- viewer.cpp ---
  float draw_menu_bar(); // returns its height
  void draw_windows();
  void run_deferred();
  bool animating() const;

  // --- input.cpp ---
  void install_callbacks();
  void on_mouse_button(int button, int action, int mods);
  void on_cursor_pos(double mx, double my);
  void on_scroll(double dy);
  void on_key(int key, int action, int mods);
  void on_drop(int count, const char **paths);
  void on_resize(int width, int height);

  // The cursor in framebuffer pixels, which is what the camera works in. GLFW
  // reports it in window coordinates, and on a scaled display the two differ.
  void cursor_pixels(double &mx, double &my) const;

  GLFWwindow *window_ = nullptr;
  std::string title_ = "mesh-viewer";

  const MeshSource *meshes_ = nullptr;
  const OverlaySource *overlay_ = nullptr;

  Scene scene_;
  Camera cam_;
  CutPlane plane_;
  PlaneController plane_drag_;
  Renderer renderer_;
  ViewSettings view_;

  std::vector<Window> windows_;
  std::vector<std::unique_ptr<Panel>> builtin_; // Inspect and View, if asked
  std::vector<std::function<void()>> deferred_;
  DropHandler drop_handler_;

  // --- what the mouse is doing ---
  //
  // One gesture at a time, by construction rather than by convention: a drag
  // that starts on the plane's handle belongs to the plane (plane_drag_),
  // otherwise it belongs to the camera.
  enum class Gesture
  {
    None,
    Orbit,
    Pan
  };
  Gesture gesture_ = Gesture::None;
  double last_x_ = 0.0, last_y_ = 0.0;
};

} // namespace mesh_viewer
