// A window a host adds to the viewer.
//
// This is the extension point: a program that uses the viewer puts its own
// controls -- an algorithm's parameters, a run button, a report -- in a Panel
// and registers it with Viewer::add_window. The viewer draws the window frame,
// lists it in the Windows menu, and calls the panel at the right moments of
// the frame; the panel never touches the frame loop and the viewer never
// learns what the panel is for.
//
// The three hooks are the three things a frame does for a host:
//
//   ui       inside the ImGui frame, between Begin and End of the panel's
//            window. Widgets only: anything that changes the meshes goes
//            through Viewer::defer, so the meshes do not change halfway
//            through drawing the windows that describe them.
//   update   once a frame, whether the window is open or not, after the
//            deferred work and before the scene syncs. For work that runs on
//            its own -- a simulation clock, say.
//   on_key   a key press nothing else claimed.
#pragma once

namespace mesh_viewer
{

class Viewer;

class Panel
{
public:
  virtual ~Panel() = default;

  virtual void ui(Viewer &viewer) = 0;

  virtual void update(Viewer &viewer)
  {
    (void)viewer;
  }

  // True while the panel wants frames drawn without waiting for input -- a
  // running simulation. Otherwise the viewer sleeps until something happens.
  virtual bool animating() const
  {
    return false;
  }

  // A GLFW key code and modifier bits. Return true to claim the key.
  virtual bool on_key(Viewer &viewer, int key, int mods)
  {
    (void)viewer;
    (void)key;
    (void)mods;
    return false;
  }
};

// Where a window first appears, in ImGui's window units. Used only the first
// time the window is ever shown: after that ImGui remembers where it was left
// (in imgui.ini). A negative x is measured from the right edge of the screen,
// so a window can be anchored there. A zero size lets ImGui choose.
struct WindowPlacement
{
  float x = 12.0f, y = 12.0f;
  float width = 0.0f, height = 0.0f;
};

} // namespace mesh_viewer
