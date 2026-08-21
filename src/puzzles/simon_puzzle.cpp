#include "puzzles/simon_puzzle.h"

#include <random>

#include "raylib.h"

namespace {

// PANIC's own colour mapping. Rows are (no vowel / vowel) x (0, 1, 2 strikes);
// columns are the flashing colour in SimonColor order. manual/index.html prints
// the same six tables.
using SC = SimonPuzzle::SimonColor;
constexpr int mapping_rows = 6;
constexpr SC color_map[mapping_rows][SimonPuzzle::color_count] = {
    // Serial contains a vowel.
    {SC::SIMON_YELLOW, SC::SIMON_BLUE,   SC::SIMON_RED,    SC::SIMON_YELLOW},
    {SC::SIMON_YELLOW, SC::SIMON_GREEN,  SC::SIMON_GREEN,  SC::SIMON_BLUE},
    {SC::SIMON_YELLOW, SC::SIMON_RED,    SC::SIMON_YELLOW, SC::SIMON_BLUE},
    // Serial contains no vowel.
    {SC::SIMON_BLUE,   SC::SIMON_YELLOW, SC::SIMON_RED,    SC::SIMON_RED},
    {SC::SIMON_YELLOW, SC::SIMON_RED,    SC::SIMON_GREEN,  SC::SIMON_GREEN},
    {SC::SIMON_RED,    SC::SIMON_GREEN,  SC::SIMON_BLUE,   SC::SIMON_GREEN},
};

// Playback timing.
constexpr float flash_on_time = 0.45f;
constexpr float flash_off_time = 0.25f;
constexpr float sequence_pause = 1.4f;

// Module-local layout: four buttons in a diamond.
constexpr float centre = 256.0f;
constexpr float arm = 132.0f;
constexpr float button_r = 74.0f;

Vector2 button_centre(int idx) {
    switch (idx) {
        case 0: return Vector2{centre, centre - arm};   // top    (red)
        case 1: return Vector2{centre + arm, centre};   // right  (blue)
        case 2: return Vector2{centre, centre + arm};   // bottom (green)
        default: return Vector2{centre - arm, centre};  // left   (yellow)
    }
}

Color simon_display_color(SC c, bool lit) {
    switch (c) {
        case SC::SIMON_RED:
            return lit ? Color{255, 96, 96, 255} : Color{128, 40, 40, 255};
        case SC::SIMON_BLUE:
            return lit ? Color{110, 160, 255, 255} : Color{40, 62, 128, 255};
        case SC::SIMON_GREEN:
            return lit ? Color{120, 245, 140, 255} : Color{38, 110, 56, 255};
        case SC::SIMON_YELLOW:
            return lit ? Color{255, 236, 120, 255} : Color{132, 118, 40, 255};
    }
    return Color{128, 128, 128, 255};
}

} // namespace

SimonPuzzle::SimonColor SimonPuzzle::mapped_color(SimonColor flashed,
                                                  bool serial_has_vowel,
                                                  int strikes) {
    const int clamped = strikes < 0 ? 0 : (strikes > 2 ? 2 : strikes);
    const int row = (serial_has_vowel ? 0 : 3) + clamped;
    return color_map[row][static_cast<int>(flashed)];
}

void SimonPuzzle::init(const BombAttributes& attrs) {
    std::mt19937 rng(std::random_device{}());
    serial_has_vowel_ = attrs.serial_has_vowel();

    sequence_.clear();
    sequence_.reserve(total_stages);
    for (int i = 0; i < total_stages; ++i) {
        sequence_.push_back(static_cast<SimonColor>(
            std::uniform_int_distribution<int>(0, color_count - 1)(rng)));
    }

    stage_ = 1;
    input_index_ = 0;
    timer_ = 0.0f;
    flash_index_ = 0;
    flash_on_ = false;
    lit_button_ = -1;
}

int SimonPuzzle::button_at_pixel(Vector2 p) const {
    for (int i = 0; i < color_count; ++i) {
        const Vector2 c = button_centre(i);
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        if (dx * dx + dy * dy <= button_r * button_r) return i;
    }
    return -1;
}

void SimonPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                         float dt) {
    if (is_solved()) {
        lit_button_ = -1;
        return;
    }

    // Play the sequence back on a loop while the defuser works.
    timer_ -= dt;
    if (timer_ <= 0.0f) {
        if (flash_on_) {
            flash_on_ = false;
            lit_button_ = -1;
            ++flash_index_;
            timer_ = flash_index_ >= stage_ ? sequence_pause : flash_off_time;
            if (flash_index_ >= stage_) flash_index_ = 0;
        } else {
            flash_on_ = true;
            lit_button_ = static_cast<int>(
                sequence_[static_cast<size_t>(flash_index_)]);
            timer_ = flash_on_time;
        }
    }

    if (!in.tapped) return;
    const int pressed = button_at_pixel(in.tap_pos);
    if (pressed < 0) return;

    const SimonColor expected = mapped_color(
        sequence_[static_cast<size_t>(input_index_)], serial_has_vowel_,
        ctx.strikes);

    if (static_cast<SimonColor>(pressed) != expected) {
        // A strike also changes the mapping, so the replay restarts from the
        // first flash under the new table.
        raise_strike();
        input_index_ = 0;
        flash_index_ = 0;
        flash_on_ = false;
        lit_button_ = -1;
        timer_ = sequence_pause;
        return;
    }

    if (++input_index_ >= stage_) {
        input_index_ = 0;
        flash_index_ = 0;
        flash_on_ = false;
        lit_button_ = -1;
        timer_ = sequence_pause;
        if (++stage_ > total_stages) mark_solved();
    }
}

void SimonPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    for (int i = 0; i < color_count; ++i) {
        const Vector2 c = button_centre(i);
        const bool lit = (lit_button_ == i);
        DrawCircleV(c, button_r + 8.0f, Color{28, 29, 34, 255});
        DrawCircleV(c, button_r,
                    simon_display_color(static_cast<SimonColor>(i), lit));
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y), button_r,
                        Color{16, 16, 20, 255});
    }

    // Stage pips: how many stages have been cleared.
    const int cleared = is_solved() ? total_stages : stage_ - 1;
    for (int i = 0; i < total_stages; ++i) {
        DrawCircle(46 + i * 30, module_tex_size - 40, 10,
                   i < cleared ? Color{90, 220, 120, 255}
                               : Color{60, 63, 70, 255});
    }
}
