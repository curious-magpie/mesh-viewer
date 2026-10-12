// Mouse and keyboard handling.
//
// GLFW only knows about free functions, so each callback is a small trampoline
// that recovers the Viewer from the window's user pointer and forwards to a
// member.
//
// Every callback hands the event to ImGui first, then bails out if ImGui wants
// it -- that is what stops a drag on the panel from also spinning the camera.
#include "mesh_viewer/viewer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"

namespace mesh_viewer
{

namespace
{

Viewer &viewer_of(GLFWwindow *w)
{
  return *static_cast<Viewer *>(glfwGetWindowUserPointer(w));
}

// The letter key the active layout puts where `key` is.
//
// GLFW names keys by where they sit on a US keyboard, not by what is printed
// on them: on a Belgian AZERTY the key labelled A arrives as GLFW_KEY_Q. Every
// shortcut here means a letter -- Q quit, X/Y/Z the axes -- so a letter key is
// translated through the layout first, and a shortcut is the key with that
// letter on it, whichever layout is active and however often it changes.
// Keys that make no single letter in the layout keep GLFW's code.
int layout_key(int key, int scancode)
{
  const char *name = glfwGetKeyName(key, scancode);
  if (!name || !name[0] || name[1])
    return key;
  const char c = name[0];
  if (c >= 'a' && c <= 'z')
    return GLFW_KEY_A + (c - 'a');
  if (c >= 'A' && c <= 'Z')
    return GLFW_KEY_A + (c - 'A');
  return key;
}

} // namespace

void Viewer::install_callbacks()
{
  glfwSetWindowUserPointer(window_, this);

  glfwSetMouseButtonCallback(
      window_,
      [](GLFWwindow *w, int button, int action, int mods)
      {
        ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
        if (!ImGui::GetIO().WantCaptureMouse)
          viewer_of(w).on_mouse_button(button, action, mods);
      });

  glfwSetCursorPosCallback(window_,
                           [](GLFWwindow *w, double mx, double my)
                           {
                             ImGui_ImplGlfw_CursorPosCallback(w, mx, my);
                             viewer_of(w).on_cursor_pos(mx, my);
                           });

  glfwSetScrollCallback(window_,
                        [](GLFWwindow *w, double dx, double dy)
                        {
                          ImGui_ImplGlfw_ScrollCallback(w, dx, dy);
                          if (!ImGui::GetIO().WantCaptureMouse)
                            viewer_of(w).on_scroll(dy);
                        });

  glfwSetKeyCallback(
      window_,
      [](GLFWwindow *w, int key, int scancode, int action, int mods)
      {
        ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
        if (!ImGui::GetIO().WantCaptureKeyboard)
          viewer_of(w).on_key(key, scancode, action, mods);
      });

  glfwSetDropCallback(window_,
                      [](GLFWwindow *w, int count, const char **paths)
                      { viewer_of(w).on_drop(count, paths); });

  glfwSetFramebufferSizeCallback(window_,
                                 [](GLFWwindow *w, int width, int height)
                                 { viewer_of(w).on_resize(width, height); });

  glfwSetCharCallback(window_, ImGui_ImplGlfw_CharCallback);
}

void Viewer::on_mouse_button(int button, int action, int mods)
{
  if (action == GLFW_RELEASE)
  {
    gesture_ = Gesture::None;
    plane_drag_.end();
    return;
  }

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  cursor_pixels(mx, my);
  last_x_ = mx;
  last_y_ = my;

  // Right turns the scene, middle drags it. Left drags it too, after the
  // plane's handle has had its say -- or turns it with shift held, for a
  // trackpad that has no right drag to speak of.
  if (button == GLFW_MOUSE_BUTTON_RIGHT)
  {
    gesture_ = Gesture::Orbit;
    return;
  }
  if (button == GLFW_MOUSE_BUTTON_MIDDLE)
  {
    gesture_ = Gesture::Pan;
    return;
  }
  if (button != GLFW_MOUSE_BUTTON_LEFT)
    return;

  // The plane's handle gets first refusal on a left drag; whatever it does not
  // claim belongs to the camera.
  if (plane_.enabled && plane_.show_gizmo)
  {
    const auto mode = (mods & GLFW_MOD_CONTROL)
                          ? PlaneController::Mode::Rotate
                          : PlaneController::Mode::Translate;
    if (plane_drag_.try_begin(plane_,
                              cam_,
                              float(mx),
                              float(my),
                              scene_.bounds().center,
                              handle_half(scene_.bounds()),
                              mode))
      return;
  }

  gesture_ = (mods & GLFW_MOD_SHIFT) ? Gesture::Orbit : Gesture::Pan;
}

void Viewer::cursor_pixels(double &mx, double &my) const
{
  int ww = 0, wh = 0, fw = 0, fh = 0;
  glfwGetWindowSize(window_, &ww, &wh);
  glfwGetFramebufferSize(window_, &fw, &fh);
  if (ww > 0 && wh > 0)
  {
    mx *= double(fw) / double(ww);
    my *= double(fh) / double(wh);
  }
  // And from the scene's own corner, which the docked windows push in from
  // the window's: the camera's pixels are its viewport's.
  mx -= double(scene_x_);
  my -= double(scene_y_);
}

void Viewer::on_cursor_pos(double mx, double my)
{
  cursor_pixels(mx, my);
  const float dx = float(mx - last_x_);
  const float dy = float(my - last_y_);
  last_x_ = mx;
  last_y_ = my;

  if (plane_drag_.dragging())
    plane_drag_.update(plane_, cam_, float(mx), float(my));
  else if (gesture_ == Gesture::Orbit)
    cam_.orbit(dx, dy);
  else if (gesture_ == Gesture::Pan)
    cam_.pan(dx, dy);
}

void Viewer::on_scroll(double dy)
{
  cam_.dolly(float(dy));
}

void Viewer::on_key(int key, int scancode, int action, int mods)
{
  if (action != GLFW_PRESS)
    return;

  // By the letter on the key, not its US position -- for the panels too.
  key = layout_key(key, scancode);

  switch (key)
  {
  // Q, not Escape: Escape is too easy to reach for to leave a text field or a
  // menu and lose the window instead.
  case GLFW_KEY_Q:
    close();
    return;
  case GLFW_KEY_F:
    frame_all();
    return;
  case GLFW_KEY_W:
    view_.surface_mode = next_mode(view_.surface_mode);
    return;
  case GLFW_KEY_P:
    plane_.enabled = !plane_.enabled;
    return;
  // The axis views: from the + side, or with shift the - side; twice turns
  // round. R back to the opening view, O perspective on and off.
  case GLFW_KEY_X:
  case GLFW_KEY_Y:
  case GLFW_KEY_Z:
    look_from(key - GLFW_KEY_X, (mods & GLFW_MOD_SHIFT) != 0);
    return;
  case GLFW_KEY_R:
    look_default();
    return;
  case GLFW_KEY_O:
    cam_.orthographic = !cam_.orthographic;
    return;
  default:
    break;
  }

  // The viewer's keys first, then whichever panel claims it.
  for (Window &w : windows_)
    if (w.panel->on_key(*this, key, mods))
      return;
}

void Viewer::on_drop(int count, const char **paths)
{
  if (!drop_handler_)
    return;
  drop_handler_(std::vector<std::filesystem::path>(paths, paths + count));

  // The handler added to the source; the view follows it now rather than at
  // the end of the frame, so the camera frames what was just loaded.
  sync();
  frame_all();
}

void Viewer::on_resize(int width, int height)
{
  // Until the next frame lays the windows out again; apply_viewport then
  // takes the rectangle they leave.
  cam_.vw = width;
  cam_.vh = height;
  glViewport(0, 0, width, height);
}

} // namespace mesh_viewer
