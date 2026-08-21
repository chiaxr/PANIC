#include "puzzles/venting_gas_puzzle.h"

#include <random>

#include "raylib.h"

namespace {

// Module-local layout.
constexpr float display_x = 46.0f;
constexpr float display_y = 96.0f;
constexpr float display_w = 420.0f;
constexpr float display_h = 130.0f;
constexpr float btn_w = 180.0f;
constexpr float btn_h = 100.0f;
constexpr float btn_y = 288.0f;

Rectangle yes_rect() { return Rectangle{58.0f, btn_y, btn_w, btn_h}; }
Rectangle no_rect() { return Rectangle{274.0f, btn_y, btn_w, btn_h}; }

} // namespace

void VentingGasPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;
    reset_needy();
}

void VentingGasPuzzle::on_activate() {
    // "VENT GAS?" wants Y; "DETONATE?" wants N.
    vent_prompt_ = std::uniform_int_distribution<int>(0, 1)(needy_rng()) != 0;
}

void VentingGasPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                              float dt) {
    (void)ctx;
    tick_needy(dt);

    if (!active() || !in.tapped) return;

    const bool yes = CheckCollisionPointRec(in.tap_pos, yes_rect());
    const bool no = CheckCollisionPointRec(in.tap_pos, no_rect());
    if (!yes && !no) return;

    // Venting is right when it asks to vent; detonating never is.
    if (yes == vent_prompt_) {
        satisfy();
    } else {
        raise_strike();
        satisfy();
    }
}

void VentingGasPuzzle::draw() {
    // Needy modules are never disarmed, so the corner lamp shows "awake"
    // rather than "solved".
    DrawCircle(module_tex_size - 54, 48, 19,
               active() ? Color{240, 180, 60, 255} : Color{60, 63, 70, 255});
    DrawText("NEEDY", 34, 34, 26, Color{150, 158, 170, 255});

    const Rectangle display{display_x, display_y, display_w, display_h};
    DrawRectangleRec(display, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(display, 5, Color{16, 16, 20, 255});

    if (active()) {
        const char* text = vent_prompt_ ? "VENT GAS?" : "DETONATE?";
        const int fs = 48;
        DrawText(text,
                 static_cast<int>(display.x +
                                  (display.width - MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(display.y + (display.height - fs) * 0.5f), fs,
                 Color{120, 240, 150, 255});

        // Countdown bar.
        DrawRectangle(static_cast<int>(display_x),
                      static_cast<int>(display_y + display_h + 14),
                      static_cast<int>(display_w * needy_fraction()), 18,
                      Color{240, 180, 60, 255});
    }

    const char* const labels[2] = {"Y", "N"};
    const Rectangle rects[2] = {yes_rect(), no_rect()};
    for (int i = 0; i < 2; ++i) {
        DrawRectangleRec(rects[i], Color{60, 64, 76, 255});
        DrawRectangleLinesEx(rects[i], 4, Color{16, 16, 20, 255});
        const int fs = 56;
        DrawText(labels[i],
                 static_cast<int>(rects[i].x +
                                  (rects[i].width -
                                   MeasureText(labels[i], fs)) * 0.5f),
                 static_cast<int>(rects[i].y + (rects[i].height - fs) * 0.5f),
                 fs, Color{232, 234, 240, 255});
    }
}
