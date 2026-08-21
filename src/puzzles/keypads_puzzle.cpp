#include "puzzles/keypads_puzzle.h"

#include <algorithm>
#include <random>

#include "raylib.h"

namespace {

// The six columns. Any four symbols taken from one column appear together in
// no other column, which is what makes the module solvable; the table was
// generated and checked exhaustively for that property, so do not edit a single
// symbol without re-checking it (and manual/index.html) as a whole.
constexpr int column_count = 6;
constexpr int column_len = 7;
constexpr char columns[column_count][column_len] = {
    {'!', '&', ';', '|', '}', '=', '/'},
    {'@', '=', '/', '?', '[', '{', '%'},
    {'\'', '%', '{', '\\', '$', '"', '^'},
    {')', '<', '*', '#', '}', '&', '{'},
    {'(', '>', '^', '"', '[', '\'', ';'},
    {']', '~', ':', '+', '$', '&', '/'},
};

// Module-local layout: a 2x2 grid of keys.
constexpr float key_size = 170.0f;
constexpr float key_x0 = 62.0f;
constexpr float key_y0 = 104.0f;
constexpr float key_gap = 36.0f;

Rectangle key_rect(int idx) {
    const float x = key_x0 + static_cast<float>(idx % 2) * (key_size + key_gap);
    const float y = key_y0 + static_cast<float>(idx / 2) * (key_size + key_gap);
    return Rectangle{x, y, key_size, key_size};
}

} // namespace

void KeypadsPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    (void)attrs;   // Keypads depends only on the symbols shown

    const int col = std::uniform_int_distribution<int>(0, column_count - 1)(rng);

    // Take four of the column's seven symbols, keeping the column's order so
    // the required press order falls out of it.
    std::array<int, column_len> pick{};
    for (int i = 0; i < column_len; ++i) pick[i] = i;
    std::shuffle(pick.begin(), pick.end(), rng);
    std::array<int, key_count> chosen{};
    for (int i = 0; i < key_count; ++i) chosen[i] = pick[i];
    std::sort(chosen.begin(), chosen.end());

    // Scatter them across the four key positions.
    std::array<int, key_count> slot{};
    for (int i = 0; i < key_count; ++i) slot[i] = i;
    std::shuffle(slot.begin(), slot.end(), rng);

    for (int i = 0; i < key_count; ++i) {
        keys_[slot[i]] = columns[col][chosen[i]];
        press_order_[i] = slot[i];
    }

    pressed_.fill(false);
    next_press_ = 0;
}

int KeypadsPuzzle::key_at_pixel(Vector2 p) const {
    for (int i = 0; i < key_count; ++i) {
        if (CheckCollisionPointRec(p, key_rect(i))) return i;
    }
    return -1;
}

void KeypadsPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                           float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    const int key = key_at_pixel(in.tap_pos);
    if (key < 0 || pressed_[key]) return;

    if (key == press_order_[next_press_]) {
        pressed_[key] = true;
        if (++next_press_ >= key_count) mark_solved();
    } else {
        // A wrong key restarts the sequence, as on the real module.
        raise_strike();
        pressed_.fill(false);
        next_press_ = 0;
    }
}

void KeypadsPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    for (int i = 0; i < key_count; ++i) {
        const Rectangle r = key_rect(i);
        DrawRectangleRec(r, Color{206, 202, 194, 255});
        DrawRectangleLinesEx(r, 4, Color{16, 16, 20, 255});

        const char text[2] = {keys_[i], '\0'};
        const int fs = 92;
        DrawText(text,
                 static_cast<int>(r.x + (r.width - MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(r.y + (r.height - fs) * 0.5f), fs,
                 Color{22, 22, 26, 255});

        // Small lamp per key, lit once that key has been correctly pressed.
        DrawCircle(static_cast<int>(r.x + r.width - 22),
                   static_cast<int>(r.y + 22), 10,
                   pressed_[i] ? Color{90, 220, 120, 255}
                               : Color{70, 70, 76, 255});
    }
}
