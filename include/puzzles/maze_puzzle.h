#pragma once

// Mazes: a 6x6 grid whose walls are invisible on the bomb. Two circular
// markers identify which of the module's mazes is in play; the Expert reads
// the walls off the manual and steers the defuser's white square to the red
// triangle. Walking into a wall is a strike.

#include "puzzle.h"

class MazePuzzle : public Puzzle {
public:
    static constexpr int grid = 6;

    const char* name() const override { return "Mazes"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    struct Cell { int row = 0; int col = 0; };

    // Direction index: 0 up, 1 right, 2 down, 3 left; -1 if no arrow was hit.
    int arrow_at_pixel(Vector2 p) const;
    bool blocked(Cell from, int direction) const;

    int maze_index_ = 0;
    Cell player_{};
    Cell target_{};
};
