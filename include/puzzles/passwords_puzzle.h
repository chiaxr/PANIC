#pragma once

// The Passwords module: five letter columns, each cycled with an up/down
// arrow. Exactly one of the module's word list can be spelled from the
// letters on offer; spelling it and pressing SUBMIT disarms the module.

#include <array>
#include <string>

#include "puzzle.h"

class PasswordsPuzzle : public Puzzle {
public:
    static constexpr int columns = 5;
    static constexpr int letters_per_column = 6;

    const char* name() const override { return "Passwords"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

private:
    // The word currently spelled across the five columns.
    std::string current_word() const;

    std::array<std::array<char, letters_per_column>, columns> wheels_{};
    std::array<int, columns> position_{};   // index into the column's letters
    std::string answer_;
};
