#pragma once

// Complicated Wires: a row of wires, each carrying up to four properties —
// red colouring, blue colouring, a star below it, and a lit LED above it.
// Those four bits index a table that says cut, don't cut, or cut only if some
// bomb-wide fact holds. Every wire that must be cut has to be cut.

#include <vector>

#include "puzzle.h"

class ComplicatedWiresPuzzle : public Puzzle {
public:
    const char* name() const override { return "Complicated Wires"; }
    void init(const BombAttributes& attrs) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    struct Wire {
        bool red = false;
        bool blue = false;
        bool star = false;
        bool led = false;
        bool cut = false;
        bool should_cut = false;
    };

    int wire_at_pixel(Vector2 p) const;
    bool all_required_cut() const;

    std::vector<Wire> wires_;
};
