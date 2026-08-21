#pragma once

// The Memory module: five stages, each showing a digit above four labelled
// buttons. Which button is correct depends on the digit and on what was
// pressed in earlier stages, so the Expert has to write the stages down.
// A wrong press restarts the module from stage one.

#include <array>

#include "puzzle.h"

class MemoryPuzzle : public Puzzle {
public:
    static constexpr int button_count = 4;
    static constexpr int stage_count = 5;

    const char* name() const override { return "Memory"; }
    void init(const BombAttributes& attrs) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    void deal_stage();
    // 0-based index of the button that advances the current stage.
    int solve_correct_button() const;
    // 0-based position of the button carrying `label`; -1 if absent.
    int position_of_label(int label) const;

    std::array<int, button_count> labels_{};   // digit printed on each button
    int display_ = 1;                          // digit in the big display
    int stage_ = 0;                            // 0-based

    // What was pressed in each completed stage, both 1-based as the manual
    // states them.
    std::array<int, stage_count> pressed_position_{};
    std::array<int, stage_count> pressed_label_{};
};
