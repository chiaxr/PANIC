#pragma once

// Needy module — Venting Gas: every so often the display asks a question and
// the defuser has to answer it before the countdown runs out.

#include "puzzles/needy_puzzle.h"

class VentingGasPuzzle : public NeedyPuzzle {
public:
    const char* name() const override { return "Venting Gas"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // The face is one lit question display.
    SurfaceMaterial material() const override {
        return materials::screen;
    }

protected:
    void on_activate() override;

private:
    bool vent_prompt_ = true;   // true = "VENT GAS?", false = "DETONATE?"
};
