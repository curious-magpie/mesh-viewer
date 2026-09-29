cmake_minimum_required(VERSION 3.20)
project(mesh_viewer VERSION 0.1.0 LANGUAGES C CXX)

# Built on its own, or pulled into another project with add_subdirectory -- as
# mesh-dev does, from ext/mesh-viewer. The settings below that belong to the
# whole build are made only in the first case, so a host keeps its own.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(MESH_VIEWER_TOP_LEVEL ON)
else()
    set(MESH_VIEWER_TOP_LEVEL OFF)
endif()

if(MESH_VIEWER_TOP_LEVEL)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

option(MESH_VIEWER_BUILD_EXAMPLES "Build the example program"
       ${MESH_VIEWER_TOP_LEVEL})

# --------------------------------------------------------------- dependencies
#
# GLFW and GLM come from the system. glad and Dear ImGui are vendored in ext/;
# see ext/README.md.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
find_package(OpenGL REQUIRED)

# glm is header-only; prefer the config package, fall back to the bare header path.
find_package(glm QUIET)

# The names are prefixed so that a host with a glad or an imgui target of its
# own can still add this directory.
add_library(mesh_viewer_glad STATIC ext/glad/src/glad.c)
target_include_directories(mesh_viewer_glad PUBLIC ext/glad/include)
target_link_libraries(mesh_viewer_glad PUBLIC ${CMAKE_DL_LIBS})

add_library(mesh_viewer_imgui STATIC
    ext/imgui/imgui.cpp
    ext/imgui/imgui_draw.cpp
    ext/imgui/imgui_tables.cpp
    ext/imgui/imgui_widgets.cpp
    ext/imgui/backends/imgui_impl_glfw.cpp
    ext/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(mesh_viewer_imgui PUBLIC ext/imgui ext/imgui/backends)
target_link_libraries(mesh_viewer_imgui PUBLIC PkgConfig::GLFW mesh_viewer_glad)

# The GLFW backend calls glfwGetX11Display() for its tiling-WM helper, so
# it needs -lX11 even in a Wayland session (where that call simply returns
# NULL).
if(UNIX AND NOT APPLE)
    pkg_check_modules(X11 IMPORTED_TARGET x11)
    if(X11_FOUND)
        target_link_libraries(mesh_viewer_imgui PUBLIC PkgConfig::X11)
    else()
        target_compile_definitions(mesh_viewer_imgui PRIVATE IMGUI_IMPL_GLFW_DISABLE_X11)
    endif()
endif()

# ------------------------------------------------------------------ the viewer
#
# One library. Its dependencies are all PUBLIC: a host's panels are ImGui code,
# and its sources hand over glm vectors, so whoever links this uses them too.
add_library(mesh_viewer STATIC
    # the window, the frame loop, and the windows the viewer brings itself
    src/mesh_viewer/builtin_windows.cpp
    src/mesh_viewer/input.cpp
    src/mesh_viewer/viewer.cpp
    src/mesh_viewer/widgets.cpp

    # scene/  what is drawn, and the camera and cutting plane that view it
    src/mesh_viewer/scene/draw_objects/debug_overlay.cpp
    src/mesh_viewer/scene/draw_objects/element_slice.cpp
    src/mesh_viewer/scene/gpu_mesh.cpp
    src/mesh_viewer/scene/layer.cpp
    src/mesh_viewer/scene/scene.cpp

    # render/ turning the scene into pixels
    src/mesh_viewer/render/renderer.cpp
    src/mesh_viewer/render/shader.cpp
)
add_library(mesh_viewer::mesh_viewer ALIAS mesh_viewer)
target_include_directories(mesh_viewer PUBLIC src)
target_compile_features(mesh_viewer PUBLIC cxx_std_17)
target_compile_options(mesh_viewer PRIVATE -Wall -Wextra)
target_link_libraries(mesh_viewer PUBLIC mesh_viewer_glad mesh_viewer_imgui
                                         PkgConfig::GLFW OpenGL::GL)
if(glm_FOUND)
    target_link_libraries(mesh_viewer PUBLIC glm::glm)
endif()

# ----------------------------------------------------------------- the example
#
# A host in one file: generated meshes, a source over them, one window of its
# own and one overlay channel. What to copy when starting a new program.
if(MESH_VIEWER_BUILD_EXAMPLES)
    add_executable(mesh-viewer-demo examples/demo.cpp)
    target_link_libraries(mesh-viewer-demo PRIVATE mesh_viewer::mesh_viewer)
    target_compile_options(mesh-viewer-demo PRIVATE -Wall -Wextra)
endif()
