#pragma once

// Who's on First: a display word tells you which of six buttons to *read*,
// and that button's label gives an ordered list of labels; press the first
// label on that list which appears on the module. Three stages.
//
// The word and priority data is PANIC's own, generated and checked so that
// every lookup resolves; manual/index.html carries the same two tables.

#include <array>
#include <string>

#include "puzzle.h"

class WhosOnFirstPuzzle : public Puzzle {
public:
    static constexpr int button_count = 6;
    static constexpr int total_stages = 3;

    const char* name() const override { return "Who's on First"; }
    void init(const BombAttributes& attrs) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    void deal_stage();
    // 0-based index of the button that advances the current stage.
    int solve_correct_button() const;

    std::array<const char*, button_count> labels_{};
    const char* display_ = "";
    int stage_ = 0;
};
