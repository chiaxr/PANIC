#include "puzzles/maze_puzzle.h"

#include <random>

#include "raylib.h"

namespace {

// Wall bits per cell: 1 = north, 2 = east, 4 = south, 8 = west.
constexpr int wall_n = 1;
constexpr int wall_e = 2;
constexpr int wall_s = 4;
constexpr int wall_w = 8;

constexpr int maze_count = 6;

// PANIC's own mazes. Each is fully connected, so every target is reachable;
// manual/index.html draws these exact grids.
constexpr int mazes[maze_count][MazePuzzle::grid][MazePuzzle::grid] = {
    {{11,  9,  5,  5,  1,  3}, {10, 12,  7,  9,  6, 10},
     {12,  5,  3, 10,  9,  2}, { 9,  3, 10, 12,  6, 10},
     {10, 12,  2,  9,  3, 10}, {12,  5,  4,  6, 12,  6}},
    {{13,  5,  3, 13,  5,  3}, { 9,  7, 12,  5,  5,  2},
     { 8,  5,  1,  3,  9,  6}, {10,  9,  6, 14, 12,  3},
     {10, 10,  9,  1,  7, 10}, {12,  4,  6, 12,  5,  6}},
    {{13,  5,  5,  3, 13,  3}, { 9,  1,  7, 12,  3, 10},
     {10, 10,  9,  5,  6, 10}, { 8,  4,  6, 11,  9,  2},
     {10,  9,  3, 12,  6, 10}, {12,  6, 12,  5,  5,  6}},
    {{13,  5,  3,  9,  5,  3}, { 9,  5,  6, 10, 11, 10},
     {12,  3,  9,  6,  8,  6}, {11, 10,  8,  3, 10, 11},
     { 8,  2, 14,  8,  2, 10}, {12,  4,  5,  6, 12,  6}},
    {{13,  3, 13,  5,  5,  3}, {11, 12,  3,  9,  3, 10},
     { 8,  7, 10, 10, 10, 10}, {12,  3, 10,  8,  0,  2},
     { 9,  6, 12,  6, 10, 10}, {12,  5,  5,  5,  4,  6}},
    {{11,  9,  1,  3,  9,  3}, {10, 12,  2, 10, 12,  2},
     {12,  5,  2, 12,  5,  2}, {11,  9,  0,  3,  9,  6},
     {10, 10, 10, 14, 10, 11}, {12,  6, 12,  5,  4,  6}},
};

// The two marker circles that name each maze. The unordered pair is unique
// across the set, so the two circles identify the grid.
struct MarkerPair { int r0, c0, r1, c1; };
constexpr MarkerPair markers[maze_count] = {
    {2, 1, 3, 5},   // maze 0
    {0, 0, 4, 0},   // maze 1
    {2, 4, 0, 4},   // maze 2
    {1, 0, 0, 3},   // maze 3
    {3, 0, 1, 0},   // maze 4
    {4, 3, 0, 4},   // maze 5
};

// Module-local layout.
constexpr float board_x = 32.0f;
constexpr float board_y = 56.0f;
constexpr float board_size = 264.0f;
constexpr float cell_size = board_size / MazePuzzle::grid;
constexpr float arrow_size = 58.0f;
constexpr float pad_cx = 412.0f;   // centre of the direction pad, clear of the board
constexpr float pad_cy = 240.0f;

Vector2 cell_centre(int row, int col) {
    return Vector2{board_x + cell_size * (static_cast<float>(col) + 0.5f),
                   board_y + cell_size * (static_cast<float>(row) + 0.5f)};
}

Rectangle arrow_rect(int direction) {
    switch (direction) {
        case 0: return Rectangle{pad_cx - arrow_size * 0.5f,
                                 pad_cy - arrow_size * 1.6f, arrow_size,
                                 arrow_size};
        case 1: return Rectangle{pad_cx + arrow_size * 0.6f,
                                 pad_cy - arrow_size * 0.5f, arrow_size,
                                 arrow_size};
        case 2: return Rectangle{pad_cx - arrow_size * 0.5f,
                                 pad_cy + arrow_size * 0.6f, arrow_size,
                                 arrow_size};
        default: return Rectangle{pad_cx - arrow_size * 1.6f,
                                  pad_cy - arrow_size * 0.5f, arrow_size,
                                  arrow_size};
    }
}

} // namespace

void MazePuzzle::init(const BombAttributes& attrs) {
    (void)attrs;   // which maze is in play is shown by the markers, not the bomb
    std::mt19937 rng(std::random_device{}());
    maze_index_ = std::uniform_int_distribution<int>(0, maze_count - 1)(rng);

    std::uniform_int_distribution<int> coord(0, grid - 1);
    player_ = Cell{coord(rng), coord(rng)};
    do {
        target_ = Cell{coord(rng), coord(rng)};
    } while (target_.row == player_.row && target_.col == player_.col);
}

bool MazePuzzle::blocked(Cell from, int direction) const {
    const int walls = mazes[maze_index_][from.row][from.col];
    switch (direction) {
        case 0: return from.row == 0 || (walls & wall_n) != 0;
        case 1: return from.col == grid - 1 || (walls & wall_e) != 0;
        case 2: return from.row == grid - 1 || (walls & wall_s) != 0;
        default: return from.col == 0 || (walls & wall_w) != 0;
    }
}

int MazePuzzle::arrow_at_pixel(Vector2 p) const {
    for (int d = 0; d < 4; ++d) {
        if (CheckCollisionPointRec(p, arrow_rect(d))) return d;
    }
    return -1;
}

void MazePuzzle::update(const ModuleInput& in, const BombContext& ctx,
                        float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    const int dir = arrow_at_pixel(in.tap_pos);
    if (dir < 0) return;

    if (blocked(player_, dir)) {
        raise_strike();
        return;
    }

    switch (dir) {
        case 0: --player_.row; break;
        case 1: ++player_.col; break;
        case 2: ++player_.row; break;
        default: --player_.col; break;
    }

    if (player_.row == target_.row && player_.col == target_.col) mark_solved();
}

void MazePuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    // The board. Walls are deliberately not drawn — that is the whole module.
    DrawRectangle(static_cast<int>(board_x) - 8, static_cast<int>(board_y) - 8,
                  static_cast<int>(board_size) + 16,
                  static_cast<int>(board_size) + 16, Color{22, 23, 28, 255});
    for (int r = 0; r <= grid; ++r) {
        const float y = board_y + cell_size * static_cast<float>(r);
        DrawLineEx(Vector2{board_x, y}, Vector2{board_x + board_size, y}, 2.0f,
                   Color{46, 48, 56, 255});
        const float x = board_x + cell_size * static_cast<float>(r);
        DrawLineEx(Vector2{x, board_y}, Vector2{x, board_y + board_size}, 2.0f,
                   Color{46, 48, 56, 255});
    }

    // Marker circles: these name the maze for the Expert.
    const MarkerPair& m = markers[maze_index_];
    const Color marker = Color{90, 200, 250, 255};
    DrawCircleV(cell_centre(m.r0, m.c0), 13.0f, marker);
    DrawCircleV(cell_centre(m.r1, m.c1), 13.0f, marker);

    // Target triangle, then the player square on top.
    const Vector2 t = cell_centre(target_.row, target_.col);
    DrawTriangle(Vector2{t.x - 15.0f, t.y + 13.0f},
                 Vector2{t.x + 15.0f, t.y + 13.0f}, Vector2{t.x, t.y - 15.0f},
                 Color{230, 70, 70, 255});

    const Vector2 p = cell_centre(player_.row, player_.col);
    DrawRectangle(static_cast<int>(p.x) - 11, static_cast<int>(p.y) - 11, 22, 22,
                  Color{240, 240, 246, 255});

    // Direction pad.
    const Color face = Color{48, 50, 58, 255};
    const Color glyph = Color{200, 204, 212, 255};
    for (int d = 0; d < 4; ++d) {
        const Rectangle r = arrow_rect(d);
        DrawRectangleRec(r, face);
        DrawRectangleLinesEx(r, 3, Color{16, 16, 20, 255});
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        const float s = 18.0f;
        switch (d) {
            case 0:
                DrawTriangle(Vector2{cx - s, cy + s}, Vector2{cx + s, cy + s},
                             Vector2{cx, cy - s}, glyph);
                break;
            case 1:
                DrawTriangle(Vector2{cx - s, cy + s}, Vector2{cx + s, cy},
                             Vector2{cx - s, cy - s}, glyph);
                break;
            case 2:
                DrawTriangle(Vector2{cx - s, cy - s}, Vector2{cx, cy + s},
                             Vector2{cx + s, cy - s}, glyph);
                break;
            default:
                DrawTriangle(Vector2{cx + s, cy - s}, Vector2{cx - s, cy},
                             Vector2{cx + s, cy + s}, glyph);
                break;
        }
    }
}
