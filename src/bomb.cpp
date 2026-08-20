#include "bomb.h"

#include <array>
#include <string>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

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

    PuzzleRegistry::instance().add(
        "Wires", [] { return std::unique_ptr<Puzzle>(new WiresPuzzle()); });
}

namespace {

// Slab geometry (bomb-local units).
constexpr float half_width = 2.20f;    // half of the slab width  (X)
constexpr float half_height = 0.90f;    // half of the slab height (Y)
constexpr float half_thick = 0.30f;    // half of the slab depth  (Z)
constexpr float slot_half = 0.62f;        // half-size of a square module face
constexpr float face_offset = 0.01f;    // push quads just outside the casing
constexpr std::array<float, 3> slot_x = {-1.40f, 0.0f, 1.40f};

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

void Bomb::setup(std::mt19937& rng) {
    register_builtin_puzzles();

    attrs_ = BombAttributes::random(rng);
    build_slots();
    build_info_panels();

    for (auto& slot : slots_) {
        if (slot.puzzle) slot.puzzle->init(attrs_);
        slot.quad.tex = LoadRenderTexture(module_tex_size, module_tex_size);
        slot.quad.tex_valid = true;
    }

    // Rim panels get textures sized to their face aspect so text is not squashed.
    for (auto& panel : info_panels_) {
        const float aspect = panel.quad.half_w / panel.quad.half_h;
        int tw, th;
        if (aspect >= 1.0f) {
            th = 128;
            tw = static_cast<int>(128.0f * aspect);
        } else {
            tw = 128;
            th = static_cast<int>(128.0f / aspect);
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

void Bomb::build_slots() {
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

    auto& reg = PuzzleRegistry::instance();

    // All six bays are available for modules; this milestone fills one with Wires.
    slots_.push_back(make_slot(slot_x[0], true, nullptr));
    slots_.push_back(make_slot(slot_x[1], true, reg.create("Wires")));
    slots_.push_back(make_slot(slot_x[2], true, nullptr));
    slots_.push_back(make_slot(slot_x[0], false, nullptr));
    slots_.push_back(make_slot(slot_x[1], false, nullptr));
    slots_.push_back(make_slot(slot_x[2], false, nullptr));
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

    // Batteries: top rim (full width).
    info_panels_.push_back(make_panel(
        WidgetType::BATTERIES, Vector3{0.0f, half_height + eps, 0.0f},
        Vector3{0.0f, 1.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, -1.0f}, half_width, half_thick));

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

void Bomb::update(float dt) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        Puzzle* p = slots_[i].puzzle.get();
        if (!p) continue;
        p->update(pending_[i], dt);
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
            const int fs = static_cast<int>(h * 0.22f);
            DrawText("BATTERIES", 16, (h - fs) / 2, fs, label);
            const int n = attrs_.battery_count;
            const int bw = static_cast<int>(h * 0.34f);
            const int bh = static_cast<int>(h * 0.64f);
            const int gap = static_cast<int>(h * 0.12f);
            const int label_w = MeasureText("BATTERIES", fs) + 40;
            int x = label_w;
            const int y = (h - bh) / 2;
            for (int i = 0; i < n; ++i) {
                DrawRectangle(x, y, bw, bh, Color{70, 74, 84, 255});
                DrawRectangle(x + bw / 3, y - bh / 8, bw / 3, bh / 8,
                              Color{70, 74, 84, 255});
                DrawRectangleLines(x, y, bw, bh, value);
                x += bw + gap;
            }
            if (n == 0) DrawText("- none -", label_w, (h - fs) / 2, fs, label);
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

void Bomb::draw_faces_3d() const {
    // Slab casing.
    DrawCube(Vector3Zero(), half_width * 2, half_height * 2, half_thick * 2,
             casing_color(attrs_.color));
    DrawCubeWires(Vector3Zero(), half_width * 2, half_height * 2, half_thick * 2,
                  Color{12, 12, 14, 255});

    // Module bays and rim panels (both sides visible; sit just proud of casing).
    rlDisableBackfaceCulling();
    for (const auto& slot : slots_) {
        if (slot.quad.tex_valid) draw_face_quad(slot.quad);
    }
    for (const auto& panel : info_panels_) {
        if (panel.quad.tex_valid) draw_face_quad(panel.quad);
    }
    rlEnableBackfaceCulling();
}

int Bomb::take_strike_events() {
    const int n = strike_events_;
    strike_events_ = 0;
    return n;
}

bool Bomb::all_solved() const {
    bool any = false;
    for (const auto& slot : slots_) {
        if (!slot.puzzle) continue;
        any = true;
        if (!slot.puzzle->is_solved()) return false;
    }
    return any;
}

int Bomb::puzzle_module_count() const {
    int n = 0;
    for (const auto& slot : slots_) {
        if (slot.puzzle) ++n;
    }
    return n;
}

int Bomb::solved_module_count() const {
    int n = 0;
    for (const auto& slot : slots_) {
        if (slot.puzzle && slot.puzzle->is_solved()) ++n;
    }
    return n;
}
