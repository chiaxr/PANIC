#pragma once

// The Wires module: 3-6 coloured wires; exactly one must be cut. Which one is
// correct is decided by the wire colours/count and the bomb's serial number
// (see the defuser manual, manual/index.html). A worked example of a puzzle
// template that reads BombAttributes.

#include <vector>

#include "puzzle.h"

class WiresPuzzle : public Puzzle {
public:
    enum class WireColor {
        WIRE_RED, WIRE_BLUE, WIRE_YELLOW, WIRE_WHITE, WIRE_BLACK };

    const char* name() const override { return "Wires"; }
    void init(const BombAttributes& attrs) override;
    void update(const ModuleInput& in, float dt) override;
    void draw() override;

private:
    struct Wire {
        WireColor color;
        bool cut = false;
    };

    int count_color(WireColor c) const;
    // 1-based index of the last wire of a given colour; 0 if none.
    int last_index_of_color(WireColor c) const;
    // Applies the manual's rules to select which wire (0-based) is correct.
    int solve_correct_wire(const BombAttributes& attrs) const;
    // Hit-test a module-local pixel against the wire strips; -1 if no wire hit.
    int wire_at_pixel(Vector2 p) const;

    std::vector<Wire> wires_;
    int correct_index_ = 0;  // 0-based index of the wire that disarms the module
};
