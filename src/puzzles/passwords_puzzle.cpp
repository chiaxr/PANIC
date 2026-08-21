#include "puzzles/passwords_puzzle.h"

#include <algorithm>
#include <cctype>
#include <random>

#include "raylib.h"

namespace {

// The module's word list. Generation guarantees exactly one of these can be
// spelled from the five columns; manual/index.html lists the same 35 words.
constexpr int word_count = 35;
const char* const words[word_count] = {
    "about", "after", "again", "below", "could", "every", "first", "found",
    "great", "house", "large", "learn", "never", "other", "place", "plant",
    "point", "right", "small", "sound", "spell", "still", "study", "their",
    "there", "these", "thing", "think", "three", "water", "where", "which",
    "world", "would", "write",
};

// Module-local layout.
constexpr float col_w = 78.0f;
constexpr float col_gap = 12.0f;
constexpr float col_x0 = 46.0f;
constexpr float letter_y = 214.0f;
constexpr float letter_h = 84.0f;
constexpr float arrow_h = 56.0f;
constexpr float up_y = letter_y - arrow_h - 12.0f;
constexpr float down_y = letter_y + letter_h + 12.0f;
constexpr float submit_y = 398.0f;
constexpr float submit_h = 66.0f;

Rectangle column_rect(int col, float y, float h) {
    const float x = col_x0 + static_cast<float>(col) * (col_w + col_gap);
    return Rectangle{x, y, col_w, h};
}

Rectangle submit_rect() {
    return Rectangle{col_x0, submit_y,
                     static_cast<float>(PasswordsPuzzle::columns) *
                             (col_w + col_gap) - col_gap,
                     submit_h};
}

} // namespace

void PasswordsPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;   // Passwords depends only on its own letter wheels
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> pick_word(0, word_count - 1);
    std::uniform_int_distribution<int> pick_letter(0, 25);

    // Deal letters until only the answer can be spelled. Retrying is cheap and
    // keeps the invariant the manual promises: exactly one valid password.
    for (;;) {
        answer_ = words[pick_word(rng)];

        for (int c = 0; c < columns; ++c) {
            std::array<char, letters_per_column>& wheel = wheels_[c];
            wheel[0] = answer_[static_cast<size_t>(c)];
            for (int i = 1; i < letters_per_column; ++i) {
                for (;;) {
                    const char candidate =
                        static_cast<char>('a' + pick_letter(rng));
                    if (std::find(wheel.begin(), wheel.begin() + i, candidate) ==
                        wheel.begin() + i) {
                        wheel[static_cast<size_t>(i)] = candidate;
                        break;
                    }
                }
            }
            std::shuffle(wheel.begin(), wheel.end(), rng);
        }

        int spellable = 0;
        for (const char* word : words) {
            bool ok = true;
            for (int c = 0; c < columns && ok; ++c) {
                const auto& wheel = wheels_[static_cast<size_t>(c)];
                ok = std::find(wheel.begin(), wheel.end(),
                               word[c]) != wheel.end();
            }
            if (ok) ++spellable;
        }
        if (spellable == 1) break;
    }

    position_.fill(0);
}

std::string PasswordsPuzzle::current_word() const {
    std::string word;
    word.reserve(columns);
    for (int c = 0; c < columns; ++c) {
        word += wheels_[static_cast<size_t>(c)][
            static_cast<size_t>(position_[static_cast<size_t>(c)])];
    }
    return word;
}

void PasswordsPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                             float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    for (int c = 0; c < columns; ++c) {
        if (CheckCollisionPointRec(in.tap_pos, column_rect(c, up_y, arrow_h))) {
            position_[static_cast<size_t>(c)] =
                (position_[static_cast<size_t>(c)] + letters_per_column - 1) %
                letters_per_column;
            return;
        }
        if (CheckCollisionPointRec(in.tap_pos,
                                   column_rect(c, down_y, arrow_h))) {
            position_[static_cast<size_t>(c)] =
                (position_[static_cast<size_t>(c)] + 1) % letters_per_column;
            return;
        }
    }

    if (CheckCollisionPointRec(in.tap_pos, submit_rect())) {
        if (current_word() == answer_) {
            mark_solved();
        } else {
            raise_strike();
        }
    }
}

void PasswordsPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    for (int c = 0; c < columns; ++c) {
        const Rectangle up = column_rect(c, up_y, arrow_h);
        const Rectangle down = column_rect(c, down_y, arrow_h);
        const Rectangle cell = column_rect(c, letter_y, letter_h);

        DrawRectangleRec(up, Color{48, 50, 58, 255});
        DrawRectangleRec(down, Color{48, 50, 58, 255});
        const Color arrow = Color{200, 204, 212, 255};
        DrawTriangle(Vector2{up.x + up.width * 0.5f, up.y + 14.0f},
                     Vector2{up.x + 18.0f, up.y + up.height - 16.0f},
                     Vector2{up.x + up.width - 18.0f, up.y + up.height - 16.0f},
                     arrow);
        DrawTriangle(Vector2{down.x + 18.0f, down.y + 16.0f},
                     Vector2{down.x + down.width * 0.5f,
                             down.y + down.height - 14.0f},
                     Vector2{down.x + down.width - 18.0f, down.y + 16.0f},
                     arrow);

        DrawRectangleRec(cell, Color{206, 202, 194, 255});
        DrawRectangleLinesEx(cell, 3, Color{16, 16, 20, 255});

        const char text[2] = {
            static_cast<char>(std::toupper(
                wheels_[static_cast<size_t>(c)][
                    static_cast<size_t>(position_[static_cast<size_t>(c)])])),
            '\0'};
        const int fs = 62;
        DrawText(text,
                 static_cast<int>(cell.x + (cell.width -
                                            MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(cell.y + (cell.height - fs) * 0.5f), fs,
                 Color{22, 22, 26, 255});
    }

    const Rectangle submit = submit_rect();
    DrawRectangleRec(submit, Color{58, 62, 72, 255});
    DrawRectangleLinesEx(submit, 4, Color{16, 16, 20, 255});
    const int fs = 34;
    DrawText("SUBMIT",
             static_cast<int>(submit.x +
                              (submit.width - MeasureText("SUBMIT", fs)) * 0.5f),
             static_cast<int>(submit.y + (submit.height - fs) * 0.5f), fs,
             Color{226, 228, 234, 255});
}
