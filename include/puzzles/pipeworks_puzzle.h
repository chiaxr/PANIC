#pragma once

// The Pipeworks module: a 4x4 grid of pipe tiles the Defuser rotates freely,
// then commits with the valve. The Expert holds the two things the Defuser
// cannot see - which outlet to reach, and the constraint the route must meet.
//
// Rotating costs nothing, so this is the one build-then-commit module on the
// bomb: the pair can afford to be wrong out loud.
//
// Commits are graded by flooding the grid from the inlet, never by comparing
// rotations against the generated solution. A 4x4 grid usually admits several
// valid routings, and refusing all but one would make the module feel broken.

#include <array>
#include <cstdint>

#include "puzzle.h"

class PipeworksPuzzle : public Puzzle {
public:
    static constexpr int grid = 4;
    static constexpr int outlet_count = 3;

    // The constraint in play, over and above reaching the target outlet and
    // keeping the flow away from every burst seal.
    enum class Rule {
        RULE_TEES,           // pass through exactly n tees
        RULE_FROM_ABOVE,     // enter the outlet from the tile directly above
        RULE_THROUGH_RIVET,  // pass through every rivetted tile
        RULE_COUNT };

    const char* name() const override { return "Pipeworks"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // Pipe tiles and a valve, all metal.
    SurfaceMaterial material() const override {
        return materials::metal_panel;
    }

private:
    // A tile is the set of edges it joins: bit 0 north, 1 east, 2 south,
    // 3 west. Turning it 90 degrees clockwise is a four-bit rotate, so no
    // shape table is needed for either drawing or grading.
    struct Tile {
        uint8_t mask = 0;
        bool rivet = false;
        bool seal = false;
    };

    // The letter stamped on the module's rule plate. It is all the Defuser
    // can see of the constraint; the manual turns it into a rule.
    char rule_code() const;
    // Lattice index under a module-local pixel; -1 if outside the grid.
    int tile_at_pixel(Vector2 p) const;
    // Marks every tile the inlet can reach through joined edges.
    void flood(std::array<bool, grid * grid>& wet) const;
    // True when the current rotations satisfy outlet, seals and constraint.
    bool commit_passes() const;

    std::array<Tile, grid * grid> tiles_{};
    std::array<int, outlet_count> outlet_rows_{};
    int inlet_row_ = 0;
    int target_outlet_ = 0;   // index into outlet_rows_
    Rule rule_ = Rule::RULE_TEES;
    int tee_target_ = 1;      // only meaningful for RULE_TEES

    int flash_tile_ = -1;     // rivetted tile that refused to turn
    float flash_time_ = 0.0f;
};
