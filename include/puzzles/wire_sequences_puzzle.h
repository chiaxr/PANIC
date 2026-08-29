#pragma once

// Wire Sequences: four panels of up to three wires. Each wire has a colour and
// runs to a lettered terminal. Whether it should be cut depends on how many
// wires of that colour have already appeared across the whole module, so the
// Expert has to keep a running count. The down arrow advances a panel and is
// only safe once the panel is dealt with.

#include <array>
#include <vector>

#include "puzzle.h"

class WireSequencesPuzzle : public Puzzle {
public:
    enum class SeqColor { SEQ_RED, SEQ_BLUE, SEQ_BLACK };
    static constexpr int panel_count = 4;
    static constexpr int rows_per_panel = 3;

    const char* name() const override { return "Wire Sequences"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    struct Wire {
        bool present = false;
        SeqColor color = SeqColor::SEQ_RED;
        // Which lettered terminal the wire runs to: 0 = A, 1 = B, 2 = C. The
        // wire is drawn sloping to that terminal, so it is what the Defuser
        // reads out and what the manual's occurrence table asks about.
        int connection = 0;
        int occurrence = 1;   // which wire of this colour it is, 1-based
        bool cut = false;
        bool should_cut = false;
    };

    int wire_at_pixel(Vector2 p) const;

    std::array<std::array<Wire, rows_per_panel>, panel_count> panels_{};
    int panel_ = 0;
};
