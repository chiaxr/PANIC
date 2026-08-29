#pragma once

// The Star Chart module: a constellation drawn at an arbitrary rotation. The
// Defuser describes what they see, the Expert identifies it from properties
// that survive rotation, and then names the star to press.
//
// Two catalogue entries share every (total stars, bright stars) signature, so
// counting only narrows the chart to a pair; the manual's description of the
// layout -- rotation-proof like the counts -- is what decides between them.
//
// The catalogue points are used exactly as stored, only rotated and translated.
// Rotation is an isometry, so every distance ratio the manual's target rules
// depend on is preserved and the stored answer is correct by construction --
// which is why there is no per-star jitter here.

#include <cstdint>
#include <vector>

#include "puzzle.h"

class StarChartPuzzle : public Puzzle {
public:
    static constexpr int grid = 6;
    static constexpr int max_stars = 8;

    enum class Tier { TIER_BRIGHT, TIER_MEDIUM, TIER_DIM };

    const char* name() const override { return "Star Chart"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // A night field read through glass.
    SurfaceMaterial material() const override {
        return materials::screen;
    }

private:
    struct Star {
        Vector2 pos{};
        uint8_t tier = 0;
        float phase = 0.0f;   // twinkle offset, too small to change the tier
    };

    // Index of the star nearest a module-local pixel, or -1 if the tap landed
    // outside every hit-box. The hit radius is deliberately larger than any
    // star: this module is about identification, not precision.
    int star_at_pixel(Vector2 p) const;

    std::vector<Star> stars_;
    int answer_ = 0;
    float time_ = 0.0f;
};
