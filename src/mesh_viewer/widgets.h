// Small widgets a panel can use to talk about the scene the way the viewer's
// own windows do.
#pragma once

#include "mesh_viewer/scene/layer.h"

namespace mesh_viewer
{

// A mesh's colour, drawn wherever its name appears. The same mesh is named in
// several windows, and the swatch is what makes it recognisable at a glance
// without reading. Null (no layer yet) draws a grey one. Call inside a PushID
// scope that names the mesh.
void mesh_chip(const Layer *l);

} // namespace mesh_viewer
