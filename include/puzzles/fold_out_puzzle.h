#pragma once

// The Fold-Out module: six squares on a 4x4 lattice form a cube net. The
// Expert folds it in their head, works out the three pairs of opposite faces,
// and names the square to press.
//
// The module never applies the manual's folding rules. It rolls a cube across
// the net and records which face lands on each square; that roll is also the
// validity test, because a six-square net visits all six faces exactly once.

#include <array>
#include <cstdint>

#include "puzzle.h"

class FoldOutPuzzle : public Puzzle {
public:
    static constexpr int grid = 4;
    static constexpr int net_cells = 6;

    // Shape drawn on a filled square. Each carries its own fixed colour, so
    // shape and colour are redundant channels and a colourblind player can
    // still name every square.
    enum class Symbol {
        SYM_CIRCLE, SYM_TRIANGLE, SYM_SQUARE, SYM_STAR, SYM_CROSS,
        SYM_CRESCENT };

    const char* name() const override { return "Fold-Out"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // A printed card net on a lattice.
    SurfaceMaterial material() const override {
        return materials::paper;
    }

private:
    // Lattice index under a module-local pixel; -1 if outside the grid.
    int cell_at_pixel(Vector2 p) const;
    // The square the manual's target table names for this bomb.
    int keyed_cell(const BombAttributes& attrs) const;
    // The square folding onto the opposite face from `cell`.
    int partner_of(int cell) const;

    static constexpr uint8_t empty_cell = 0xFF;

    // Symbol per lattice cell, row-major, or empty_cell where nothing is drawn.
    std::array<uint8_t, grid * grid> symbol_{};
    // Cube face index per filled cell; meaningless where symbol_ is empty.
    std::array<uint8_t, grid * grid> face_{};

    int anchor_ = -1;   // lattice index of the ringed square
    int answer_ = -1;   // lattice index of the square that disarms the module
};
