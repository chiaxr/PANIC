#pragma once

// The Tape Reader module: the face prints a short program, the Expert executes
// it by hand as if they were the CPU, and the Defuser types the result.
//
// Several instructions reach into the bomb, so the Expert cannot start until
// the casing widgets have been read out. The tape's reading direction is set
// by the serial number, which is the module's most reliable source of
// confidently wrong answers.

#include <array>
#include <cstdint>
#include <vector>

#include "puzzle.h"

class TapeReaderPuzzle : public Puzzle {
public:
    static constexpr int min_tape = 6;
    static constexpr int max_tape = 8;
    static constexpr int key_count = 12;
    static constexpr int max_digits = 3;

    // The instruction set. Every glyph is drawn as a shape rather than a
    // character: raylib's built-in font is ASCII only and has no symbols.
    enum class Instr {
        INSTR_CELLS,    // add the number of batteries
        INSTR_FORK,     // lit indicator? subtract 3 : add 3
        INSTR_HALVE,    // halve, rounding toward zero
        INSTR_DOUBLE,   // double
        INSTR_STAMP,    // replace with the serial's last digit
        INSTR_LIFT,     // add 7
        INSTR_DROP,     // subtract 4
        INSTR_STASH,    // copy the value to the spare register
        INSTR_RECALL,   // replace the value with the stashed one
        INSTR_COUNT };

    const char* name() const override { return "Tape Reader"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // A punched paper tape across the face.
    SurfaceMaterial material() const override {
        return materials::paper;
    }

private:
    // Key index under a module-local pixel; -1 if no key was hit.
    int key_at_pixel(Vector2 p) const;

    std::vector<uint8_t> tape_;
    int start_value_ = 0;
    int answer_ = 0;
    bool reverse_ = false;   // read right-to-left (serial contains a vowel)

    std::array<char, max_digits> entry_{};
    int entry_len_ = 0;
};
