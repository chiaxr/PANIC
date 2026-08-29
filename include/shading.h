#pragma once

// Phong shading for the 3D scene.
//
// raylib's default batch shader has no lighting: every face comes out flat, so
// a cube reads as a silhouette and a cylinder as a blob. PhongShader replaces
// it for the 3D pass with one directional light (ambient + diffuse + specular,
// Blinn's halfway vector for the highlight) plus a weak headlight from the
// camera, which is what keeps a module's face readable whichever way the bomb
// is turned.
//
// Geometry reaches the shader already in world space: rlgl transforms
// immediate-mode vertices *and* normals by the current `rlPushMatrix` transform
// on the CPU, so the shader needs no model matrix, only the camera's `mvp`.
// Every face drawn under it must therefore set a real normal (`rlNormal3f`,
// which raylib's own DrawCube and friends do) -- rlgl carries the last normal
// forward, so geometry that sets none inherits whatever came before it.
//
// A SurfaceMaterial says how a surface answers that light, and materials are
// per draw call: `set_material` flushes the pending batch, because rlgl records
// geometry now and draws it later, when only the most recent uniform values are
// still set.
//
// (The name avoids raylib's own `Material`, which is the model/mesh one.)

#include "raylib.h"

// How a surface answers the light. `spec_tint` is what separates a metal from a
// dielectric: a metal tints its highlight with its own colour, while paint,
// plastic and paper keep it white.
struct SurfaceMaterial {
    float ambient = 0.26f;    // fraction of the base colour shown unlit
    float diffuse = 0.46f;    // lambert weight
    float specular = 0.10f;   // highlight strength
    float shininess = 12.0f;  // highlight tightness
    float spec_tint = 0.0f;   // 0 = white highlight, 1 = the surface's colour
};

// The surfaces the bomb is built from. Modules pick from this list rather than
// inventing numbers, so a bomb reads as one object made of a few real
// materials -- see Puzzle::material().
//
// The panel entries are deliberately less shiny than the solid parts below
// them. A module face is one flat quad, so it meets the highlight all at once
// and a panel-sized mirror washes the whole module out just when the players
// are trying to read it; a curved part catches the same highlight in a narrow
// band, which is exactly what makes it read as curved.
namespace materials {

// Flat colour, no lighting at all: for the casing's outline edges, which are a
// drawing convention rather than a surface.
inline constexpr SurfaceMaterial unlit{1.00f, 0.00f, 0.00f, 1.0f, 0.0f};

// The casing itself: painted sheet metal, with the faint sheen paint keeps.
inline constexpr SurfaceMaterial casing{0.22f, 0.62f, 0.10f, 24.0f, 0.25f};

// Moulded panel plastic, the default a module face is made of.
inline constexpr SurfaceMaterial matte_plastic{0.26f, 0.46f, 0.04f, 10.0f, 0.0f};

// Polished mouldings: keycaps, a big button, Simon's translucent domes.
inline constexpr SurfaceMaterial glossy_plastic{0.24f, 0.46f, 0.13f, 50.0f, 0.0f};

// Wire insulation: soft, almost no highlight.
inline constexpr SurfaceMaterial rubber{0.26f, 0.48f, 0.02f, 6.0f, 0.0f};

// Printed card and punched tape: diffuse, and brighter unlit than plastic.
inline constexpr SurfaceMaterial paper{0.30f, 0.44f, 0.01f, 4.0f, 0.0f};

// Glass over a lit display: a tight, bright highlight and little diffuse.
inline constexpr SurfaceMaterial screen{0.30f, 0.36f, 0.18f, 110.0f, 0.0f};

// A face made of metal components -- pipework, a knob, a discharge lever --
// rather than a sheet of metal, so it keeps a panel's restrained highlight.
inline constexpr SurfaceMaterial metal_panel{0.22f, 0.50f, 0.15f, 34.0f, 0.50f};

// A solid machined part, such as a battery can.
inline constexpr SurfaceMaterial brushed_metal{0.20f, 0.52f, 0.45f, 26.0f, 0.75f};

// Plating and contacts: a tight highlight in the metal's own colour.
inline constexpr SurfaceMaterial polished_metal{0.16f, 0.44f, 0.85f, 90.0f, 1.0f};

}   // namespace materials

class PhongShader {
public:
    // Compiles the shader. Needs the GL context, so call it after InitWindow.
    // A shader that fails to compile leaves the object invalid rather than
    // failing the build of the scene: begin/end/set_material then do nothing
    // and the bomb draws flat, as it did before there was any lighting.
    void load();
    void unload();
    bool valid() const { return loaded_; }

    // Wrap the 3D pass. `view_pos` is the camera position, in world space.
    void begin(Vector3 view_pos) const;
    void end() const;

    // Shade everything drawn after this call with `m`. Flushes the geometry
    // recorded so far, which is what makes the material a per-object choice
    // rather than a per-frame one.
    void set_material(const SurfaceMaterial& m) const;

private:
    Shader shader_{};
    bool loaded_ = false;
    int loc_view_pos_ = -1;
    int loc_light_dir_ = -1;
    int loc_material_ = -1;
    int loc_spec_tint_ = -1;
};
