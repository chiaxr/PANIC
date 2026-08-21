#pragma once

// Morse Code: a light blinks one word from the module's list in international
// Morse. Look the word up to get a frequency, tune the dial to it and
// transmit. The Morse alphabet is the ITU standard; the word list and the
// frequencies are PANIC's own and match manual/index.html.

#include <string>
#include <vector>

#include "puzzle.h"

class MorsePuzzle : public Puzzle {
public:
    const char* name() const override { return "Morse Code"; }
    void init(const BombAttributes& attrs) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    // Expand the answer word into the on/off pattern the light plays.
    void build_signal();

    // Symbol durations, in units of one dot.
    struct Blink { bool on; float units; };

    std::vector<Blink> signal_;
    size_t signal_index_ = 0;
    float signal_timer_ = 0.0f;
    bool light_on_ = false;

    int word_index_ = 0;      // index into the module's word list
    int dial_index_ = 0;      // index into the same list, i.e. the frequency
};
