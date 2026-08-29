#pragma once

// Needy module — Capacitor Discharge: a capacitor charges continuously and
// overloads if it is left alone. Holding the lever down discharges it. One
// pointer, one lever, nothing else to do while holding.

#include "puzzles/needy_puzzle.h"

class CapacitorPuzzle : public NeedyPuzzle {
public:
    const char* name() const override { return "Capacitor Discharge"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // A metal discharge lever over the meter.
    SurfaceMaterial material() const override {
        return materials::metal_panel;
    }

private:
    bool lever_at_pixel(Vector2 p) const;

    float charge_ = 0.0f;    // 0 empty, 1 overloaded
    bool holding_ = false;
};
