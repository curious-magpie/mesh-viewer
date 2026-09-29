// GLSL kept as string literals so the binary is self-contained -- no runtime
// search for a shaders/ directory next to the executable.
#pragma once

namespace mesh_viewer
{

// --------------------------------------------------------------------------
// Surface: the program a layer is shaded with. Its normal comes from
// screen-space derivatives, so no mesh needs normals of its own.
// --------------------------------------------------------------------------
inline const char *kSurfaceVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec4 uPlane;          // xyz = normal, w = offset; clipped where dot < 0

out vec3 vViewPos;

// Do NOT redeclare gl_ClipDistance here. `out float gl_ClipDistance[1];` is legal
// GLSL 330, but Mesa links it into a variable the clipper never reads: the shader
// compiles, GL_CLIP_DISTANCE0 reports enabled, and nothing is ever clipped.
// Writing the unsized built-in sizes it implicitly and works everywhere.

void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    gl_ClipDistance[0] = dot(uPlane.xyz, aPos) + uPlane.w;
    gl_Position = uProj * vp;
}
)";

inline const char *kSurfaceFS = R"(#version 330 core
in vec3 vViewPos;
uniform vec4 uColor;
out vec4 FragColor;

void main() {
    // Flat shading straight off the rasterizer: exact for a triangle, and it
    // saves carrying a normal per vertex on meshes with millions of them.
    vec3 g = cross(dFdx(vViewPos), dFdy(vViewPos));
    float len = length(g);
    vec3 N = (len > 1e-12) ? g / len : vec3(0.0, 0.0, 1.0);
    N = faceforward(N, vViewPos, N);   // always toward the eye: cut-open interiors stay lit

    vec3 L = normalize(vec3(0.35, 0.55, 1.0));
    vec3 V = normalize(-vViewPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 c = uColor.rgb * (0.30 + 0.70 * diff) + vec3(0.18) * spec;
    FragColor = vec4(c, uColor.a);
}
)";

// --------------------------------------------------------------------------
// Stencil: position and clip distance only. No varyings, no uniforms past the
// matrices -- this pass runs over the full mesh every frame.
// --------------------------------------------------------------------------
inline const char *kStencilVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec4 uPlane;

void main() {
    gl_ClipDistance[0] = dot(uPlane.xyz, aPos) + uPlane.w;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

inline const char *kStencilFS = R"(#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0); }   // colour mask is off; this never lands
)";

// --------------------------------------------------------------------------
// Cap: the cross-section fill and the gizmo. Writes no clip distance, so
// GL_CLIP_DISTANCE0 must be disabled around it -- which is no loss, because
// both things it draws *lie in* the plane, and clipping geometry against the
// plane it is coplanar with is a coin toss per vertex. Takes its normal as a
// uniform because derivatives degenerate exactly when the plane is seen
// edge-on, which happens constantly while dragging it.
// --------------------------------------------------------------------------
inline const char *kCapVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vViewPos;

void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    gl_Position = uProj * vp;
}
)";

inline const char *kCapFS = R"(#version 330 core
in vec3 vViewPos;
uniform vec4 uColor;
uniform vec3 uNormalView;    // plane normal, already in view space
uniform float uUnlit;        // 1.0 for the gizmo, 0.0 for a shaded cap
out vec4 FragColor;

void main() {
    vec3 N = normalize(uNormalView);
    N = faceforward(N, vViewPos, N);
    vec3 L = normalize(vec3(0.35, 0.55, 1.0));
    float diff = max(dot(N, L), 0.0);
    vec3 lit = uColor.rgb * (0.40 + 0.60 * diff);
    FragColor = vec4(mix(lit, uColor.rgb, uUnlit), uColor.a);
}
)";

// --------------------------------------------------------------------------
// Overlay: the one program the debug channels use, whatever they are made of.
//
// Points, lines and triangles all want the same things -- the cutting plane and
// a colour of their own -- so they share a program rather than borrowing the
// ones the meshes use. That borrowing is what this replaces: it meant setting
// uUnlit on the cap program to mix its lighting back out, and handing it a
// uNormalView that was never read but still had to be a finite number, because
// the shader computed the lit colour either way and NaN * 0.0 is NaN.
//
// What does differ is how a fragment is coloured, and `uMode` selects it:
//
//   0  flat -- a line, which has no surface to shade
//   1  a surface, shaded off the rasterizer like the meshes
//   2  a round point: a camera-facing hemisphere, so that a cloud of them reads
//      as depth rather than as confetti
//
// Each branch is on a uniform, so it is the same for every fragment in a draw.
// gl_PointCoord is only defined while drawing points; it is named in a branch
// the other modes never take, which GLSL allows.
//
// A point's depth is its vertex's across the whole disc, so two of them occlude
// each other by their centres rather than intersecting as actual spheres --
// visible as a point popping in front of its neighbour as the camera turns.
// --------------------------------------------------------------------------
inline const char *kOverlayVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec4 uPlane;          // xyz = normal, w = offset; clipped where dot < 0

out vec3 vViewPos;            // mode 1 takes its normal from the slope of this

void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    gl_ClipDistance[0] = dot(uPlane.xyz, aPos) + uPlane.w;
    gl_Position = uProj * vp;
}
)";

inline const char *kOverlayFS = R"(#version 330 core
in vec3 vViewPos;

uniform vec4 uColor;
uniform float uMode;          // 0 flat, 1 shaded surface, 2 round point

out vec4 FragColor;

void main() {
    // The same light the meshes use, fixed in view space so that shading does
    // not swing as the camera turns.
    vec3 L = normalize(vec3(0.35, 0.55, 1.0));

    if (uMode > 1.5) {
        vec2 p = gl_PointCoord * 2.0 - 1.0;
        p.y = -p.y; // default point-coordinate origin is upper-left
        float r2 = dot(p, p);
        if (r2 >= 1.0)
            discard; // square corners write neither colour nor depth

        vec3 N = vec3(p, sqrt(1.0 - r2));
        vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
        float diff = max(dot(N, L), 0.0);
        float spec = pow(max(dot(N, H), 0.0), 32.0);
        FragColor = vec4(uColor.rgb * (0.25 + 0.75 * diff) + vec3(0.30) * spec,
                         uColor.a);
        return;
    }

    if (uMode < 0.5) {
        FragColor = uColor; // a line: there is no surface to shade
        return;
    }

    // Flat shading off the rasterizer, as the meshes do it: exact per triangle,
    // and no normal to carry per vertex. Derivatives degenerate on a primitive
    // with no area, hence the fallback.
    vec3 g = cross(dFdx(vViewPos), dFdy(vViewPos));
    float len = length(g);
    vec3 N = (len > 1e-12) ? g / len : vec3(0.0, 0.0, 1.0);
    N = faceforward(N, vViewPos, N); // toward the eye, so both sides stay lit

    // Lifted much further off black than a mesh is, and with no specular: a
    // channel is an annotation first, so the shading is here to show the shape
    // and the colour has to stay recognisable from every angle. A highlight
    // would read as glare and move about more than the diffuse term does.
    float diff = max(dot(N, L), 0.0);
    FragColor = vec4(uColor.rgb * (0.55 + 0.45 * diff), uColor.a);
}
)";

} // namespace mesh_viewer
