#pragma once

// The Bomb owns its randomized attributes and up to six module slots laid out on
// a rectangular slab (three on the front face, three on the back). Every slot is
// available for a Puzzle module. The bomb-wide attributes the modules depend on
// (serial number, batteries, indicators) are printed on the thin rim edges of
// the casing as InfoPanels, so they occupy no module space.
//
// Each slot / panel renders its 2D content into its own RenderTexture, which the
// renderer maps onto a face quad in 3D.
//
// Coordinate note: all geometry is expressed in bomb-local space (the slab
// centred at the origin, front face towards +Z). The renderer (Game) applies the
// current rotation transform around this, and ray-casts pointer input by
// transforming the ray into this same local space.

#include <memory>
#include <random>
#include <vector>

#include "raylib.h"

#include "bomb_attributes.h"
#include "puzzle.h"

enum class WidgetType { SERIAL, BATTERIES, INDICATOR };

// A face quad in bomb-local space plus its render target.
struct FaceQuad {
    Vector3 center{};
    Vector3 normal{};  // outward face normal
    Vector3 right{};   // unit U axis across the face
    Vector3 up{};      // unit V axis up the face
    float half_w = 0.0f;
    float half_h = 0.0f;
    RenderTexture2D tex{};
    bool tex_valid = false;
};

// A module bay on the front or back face. Empty when puzzle is null.
struct ModuleSlot {
    FaceQuad quad;
    std::unique_ptr<Puzzle> puzzle;
};

// A passive attribute readout on a thin rim edge (serial / batteries / …).
struct InfoPanel {
    FaceQuad quad;
    WidgetType widget = WidgetType::SERIAL;
};

class Bomb {
 public:
    // Build attributes, lay out slots and rim panels, instantiate puzzle
    // templates, and create render targets. Call after the window/GL context
    // exists.
    void setup(std::mt19937& rng);
    void unload();

    // Queue a tap for the module in the given slot (module-local pixel coords).
    void send_input(int slot_index, const ModuleInput& in);

    // Advance all modules; aggregate strike events and solved state.
    void update(float dt);

    // Render each slot's / panel's 2D content into its RenderTexture. Call before
    // BeginDrawing.
    void render_module_textures();

    // Draw the slab body, module face quads, and rim panels in bomb-local space
    // (the caller applies the rotation transform).
    void draw_faces_3d() const;

    // ---- Queries ----
    const std::vector<ModuleSlot>& slots() const { return slots_; }
    const BombAttributes& attributes() const { return attrs_; }

    int take_strike_events(); // strikes raised since the last call
    bool all_solved() const;
    int puzzle_module_count() const;
    int solved_module_count() const;

private:
    void build_slots();
    void build_info_panels();
    void draw_info_panel(const InfoPanel& panel) const;

    BombAttributes attrs_;
    std::vector<ModuleSlot> slots_;
    std::vector<InfoPanel> info_panels_;
    std::vector<ModuleInput> pending_; // one queued input per slot
    int strike_events_ = 0;
};
