#include "puzzles/morse_puzzle.h"

#include <cstring>
#include <random>

#include "raylib.h"

namespace {

// PANIC's word list. Frequencies run 3.505 MHz upwards in 5 kHz steps, stored
// in kHz to keep the arithmetic exact; the manual prints the same pairs.
constexpr int word_count = 16;
const char* const words[word_count] = {
    "boxes", "clamp", "drift", "flint", "ghost", "hover", "ivory", "jolts",
    "knots", "lunar", "mirth", "noble", "optic", "prism", "quilt", "rusty",
};
constexpr int base_frequency_khz = 3505;
constexpr int frequency_step_khz = 5;

// International Morse, a-z.
const char* const morse[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....",
    "..",   ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.",
    "--.-", ".-.",  "...",  "-",    "..-",  "...-", ".--",  "-..-",
    "-.--", "--..",
};

// Timing, in dot units per second.
constexpr float dots_per_second = 4.0f;
constexpr float dot_units = 1.0f;
constexpr float dash_units = 3.0f;
constexpr float symbol_gap_units = 1.0f;
constexpr float letter_gap_units = 3.0f;
constexpr float word_gap_units = 7.0f;

// Module-local layout.
constexpr float light_cx = 256.0f;
constexpr float light_cy = 86.0f;
constexpr float light_r = 40.0f;
constexpr float dial_x = 56.0f;
constexpr float dial_y = 176.0f;
constexpr float dial_w = 400.0f;
constexpr float dial_h = 92.0f;
constexpr float arrow_w = 110.0f;
constexpr float arrow_h = 76.0f;
constexpr float arrow_y = 292.0f;
constexpr float tx_y = 392.0f;
constexpr float tx_h = 76.0f;

Rectangle down_rect() { return Rectangle{dial_x, arrow_y, arrow_w, arrow_h}; }
Rectangle up_rect() {
    return Rectangle{dial_x + dial_w - arrow_w, arrow_y, arrow_w, arrow_h};
}
Rectangle tx_rect() { return Rectangle{dial_x, tx_y, dial_w, tx_h}; }

} // namespace

void MorsePuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    (void)attrs;   // the blinked word decides everything
    word_index_ = std::uniform_int_distribution<int>(0, word_count - 1)(rng);
    dial_index_ = std::uniform_int_distribution<int>(0, word_count - 1)(rng);

    build_signal();
}

void MorsePuzzle::build_signal() {
    signal_.clear();
    const char* word = words[word_index_];
    const size_t len = std::strlen(word);

    for (size_t i = 0; i < len; ++i) {
        const char* code = morse[static_cast<size_t>(word[i] - 'a')];
        for (size_t s = 0; code[s] != '\0'; ++s) {
            signal_.push_back({true, code[s] == '-' ? dash_units : dot_units});
            if (code[s + 1] != '\0') {
                signal_.push_back({false, symbol_gap_units});
            }
        }
        signal_.push_back(
            {false, i + 1 == len ? word_gap_units : letter_gap_units});
    }

    signal_index_ = 0;
    signal_timer_ = signal_.empty() ? 0.0f : signal_[0].units / dots_per_second;
    light_on_ = signal_.empty() ? false : signal_[0].on;
}

void MorsePuzzle::update(const ModuleInput& in, const BombContext& ctx,
                         float dt) {
    (void)ctx;

    // The light keeps transmitting on a loop, solved or not.
    if (!signal_.empty()) {
        signal_timer_ -= dt;
        while (signal_timer_ <= 0.0f) {
            signal_index_ = (signal_index_ + 1) % signal_.size();
            signal_timer_ += signal_[signal_index_].units / dots_per_second;
            light_on_ = signal_[signal_index_].on;
        }
    }
    if (is_solved()) {
        light_on_ = false;
        return;
    }

    if (!in.tapped) return;

    if (CheckCollisionPointRec(in.tap_pos, down_rect())) {
        dial_index_ = (dial_index_ + word_count - 1) % word_count;
        return;
    }
    if (CheckCollisionPointRec(in.tap_pos, up_rect())) {
        dial_index_ = (dial_index_ + 1) % word_count;
        return;
    }
    if (CheckCollisionPointRec(in.tap_pos, tx_rect())) {
        if (dial_index_ == word_index_) {
            mark_solved();
        } else {
            raise_strike();
        }
    }
}

void MorsePuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    // Transmitting light.
    DrawCircle(static_cast<int>(light_cx), static_cast<int>(light_cy),
               light_r + 10.0f, Color{28, 29, 34, 255});
    DrawCircle(static_cast<int>(light_cx), static_cast<int>(light_cy), light_r,
               light_on_ ? Color{255, 244, 180, 255} : Color{72, 68, 52, 255});

    // Frequency readout.
    const Rectangle dial{dial_x, dial_y, dial_w, dial_h};
    DrawRectangleRec(dial, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(dial, 5, Color{16, 16, 20, 255});
    const int khz = base_frequency_khz + dial_index_ * frequency_step_khz;
    const char* text = TextFormat("%d.%03d MHz", khz / 1000, khz % 1000);
    const int fs = 44;
    DrawText(text,
             static_cast<int>(dial.x + (dial.width - MeasureText(text, fs)) * 0.5f),
             static_cast<int>(dial.y + (dial.height - fs) * 0.5f), fs,
             Color{120, 240, 150, 255});

    // Tuning arrows.
    const Color arrow = Color{200, 204, 212, 255};
    const Rectangle down = down_rect();
    const Rectangle up = up_rect();
    DrawRectangleRec(down, Color{48, 50, 58, 255});
    DrawRectangleRec(up, Color{48, 50, 58, 255});
    DrawTriangle(Vector2{down.x + down.width - 24.0f, down.y + 16.0f},
                 Vector2{down.x + 22.0f, down.y + down.height * 0.5f},
                 Vector2{down.x + down.width - 24.0f,
                         down.y + down.height - 16.0f},
                 arrow);
    // Wound the same way round as the down arrow: raylib culls the other
    // winding and the triangle silently never appears.
    DrawTriangle(Vector2{up.x + 24.0f, up.y + up.height - 16.0f},
                 Vector2{up.x + up.width - 22.0f, up.y + up.height * 0.5f},
                 Vector2{up.x + 24.0f, up.y + 16.0f}, arrow);

    // Transmit.
    const Rectangle tx = tx_rect();
    DrawRectangleRec(tx, Color{58, 62, 72, 255});
    DrawRectangleLinesEx(tx, 4, Color{16, 16, 20, 255});
    const int tfs = 34;
    DrawText("TX",
             static_cast<int>(tx.x + (tx.width - MeasureText("TX", tfs)) * 0.5f),
             static_cast<int>(tx.y + (tx.height - tfs) * 0.5f), tfs,
             Color{226, 228, 234, 255});
}
