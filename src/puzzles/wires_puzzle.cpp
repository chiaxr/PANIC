#include "puzzles/wires_puzzle.h"

#include <random>

#include "raylib.h"

namespace {

// Module-local layout (pixels within the module_tex_size square).
constexpr float top = 74.0f;
constexpr float bottom = 458.0f;
constexpr float left_terminal = 35.0f;
constexpr float right_terminal = 477.0f;
constexpr float wire_left = 70.0f;
constexpr float wire_right = 442.0f;
constexpr float wire_thickness = 32.0f;
constexpr float terminal_w = 32.0f;
constexpr float terminal_h = 45.0f;
constexpr float cut_gap = 38.0f;    // half-gap opened up by a cut
constexpr float cut_droop = 10.0f;  // the free end sags

Color wire_display_color(WiresPuzzle::WireColor c) {
    switch (c) {
        case WiresPuzzle::WireColor::WIRE_RED:       return Color{212, 64, 64, 255};
        case WiresPuzzle::WireColor::WIRE_BLUE:      return Color{72, 112, 220, 255};
        case WiresPuzzle::WireColor::WIRE_YELLOW:    return Color{232, 208, 72, 255};
        case WiresPuzzle::WireColor::WIRE_WHITE:     return Color{236, 236, 240, 255};
        case WiresPuzzle::WireColor::WIRE_BLACK:     return Color{34, 34, 40, 255};
    }
    return Color{255, 255, 255, 255};
}

} // namespace

void WiresPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    const int count = std::uniform_int_distribution<int>(3, 6)(rng);
    std::uniform_int_distribution<int> color_pick(0, 4);

    wires_.clear();
    wires_.reserve(count);
    for (int i = 0; i < count; ++i) {
        wires_.push_back({static_cast<WireColor>(color_pick(rng)), false});
    }

    correct_index_ = solve_correct_wire(attrs);
}

int WiresPuzzle::count_color(WireColor c) const {
    int n = 0;
    for (const auto& w : wires_) {
        if (w.color == c) ++n;
    }
    return n;
}

int WiresPuzzle::last_index_of_color(WireColor c) const {
    for (int i = static_cast<int>(wires_.size()) - 1; i >= 0; --i) {
        if (wires_[i].color == c) return i + 1;    // 1-based
    }
    return 0;
}

// Faithful to the Keep Talking and Nobody Explodes "Wires" rules. Returns a
// 0-based index. The defuser manual (manual/index.html) documents these exactly.
int WiresPuzzle::solve_correct_wire(const BombAttributes& attrs) const {
    const int n = static_cast<int>(wires_.size());
    const bool serial_odd = attrs.serial_last_digit_odd();
    const WireColor last = wires_[n - 1].color;

    // Convert the manual's 1-based wire numbers to 0-based indices.
    auto one_based = [](int i) { return i - 1; };

    switch (n) {
        case 3:
            if (count_color(WireColor::WIRE_RED) == 0) return one_based(2);
            if (last == WireColor::WIRE_WHITE) return one_based(3);
            if (count_color(WireColor::WIRE_BLUE) > 1) {
                return one_based(last_index_of_color(WireColor::WIRE_BLUE));
            }
            return one_based(3);

        case 4:
            if (count_color(WireColor::WIRE_RED) > 1 && serial_odd) {
                return one_based(last_index_of_color(WireColor::WIRE_RED));
            }
            if (last == WireColor::WIRE_YELLOW && count_color(WireColor::WIRE_RED) == 0) {
                return one_based(1);
            }
            if (count_color(WireColor::WIRE_BLUE) == 1) return one_based(1);
            if (count_color(WireColor::WIRE_YELLOW) > 1) return one_based(4);
            return one_based(2);

        case 5:
            if (last == WireColor::WIRE_BLACK && serial_odd) return one_based(4);
            if (count_color(WireColor::WIRE_RED) == 1 &&
                count_color(WireColor::WIRE_YELLOW) > 1) {
                return one_based(1);
            }
            if (count_color(WireColor::WIRE_BLACK) == 0) return one_based(2);
            return one_based(1);

        case 6:
        default:
            if (count_color(WireColor::WIRE_YELLOW) == 0 && serial_odd) {
                return one_based(3);
            }
            if (count_color(WireColor::WIRE_YELLOW) == 1 &&
                    count_color(WireColor::WIRE_WHITE) > 1) {
                return one_based(4);
            }
            if (count_color(WireColor::WIRE_RED) == 0) return one_based(6);
            return one_based(4);
    }
}

int WiresPuzzle::wire_at_pixel(Vector2 p) const {
    const int n = static_cast<int>(wires_.size());
    if (n == 0) return -1;
    if (p.x < left_terminal - 10 || p.x > right_terminal + 10) return -1;
    if (p.y < top || p.y > bottom) return -1;

    const float slot = (bottom - top) / static_cast<float>(n);
    const int idx = static_cast<int>((p.y - top) / slot);
    if (idx < 0 || idx >= n) return -1;
    return idx;
}

void WiresPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                         float dt) {
    (void)dt;
    (void)ctx;   // Wires depends only on the fixed bomb attributes
    if (is_solved() || !in.tapped) return;

    const int idx = wire_at_pixel(in.tap_pos);
    if (idx < 0 || wires_[idx].cut) return;

    wires_[idx].cut = true;
    if (idx == correct_index_) {
        mark_solved();
    } else {
        raise_strike();
    }
}

void WiresPuzzle::draw() {
    const int n = static_cast<int>(wires_.size());
    const float slot = (bottom - top) / static_cast<float>(n);

    // Solved indicator LED in the corner.
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    for (int i = 0; i < n; ++i) {
        const float cy = top + slot * i + slot * 0.5f;
        const Color col = wire_display_color(wires_[i].color);

        // Terminals (screw posts).
        const int th = static_cast<int>(terminal_h);
        const int tw = static_cast<int>(terminal_w);
        DrawRectangle(static_cast<int>(left_terminal) - 13,
                      static_cast<int>(cy) - th / 2, tw, th,
                      Color{90, 92, 100, 255});
        DrawRectangle(static_cast<int>(right_terminal) - 19,
                      static_cast<int>(cy) - th / 2, tw, th,
                      Color{90, 92, 100, 255});

        const Rectangle bar{wire_left, cy - wire_thickness * 0.5f,
                            wire_right - wire_left, wire_thickness};

        if (!wires_[i].cut) {
            DrawRectangleRec(bar, col);
            DrawRectangleLinesEx(bar, 2, Color{12, 12, 14, 255});
        } else {
            // Cut: two stubs with a gap, the right end drooping slightly.
            const float mid = (wire_left + wire_right) * 0.5f;
            DrawRectangleRec(Rectangle{wire_left, cy - wire_thickness * 0.5f,
                                       mid - wire_left - cut_gap, wire_thickness},
                             Fade(col, 0.6f));
            DrawRectangleRec(
                Rectangle{mid + cut_gap, cy - wire_thickness * 0.5f + cut_droop,
                          wire_right - mid - cut_gap, wire_thickness},
                Fade(col, 0.6f));
        }
    }
}
