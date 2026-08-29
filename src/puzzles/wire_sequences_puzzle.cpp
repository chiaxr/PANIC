#include "puzzles/wire_sequences_puzzle.h"

#include <algorithm>
#include <cmath>
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

// Where a wire leaves the left-hand posts and where it lands on the lettered
// terminals. A wire in row 1 running to terminal C slopes across the panel;
// the letter beside its right-hand end is the one the manual's table asks for.
Vector2 wire_start(int row) { return Vector2{wire_left, row_centre(row)}; }

Vector2 wire_end(int connection) {
    return Vector2{wire_right, row_centre(connection)};
}

// Distance from a point to the wire's segment, for picking.
float distance_to_wire(Vector2 p, int row, int connection) {
    const Vector2 a = wire_start(row);
    const Vector2 b = wire_end(connection);
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cx = a.x + dx * t - p.x;
    const float cy = a.y + dy * t - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

// The point a fraction of the way along a wire, for drawing the cut stubs.
Vector2 wire_point(int row, int connection, float t) {
    const Vector2 a = wire_start(row);
    const Vector2 b = wire_end(connection);
    return Vector2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
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
        // Each panel carries one to three wires, in a random pick of the rows.
        const int wires_here = std::uniform_int_distribution<int>(1, 3)(rng);
        std::array<int, rows_per_panel> rows{0, 1, 2};
        std::shuffle(rows.begin(), rows.end(), rng);

        auto& panel = panels_[static_cast<size_t>(p)];
        for (int i = 0; i < wires_here; ++i) {
            Wire& w = panel[static_cast<size_t>(rows[static_cast<size_t>(i)])];
            w.present = true;
            w.color = static_cast<SeqColor>(pick_color(rng));
            w.connection = pick_conn(rng);
        }

        // Occurrences are numbered in the order the Defuser reads the panel out
        // -- top to bottom -- because that is the order the manual's running
        // count assumes. Numbering them as the wires were dealt would put the
        // two out of step whenever one panel carries two wires of a colour.
        for (int r = 0; r < rows_per_panel; ++r) {
            Wire& w = panel[static_cast<size_t>(r)];
            if (!w.present) continue;

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

    // Wires run diagonally, so a row band is not enough: pick whichever wire
    // the tap actually landed on.
    int best = -1;
    float best_dist = row_h * 0.5f;
    for (int r = 0; r < rows_per_panel; ++r) {
        const Wire& w =
            panels_[static_cast<size_t>(panel_)][static_cast<size_t>(r)];
        if (!w.present) continue;
        const float away = distance_to_wire(p, r, w.connection);
        if (away < best_dist) {
            best_dist = away;
            best = r;
        }
    }
    return best;
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

        // The wire slopes from its own row to the terminal it is wired to, so
        // the letter the Defuser reads out is the one the table asks about.
        const Color col = sequence_color(w.color);
        const Vector2 a = wire_start(r);
        const Vector2 b = wire_end(w.connection);
        if (!w.cut) {
            DrawLineEx(a, b, wire_thickness, col);
        } else {
            DrawLineEx(a, wire_point(r, w.connection, 0.44f), wire_thickness,
                       Fade(col, 0.55f));
            const Vector2 stub = wire_point(r, w.connection, 0.56f);
            DrawLineEx(Vector2{stub.x, stub.y + 8.0f}, b, wire_thickness,
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
