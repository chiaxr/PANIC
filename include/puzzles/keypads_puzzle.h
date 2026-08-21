#pragma once

// The Keypads module: four symbols drawn from one column of a six-column
// table; they must be pressed in the order that column lists them.
//
// The symbol pool is ASCII rather than the original's unicode glyphs, because
// raylib's built-in font has no glyphs for those. The column table below is the
// authority; manual/index.html prints the same characters.

#include <array>
#include <vector>

#include "puzzle.h"

class KeypadsPuzzle : public Puzzle {
public:
    static constexpr int key_count = 4;

    const char* name() const override { return "Keypads"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    int key_at_pixel(Vector2 p) const;

    // Symbols shown on the four keys, in display order.
    std::array<char, key_count> keys_{};
    // Display-order indices of the keys, sorted into the column's order.
    std::array<int, key_count> press_order_{};
    std::array<bool, key_count> pressed_{};
    int next_press_ = 0;
};
