# Vendored dependencies

Copied in verbatim, and unmodified, so the build needs no network access and no package
manager.

| | version | licence |
|---|---|---|
| `glad/` | 0.1.13a, OpenGL 3.3 core loader, generated at <https://glad.dav1d.de/> | public domain / MIT (see the header of `include/glad/glad.h`) |
| `imgui/` | Dear ImGui 1.92.9b, plus the GLFW and OpenGL 3 backends | MIT, see `imgui/LICENSE.txt` |

GLFW and GLM are *not* vendored; they come from the system.
