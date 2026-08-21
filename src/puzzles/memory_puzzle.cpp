#include "puzzles/memory_puzzle.h"

#include <algorithm>
#include <random>

#include "raylib.h"

namespace {

// Module-local layout.
constexpr float display_x = 106.0f;
constexpr float display_y = 62.0f;
constexpr float display_w = 300.0f;
constexpr float display_h = 168.0f;
constexpr float button_w = 104.0f;
constexpr float button_h = 128.0f;
constexpr float button_y = 292.0f;
constexpr float button_x0 = 30.0f;
constexpr float button_gap = 14.0f;

Rectangle button_rect(int idx) {
    const float x = button_x0 + static_cast<float>(idx) * (button_w + button_gap);
    return Rectangle{x, button_y, button_w, button_h};
}

std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

} // namespace

void MemoryPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;   // Memory depends only on its own stage history
    stage_ = 0;
    pressed_position_.fill(0);
    pressed_label_.fill(0);
    deal_stage();
}

void MemoryPuzzle::deal_stage() {
    for (int i = 0; i < button_count; ++i) labels_[static_cast<size_t>(i)] = i + 1;
    std::shuffle(labels_.begin(), labels_.end(), rng());
    display_ = std::uniform_int_distribution<int>(1, 4)(rng());
}

int MemoryPuzzle::position_of_label(int label) const {
    for (int i = 0; i < button_count; ++i) {
        if (labels_[static_cast<size_t>(i)] == label) return i;
    }
    return -1;
}

// The manual's per-stage tables. Positions and labels are 1-based there and
// converted to 0-based indices on the way out.
int MemoryPuzzle::solve_correct_button() const {
    auto by_position = [](int position_1based) { return position_1based - 1; };
    auto by_label = [this](int label) { return position_of_label(label); };

    switch (stage_) {
        case 0:
            switch (display_) {
                case 1: return by_position(2);
                case 2: return by_position(2);
                case 3: return by_position(3);
                default: return by_position(4);
            }
        case 1:
            switch (display_) {
                case 1: return by_label(4);
                case 2: return by_position(pressed_position_[0]);
                case 3: return by_position(1);
                default: return by_position(pressed_position_[0]);
            }
        case 2:
            switch (display_) {
                case 1: return by_label(pressed_label_[1]);
                case 2: return by_label(pressed_label_[0]);
                case 3: return by_position(3);
                default: return by_label(4);
            }
        case 3:
            switch (display_) {
                case 1: return by_position(pressed_position_[0]);
                case 2: return by_position(1);
                case 3: return by_position(pressed_position_[1]);
                default: return by_position(pressed_position_[1]);
            }
        default:
            switch (display_) {
                case 1: return by_label(pressed_label_[0]);
                case 2: return by_label(pressed_label_[1]);
                case 3: return by_label(pressed_label_[3]);
                default: return by_label(pressed_label_[2]);
            }
    }
}

void MemoryPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                          float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    int hit = -1;
    for (int i = 0; i < button_count; ++i) {
        if (CheckCollisionPointRec(in.tap_pos, button_rect(i))) {
            hit = i;
            break;
        }
    }
    if (hit < 0) return;

    if (hit != solve_correct_button()) {
        raise_strike();
        stage_ = 0;
        pressed_position_.fill(0);
        pressed_label_.fill(0);
        deal_stage();
        return;
    }

    pressed_position_[static_cast<size_t>(stage_)] = hit + 1;
    pressed_label_[static_cast<size_t>(stage_)] = labels_[static_cast<size_t>(hit)];

    if (++stage_ >= stage_count) {
        stage_ = stage_count;
        mark_solved();
        return;
    }
    deal_stage();
}

void MemoryPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    // Big display.
    const Rectangle display{display_x, display_y, display_w, display_h};
    DrawRectangleRec(display, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(display, 5, Color{16, 16, 20, 255});
    if (!is_solved()) {
        const char text[2] = {static_cast<char>('0' + display_), '\0'};
        const int fs = 118;
        DrawText(text,
                 static_cast<int>(display.x +
                                  (display.width - MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(display.y + (display.height - fs) * 0.5f), fs,
                 Color{120, 240, 150, 255});
    }

    // Stage pips, so the defuser can say which stage they are on.
    for (int i = 0; i < stage_count; ++i) {
        DrawCircle(static_cast<int>(display_x + 26.0f + i * 34.0f),
                   static_cast<int>(display_y + display_h + 26.0f), 11,
                   i < stage_ ? Color{90, 220, 120, 255}
                              : Color{60, 63, 70, 255});
    }

    for (int i = 0; i < button_count; ++i) {
        const Rectangle r = button_rect(i);
        DrawRectangleRec(r, Color{60, 64, 76, 255});
        DrawRectangleLinesEx(r, 4, Color{16, 16, 20, 255});
        const char text[2] = {
            static_cast<char>('0' + labels_[static_cast<size_t>(i)]), '\0'};
        const int fs = 68;
        DrawText(text,
                 static_cast<int>(r.x + (r.width - MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(r.y + (r.height - fs) * 0.5f), fs,
                 Color{232, 234, 240, 255});
    }
}
