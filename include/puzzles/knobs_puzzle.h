#pragma once

// Needy module — Knobs: twelve indicator lights and a knob with four
// positions. The light pattern says which way the knob must point; being in
// the right position when the countdown ends is what satisfies it, so unlike
// the other needy modules there is nothing to "answer".

#include "puzzles/needy_puzzle.h"

class KnobsPuzzle : public NeedyPuzzle {
public:
    enum class KnobPosition { KNOB_UP, KNOB_RIGHT, KNOB_DOWN, KNOB_LEFT };
    static constexpr int led_count = 12;

    const char* name() const override { return "Knobs"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

protected:
    void on_activate() override;
    void on_expire() override;

private:
    int pattern_ = 0;                                  // 12-bit LED pattern
    KnobPosition position_ = KnobPosition::KNOB_UP;    // where the knob points
};
