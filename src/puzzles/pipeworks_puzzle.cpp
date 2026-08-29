#include "puzzles/pipeworks_puzzle.h"

#include <algorithm>
#include <random>

#include "raylib.h"

namespace {

using Rule = PipeworksPuzzle::Rule;

constexpr int grid_n = PipeworksPuzzle::grid;
constexpr int cell_count = grid_n * grid_n;

// Edge bits, clockwise from north. The manual names every tile by the edges it
// joins in this order, so "C2 is an elbow, SW" is complete and unambiguous.
constexpr uint8_t edge_n = 1;
constexpr uint8_t edge_e = 2;
constexpr uint8_t edge_s = 4;
constexpr uint8_t edge_w = 8;

// Direction index d matches bit d: 0 north, 1 east, 2 south, 3 west.
constexpr int dir_dr[4] = {-1, 0, 1, 0};
constexpr int dir_dc[4] = {0, 1, 0, -1};

uint8_t rotate_cw(uint8_t mask) {
    return static_cast<uint8_t>(((mask << 1) | (mask >> 3)) & 0x0F);
}

int popcount4(uint8_t mask) {
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (mask & (1 << i)) ++n;
    }
    return n;
}

bool is_tee(uint8_t mask) { return popcount4(mask) == 3; }

// Shapes a filler tile may take: two straights, four elbows, four tees. A tile
// is never a dead end or a cross, so every tile the Defuser describes is one of
// the ten names in the manual's vocabulary table.
constexpr int filler_count = 10;
constexpr uint8_t filler_shapes[filler_count] = {
    edge_n | edge_s,                   // NS
    edge_e | edge_w,                   // EW
    edge_n | edge_e,                   // NE
    edge_e | edge_s,                   // ES
    edge_s | edge_w,                   // SW
    edge_w | edge_n,                   // WN
    edge_n | edge_e | edge_s,          // NES
    edge_e | edge_s | edge_w,          // ESW
    edge_s | edge_w | edge_n,          // SWN
    edge_w | edge_n | edge_e,          // WNE
};

// ---------------------------------------------------------------------------
// Module-local layout.
// ---------------------------------------------------------------------------
constexpr float cell_size = 88.0f;
constexpr float grid_x = 86.0f;
constexpr float grid_y = 96.0f;

Rectangle tile_rect(int index) {
    const float r = static_cast<float>(index / grid_n);
    const float c = static_cast<float>(index % grid_n);
    return Rectangle{grid_x + c * cell_size, grid_y + r * cell_size, cell_size,
                     cell_size};
}

Vector2 tile_centre(int index) {
    const Rectangle r = tile_rect(index);
    return Vector2{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
}

float row_centre_y(int row) {
    return grid_y + (static_cast<float>(row) + 0.5f) * cell_size;
}

Rectangle valve_rect() { return Rectangle{158.0f, 458.0f, 214.0f, 42.0f}; }
Rectangle plate_rect() { return Rectangle{86.0f, 458.0f, 52.0f, 42.0f}; }

} // namespace

void PipeworksPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    std::uniform_int_distribution<int> row_pick(0, grid_n - 1);
    std::uniform_int_distribution<int> coin(0, 1);

    bool ok = false;
    for (int attempt = 0; attempt < 400 && !ok; ++attempt) {
        inlet_row_ = row_pick(rng);

        // Three of the four rows carry an outlet, lettered X, Y, Z downwards.
        const int missing = row_pick(rng);
        int next = 0;
        for (int r = 0; r < grid_n; ++r) {
            if (r == missing) continue;
            outlet_rows_[next++] = r;
        }

        // The Expert's outlet rule. Both halves are read off the casing.
        target_outlet_ =
            (attrs.battery_count + attrs.lit_indicator_count()) % outlet_count;
        const int target_row = outlet_rows_[target_outlet_];

        // ---- carve a self-avoiding route from the inlet to that outlet ----
        std::array<bool, cell_count> seen{};
        std::array<int, 12> path{};
        int path_len = 0;
        bool found = false;

        // Iterative depth-first search with backtracking, trying the four
        // directions in a fresh random order at every cell.
        struct Frame { int cell; int order[4]; int next_dir; };
        std::array<Frame, 12> stack{};
        int depth = 0;

        const int start = inlet_row_ * grid_n;
        seen[start] = true;
        path[path_len++] = start;
        stack[depth] = Frame{start, {0, 1, 2, 3}, 0};
        std::shuffle(stack[depth].order, stack[depth].order + 4, rng);

        while (depth >= 0 && !found) {
            Frame& f = stack[depth];
            if (f.cell == target_row * grid_n + (grid_n - 1) &&
                    path_len >= 6 && path_len <= 11) {
                found = true;
                break;
            }
            if (f.next_dir >= 4 || path_len >= 11) {
                seen[f.cell] = false;
                --path_len;
                --depth;
                continue;
            }
            const int d = f.order[f.next_dir++];
            const int r = f.cell / grid_n + dir_dr[d];
            const int c = f.cell % grid_n + dir_dc[d];
            if (r < 0 || r >= grid_n || c < 0 || c >= grid_n) continue;
            const int n = r * grid_n + c;
            if (seen[n]) continue;

            seen[n] = true;
            path[path_len++] = n;
            ++depth;
            stack[depth] = Frame{n, {0, 1, 2, 3}, 0};
            std::shuffle(stack[depth].order, stack[depth].order + 4, rng);
        }
        if (!found) continue;

        // ---- lay the route, then fill in around it ----
        for (Tile& t : tiles_) t = Tile{};

        std::array<bool, cell_count> on_path{};
        for (int i = 0; i < path_len; ++i) on_path[path[i]] = true;

        for (int i = 0; i < path_len; ++i) {
            uint8_t mask = 0;
            if (i == 0) {
                mask |= edge_w;   // the inlet feeds this tile from outside
            } else {
                const int prev = path[i - 1];
                for (int d = 0; d < 4; ++d) {
                    if (path[i] / grid_n + dir_dr[d] == prev / grid_n &&
                            path[i] % grid_n + dir_dc[d] == prev % grid_n) {
                        mask |= static_cast<uint8_t>(1 << d);
                    }
                }
            }
            if (i == path_len - 1) {
                mask |= edge_e;   // and this one spills into the outlet
            } else {
                const int nxt = path[i + 1];
                for (int d = 0; d < 4; ++d) {
                    if (path[i] / grid_n + dir_dr[d] == nxt / grid_n &&
                            path[i] % grid_n + dir_dc[d] == nxt % grid_n) {
                        mask |= static_cast<uint8_t>(1 << d);
                    }
                }
            }
            tiles_[path[i]].mask = mask;
        }

        // Upgrade a couple of route tiles to tees, the spare arm dangling as a
        // decoy branch.
        for (int i = 1; i < path_len - 1; ++i) {
            if (popcount4(tiles_[path[i]].mask) != 2) continue;
            if (coin(rng) == 0) continue;
            for (int d = 0; d < 4; ++d) {
                const uint8_t bit = static_cast<uint8_t>(1 << d);
                if (tiles_[path[i]].mask & bit) continue;
                const int r = path[i] / grid_n + dir_dr[d];
                const int c = path[i] % grid_n + dir_dc[d];
                if (r < 0 || r >= grid_n || c < 0 || c >= grid_n) continue;
                tiles_[path[i]].mask |= bit;
                break;
            }
        }

        std::uniform_int_distribution<int> shape(0, filler_count - 1);
        for (int i = 0; i < cell_count; ++i) {
            if (on_path[i]) continue;
            tiles_[i].mask = filler_shapes[shape(rng)];
        }

        // Burst seals never sit on the route, so a clean routing always exists.
        std::array<int, cell_count> off_path{};
        int off_count = 0;
        for (int i = 0; i < cell_count; ++i) {
            if (!on_path[i]) off_path[off_count++] = i;
        }
        if (off_count < 3) continue;
        std::shuffle(off_path.begin(), off_path.begin() + off_count, rng);
        const int seal_count = 2 + coin(rng);
        for (int i = 0; i < seal_count; ++i) tiles_[off_path[i]].seal = true;

        // Rivets pin route tiles at their solution orientation, turning them
        // from an obstacle into a waypoint the Expert can lean on.
        const int rivet_count = 1 + coin(rng);
        std::array<int, 12> route{};
        for (int i = 0; i < path_len; ++i) route[i] = path[i];
        std::shuffle(route.begin(), route.begin() + path_len, rng);
        for (int i = 0; i < rivet_count && i < path_len; ++i) {
            tiles_[route[i]].rivet = true;
        }

        // ---- pick a constraint the generated route already satisfies ----
        std::array<bool, cell_count> wet{};
        flood(wet);
        int wet_tees = 0;
        for (int i = 0; i < cell_count; ++i) {
            if (wet[i] && is_tee(tiles_[i].mask)) ++wet_tees;
        }

        const int last = path[path_len - 1];
        const bool from_above =
            path_len >= 2 && path[path_len - 2] == last - grid_n;

        // Whichever constraints this route already meets. The rivet rule is
        // the fallback rather than an equal contender: it holds for almost any
        // route, so drawing it evenly would crowd the other two out.
        int choices[static_cast<int>(Rule::RULE_COUNT)];
        int choice_count = 0;
        if (wet_tees == 1 || wet_tees == 2) {
            choices[choice_count++] = static_cast<int>(Rule::RULE_TEES);
        }
        if (from_above) {
            choices[choice_count++] = static_cast<int>(Rule::RULE_FROM_ABOVE);
        }
        rule_ = choice_count == 0
                    ? Rule::RULE_THROUGH_RIVET
                    : static_cast<Rule>(choices[
                          std::uniform_int_distribution<int>(
                              0, choice_count - 1)(rng)]);
        tee_target_ = wet_tees;

        // The route must genuinely pass: filler tiles can wet a sealed tile,
        // and only the flood-fill grader knows whether they do.
        if (!commit_passes()) continue;

        // ---- scramble, and make sure the scramble is not already a pass ----
        bool scrambled_ok = false;
        for (int shuffle_try = 0; shuffle_try < 24 && !scrambled_ok;
             ++shuffle_try) {
            for (int i = 0; i < cell_count; ++i) {
                if (tiles_[i].rivet) continue;
                const int turns = std::uniform_int_distribution<int>(0, 3)(rng);
                for (int t = 0; t < turns; ++t) {
                    tiles_[i].mask = rotate_cw(tiles_[i].mask);
                }
            }
            scrambled_ok = !commit_passes();
        }
        if (!scrambled_ok) continue;

        ok = true;
    }

    if (!ok) {
        // Fallback: a straight run along the inlet row, no seals, no rivets,
        // scrambled by one turn each. Dull, but always solvable.
        inlet_row_ = 1;
        outlet_rows_ = {0, 1, 2};
        target_outlet_ = 1;
        rule_ = Rule::RULE_THROUGH_RIVET;
        tee_target_ = 0;
        for (Tile& t : tiles_) t = Tile{edge_n | edge_s, false, false};
        for (int c = 0; c < grid_n; ++c) {
            tiles_[inlet_row_ * grid_n + c].mask = edge_e | edge_w;
        }
        for (int i = 0; i < cell_count; ++i) {
            tiles_[i].mask = rotate_cw(tiles_[i].mask);
        }
    }

    flash_tile_ = -1;
    flash_time_ = 0.0f;
}

void PipeworksPuzzle::flood(std::array<bool, grid * grid>& wet) const {
    wet.fill(false);

    // The inlet feeds its tile from the west, so that tile only fills if it
    // actually opens westward.
    const int start = inlet_row_ * grid_n;
    if (!(tiles_[start].mask & edge_w)) return;

    std::array<int, cell_count> queue{};
    int head = 0;
    int tail = 0;
    wet[start] = true;
    queue[tail++] = start;

    while (head < tail) {
        const int cur = queue[head++];
        for (int d = 0; d < 4; ++d) {
            if (!(tiles_[cur].mask & (1 << d))) continue;
            const int r = cur / grid_n + dir_dr[d];
            const int c = cur % grid_n + dir_dc[d];
            if (r < 0 || r >= grid_n || c < 0 || c >= grid_n) continue;
            const int n = r * grid_n + c;
            if (wet[n]) continue;
            // Both sides must open onto the shared edge.
            const int back = (d + 2) & 3;
            if (!(tiles_[n].mask & (1 << back))) continue;
            wet[n] = true;
            queue[tail++] = n;
        }
    }
}

bool PipeworksPuzzle::commit_passes() const {
    std::array<bool, cell_count> wet{};
    flood(wet);

    const int outlet_cell =
        outlet_rows_[target_outlet_] * grid_n + (grid_n - 1);
    if (!wet[outlet_cell]) return false;
    if (!(tiles_[outlet_cell].mask & edge_e)) return false;

    for (int i = 0; i < cell_count; ++i) {
        if (wet[i] && tiles_[i].seal) return false;
    }

    switch (rule_) {
        case Rule::RULE_TEES: {
            int tees = 0;
            for (int i = 0; i < cell_count; ++i) {
                if (wet[i] && is_tee(tiles_[i].mask)) ++tees;
            }
            return tees == tee_target_;
        }
        case Rule::RULE_FROM_ABOVE: {
            if (!(tiles_[outlet_cell].mask & edge_n)) return false;
            const int above = outlet_cell - grid_n;
            if (outlet_cell < grid_n) return false;
            return wet[above] && (tiles_[above].mask & edge_s) != 0;
        }
        default: {
            for (int i = 0; i < cell_count; ++i) {
                if (tiles_[i].rivet && !wet[i]) return false;
            }
            return true;
        }
    }
}

char PipeworksPuzzle::rule_code() const {
    switch (rule_) {
        case Rule::RULE_TEES:       return tee_target_ == 1 ? 'A' : 'B';
        case Rule::RULE_FROM_ABOVE: return 'C';
        default:                    return 'D';
    }
}

int PipeworksPuzzle::tile_at_pixel(Vector2 p) const {
    if (p.x < grid_x || p.y < grid_y) return -1;
    const int c = static_cast<int>((p.x - grid_x) / cell_size);
    const int r = static_cast<int>((p.y - grid_y) / cell_size);
    if (r < 0 || r >= grid_n || c < 0 || c >= grid_n) return -1;
    return r * grid_n + c;
}

void PipeworksPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                             float dt) {
    (void)ctx;
    if (flash_time_ > 0.0f) {
        flash_time_ -= dt;
        if (flash_time_ <= 0.0f) flash_tile_ = -1;
    }
    if (is_solved() || !in.tapped) return;

    if (CheckCollisionPointRec(in.tap_pos, valve_rect())) {
        if (commit_passes()) {
            mark_solved();
        } else {
            // The grid is left exactly as it is, so the pair adjust rather
            // than start over.
            raise_strike();
        }
        return;
    }

    const int tile = tile_at_pixel(in.tap_pos);
    if (tile < 0) return;

    if (tiles_[tile].rivet) {
        // A rivet is information, not a trap: it flashes and costs nothing.
        flash_tile_ = tile;
        flash_time_ = 0.2f;
        return;
    }
    tiles_[tile].mask = rotate_cw(tiles_[tile].mask);
}

void PipeworksPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    const Color outline = Color{16, 16, 20, 255};
    const Color label = Color{170, 176, 188, 255};
    const Color pipe = Color{158, 164, 178, 255};

    // Printed coordinate frame, so tiles can be named out loud.
    const int fs = 24;
    for (int i = 0; i < grid_n; ++i) {
        const char col_text[2] = {static_cast<char>('1' + i), '\0'};
        DrawText(col_text,
                 static_cast<int>(grid_x + i * cell_size +
                                  (cell_size - MeasureText(col_text, fs)) *
                                      0.5f),
                 static_cast<int>(grid_y) - fs - 6, fs, label);

        const char row_text[2] = {static_cast<char>('A' + i), '\0'};
        // Well clear of the inlet stub, which crosses the same band.
        DrawText(row_text, 12,
                 static_cast<int>(row_centre_y(i)) - fs / 2, fs, label);
    }

    // Tiles. Pipes are thick lines from the centre to each open edge, so every
    // shape and rotation falls out of the same three lines of code.
    for (int i = 0; i < cell_count; ++i) {
        const Rectangle r = tile_rect(i);
        const Rectangle inner{r.x + 3, r.y + 3, r.width - 6, r.height - 6};
        DrawRectangleRec(inner, Color{40, 42, 50, 255});
        DrawRectangleLinesEx(inner, 2, Color{28, 30, 36, 255});

        const Vector2 c = tile_centre(i);
        for (int d = 0; d < 4; ++d) {
            if (!(tiles_[i].mask & (1 << d))) continue;
            const Vector2 edge{c.x + dir_dc[d] * cell_size * 0.5f,
                               c.y + dir_dr[d] * cell_size * 0.5f};
            DrawLineEx(c, edge, 15.0f, pipe);
        }
        DrawCircleV(c, 8.0f, pipe);

        if (tiles_[i].seal) {
            const Color red = Color{230, 70, 70, 255};
            DrawLineEx(Vector2{c.x - 18, c.y - 18}, Vector2{c.x + 18, c.y + 18},
                       7.0f, red);
            DrawLineEx(Vector2{c.x - 18, c.y + 18}, Vector2{c.x + 18, c.y - 18},
                       7.0f, red);
        }
        if (tiles_[i].rivet) {
            const bool lit = (flash_tile_ == i);
            DrawCircleV(Vector2{r.x + 16, r.y + 16}, 8.0f,
                        lit ? Color{250, 220, 120, 255}
                            : Color{200, 170, 90, 255});
            if (lit) DrawRectangleLinesEx(inner, 3, Color{250, 220, 120, 255});
        }
    }

    // Inlet stub on the left edge.
    const float iy = row_centre_y(inlet_row_);
    DrawLineEx(Vector2{grid_x - 40, iy}, Vector2{grid_x, iy}, 15.0f, pipe);
    DrawTriangle(Vector2{grid_x - 20, iy - 15}, Vector2{grid_x - 20, iy + 15},
                 Vector2{grid_x - 2, iy}, Color{110, 180, 240, 255});

    // Outlets on the right edge. Which one is the target is the Expert's half.
    for (int i = 0; i < outlet_count; ++i) {
        const float oy = row_centre_y(outlet_rows_[i]);
        DrawLineEx(Vector2{grid_x + grid_n * cell_size, oy},
                   Vector2{grid_x + grid_n * cell_size + 20, oy}, 15.0f, pipe);
        const Rectangle box{grid_x + grid_n * cell_size + 20, oy - 20, 40, 40};
        DrawRectangleRec(box, Color{48, 50, 58, 255});
        DrawRectangleLinesEx(box, 3, outline);
        const char text[2] = {static_cast<char>('X' + i), '\0'};
        DrawText(text,
                 static_cast<int>(box.x + (box.width -
                                           MeasureText(text, 26)) * 0.5f),
                 static_cast<int>(box.y + (box.height - 26) * 0.5f), 26,
                 Color{226, 228, 234, 255});
    }

    // Rule plate: the Defuser reads the letter out, the Expert looks it up.
    const Rectangle plate = plate_rect();
    DrawRectangleRec(plate, Color{206, 202, 194, 255});
    DrawRectangleLinesEx(plate, 3, outline);
    const char code[2] = {rule_code(), '\0'};
    DrawText(code,
             static_cast<int>(plate.x + (plate.width -
                                         MeasureText(code, 30)) * 0.5f),
             static_cast<int>(plate.y + (plate.height - 30) * 0.5f), 30,
             Color{22, 22, 26, 255});

    // Valve.
    const Rectangle v = valve_rect();
    DrawRectangleRec(v, Color{58, 62, 72, 255});
    DrawRectangleLinesEx(v, 3, outline);
    const int vfs = 30;
    DrawText("VALVE",
             static_cast<int>(v.x + (v.width - MeasureText("VALVE", vfs)) *
                                        0.5f),
             static_cast<int>(v.y + (v.height - vfs) * 0.5f), vfs,
             Color{226, 228, 234, 255});
}
