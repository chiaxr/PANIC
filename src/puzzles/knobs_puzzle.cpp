#include "puzzles/knobs_puzzle.h"

#include <cmath>
#include <random>

#include "raylib.h"

namespace {

using KP = KnobsPuzzle::KnobPosition;

// PANIC's own patterns. Bit 0 is the top-left light; bits run left to right
// along the top row of six, then the bottom row. manual/index.html draws the
// same sixteen patterns.
struct PatternEntry { int pattern; KP position; };
constexpr int pattern_count = 16;
constexpr PatternEntry patterns[pattern_count] = {
    {0xAE1, KP::KNOB_RIGHT}, {0x811, KP::KNOB_LEFT},
    {0x2BB, KP::KNOB_UP},    {0x4D4, KP::KNOB_DOWN},
    {0xF2F, KP::KNOB_LEFT},  {0x983, KP::KNOB_DOWN},
    {0x0E6, KP::KNOB_RIGHT}, {0x340, KP::KNOB_LEFT},
    {0xD3A, KP::KNOB_UP},    {0x2E0, KP::KNOB_UP},
    {0x508, KP::KNOB_RIGHT}, {0x1B5, KP::KNOB_UP},
    {0x8E0, KP::KNOB_DOWN},  {0x6CE, KP::KNOB_RIGHT},
    {0xE88, KP::KNOB_LEFT},  {0x967, KP::KNOB_DOWN},
};

// Module-local layout.
constexpr float led_x0 = 56.0f;
constexpr float led_y0 = 104.0f;
constexpr float led_dx = 68.0f;
constexpr float led_dy = 72.0f;
constexpr float knob_cx = 256.0f;
constexpr float knob_cy = 356.0f;
constexpr float knob_r = 76.0f;

Rectangle knob_rect() {
    return Rectangle{knob_cx - knob_r, knob_cy - knob_r, knob_r * 2.0f,
                     knob_r * 2.0f};
}

} // namespace

void KnobsPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;
    pattern_ = patterns[0].pattern;
    position_ = KnobPosition::KNOB_UP;
    reset_needy();
}

void KnobsPuzzle::on_activate() {
    pattern_ = patterns[std::uniform_int_distribution<int>(
                            0, pattern_count - 1)(needy_rng())]
                   .pattern;
}

void KnobsPuzzle::on_expire() {
    // Being in the right position when time runs out is the whole module.
    for (const PatternEntry& e : patterns) {
        if (e.pattern == pattern_) {
            if (position_ != e.position) raise_strike();
            return;
        }
    }
}

void KnobsPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                         float dt) {
    (void)ctx;
    tick_needy(dt);

    if (!in.tapped) return;
    if (!CheckCollisionPointRec(in.tap_pos, knob_rect())) return;

    // The knob only turns one way, a quarter at a time.
    position_ = static_cast<KnobPosition>(
        (static_cast<int>(position_) + 1) % 4);
}

void KnobsPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               active() ? Color{240, 180, 60, 255} : Color{60, 63, 70, 255});
    DrawText("NEEDY", 34, 34, 26, Color{150, 158, 170, 255});

    // Twelve lights: two rows of six. They only show while the module is awake.
    for (int i = 0; i < led_count; ++i) {
        const float x = led_x0 + static_cast<float>(i % 6) * led_dx;
        const float y = led_y0 + static_cast<float>(i / 6) * led_dy;
        const bool lit = active() && ((pattern_ >> i) & 1) != 0;
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 24.0f,
                   Color{26, 27, 32, 255});
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 17.0f,
                   lit ? Color{255, 236, 120, 255} : Color{58, 58, 52, 255});
    }

    // Countdown bar while awake.
    if (active()) {
        DrawRectangle(static_cast<int>(led_x0) - 20, static_cast<int>(led_y0) + 108,
                      static_cast<int>(400.0f * needy_fraction()), 14,
                      Color{240, 180, 60, 255});
    }

    // The knob, with a pointer showing which way it faces.
    DrawCircle(static_cast<int>(knob_cx), static_cast<int>(knob_cy),
               knob_r + 10.0f, Color{26, 27, 32, 255});
    DrawCircle(static_cast<int>(knob_cx), static_cast<int>(knob_cy), knob_r,
               Color{92, 96, 108, 255});

    const float angle = (-90.0f + 90.0f * static_cast<float>(
                                      static_cast<int>(position_))) * DEG2RAD;
    const Vector2 tip{knob_cx + std::cos(angle) * (knob_r - 16.0f),
                      knob_cy + std::sin(angle) * (knob_r - 16.0f)};
    DrawLineEx(Vector2{knob_cx, knob_cy}, tip, 12.0f,
               Color{240, 240, 246, 255});
    DrawCircle(static_cast<int>(knob_cx), static_cast<int>(knob_cy), 12.0f,
               Color{40, 42, 48, 255});
}
