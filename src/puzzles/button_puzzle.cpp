#include "puzzles/button_puzzle.h"

#include <cmath>
#include <random>

#include "raylib.h"

namespace {

// Module-local layout (pixels within the module_tex_size square).
constexpr float button_cx = 196.0f;
constexpr float button_cy = 268.0f;
constexpr float button_r = 128.0f;
constexpr float strip_x = 384.0f;
constexpr float strip_y = 96.0f;
constexpr float strip_w = 76.0f;
constexpr float strip_h = 344.0f;

// A press shorter than this reads as "press and immediately release"; anything
// longer is a hold and has to be released on the right countdown digit.
constexpr float tap_max_hold = 0.4f;

Color button_face_color(ButtonPuzzle::ButtonColor c) {
    switch (c) {
        case ButtonPuzzle::ButtonColor::BTN_RED:    return Color{198, 58, 58, 255};
        case ButtonPuzzle::ButtonColor::BTN_BLUE:   return Color{62, 104, 214, 255};
        case ButtonPuzzle::ButtonColor::BTN_YELLOW: return Color{226, 200, 68, 255};
        case ButtonPuzzle::ButtonColor::BTN_WHITE:  return Color{232, 232, 236, 255};
        case ButtonPuzzle::ButtonColor::BTN_BLACK:  return Color{38, 38, 44, 255};
    }
    return Color{198, 58, 58, 255};
}

// Labels are read out loud, so keep them exactly as the manual prints them.
const char* button_label_text(ButtonPuzzle::ButtonLabel l) {
    switch (l) {
        case ButtonPuzzle::ButtonLabel::ABORT:    return "ABORT";
        case ButtonPuzzle::ButtonLabel::DETONATE: return "DETONATE";
        case ButtonPuzzle::ButtonLabel::HOLD:     return "HOLD";
        case ButtonPuzzle::ButtonLabel::PRESS:    return "PRESS";
    }
    return "PRESS";
}

Color strip_face_color(ButtonPuzzle::StripColor s) {
    switch (s) {
        case ButtonPuzzle::StripColor::STRIP_BLUE:   return Color{62, 104, 214, 255};
        case ButtonPuzzle::StripColor::STRIP_WHITE:  return Color{232, 232, 236, 255};
        case ButtonPuzzle::StripColor::STRIP_YELLOW: return Color{226, 200, 68, 255};
        case ButtonPuzzle::StripColor::STRIP_OTHER:  return Color{90, 200, 120, 255};
    }
    return Color{62, 104, 214, 255};
}

// Text that reads well on the button face.
Color label_text_color(ButtonPuzzle::ButtonColor c) {
    switch (c) {
        case ButtonPuzzle::ButtonColor::BTN_YELLOW:
        case ButtonPuzzle::ButtonColor::BTN_WHITE:
            return Color{20, 20, 24, 255};
        default:
            return Color{242, 242, 246, 255};
    }
}

void draw_centered(const char* text, int cx, int cy, int size, Color color) {
    DrawText(text, cx - MeasureText(text, size) / 2, cy - size / 2, size, color);
}

} // namespace

int ButtonPuzzle::strip_release_digit(StripColor strip) {
    switch (strip) {
        case StripColor::STRIP_BLUE:   return 4;
        case StripColor::STRIP_YELLOW: return 5;
        case StripColor::STRIP_WHITE:  return 1;
        case StripColor::STRIP_OTHER:  return 1;   // "any other colour"
    }
    return 1;
}

void ButtonPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    color_ = static_cast<ButtonColor>(
        std::uniform_int_distribution<int>(0, 4)(rng));
    label_ = static_cast<ButtonLabel>(
        std::uniform_int_distribution<int>(0, 3)(rng));
    strip_ = static_cast<StripColor>(
        std::uniform_int_distribution<int>(0, 3)(rng));

    should_hold_ = solve_should_hold(attrs);
}

// The manual's seven rules, in order; the first that matches wins. Anything
// that is not "press and immediately release" is a hold.
bool ButtonPuzzle::solve_should_hold(const BombAttributes& attrs) const {
    if (color_ == ButtonColor::BTN_BLUE && label_ == ButtonLabel::ABORT) {
        return true;
    }
    if (attrs.battery_count > 1 && label_ == ButtonLabel::DETONATE) {
        return false;
    }
    if (color_ == ButtonColor::BTN_WHITE && attrs.has_lit_indicator("CAR")) {
        return true;
    }
    if (attrs.battery_count > 2 && attrs.has_lit_indicator("FRK")) {
        return false;
    }
    if (color_ == ButtonColor::BTN_YELLOW) {
        return true;
    }
    if (color_ == ButtonColor::BTN_RED && label_ == ButtonLabel::HOLD) {
        return false;
    }
    return true;
}

bool ButtonPuzzle::button_at_pixel(Vector2 p) const {
    const float dx = p.x - button_cx;
    const float dy = p.y - button_cy;
    return dx * dx + dy * dy <= button_r * button_r;
}

// The defuser reads MM:SS off the casing, so match against those four digits
// rather than the raw seconds.
bool ButtonPuzzle::timer_shows_digit(float time_left, int digit) {
    const int total = static_cast<int>(
        std::ceil(time_left < 0.0f ? 0.0f : time_left));
    const int mm = total / 60;
    const int ss = total % 60;
    const int digits[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
    for (int d : digits) {
        if (d == digit) return true;
    }
    return false;
}

void ButtonPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                          float dt) {
    if (is_solved()) return;

    if (in.pressed && button_at_pixel(in.pointer_pos)) {
        holding_ = true;
        hold_time_ = 0.0f;
        strip_lit_ = false;
    }

    if (holding_) {
        if (in.held) {
            hold_time_ += dt;
            if (hold_time_ >= tap_max_hold) strip_lit_ = true;
        }

        if (in.released) {
            const bool was_tap = hold_time_ < tap_max_hold;
            holding_ = false;
            strip_lit_ = false;

            if (was_tap) {
                // "Press and immediately release."
                if (should_hold_) {
                    raise_strike();
                } else {
                    mark_solved();
                }
            } else {
                // A held button is only released correctly on the strip's digit.
                const bool on_digit =
                    timer_shows_digit(ctx.time_left, strip_release_digit(strip_));
                if (should_hold_ && on_digit) {
                    mark_solved();
                } else {
                    raise_strike();
                }
            }
        }
    }
}

void ButtonPuzzle::draw() {
    // Solved indicator LED in the corner.
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    // Button housing, then the button itself, pushed in slightly while held.
    DrawCircle(static_cast<int>(button_cx), static_cast<int>(button_cy),
               button_r + 18.0f, Color{44, 46, 52, 255});
    DrawCircle(static_cast<int>(button_cx), static_cast<int>(button_cy),
               button_r + 10.0f, Color{28, 29, 34, 255});

    const float r = holding_ ? button_r - 6.0f : button_r;
    DrawCircle(static_cast<int>(button_cx), static_cast<int>(button_cy), r,
               button_face_color(color_));
    DrawCircleLines(static_cast<int>(button_cx), static_cast<int>(button_cy), r,
                    Color{16, 16, 20, 255});

    draw_centered(button_label_text(label_), static_cast<int>(button_cx),
                  static_cast<int>(button_cy), 34, label_text_color(color_));

    // The colour strip beside the button, dark until a hold registers.
    const Rectangle strip{strip_x, strip_y, strip_w, strip_h};
    DrawRectangleRec(strip, Color{26, 27, 32, 255});
    if (strip_lit_) {
        DrawRectangleRec(Rectangle{strip_x + 8.0f, strip_y + 8.0f,
                                   strip_w - 16.0f, strip_h - 16.0f},
                         strip_face_color(strip_));
    }
    DrawRectangleLinesEx(strip, 4, Color{16, 16, 20, 255});
}
