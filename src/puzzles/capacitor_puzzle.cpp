#include "puzzles/capacitor_puzzle.h"

#include "raylib.h"

namespace {

// The capacitor fills in a little over a minute and empties in a few seconds,
// so it needs attention now and then but never for long.
constexpr float charge_per_second = 0.014f;
constexpr float discharge_per_second = 0.30f;

// Module-local layout.
constexpr float meter_x = 56.0f;
constexpr float meter_y = 96.0f;
constexpr float meter_w = 150.0f;
constexpr float meter_h = 320.0f;
constexpr float lever_x = 268.0f;
constexpr float lever_y = 96.0f;
constexpr float lever_w = 190.0f;
constexpr float lever_h = 320.0f;

Rectangle lever_rect() { return Rectangle{lever_x, lever_y, lever_w, lever_h}; }

} // namespace

void CapacitorPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;
    charge_ = 0.0f;
    holding_ = false;
}

bool CapacitorPuzzle::lever_at_pixel(Vector2 p) const {
    return CheckCollisionPointRec(p, lever_rect());
}

void CapacitorPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                             float dt) {
    (void)ctx;

    // This module ignores the dormant/active cycle: the capacitor is always
    // charging, which is what makes it needy.
    if (in.pressed && lever_at_pixel(in.pointer_pos)) holding_ = true;
    if (in.released || !in.held) holding_ = false;

    if (holding_) {
        charge_ -= discharge_per_second * dt;
        if (charge_ < 0.0f) charge_ = 0.0f;
    } else {
        charge_ += charge_per_second * dt;
        if (charge_ >= 1.0f) {
            raise_strike();
            charge_ = 0.0f;
        }
    }
}

void CapacitorPuzzle::draw() {
    const bool danger = charge_ > 0.7f;
    DrawCircle(module_tex_size - 54, 48, 19,
               danger ? Color{230, 70, 70, 255} : Color{60, 63, 70, 255});
    DrawText("NEEDY", 34, 34, 26, Color{150, 158, 170, 255});

    // Charge meter, filling from the bottom.
    const Rectangle meter{meter_x, meter_y, meter_w, meter_h};
    DrawRectangleRec(meter, Color{20, 21, 26, 255});
    const float fill = meter_h * (charge_ < 0.0f ? 0.0f
                                                 : (charge_ > 1.0f ? 1.0f
                                                                   : charge_));
    DrawRectangleRec(
        Rectangle{meter_x, meter_y + meter_h - fill, meter_w, fill},
        danger ? Color{230, 70, 70, 255} : Color{240, 180, 60, 255});
    DrawRectangleLinesEx(meter, 5, Color{16, 16, 20, 255});

    // Lever: swings down while held.
    const Rectangle lever = lever_rect();
    DrawRectangleRec(lever, Color{34, 36, 42, 255});
    DrawRectangleLinesEx(lever, 4, Color{16, 16, 20, 255});

    const float knob_y = holding_ ? lever_y + lever_h - 70.0f : lever_y + 34.0f;
    DrawRectangleRec(
        Rectangle{lever_x + 40.0f, knob_y, lever_w - 80.0f, 62.0f},
        Color{170, 174, 184, 255});
    DrawLineEx(Vector2{lever_x + lever_w * 0.5f, lever_y + 20.0f},
               Vector2{lever_x + lever_w * 0.5f, knob_y + 31.0f}, 10.0f,
               Color{110, 114, 124, 255});

    const char* hint = holding_ ? "DISCHARGING" : "HOLD";
    const int fs = 24;
    DrawText(hint,
             static_cast<int>(lever.x + (lever.width -
                                         MeasureText(hint, fs)) * 0.5f),
             static_cast<int>(lever.y + lever.height - 32.0f), fs,
             Color{200, 204, 212, 255});
}
