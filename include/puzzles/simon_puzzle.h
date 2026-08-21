#pragma once

// Simon Says: the module flashes a growing colour sequence and the defuser
// presses the *mapped* colour for each flash. The mapping depends on whether
// the serial number contains a vowel and on how many strikes the bomb has
// taken, so it can change mid-module — this is the module that reads
// BombContext::strikes live.

#include <vector>

#include "puzzle.h"

class SimonPuzzle : public Puzzle {
public:
    enum class SimonColor { SIMON_RED, SIMON_BLUE, SIMON_GREEN, SIMON_YELLOW };
    static constexpr int color_count = 4;
    static constexpr int total_stages = 4;

    const char* name() const override { return "Simon Says"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

    // The colour to press when `flashed` lights up, for the given bomb state.
    static SimonColor mapped_color(SimonColor flashed, bool serial_has_vowel,
                                   int strikes);

private:
    int button_at_pixel(Vector2 p) const;

    std::vector<SimonColor> sequence_;
    bool serial_has_vowel_ = false;

    int stage_ = 1;        // how many flashes are being played back
    int input_index_ = 0;  // how far through the reply the defuser is

    // Playback animation.
    float timer_ = 0.0f;
    int flash_index_ = 0;
    bool flash_on_ = false;
    int lit_button_ = -1;   // button currently lit, or -1
};
