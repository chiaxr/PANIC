#include "puzzles/needy_puzzle.h"

namespace {

constexpr float active_seconds = 40.0f;
constexpr float dormant_min_seconds = 18.0f;
constexpr float dormant_max_seconds = 32.0f;

} // namespace

void NeedyPuzzle::reset_needy(std::mt19937& rng) {
    // Own engine, seeded from the bomb's, so wake-up times replay identically.
    rng_.seed(rng());
    active_ = false;
    go_dormant();
}

void NeedyPuzzle::go_dormant() {
    active_ = false;
    timer_ = std::uniform_real_distribution<float>(dormant_min_seconds,
                                                   dormant_max_seconds)(
        rng_);
}

float NeedyPuzzle::needy_fraction() const {
    if (!active_) return 0.0f;
    const float f = timer_ / active_seconds;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

void NeedyPuzzle::tick_needy(float dt) {
    timer_ -= dt;
    if (timer_ > 0.0f) return;

    if (active_) {
        on_expire();
        go_dormant();
    } else {
        active_ = true;
        timer_ = active_seconds;
        on_activate();
    }
}
