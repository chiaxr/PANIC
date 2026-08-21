#include "puzzles/wire_sequences_puzzle.h"

#include <algorithm>
#include <random>

#include "raylib.h"

namespace {

constexpr int conn_a = 1;
constexpr int conn_b = 2;
constexpr int conn_c = 4;

// PANIC's own tables: for each colour, which terminals are cut on the Nth wire
// of that colour. manual/index.html prints the same three tables.
constexpr int max_occurrences = 9;
constexpr int sequence_table[3][max_occurrences] = {
    // Red
    {conn_a, conn_a | conn_c, conn_a, conn_a | conn_c, conn_b | conn_c,
     conn_a | conn_b | conn_c, conn_c, conn_b, conn_a | conn_c},
    // Blue
    {conn_c, conn_a | conn_b, conn_c, conn_a, conn_a | conn_b | conn_c,
     conn_a | conn_b | conn_c, conn_a | conn_c, conn_a | conn_b | conn_c, conn_a},
    // Black
    {conn_a | conn_c, conn_b | conn_c, conn_a | conn_c, conn_a | conn_b, conn_a,
     conn_b | conn_c, conn_a | conn_b, conn_a | conn_b | conn_c, conn_b},
};

// Module-local layout.
constexpr float row_h = 96.0f;
constexpr float rows_top = 96.0f;
constexpr float wire_left = 92.0f;
constexpr float wire_right = 372.0f;
constexpr float wire_thickness = 22.0f;
constexpr float down_x = 176.0f;
constexpr float down_y = 404.0f;
constexpr float down_w = 160.0f;
constexpr float down_h = 76.0f;

Rectangle down_rect() { return Rectangle{down_x, down_y, down_w, down_h}; }

float row_centre(int row) {
    return rows_top + row_h * (static_cast<float>(row) + 0.5f);
}

Color sequence_color(WireSequencesPuzzle::SeqColor c) {
    switch (c) {
        case WireSequencesPuzzle::SeqColor::SEQ_RED:
            return Color{212, 64, 64, 255};
        case WireSequencesPuzzle::SeqColor::SEQ_BLUE:
            return Color{72, 112, 220, 255};
        case WireSequencesPuzzle::SeqColor::SEQ_BLACK:
            return Color{40, 40, 48, 255};
    }
    return Color{212, 64, 64, 255};
}

} // namespace

void WireSequencesPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    (void)attrs;   // the module's own occurrence tables decide everything
    std::uniform_int_distribution<int> pick_color(0, 2);
    std::uniform_int_distribution<int> pick_conn(0, 2);

    int seen[3] = {0, 0, 0};
    for (int p = 0; p < panel_count; ++p) {
        // Each panel carries one to three wires.
        const int wires_here = std::uniform_int_distribution<int>(1, 3)(rng);
        std::array<int, rows_per_panel> rows{0, 1, 2};
        std::shuffle(rows.begin(), rows.end(), rng);

        for (int i = 0; i < wires_here; ++i) {
            Wire& w = panels_[static_cast<size_t>(p)]
                             [static_cast<size_t>(rows[static_cast<size_t>(i)])];
            w.present = true;
            w.color = static_cast<SeqColor>(pick_color(rng));
            w.connection = pick_conn(rng);

            const int ci = static_cast<int>(w.color);
            if (seen[ci] >= max_occurrences) {
                // Past the end of the table this colour can no longer appear.
                w.present = false;
                continue;
            }
            w.occurrence = ++seen[ci];

            const int mask = sequence_table[ci][w.occurrence - 1];
            const int bit = w.connection == 0 ? conn_a
                                              : (w.connection == 1 ? conn_b
                                                                   : conn_c);
            w.should_cut = (mask & bit) != 0;
        }
    }
    panel_ = 0;
}

int WireSequencesPuzzle::wire_at_pixel(Vector2 p) const {
    if (p.x < wire_left - 40.0f || p.x > wire_right + 40.0f) return -1;
    for (int r = 0; r < rows_per_panel; ++r) {
        const float cy = row_centre(r);
        if (p.y >= cy - row_h * 0.5f && p.y <= cy + row_h * 0.5f) {
            return panels_[static_cast<size_t>(panel_)][static_cast<size_t>(r)]
                           .present
                       ? r
                       : -1;
        }
    }
    return -1;
}

void WireSequencesPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                                 float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    if (CheckCollisionPointRec(in.tap_pos, down_rect())) {
        // Advancing is only safe once every wire this panel needed is cut.
        const auto& panel = panels_[static_cast<size_t>(panel_)];
        for (const Wire& w : panel) {
            if (w.present && w.should_cut && !w.cut) {
                raise_strike();
                return;
            }
        }
        if (++panel_ >= panel_count) {
            panel_ = panel_count - 1;
            mark_solved();
        }
        return;
    }

    const int row = wire_at_pixel(in.tap_pos);
    if (row < 0) return;

    Wire& w = panels_[static_cast<size_t>(panel_)][static_cast<size_t>(row)];
    if (w.cut) return;
    w.cut = true;
    if (!w.should_cut) raise_strike();
}

void WireSequencesPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    // Panel number, so the defuser can say which panel they are on.
    const char* label = TextFormat("PANEL %d / %d", panel_ + 1, panel_count);
    DrawText(label, 34, 34, 30, Color{150, 158, 170, 255});

    // Backing plate: black wires are invisible against the module's own dark
    // background without it.
    DrawRectangleRec(Rectangle{wire_left - 46.0f, rows_top - 10.0f,
                               wire_right - wire_left + 140.0f,
                               row_h * rows_per_panel + 20.0f},
                     Color{96, 100, 110, 255});

    const char* const letters[rows_per_panel] = {"A", "B", "C"};
    for (int r = 0; r < rows_per_panel; ++r) {
        const float cy = row_centre(r);
        const Wire& w =
            panels_[static_cast<size_t>(panel_)][static_cast<size_t>(r)];

        // Left terminal post and the lettered right-hand terminal.
        DrawRectangle(static_cast<int>(wire_left) - 34,
                      static_cast<int>(cy) - 20, 30, 40,
                      Color{54, 56, 64, 255});
        DrawText(letters[r], static_cast<int>(wire_right) + 26,
                 static_cast<int>(cy) - 20, 40, Color{20, 20, 24, 255});

        if (!w.present) continue;

        const Color col = sequence_color(w.color);
        if (!w.cut) {
            DrawRectangleRec(Rectangle{wire_left, cy - wire_thickness * 0.5f,
                                       wire_right - wire_left, wire_thickness},
                             col);
        } else {
            const float mid = (wire_left + wire_right) * 0.5f;
            DrawRectangleRec(Rectangle{wire_left, cy - wire_thickness * 0.5f,
                                       mid - wire_left - 26.0f, wire_thickness},
                             Fade(col, 0.55f));
            DrawRectangleRec(
                Rectangle{mid + 26.0f, cy - wire_thickness * 0.5f + 8.0f,
                          wire_right - mid - 26.0f, wire_thickness},
                Fade(col, 0.55f));
        }
    }

    // Down arrow: advance to the next panel.
    const Rectangle down = down_rect();
    DrawRectangleRec(down, Color{48, 50, 58, 255});
    DrawRectangleLinesEx(down, 4, Color{16, 16, 20, 255});
    DrawTriangle(Vector2{down.x + 30.0f, down.y + 20.0f},
                 Vector2{down.x + down.width * 0.5f, down.y + down.height - 18.0f},
                 Vector2{down.x + down.width - 30.0f, down.y + 20.0f},
                 Color{200, 204, 212, 255});
}
