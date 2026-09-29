// The viewer's own windows. Both are ordinary Panels, registered by
// Viewer::init through the same add_window a host uses.
//
// Which window a control belongs in is decided by one question: is it about
// what the program computes, or about looking at it? The first kind is the
// host's -- its panels are the Model side of the program. These two are the
// second kind, split because the tempos differ:
//
//   Inspect  about a result: the cutting plane, and the cap and elements that
//            make a cut readable, plus the overlay channels. Used *while*
//            reading a result rather than instead of it -- which is why it is
//            its own window and not a section of View.
//   View     colour, alpha, order, camera. Set once and left.
#pragma once

#include "mesh_viewer/panel.h"

namespace mesh_viewer
{

class InspectWindow : public Panel
{
public:
  static constexpr WindowPlacement kPlacement{12.0f, 544.0f, 400.0f, 244.0f};

  void ui(Viewer &viewer) override;
};

class ViewWindow : public Panel
{
public:
  // Anchored to the right edge.
  static constexpr WindowPlacement kPlacement{-312.0f, 12.0f, 300.0f, 340.0f};

  void ui(Viewer &viewer) override;
};

} // namespace mesh_viewer
