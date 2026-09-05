#include "bomb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "puzzles/button_puzzle.h"
#include "puzzles/capacitor_puzzle.h"
#include "puzzles/color_match_puzzle.h"
#include "puzzles/complicated_wires_puzzle.h"
#include "puzzles/fold_out_puzzle.h"
#include "puzzles/keypads_puzzle.h"
#include "puzzles/knobs_puzzle.h"
#include "puzzles/maze_puzzle.h"
#include "puzzles/memory_puzzle.h"
#include "puzzles/morse_puzzle.h"
#include "puzzles/passwords_puzzle.h"
#include "puzzles/pipeworks_puzzle.h"
#include "puzzles/simon_puzzle.h"
#include "puzzles/star_chart_puzzle.h"
#include "puzzles/tape_reader_puzzle.h"
#include "puzzles/venting_gas_puzzle.h"
#include "puzzles/whos_on_first_puzzle.h"
#include "puzzles/wire_sequences_puzzle.h"
#include "puzzles/wires_puzzle.h"

// ---------------------------------------------------------------------------
// Puzzle registry population. Kept here (rather than in per-puzzle static
// initializers) so the linker never drops a puzzle translation unit from the
// static library, and so registration order is explicit.
// ---------------------------------------------------------------------------
void register_builtin_puzzles() {
    static bool done = false;
    if (done) return;
    done = true;

    PuzzleRegistry& reg = PuzzleRegistry::instance();
    reg.add("Wires", [] { return std::unique_ptr<Puzzle>(new WiresPuzzle()); });
    reg.add("The Button",
            [] { return std::unique_ptr<Puzzle>(new ButtonPuzzle()); });
    reg.add("Keypads",
            [] { return std::unique_ptr<Puzzle>(new KeypadsPuzzle()); });
    reg.add("Passwords",
            [] { return std::unique_ptr<Puzzle>(new PasswordsPuzzle()); });
    reg.add("Memory", [] { return std::unique_ptr<Puzzle>(new MemoryPuzzle()); });
    reg.add("Simon Says",
            [] { return std::unique_ptr<Puzzle>(new SimonPuzzle()); });
    reg.add("Who's on First",
            [] { return std::unique_ptr<Puzzle>(new WhosOnFirstPuzzle()); });
    reg.add("Morse Code",
            [] { return std::unique_ptr<Puzzle>(new MorsePuzzle()); });
    reg.add("Complicated Wires", [] {
        return std::unique_ptr<Puzzle>(new ComplicatedWiresPuzzle());
    });
    reg.add("Wire Sequences", [] {
        return std::unique_ptr<Puzzle>(new WireSequencesPuzzle());
    });
    reg.add("Mazes", [] { return std::unique_ptr<Puzzle>(new MazePuzzle()); });
    reg.add("Fold-Out",
            [] { return std::unique_ptr<Puzzle>(new FoldOutPuzzle()); });
    reg.add("Tape Reader",
            [] { return std::unique_ptr<Puzzle>(new TapeReaderPuzzle()); });
    reg.add("Pipeworks",
            [] { return std::unique_ptr<Puzzle>(new PipeworksPuzzle()); });
    reg.add("Star Chart",
            [] { return std::unique_ptr<Puzzle>(new StarChartPuzzle()); });
    reg.add("Colour Match",
            [] { return std::unique_ptr<Puzzle>(new ColorMatchPuzzle()); });
    reg.add("Venting Gas",
            [] { return std::unique_ptr<Puzzle>(new VentingGasPuzzle()); });
    reg.add("Capacitor Discharge",
            [] { return std::unique_ptr<Puzzle>(new CapacitorPuzzle()); });
    reg.add("Knobs", [] { return std::unique_ptr<Puzzle>(new KnobsPuzzle()); });
}

namespace {

// Slab geometry (bomb-local units).
constexpr float half_width = 2.20f;    // half of the slab width  (X)
constexpr float half_height = 0.90f;    // half of the slab height (Y)
constexpr float half_thick = 0.30f;    // half of the slab depth  (Z)
constexpr float slot_half = 0.62f;        // half-size of a square module face
constexpr float face_offset = 0.01f;    // push quads just outside the casing
constexpr std::array<float, 3> slot_x = {-1.40f, 0.0f, 1.40f};
constexpr size_t slot_count = 6;   // three front bays, three back

// The order bays are filled in as the module count rises: the front face
// first, then the back, and each face from the middle outwards, left before
// right. Indices are into `slots_`, which holds the three front bays in
// ascending local X and then the three back ones. The back face's quad faces
// -Z, so a defuser turning the bomb over sees its local +X bay on their left:
// the back is listed 4, 5, 3 to read middle, left, right from where they
// stand, the same as the front's 1, 0, 2.
constexpr std::array<size_t, slot_count> fill_order = {1, 0, 2, 4, 5, 3};

// Battery compartment: a rectangular recess milled into the left end of the top
// rim, split by dividers into `battery_slot_count` cell wells. The cells are
// real cylinders lying along Z that stand proud of the casing; a well with no
// cell in it shows its bare contacts, so the count is read by looking, not by
// reading a label.
constexpr int battery_slot_count = 4;
constexpr float battery_radius = 0.085f;   // cell radius
constexpr float battery_length = 0.38f;    // cell length, along Z
constexpr float battery_pitch = 0.245f;    // well spacing, along X
constexpr float tray_depth = 0.10f;        // how far the recess sinks
constexpr float tray_half_w =
    battery_slot_count * battery_pitch * 0.5f + 0.03f;
constexpr float tray_half_d = battery_length * 0.5f + 0.055f;
constexpr float tray_floor_y = half_height - tray_depth;
constexpr float tray_margin = 0.18f;       // rim left outboard of the recess
constexpr float tray_center_x = -(half_width - tray_half_w - tray_margin);
constexpr float divider_half_w = 0.012f;
constexpr float divider_height = 0.055f;

// Centre of cell well `i`, measured from the middle of the tray.
constexpr float battery_slot_offset(int i) {
    return (static_cast<float>(i) -
            (battery_slot_count - 1) * 0.5f) * battery_pitch;
}

// Centre of cell well `i` in bomb-local X.
constexpr float battery_slot_x(int i) {
    return tray_center_x + battery_slot_offset(i);
}

// Every module template that can appear on a bomb, in a fixed order: bomb
// generation shuffles this, and PuzzleRegistry::names() cannot be used for it
// because an unordered_map's order is not reproducible from the bomb's seed.
constexpr std::array<const char*, 19> module_templates = {
    "Wires",
    "The Button",
    "Keypads",
    "Passwords",
    "Memory",
    "Simon Says",
    "Who's on First",
    "Morse Code",
    "Complicated Wires",
    "Wire Sequences",
    "Mazes",
    "Fold-Out",
    "Tape Reader",
    "Pipeworks",
    "Star Chart",
    "Colour Match",
    "Venting Gas",
    "Capacitor Discharge",
    "Knobs",
};


Color casing_color(BombColor c) {
    switch (c) {
        case BombColor::BOMB_BLACK:  return Color{40, 42, 48, 255};
        case BombColor::BOMB_WHITE:  return Color{200, 202, 208, 255};
        case BombColor::BOMB_GREEN:  return Color{34, 70, 46, 255};
        case BombColor::BOMB_RED:    return Color{92, 38, 42, 255};
        case BombColor::BOMB_BLUE:   return Color{36, 48, 84, 255};
    }
    return Color{40, 42, 48, 255};
}

void draw_centered_text(const char* text, int cx, int cy, int font_size, Color color) {
    const int w = MeasureText(text, font_size);
    DrawText(text, cx - w / 2, cy - font_size / 2, font_size, color);
}

// Textured quad in 3D from a face frame. UV is V-flipped to compensate for the
// vertical orientation of raylib render textures.
void draw_face_quad(const FaceQuad& q) {
    const Vector3 r_w = Vector3Scale(q.right, q.half_w);
    const Vector3 u_h = Vector3Scale(q.up, q.half_h);
    const Vector3 bl = Vector3Subtract(Vector3Subtract(q.center, r_w), u_h);
    const Vector3 br = Vector3Subtract(Vector3Add(q.center, r_w), u_h);
    const Vector3 tr = Vector3Add(Vector3Add(q.center, r_w), u_h);
    const Vector3 tl = Vector3Add(Vector3Subtract(q.center, r_w), u_h);

    rlSetTexture(q.tex.texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlNormal3f(q.normal.x, q.normal.y, q.normal.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(bl.x, bl.y, bl.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(br.x, br.y, br.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(tr.x, tr.y, tr.z);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(tl.x, tl.y, tl.z);
    rlEnd();
    rlSetTexture(0);
}

// ---- 3D primitives -------------------------------------------------------
// These only lay down geometry: colour comes from the vertex colour and the
// shape from the normals, and PhongShader does the lighting. Every face is
// wound counter-clockwise as seen from outside, so backface culling can stay
// on, and every vertex carries a normal -- rlgl keeps handing the last normal
// to each new vertex, so geometry that sets none inherits whatever came before.

Color darken(Color c, float k) {
    return Color{static_cast<unsigned char>(c.r * k),
                 static_cast<unsigned char>(c.g * k),
                 static_cast<unsigned char>(c.b * k), c.a};
}

// Quad a-b-c-d, wound so that (b-a) x (d-a) is the outward normal.
void draw_quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    const Vector3 n = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(d, a)));
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(n.x, n.y, n.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
}

// Axis-aligned box from its min to its max corner.
void draw_box(Vector3 lo, Vector3 hi, Color col) {
    draw_quad(Vector3{lo.x, hi.y, hi.z}, Vector3{hi.x, hi.y, hi.z},
              Vector3{hi.x, hi.y, lo.z}, Vector3{lo.x, hi.y, lo.z}, col);
    draw_quad(Vector3{lo.x, lo.y, lo.z}, Vector3{hi.x, lo.y, lo.z},
              Vector3{hi.x, lo.y, hi.z}, Vector3{lo.x, lo.y, hi.z}, col);
    draw_quad(Vector3{lo.x, lo.y, hi.z}, Vector3{hi.x, lo.y, hi.z},
              Vector3{hi.x, hi.y, hi.z}, Vector3{lo.x, hi.y, hi.z}, col);
    draw_quad(Vector3{hi.x, lo.y, lo.z}, Vector3{lo.x, lo.y, lo.z},
              Vector3{lo.x, hi.y, lo.z}, Vector3{hi.x, hi.y, lo.z}, col);
    draw_quad(Vector3{hi.x, lo.y, hi.z}, Vector3{hi.x, lo.y, lo.z},
              Vector3{hi.x, hi.y, lo.z}, Vector3{hi.x, hi.y, hi.z}, col);
    draw_quad(Vector3{lo.x, lo.y, lo.z}, Vector3{lo.x, lo.y, hi.z},
              Vector3{lo.x, hi.y, hi.z}, Vector3{lo.x, hi.y, lo.z}, col);
}

// Capped cylinder between two points. The side vertices carry the true surface
// normal rather than one per facet, so the shader rounds the silhouette off
// instead of leaving twenty visible flats.
void draw_cylinder(Vector3 a, Vector3 b, float radius, Color col,
                   int sides = 20) {
    Vector3 axis = Vector3Subtract(b, a);
    const float len = Vector3Length(axis);
    if (len <= 0.0001f || radius <= 0.0f) return;
    axis = Vector3Scale(axis, 1.0f / len);

    // Any seed not parallel to the axis gives a usable cross-section frame.
    const Vector3 seed =
        (axis.y > 0.9f || axis.y < -0.9f) ? Vector3{1.0f, 0.0f, 0.0f}
                                          : Vector3{0.0f, 1.0f, 0.0f};
    const Vector3 u = Vector3Normalize(Vector3CrossProduct(seed, axis));
    const Vector3 v = Vector3CrossProduct(axis, u);

    auto rim = [&](float t) {
        return Vector3Add(Vector3Scale(u, cosf(t)), Vector3Scale(v, sinf(t)));
    };

    for (int i = 0; i < sides; ++i) {
        const float t0 = 2.0f * PI * i / sides;
        const float t1 = 2.0f * PI * (i + 1) / sides;
        const Vector3 n0 = rim(t0);
        const Vector3 n1 = rim(t1);
        const Vector3 o0 = Vector3Scale(n0, radius);
        const Vector3 o1 = Vector3Scale(n1, radius);
        const Vector3 a0 = Vector3Add(a, o0);
        const Vector3 a1 = Vector3Add(a, o1);
        const Vector3 b0 = Vector3Add(b, o0);
        const Vector3 b1 = Vector3Add(b, o1);

        rlBegin(RL_QUADS);
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlTexCoord2f(0.0f, 0.0f);
        rlNormal3f(n0.x, n0.y, n0.z); rlVertex3f(a0.x, a0.y, a0.z);
        rlNormal3f(n1.x, n1.y, n1.z); rlVertex3f(a1.x, a1.y, a1.z);
        rlNormal3f(n1.x, n1.y, n1.z); rlVertex3f(b1.x, b1.y, b1.z);
        rlNormal3f(n0.x, n0.y, n0.z); rlVertex3f(b0.x, b0.y, b0.z);
        rlEnd();

        rlBegin(RL_TRIANGLES);
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlNormal3f(-axis.x, -axis.y, -axis.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(a.x, a.y, a.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(a1.x, a1.y, a1.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(a0.x, a0.y, a0.z);
        rlNormal3f(axis.x, axis.y, axis.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(b.x, b.y, b.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(b0.x, b0.y, b0.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(b1.x, b1.y, b1.z);
        rlEnd();
    }
}

// One cell lying in the well centred at `x`, resting on the tray floor: a dark
// jacket with a brass label band, a plated flat end, and a nub for the +
// terminal at the front (+Z) end. The caller sets the material per part, since
// the can and the plating do not catch the light the same way.
void draw_battery_cell(float x, const PhongShader& shading) {
    const float y = tray_floor_y + battery_radius;
    const float z0 = -battery_length * 0.5f;
    const float z1 = battery_length * 0.5f;
    const float r = battery_radius;
    const Color jacket{44, 46, 54, 255};
    const Color band{182, 142, 48, 255};
    const Color plate{158, 161, 170, 255};
    const Color terminal{212, 210, 202, 255};

    auto at = [x, y](float z) { return Vector3{x, y, z}; };

    shading.set_material(materials::brushed_metal);
    draw_cylinder(at(z0 + 0.03f), at(z1 - 0.055f), r, jacket);

    shading.set_material(materials::polished_metal);
    draw_cylinder(at(z0), at(z0 + 0.03f), r * 0.99f, plate);
    draw_cylinder(at(z0 + 0.10f), at(z1 - 0.12f), r + 0.003f, band);
    draw_cylinder(at(z1 - 0.055f), at(z1 - 0.015f), r * 0.96f, plate);
    draw_cylinder(at(z1 - 0.015f), at(z1 + 0.03f), r * 0.34f, terminal);
}

// Draw a recessed, empty module bay into the currently-bound texture.
void draw_empty_bay() {
    constexpr int s = module_tex_size;
    ClearBackground(Color{20, 21, 25, 255});
    DrawRectangle(24, 24, s - 48, s - 48, Color{28, 30, 35, 255});
    const Color screw = Color{50, 52, 58, 255};
    DrawCircle(44, 44, 6, screw);
    DrawCircle(s - 44, 44, 6, screw);
    DrawCircle(44, s - 44, 6, screw);
    DrawCircle(s - 44, s - 44, 6, screw);
}

}   // namespace

const std::vector<std::string>& Bomb::module_template_names() {
    static const std::vector<std::string> names(module_templates.begin(),
                                                module_templates.end());
    return names;
}

void Bomb::setup(std::mt19937& rng, const std::string& serial, int module_count,
                 const char* only_module) {
    register_builtin_puzzles();

    attrs_ = BombAttributes::random(rng, serial);
    const std::vector<size_t> init_order =
        build_slots(rng, module_count, only_module);
    build_info_panels();

    // Modules are initialised in rank order rather than bay order, and every
    // draw made before this point takes the same number of values whatever
    // the module count. Rank N therefore always meets the rng in the same
    // state, so adding a module to a bomb cannot change the variables of the
    // ones already on it.
    for (size_t slot : init_order) slots_[slot].puzzle->init(attrs_, rng);

    for (auto& slot : slots_) {
        slot.quad.tex = LoadRenderTexture(module_tex_size, module_tex_size);
        slot.quad.tex_valid = true;
    }

    // Rim panels get textures sized to their face aspect so text is not squashed.
    for (auto& panel : info_panels_) {
        const float aspect = panel.quad.half_w / panel.quad.half_h;
        int tw, th;
        constexpr float base = 256.0f;
        if (aspect >= 1.0f) {
            th = static_cast<int>(base);
            tw = static_cast<int>(base * aspect);
        } else {
            tw = static_cast<int>(base);
            th = static_cast<int>(base / aspect);
        }
        panel.quad.tex = LoadRenderTexture(tw, th);
        panel.quad.tex_valid = true;
    }

    pending_.assign(slots_.size(), ModuleInput{});
}

void Bomb::unload() {
    for (auto& slot : slots_) {
        if (slot.quad.tex_valid) {
            UnloadRenderTexture(slot.quad.tex);
            slot.quad.tex_valid = false;
        }
    }
    for (auto& panel : info_panels_) {
        if (panel.quad.tex_valid) {
            UnloadRenderTexture(panel.quad.tex);
            panel.quad.tex_valid = false;
        }
    }
    slots_.clear();
    info_panels_.clear();
    pending_.clear();
}

std::vector<size_t> Bomb::build_slots(std::mt19937& rng, int module_count,
                                      const char* only_module) {
    slots_.clear();

    auto make_slot = [](float x, bool front, std::unique_ptr<Puzzle> puzzle) {
        ModuleSlot slot;
        FaceQuad& q = slot.quad;
        const float z = front ? (half_thick + face_offset) : -(half_thick + face_offset);
        q.center = Vector3{x, 0.0f, z};
        q.normal = Vector3{0.0f, 0.0f, front ? 1.0f : -1.0f};
        q.right = Vector3{front ? 1.0f : -1.0f, 0.0f, 0.0f};    // flip on back
        q.up = Vector3{0.0f, 1.0f, 0.0f};
        q.half_w = slot_half;
        q.half_h = slot_half;
        slot.puzzle = std::move(puzzle);
        return slot;
    };

    const auto& reg = PuzzleRegistry::instance();

    // Debug mode: one named module, alone in the front-centre bay. Choosing it
    // draws nothing from the pool, so the rng reaches its init() in the same
    // state whichever module was picked -- one serial, one set of variables.
    if (only_module != nullptr) {
        for (size_t i = 0; i < slot_count; ++i) {
            const bool front = i < slot_x.size();
            slots_.push_back(make_slot(
                slot_x[i % slot_x.size()], front,
                i == fill_order[0] ? reg.create(only_module) : nullptr));
        }
        return {fill_order[0]};
    }

    // Rank six distinct templates for the six bays. This draw is made in full
    // whatever the module count asks for, so the ranking -- and every value it
    // takes from the rng -- depends on the serial alone. The count only says
    // where to cut the list. Bays go empty only while there are fewer than six
    // templates registered.
    std::vector<const char*> pool(module_templates.begin(),
                                  module_templates.end());
    std::shuffle(pool.begin(), pool.end(), rng);

    std::vector<std::unique_ptr<Puzzle>> ranked;
    size_t needy_rank = slot_count;    // slot_count: none on the list
    for (const char* name : pool) {
        if (ranked.size() >= slot_count) break;
        std::unique_ptr<Puzzle> puzzle = reg.create(name);
        if (!puzzle) continue;
        // Needy modules are never disarmed, so a bomb built only from them
        // could never be defused: one is the limit, which leaves at least
        // five bays solvable.
        if (puzzle->is_needy()) {
            if (needy_rank < slot_count) continue;
            needy_rank = ranked.size();
        }
        ranked.push_back(std::move(puzzle));
    }

    // A bomb needs modules that can actually be disarmed, so the needy one
    // never appears on the smallest bombs. Demoting it to rank `first_needy`
    // rather than dropping it keeps every longer prefix holding the same set,
    // which is what makes one step up the slider purely additive.
    constexpr size_t first_needy = 3 - 1;
    if (needy_rank < first_needy && ranked.size() > first_needy) {
        std::swap(ranked[needy_rank], ranked[first_needy]);
    }

    const size_t wanted = static_cast<size_t>(
        module_count < 1 ? 1 : (module_count > static_cast<int>(slot_count)
                                    ? static_cast<int>(slot_count)
                                    : module_count));
    ranked.resize(slot_count);   // pads with nullptr if templates run short

    // Cut the ranking to the count and seat it, filling `fill_order`'s bays in
    // order. Where a rank sits is fixed, so a module keeps its bay as the cut
    // moves.
    std::vector<std::unique_ptr<Puzzle>> by_bay(slot_count);
    std::vector<size_t> init_order;
    for (size_t rank = 0; rank < wanted; ++rank) {
        if (!ranked[rank]) continue;
        const size_t bay = fill_order[rank];
        by_bay[bay] = std::move(ranked[rank]);
        init_order.push_back(bay);
    }

    for (size_t i = 0; i < slot_count; ++i) {
        slots_.push_back(make_slot(slot_x[i % slot_x.size()],
                                   i < slot_x.size(), std::move(by_bay[i])));
    }
    return init_order;
}

void Bomb::build_info_panels() {
    info_panels_.clear();

    auto make_panel = [](WidgetType widget, Vector3 center, Vector3 normal,
                         Vector3 right, Vector3 up, float half_w, float half_h) {
        InfoPanel panel;
        panel.widget = widget;
        panel.quad.center = center;
        panel.quad.normal = normal;
        panel.quad.right = right;
        panel.quad.up = up;
        panel.quad.half_w = half_w;
        panel.quad.half_h = half_h;
        return panel;
    };

    const float eps = face_offset;

    // Serial: bottom rim (full width).
    info_panels_.push_back(make_panel(
        WidgetType::SERIAL, Vector3{0.0f, -(half_height + eps), 0.0f},
        Vector3{0.0f, -1.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 1.0f}, half_width, half_thick));

    // Batteries: the floor of the tray recessed into the top rim. The cells
    // themselves are drawn in 3D standing in the wells this floor prints.
    info_panels_.push_back(make_panel(
        WidgetType::BATTERIES,
        Vector3{tray_center_x, tray_floor_y + 0.004f, 0.0f},
        Vector3{0.0f, 1.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, -1.0f}, tray_half_w, tray_half_d));

    // Indicators: right rim (upright, stacked).
    info_panels_.push_back(make_panel(
        WidgetType::INDICATOR, Vector3{half_width + eps, 0.0f, 0.0f},
        Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, -1.0f},
        Vector3{0.0f, 1.0f, 0.0f}, half_thick, half_height));
}

void Bomb::send_input(int slot_index, const ModuleInput& in) {
    if (slot_index < 0 || slot_index >= static_cast<int>(slots_.size())) return;
    if (!slots_[slot_index].puzzle) return;
    pending_[slot_index] = in;
}

void Bomb::update(float dt, const BombContext& ctx) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        Puzzle* p = slots_[i].puzzle.get();
        if (!p) continue;
        p->update(pending_[i], ctx, dt);
        pending_[i] = ModuleInput{};
        while (p->consume_strike()) ++strike_events_;
    }
}

void Bomb::draw_info_panel(const InfoPanel& panel) const {
    const int w = panel.quad.tex.texture.width;
    const int h = panel.quad.tex.texture.height;
    const Color label = Color{150, 158, 170, 255};
    const Color value = RAYWHITE;

    switch (panel.widget) {
        case WidgetType::SERIAL: {
            const int fs = static_cast<int>(h * 0.42f);
            DrawText("SERIAL", 16, (h - static_cast<int>(h * 0.22f)) / 2 - 2,
                     static_cast<int>(h * 0.22f), label);
            draw_centered_text(attrs_.serial.c_str(), w / 2 + static_cast<int>(w * 0.06f),
                               h / 2, fs, value);
            break;
        }
        case WidgetType::BATTERIES: {
            // The floor of the tray: one well per cell, each with a coil at the
            // negative end and a flat plate at the positive one. Wells holding a
            // cell are covered by it in 3D, so this is what an empty one shows.
            const Color floor_col{32, 33, 39, 255};
            const Color well_col{11, 12, 15, 255};
            const Color contact{150, 153, 162, 255};
            const float fw = static_cast<float>(w);
            const float fh = static_cast<float>(h);
            const float well_w = fw * battery_radius / tray_half_w;
            const float well_h = fh * (battery_length + 0.05f) /
                                 (2.0f * tray_half_d);
            DrawRectangle(0, 0, w, h, floor_col);
            for (int i = 0; i < battery_slot_count; ++i) {
                const float cx = fw * (battery_slot_offset(i) + tray_half_w) /
                                 (2.0f * tray_half_w);
                const float top = (fh - well_h) * 0.5f;
                DrawRectangleRounded(
                    Rectangle{cx - well_w * 0.5f, top, well_w, well_h}, 0.35f, 8,
                    well_col);
                // Coil at the negative (-Z) end, which is the top of the texture.
                const float coil_w = well_w * 0.62f;
                for (int k = 0; k < 4; ++k) {
                    const float cy = top + well_h * 0.06f + k * fh * 0.055f;
                    DrawRectangleRec(
                        Rectangle{cx - coil_w * 0.5f, cy, coil_w, fh * 0.022f},
                        contact);
                }
                // Flat plate at the positive (+Z) end.
                DrawRectangleRec(
                    Rectangle{cx - well_w * 0.28f,
                              top + well_h - fh * 0.10f,
                              well_w * 0.56f, fh * 0.07f},
                    contact);
            }
            break;
        }
        case WidgetType::INDICATOR: {
            const int fs = static_cast<int>(w * 0.20f);
            draw_centered_text("IND", w / 2, static_cast<int>(h * 0.06f) + fs / 2, fs,
                                                 label);
            if (attrs_.indicators.empty()) {
                draw_centered_text("none", w / 2, h / 2, fs, label);
                break;
            }
            int y = static_cast<int>(h * 0.14f);
            const int row = static_cast<int>(h * 0.13f);
            const int lfs = static_cast<int>(w * 0.22f);
            for (const auto& ind : attrs_.indicators) {
                const Color led =
                        ind.lit ? Color{90, 220, 120, 255} : Color{70, 74, 84, 255};
                DrawCircle(static_cast<int>(w * 0.22f), y + row / 2,
                                     static_cast<float>(w) * 0.08f, led);
                DrawText(ind.label.c_str(), static_cast<int>(w * 0.38f),
                                 y + (row - lfs) / 2, lfs, value);
                y += row;
            }
            break;
        }
    }
}

void Bomb::render_module_textures() {
    for (auto& slot : slots_) {
        if (!slot.quad.tex_valid) continue;
        BeginTextureMode(slot.quad.tex);
        if (slot.puzzle) {
            ClearBackground(Color{24, 26, 30, 255});
            slot.puzzle->draw();
        } else {
            draw_empty_bay();
        }
        DrawRectangleLinesEx(
                Rectangle{2, 2, module_tex_size - 4, module_tex_size - 4}, 4,
                Color{12, 12, 14, 255});
        EndTextureMode();
    }

    for (auto& panel : info_panels_) {
        if (!panel.quad.tex_valid) continue;
        BeginTextureMode(panel.quad.tex);
        ClearBackground(Color{18, 19, 22, 255});
        draw_info_panel(panel);
        EndTextureMode();
    }
}

void Bomb::draw_casing(const PhongShader& shading) const {
    const Color body = casing_color(attrs_.color);

    // Five boxes rather than one cube: a single cube would lay a solid top face
    // over the battery tray, hiding the recess. Together they are the same slab.
    shading.set_material(materials::casing);
    auto box = [](Vector3 lo, Vector3 hi, Color c) {
        draw_box(lo, hi, c);
    };
    box(Vector3{-half_width, -half_height, -half_thick},
        Vector3{half_width, tray_floor_y, half_thick}, body);
    box(Vector3{-half_width, tray_floor_y, -half_thick},
        Vector3{tray_center_x - tray_half_w, half_height, half_thick}, body);
    box(Vector3{tray_center_x + tray_half_w, tray_floor_y, -half_thick},
        Vector3{half_width, half_height, half_thick}, body);
    box(Vector3{tray_center_x - tray_half_w, tray_floor_y, -half_thick},
        Vector3{tray_center_x + tray_half_w, half_height, -tray_half_d}, body);
    box(Vector3{tray_center_x - tray_half_w, tray_floor_y, tray_half_d},
        Vector3{tray_center_x + tray_half_w, half_height, half_thick}, body);

    // The outline is a drawing convention, not a surface: keep the light off it.
    shading.set_material(materials::unlit);
    DrawCubeWires(Vector3Zero(), half_width * 2, half_height * 2, half_thick * 2,
                  Color{12, 12, 14, 255});
}

void Bomb::draw_battery_tray(const PhongShader& shading) const {
    const Color inner = darken(casing_color(attrs_.color), 0.70f);
    const Color divider = darken(casing_color(attrs_.color), 0.80f);
    const float e = 0.002f;   // keep the walls off the casing boxes
    const float lo_y = tray_floor_y;
    const float hi_y = half_height;

    // Bind the default (white) texture rather than rlSetTexture(0): that only
    // records the state, so quads issued after a textured face quad would land
    // in its draw call and come out multiplied by its top-left texel.
    rlSetTexture(rlGetTextureIdDefault());

    // The recess is milled out of the casing, so it is the same material; the
    // interior is tinted down for the shadow the single light cannot cast.
    shading.set_material(materials::casing);

    // Recess walls, wound so their normals point into the tray.
    const float xl = tray_center_x - tray_half_w + e;
    const float xr = tray_center_x + tray_half_w - e;
    const float zb = -tray_half_d + e;
    const float zf = tray_half_d - e;
    draw_quad(Vector3{xl, lo_y, zf}, Vector3{xl, lo_y, zb},
              Vector3{xl, hi_y, zb}, Vector3{xl, hi_y, zf}, inner);
    draw_quad(Vector3{xr, lo_y, zb}, Vector3{xr, lo_y, zf},
              Vector3{xr, hi_y, zf}, Vector3{xr, hi_y, zb}, inner);
    draw_quad(Vector3{xl, lo_y, zb}, Vector3{xr, lo_y, zb},
              Vector3{xr, hi_y, zb}, Vector3{xl, hi_y, zb}, inner);
    draw_quad(Vector3{xr, lo_y, zf}, Vector3{xl, lo_y, zf},
              Vector3{xl, hi_y, zf}, Vector3{xr, hi_y, zf}, inner);

    // Dividers between the wells, low enough for the cells to nestle between.
    for (int i = 1; i < battery_slot_count; ++i) {
        const float x = battery_slot_x(i) - battery_pitch * 0.5f;
        draw_box(Vector3{x - divider_half_w, lo_y, zb + 0.02f},
                 Vector3{x + divider_half_w, lo_y + divider_height, zf - 0.02f},
                 divider);
    }

    const int cells = attrs_.battery_count < battery_slot_count
                          ? attrs_.battery_count
                          : battery_slot_count;
    for (int i = 0; i < cells; ++i) {
        draw_battery_cell(battery_slot_x(i), shading);
    }

    rlSetTexture(0);
}

void Bomb::draw_faces_3d(const PhongShader& shading) const {
    draw_casing(shading);

    // Module bays: each face is lit as whatever its components are made of, so
    // the material comes from the module itself.
    for (const auto& slot : slots_) {
        if (!slot.quad.tex_valid) continue;
        shading.set_material(slot.puzzle ? slot.puzzle->material()
                                         : materials::matte_plastic);
        draw_face_quad(slot.quad);
    }

    // Rim panels: printed labels on the casing, except the battery tray floor,
    // which is machined casing and is lit with the tray around it.
    for (const auto& panel : info_panels_) {
        if (!panel.quad.tex_valid) continue;
        shading.set_material(panel.widget == WidgetType::BATTERIES
                                 ? materials::casing
                                 : materials::matte_plastic);
        draw_face_quad(panel.quad);
    }

    draw_battery_tray(shading);
}

int Bomb::take_strike_events() {
    const int n = strike_events_;
    strike_events_ = 0;
    return n;
}

bool Bomb::all_solved() const {
    bool any = false;
    for (const auto& slot : slots_) {
        if (!slot.puzzle || slot.puzzle->is_needy()) continue;
        any = true;
        if (!slot.puzzle->is_solved()) return false;
    }
    return any;
}

int Bomb::puzzle_module_count() const {
    int n = 0;
    for (const auto& slot : slots_) {
        if (slot.puzzle && !slot.puzzle->is_needy()) ++n;
    }
    return n;
}

int Bomb::solved_module_count() const {
    int n = 0;
    for (const auto& slot : slots_) {
        if (slot.puzzle && !slot.puzzle->is_needy() && slot.puzzle->is_solved()) {
            ++n;
        }
    }
    return n;
}
