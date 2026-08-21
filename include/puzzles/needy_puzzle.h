#pragma once

// Shared behaviour for needy modules: they are never disarmed, they lie
// dormant for a while, then wake up and demand attention within a countdown.
// Failing to satisfy them in time is a strike, after which they simply go
// dormant and come back later.
//
// Subclasses call tick_needy() from update(), override on_activate() to deal a
// fresh demand, and call satisfy() once the demand has been met. on_expire()
// defaults to a strike; Knobs overrides it because being in the right position
// when the timer runs out is itself the answer.

#include <random>

#include "puzzle.h"

class NeedyPuzzle : public Puzzle {
public:
    bool is_needy() const override { return true; }

protected:
    // Seeds the module's engine from the bomb's, then goes dormant.
    void reset_needy(std::mt19937& rng);
    void tick_needy(float dt);

    bool active() const { return active_; }
    float needy_time_left() const { return timer_; }
    // Fraction of the countdown still remaining, for drawing a gauge.
    float needy_fraction() const;

    void satisfy() { go_dormant(); }

    virtual void on_activate() {}
    virtual void on_expire() { raise_strike(); }

    std::mt19937& needy_rng() { return rng_; }

private:
    void go_dormant();

    std::mt19937 rng_;
    bool active_ = false;
    float timer_ = 0.0f;
};
