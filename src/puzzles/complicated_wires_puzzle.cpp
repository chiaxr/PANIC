#include "puzzles/complicated_wires_puzzle.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "raylib.h"

namespace {

// What the table tells you to do with a wire.
enum class Instruction { CUT, DONT, SERIAL, PARALLEL, BATTERY };

// PANIC's own table, indexed by red | blue<<1 | star<<2 | led<<3.
// manual/index.html prints the same sixteen rows.
constexpr Instruction wire_table[16] = {
    Instruction::BATTERY,    // red 0  blue 0  star 0  led 0
    Instruction::CUT,        // red 1  blue 0  star 0  led 0
    Instruction::BATTERY,    // red 0  blue 1  star 0  led 0
    Instruction::DONT,       // red 1  blue 1  star 0  led 0
    Instruction::PARALLEL,   // red 0  blue 0  star 1  led 0
    Instruction::SERIAL,     // red 1  blue 0  star 1  led 0
    Instruction::DONT,       // red 0  blue 1  star 1  led 0
    Instruction::CUT,        // red 1  blue 1  star 1  led 0
    Instruction::CUT,        // red 0  blue 0  star 0  led 1
    Instruction::PARALLEL,   // red 1  blue 0  star 0  led 1
    Instruction::CUT,        // red 0  blue 1  star 0  led 1
    Instruction::SERIAL,     // red 1  blue 1  star 0  led 1
    Instruction::BATTERY,    // red 0  blue 0  star 1  led 1
    Instruction::CUT,        // red 1  blue 0  star 1  led 1
    Instruction::DONT,       // red 0  blue 1  star 1  led 1
    Instruction::DONT,       // red 1  blue 1  star 1  led 1
};

constexpr int min_wires = 4;
constexpr int max_wires = 6;

// Module-local layout: wires run top to bottom, LED above, star below.
constexpr float wire_top = 128.0f;
constexpr float wire_bottom = 372.0f;
constexpr float wire_width = 26.0f;
constexpr float lane_w = 76.0f;
constexpr float lane_x0 = 34.0f;
constexpr float led_cy = 92.0f;
constexpr float star_cy = 412.0f;

// A five-pointed star as ten triangles around the centre. Angles decrease so
// the winding matches every other triangle drawn here; the other way round,
// raylib culls the lot and nothing appears.
void draw_star(Vector2 centre, float outer, float inner, Color color) {
    Vector2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float angle = (-90.0f - static_cast<float>(i) * 36.0f) * DEG2RAD;
        const float r = (i % 2 == 0) ? outer : inner;
        pts[i] = Vector2{centre.x + std::cos(angle) * r,
                         centre.y + std::sin(angle) * r};
    }
    for (int i = 0; i < 10; ++i) {
        DrawTriangle(centre, pts[i], pts[(i + 1) % 10], color);
    }
}

float lane_centre(int idx) {
    return lane_x0 + lane_w * (static_cast<float>(idx) + 0.5f);
}

bool resolve(Instruction instr, const BombAttributes& attrs) {
    switch (instr) {
        case Instruction::CUT:  return true;
        case Instruction::DONT: return false;
        case Instruction::SERIAL: {
            const int d = attrs.serial_last_digit();
            return d >= 0 && d % 2 == 0;
        }
        case Instruction::PARALLEL:
            return std::find(attrs.ports.begin(), attrs.ports.end(),
                             PortType::PARALLEL) != attrs.ports.end();
        case Instruction::BATTERY:
            return attrs.battery_count >= 2;
    }
    return false;
}

} // namespace

void ComplicatedWiresPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    std::uniform_int_distribution<int> coin(0, 1);

    // Deal wires until at least one of them actually has to be cut, so the
    // module always needs an action to solve.
    for (;;) {
        const int count =
            std::uniform_int_distribution<int>(min_wires, max_wires)(rng);
        wires_.assign(static_cast<size_t>(count), Wire{});

        for (Wire& w : wires_) {
            w.red = coin(rng) != 0;
            w.blue = coin(rng) != 0;
            w.star = coin(rng) != 0;
            w.led = coin(rng) != 0;

            const int index = (w.red ? 1 : 0) | (w.blue ? 2 : 0) |
                              (w.star ? 4 : 0) | (w.led ? 8 : 0);
            w.should_cut = resolve(wire_table[index], attrs);
        }

        if (std::any_of(wires_.begin(), wires_.end(),
                        [](const Wire& w) { return w.should_cut; })) {
            break;
        }
    }
}

int ComplicatedWiresPuzzle::wire_at_pixel(Vector2 p) const {
    if (p.y < wire_top - 20.0f || p.y > wire_bottom + 20.0f) return -1;
    for (size_t i = 0; i < wires_.size(); ++i) {
        const float cx = lane_centre(static_cast<int>(i));
        if (p.x >= cx - lane_w * 0.5f && p.x <= cx + lane_w * 0.5f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool ComplicatedWiresPuzzle::all_required_cut() const {
    return std::all_of(wires_.begin(), wires_.end(), [](const Wire& w) {
        return !w.should_cut || w.cut;
    });
}

void ComplicatedWiresPuzzle::update(const ModuleInput& in,
                                    const BombContext& ctx, float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    const int idx = wire_at_pixel(in.tap_pos);
    if (idx < 0) return;

    Wire& w = wires_[static_cast<size_t>(idx)];
    if (w.cut) return;
    w.cut = true;

    if (!w.should_cut) {
        raise_strike();
        return;
    }
    if (all_required_cut()) mark_solved();
}

void ComplicatedWiresPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    const Color red = Color{212, 64, 64, 255};
    const Color blue = Color{72, 112, 220, 255};
    const Color white = Color{232, 232, 236, 255};

    for (size_t i = 0; i < wires_.size(); ++i) {
        const Wire& w = wires_[i];
        const float cx = lane_centre(static_cast<int>(i));

        // LED above the wire.
        DrawCircle(static_cast<int>(cx), static_cast<int>(led_cy), 20.0f,
                   Color{26, 27, 32, 255});
        DrawCircle(static_cast<int>(cx), static_cast<int>(led_cy), 13.0f,
                   w.led ? Color{255, 236, 120, 255} : Color{62, 60, 50, 255});

        // The wire: solid, or striped when it carries both colours.
        const float x = cx - wire_width * 0.5f;
        const float top = wire_top;
        const float bottom = w.cut ? (wire_top + wire_bottom) * 0.5f - 18.0f
                                   : wire_bottom;
        if (w.red && w.blue) {
            const float band = 22.0f;
            for (float y = top; y < bottom; y += band) {
                const float h = std::min(band, bottom - y);
                DrawRectangleRec(Rectangle{x, y, wire_width, h},
                                 (static_cast<int>((y - top) / band) % 2 == 0)
                                     ? red
                                     : blue);
            }
        } else {
            DrawRectangleRec(Rectangle{x, top, wire_width, bottom - top},
                             w.red ? red : (w.blue ? blue : white));
        }
        if (w.cut) {
            // The severed lower stub, hanging from the bottom terminal.
            const float stub_top = (wire_top + wire_bottom) * 0.5f + 18.0f;
            DrawRectangleRec(
                Rectangle{x + 6.0f, stub_top, wire_width, wire_bottom - stub_top},
                Fade(w.red ? red : (w.blue ? blue : white), 0.55f));
        }

        // Terminals.
        DrawRectangle(static_cast<int>(cx) - 22, static_cast<int>(wire_top) - 14,
                      44, 18, Color{90, 92, 100, 255});
        DrawRectangle(static_cast<int>(cx) - 22,
                      static_cast<int>(wire_bottom) - 4, 44, 18,
                      Color{90, 92, 100, 255});

        // Star below the wire. Built from an explicit triangle fan: the
        // vertices go round in the winding raylib keeps, and DrawPoly can only
        // make a regular pentagon, which reads as the wrong symbol.
        if (w.star) {
            draw_star(Vector2{cx, star_cy}, 22.0f, 9.0f,
                      Color{226, 228, 234, 255});
        }
    }
}
