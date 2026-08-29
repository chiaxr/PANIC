#include "puzzles/fold_out_puzzle.h"

#include <algorithm>
#include <array>
#include <random>

#include "raylib.h"

namespace {

using Sym = FoldOutPuzzle::Symbol;

constexpr int grid_n = FoldOutPuzzle::grid;
constexpr int net_cells = FoldOutPuzzle::net_cells;
constexpr uint8_t empty_cell = 0xFF;

// ---------------------------------------------------------------------------
// The net catalogue.
//
// Ten of the eleven cube nets fit inside a 4x4 lattice; the eleventh (two rows
// of three, offset by two) spans five columns and is left out. Each entry is
// normalised so its top-left occupied cell sits at row 0 / column 0. The list
// was enumerated by brute force over every six-cell subset of the lattice and
// checked with the same cube roll used below, so every entry really does fold.
// ---------------------------------------------------------------------------
struct NetShape { int8_t rc[net_cells][2]; };

constexpr int net_count = 10;
constexpr NetShape nets[net_count] = {
    {{{0, 0}, {0, 1}, {0, 2}, {1, 1}, {2, 1}, {3, 1}}},
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {1, 3}, {2, 1}}},
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {1, 3}, {2, 2}}},
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {1, 3}, {2, 3}}},
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {2, 1}, {3, 1}}},
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {2, 2}, {2, 3}}},
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}, {2, 2}, {3, 1}}},
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {3, 2}}},
    {{{0, 1}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {2, 1}}},
    {{{0, 1}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {2, 2}}},
};

// ---------------------------------------------------------------------------
// Rolling the cube. Faces are 0 bottom, 1 top, 2 north, 3 south, 4 east,
// 5 west, so the opposite pairs are 0-1, 2-3 and 4-5. North is up the page,
// towards decreasing row.
// ---------------------------------------------------------------------------
void roll_east(int f[6]) {
    int b = f[0]; f[0] = f[4]; f[4] = f[1]; f[1] = f[5]; f[5] = b;
}
void roll_west(int f[6]) {
    int b = f[0]; f[0] = f[5]; f[5] = f[1]; f[1] = f[4]; f[4] = b;
}
void roll_north(int f[6]) {
    int b = f[0]; f[0] = f[2]; f[2] = f[1]; f[1] = f[3]; f[3] = b;
}
void roll_south(int f[6]) {
    int b = f[0]; f[0] = f[3]; f[3] = f[1]; f[1] = f[2]; f[2] = b;
}

int opposite_face(int face) { return face ^ 1; }

constexpr int dir_count = 4;
constexpr int dir_dr[dir_count] = {-1, 1, 0, 0};   // north, south, east, west
constexpr int dir_dc[dir_count] = {0, 0, 1, -1};

void roll_dir(int f[6], int dir) {
    switch (dir) {
        case 0: roll_north(f); break;
        case 1: roll_south(f); break;
        case 2: roll_east(f); break;
        default: roll_west(f); break;
    }
}

bool filled(const std::array<uint8_t, grid_n * grid_n>& cells, int r, int c) {
    if (r < 0 || r >= grid_n || c < 0 || c >= grid_n) return false;
    return cells[r * grid_n + c] != 0;
}

// Walks the net, rolling the cube one square at a time, and writes the face
// that lands on each square. Returns false when the shape is not a cube net:
// either it is disconnected, or two squares fold onto the same face.
bool roll_face_map(const std::array<uint8_t, grid_n * grid_n>& cells,
                   std::array<uint8_t, grid_n * grid_n>& out_face) {
    int start = -1;
    int total = 0;
    for (int i = 0; i < grid_n * grid_n; ++i) {
        if (cells[i] != 0) {
            if (start < 0) start = i;
            ++total;
        }
    }
    if (start < 0) return false;

    std::array<int, grid_n * grid_n * 6> arrangement{};
    std::array<bool, grid_n * grid_n> seen{};
    std::array<int, grid_n * grid_n> queue{};
    int head = 0;
    int tail = 0;

    for (int i = 0; i < 6; ++i) arrangement[start * 6 + i] = i;
    seen[start] = true;
    out_face[start] = 0;
    queue[tail++] = start;

    int visited = 0;
    int used = 0;   // bitmask of face indices already claimed
    while (head < tail) {
        const int cur = queue[head++];
        ++visited;
        const int face = out_face[cur];
        if (used & (1 << face)) return false;   // two squares, one face
        used |= 1 << face;

        const int r = cur / grid_n;
        const int c = cur % grid_n;
        for (int d = 0; d < dir_count; ++d) {
            const int nr = r + dir_dr[d];
            const int nc = c + dir_dc[d];
            if (!filled(cells, nr, nc)) continue;
            const int next = nr * grid_n + nc;
            if (seen[next]) continue;

            int f[6];
            for (int i = 0; i < 6; ++i) f[i] = arrangement[cur * 6 + i];
            roll_dir(f, d);
            for (int i = 0; i < 6; ++i) arrangement[next * 6 + i] = f[i];

            seen[next] = true;
            out_face[next] = static_cast<uint8_t>(f[0]);
            queue[tail++] = next;
        }
    }
    return visited == total && used == 0x3F;
}

// ---------------------------------------------------------------------------
// The manual's folding rules, used only to confirm the puzzle is solvable the
// way the manual says it is. Rule 1: three squares in a straight line put the
// two ends opposite each other. Rule 2: along a run that turns at every step,
// squares three steps apart are opposite. Rule 3 (elimination) needs no code:
// it always finishes off whatever the first two leave.
// ---------------------------------------------------------------------------
int rules_resolve_pairs(const std::array<uint8_t, grid_n * grid_n>& cells,
                        const std::array<uint8_t, grid_n * grid_n>& face) {
    int found = 0;   // bitmask over the three face pairs, indexed face / 2

    for (int r = 0; r < grid_n; ++r) {
        for (int c = 0; c < grid_n; ++c) {
            if (!filled(cells, r, c)) continue;
            // Rule 1, scanning right and down so each line is seen once.
            if (filled(cells, r, c + 1) && filled(cells, r, c + 2)) {
                found |= 1 << (face[r * grid_n + c] / 2);
            }
            if (filled(cells, r + 1, c) && filled(cells, r + 2, c)) {
                found |= 1 << (face[r * grid_n + c] / 2);
            }
            // Rule 2: a four-square staircase turning at every step.
            for (int d1 = 0; d1 < dir_count; ++d1) {
                const int r1 = r + dir_dr[d1];
                const int c1 = c + dir_dc[d1];
                if (!filled(cells, r1, c1)) continue;
                for (int d2 = 0; d2 < dir_count; ++d2) {
                    // A turn means swapping between the vertical and
                    // horizontal halves of the direction table.
                    if ((d1 < 2) == (d2 < 2)) continue;
                    const int r2 = r1 + dir_dr[d2];
                    const int c2 = c1 + dir_dc[d2];
                    if (!filled(cells, r2, c2)) continue;
                    for (int d3 = 0; d3 < dir_count; ++d3) {
                        if ((d2 < 2) == (d3 < 2)) continue;
                        const int r3 = r2 + dir_dr[d3];
                        const int c3 = c2 + dir_dc[d3];
                        if (!filled(cells, r3, c3)) continue;
                        if (r3 == r && c3 == c) continue;
                        found |= 1 << (face[r * grid_n + c] / 2);
                    }
                }
            }
        }
    }

    int pairs = 0;
    for (int i = 0; i < 3; ++i) {
        if (found & (1 << i)) ++pairs;
    }
    return pairs;
}

// ---------------------------------------------------------------------------
// Module-local layout.
// ---------------------------------------------------------------------------
constexpr float cell_size = 96.0f;
constexpr float grid_x = 72.0f;
constexpr float grid_y = 96.0f;

Rectangle cell_rect(int index) {
    const float r = static_cast<float>(index / grid_n);
    const float c = static_cast<float>(index % grid_n);
    return Rectangle{grid_x + c * cell_size, grid_y + r * cell_size, cell_size,
                     cell_size};
}

Vector2 cell_centre(int index) {
    const Rectangle r = cell_rect(index);
    return Vector2{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
}

Color symbol_color(int sym) {
    switch (static_cast<Sym>(sym)) {
        case Sym::SYM_CIRCLE:   return Color{80, 170, 250, 255};
        case Sym::SYM_TRIANGLE: return Color{245, 165, 50, 255};
        case Sym::SYM_SQUARE:   return Color{110, 215, 130, 255};
        case Sym::SYM_STAR:     return Color{210, 120, 235, 255};
        case Sym::SYM_CROSS:    return Color{235, 85, 85, 255};
        default:                return Color{225, 228, 236, 255};
    }
}

void draw_symbol(int sym, Vector2 c, Color col, Color panel) {
    switch (static_cast<Sym>(sym)) {
        case Sym::SYM_CIRCLE:
            DrawCircleV(c, 30.0f, col);
            break;
        case Sym::SYM_TRIANGLE: {
            const float s = 30.0f;
            DrawTriangle(Vector2{c.x - s, c.y + s}, Vector2{c.x + s, c.y + s},
                         Vector2{c.x, c.y - s}, col);
            break;
        }
        case Sym::SYM_SQUARE:
            DrawRectangleV(Vector2{c.x - 26.0f, c.y - 26.0f},
                           Vector2{52.0f, 52.0f}, col);
            break;
        case Sym::SYM_STAR: {
            // Two overlapping triangles. Both are wound the way raylib keeps;
            // the other winding is culled and silently never appears.
            const float s = 30.0f;
            DrawTriangle(Vector2{c.x - s, c.y + s * 0.5f},
                         Vector2{c.x + s, c.y + s * 0.5f},
                         Vector2{c.x, c.y - s}, col);
            DrawTriangle(Vector2{c.x - s, c.y - s * 0.5f},
                         Vector2{c.x, c.y + s},
                         Vector2{c.x + s, c.y - s * 0.5f}, col);
            break;
        }
        case Sym::SYM_CROSS: {
            const float s = 24.0f;
            DrawLineEx(Vector2{c.x - s, c.y - s}, Vector2{c.x + s, c.y + s},
                       12.0f, col);
            DrawLineEx(Vector2{c.x - s, c.y + s}, Vector2{c.x + s, c.y - s},
                       12.0f, col);
            break;
        }
        default:   // crescent: a disc with a bite taken out of it
            DrawCircleV(c, 30.0f, col);
            DrawCircleV(Vector2{c.x + 15.0f, c.y - 7.0f}, 26.0f, panel);
            break;
    }
}

} // namespace

void FoldOutPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    std::array<uint8_t, grid * grid> cells{};

    // Draw a net, turn or flip it, and drop it somewhere in the lattice, until
    // one both folds and yields to the manual's rules. The catalogue is already
    // known-good, so this loop is a guard against a bad edit rather than a real
    // search; the counter keeps it from ever spinning.
    bool ok = false;
    for (int attempt = 0; attempt < 64 && !ok; ++attempt) {
        const NetShape& net =
            nets[std::uniform_int_distribution<int>(0, net_count - 1)(rng)];
        const int turns = std::uniform_int_distribution<int>(0, 3)(rng);
        const bool flip = std::uniform_int_distribution<int>(0, 1)(rng) != 0;

        int rc[net_cells][2];
        for (int i = 0; i < net_cells; ++i) {
            int r = net.rc[i][0];
            int c = flip ? -net.rc[i][1] : net.rc[i][1];
            for (int t = 0; t < turns; ++t) {
                const int nr = c;
                c = -r;
                r = nr;
            }
            rc[i][0] = r;
            rc[i][1] = c;
        }

        int min_r = rc[0][0];
        int min_c = rc[0][1];
        int max_r = rc[0][0];
        int max_c = rc[0][1];
        for (int i = 1; i < net_cells; ++i) {
            min_r = std::min(min_r, rc[i][0]);
            min_c = std::min(min_c, rc[i][1]);
            max_r = std::max(max_r, rc[i][0]);
            max_c = std::max(max_c, rc[i][1]);
        }
        const int span_r = max_r - min_r + 1;
        const int span_c = max_c - min_c + 1;
        if (span_r > grid || span_c > grid) continue;

        const int off_r =
            std::uniform_int_distribution<int>(0, grid - span_r)(rng);
        const int off_c =
            std::uniform_int_distribution<int>(0, grid - span_c)(rng);

        cells.fill(0);
        for (int i = 0; i < net_cells; ++i) {
            const int r = rc[i][0] - min_r + off_r;
            const int c = rc[i][1] - min_c + off_c;
            cells[r * grid + c] = 1;
        }

        if (!roll_face_map(cells, face_)) continue;
        if (rules_resolve_pairs(cells, face_) < 2) continue;
        ok = true;
    }

    if (!ok) {
        // Known-good fallback: catalogue entry 0, untransformed.
        cells.fill(0);
        for (int i = 0; i < net_cells; ++i) {
            cells[nets[0].rc[i][0] * grid + nets[0].rc[i][1]] = 1;
        }
        roll_face_map(cells, face_);
    }

    // One of each symbol, scattered over the six squares, so "the star" always
    // names exactly one square.
    std::array<int, net_cells> order{};
    for (int i = 0; i < net_cells; ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);

    symbol_.fill(empty_cell);
    int next = 0;
    int occupied[net_cells] = {};
    for (int i = 0; i < grid * grid; ++i) {
        if (cells[i] == 0) continue;
        occupied[next] = i;
        symbol_[i] = static_cast<uint8_t>(order[next]);
        ++next;
    }

    std::uniform_int_distribution<int> pick_cell(0, net_cells - 1);
    anchor_ = occupied[pick_cell(rng)];
    answer_ = partner_of(keyed_cell(attrs));
}

int FoldOutPuzzle::partner_of(int cell) const {
    if (cell < 0) return -1;
    const int want = opposite_face(face_[cell]);
    for (int i = 0; i < grid * grid; ++i) {
        if (symbol_[i] != empty_cell && face_[i] == want) return i;
    }
    return -1;
}

int FoldOutPuzzle::keyed_cell(const BombAttributes& attrs) const {
    // The manual's target table, in order. Whichever square this names, the
    // answer is the square opposite it once the net is folded.
    if (attrs.lit_indicator_count() > 0) return anchor_;

    const uint8_t want = attrs.serial_last_digit_odd()
                             ? static_cast<uint8_t>(Symbol::SYM_STAR)
                             : static_cast<uint8_t>(Symbol::SYM_CIRCLE);
    for (int i = 0; i < grid * grid; ++i) {
        if (symbol_[i] == want) return i;
    }
    return anchor_;
}

int FoldOutPuzzle::cell_at_pixel(Vector2 p) const {
    const int c = static_cast<int>((p.x - grid_x) / cell_size);
    const int r = static_cast<int>((p.y - grid_y) / cell_size);
    if (p.x < grid_x || p.y < grid_y) return -1;
    if (r < 0 || r >= grid || c < 0 || c >= grid) return -1;
    return r * grid + c;
}

void FoldOutPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                           float dt) {
    (void)ctx;
    (void)dt;
    if (is_solved() || !in.tapped) return;

    const int cell = cell_at_pixel(in.tap_pos);
    if (cell < 0 || symbol_[cell] == empty_cell) return;   // empty squares idle

    if (cell == answer_) {
        mark_solved();
    } else {
        // The net is not re-dealt: a strike costs health and time, never the
        // folding the pair has already done.
        raise_strike();
    }
}

void FoldOutPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    const Color panel = Color{52, 55, 64, 255};
    const Color recess = Color{20, 21, 26, 255};
    const Color label = Color{170, 176, 188, 255};

    // Column numbers and row letters: the printed coordinate frame is what
    // lets the two players name a square without arguing about which one.
    const int fs = 26;
    for (int i = 0; i < grid; ++i) {
        const char col_text[2] = {static_cast<char>('1' + i), '\0'};
        const Rectangle r = cell_rect(i);
        DrawText(col_text,
                 static_cast<int>(r.x + (cell_size -
                                         MeasureText(col_text, fs)) * 0.5f),
                 static_cast<int>(grid_y) - fs - 8, fs, label);

        const char row_text[2] = {static_cast<char>('A' + i), '\0'};
        const Rectangle rr = cell_rect(i * grid);
        DrawText(row_text, static_cast<int>(grid_x) - 34,
                 static_cast<int>(rr.y + (cell_size - fs) * 0.5f), fs, label);
    }

    for (int i = 0; i < grid * grid; ++i) {
        const Rectangle r = cell_rect(i);
        const Rectangle inner{r.x + 4, r.y + 4, r.width - 8, r.height - 8};
        const bool has = symbol_[i] != empty_cell;
        DrawRectangleRec(inner, has ? panel : recess);
        DrawRectangleLinesEx(inner, 3, Color{16, 16, 20, 255});
        if (!has) continue;

        draw_symbol(symbol_[i], cell_centre(i), symbol_color(symbol_[i]),
                    panel);
    }

    // The anchor ring sits on top so it never hides behind a symbol.
    if (anchor_ >= 0) {
        DrawRing(cell_centre(anchor_), 38.0f, 45.0f, 0.0f, 360.0f, 48,
                 Color{240, 180, 60, 255});
    }
}
