#include "puzzles/tape_reader_puzzle.h"

#include <random>
#include <string>

#include "raylib.h"

namespace {

using Instr = TapeReaderPuzzle::Instr;

constexpr int instr_count = static_cast<int>(Instr::INSTR_COUNT);
constexpr int value_max = 999;

// ---------------------------------------------------------------------------
// The interpreter. Kept pure so init() can use it to compute the answer and to
// run the generator's checks, with nothing else in the module depending on it.
//
// `ok` comes back false when the value leaves 0..999 at any step; the generator
// re-draws the tape rather than shipping a program the Expert cannot follow.
// ---------------------------------------------------------------------------
int run_tape(const uint8_t* tape, int length, bool reverse, int start,
             const BombAttributes& attrs, bool* ok) {
    int value = start;
    int stash = 0;
    bool stashed = false;
    if (ok) *ok = true;

    for (int step = 0; step < length; ++step) {
        const int i = reverse ? length - 1 - step : step;
        switch (static_cast<Instr>(tape[i])) {
            case Instr::INSTR_CELLS:  value += attrs.battery_count; break;
            case Instr::INSTR_FORK:
                value += attrs.lit_indicator_count() > 0 ? -3 : 3;
                break;
            case Instr::INSTR_HALVE:  value /= 2; break;   // toward zero
            case Instr::INSTR_DOUBLE: value *= 2; break;
            case Instr::INSTR_STAMP: {
                const int d = attrs.serial_last_digit();
                value = d < 0 ? 0 : d;
                break;
            }
            case Instr::INSTR_LIFT:   value += 7; break;
            case Instr::INSTR_DROP:   value -= 4; break;
            case Instr::INSTR_STASH:  stash = value; stashed = true; break;
            case Instr::INSTR_RECALL:
                // Generation never emits a Recall before its Stash; this is
                // only here so a bad tape could never read an unset register.
                if (stashed) value = stash;
                break;
            default: break;
        }
        if (value < 0 || value > value_max) {
            if (ok) *ok = false;
            return value;
        }
    }
    return value;
}

// True when every Recall on the tape is preceded by a Stash in the order the
// Expert will actually read it.
bool stash_before_recall(const uint8_t* tape, int length, bool reverse) {
    bool stashed = false;
    for (int step = 0; step < length; ++step) {
        const int i = reverse ? length - 1 - step : step;
        const Instr op = static_cast<Instr>(tape[i]);
        if (op == Instr::INSTR_STASH) stashed = true;
        if (op == Instr::INSTR_RECALL && !stashed) return false;
    }
    return true;
}

bool reads_the_bomb(Instr op) {
    return op == Instr::INSTR_CELLS || op == Instr::INSTR_FORK ||
           op == Instr::INSTR_STAMP;
}

// ---------------------------------------------------------------------------
// Module-local layout.
// ---------------------------------------------------------------------------
constexpr float tape_cell = 58.0f;
constexpr float tape_gap = 10.0f;
constexpr int tape_per_row = 4;
constexpr float tape_x0 = 125.0f;
constexpr float tape_y0 = 72.0f;
constexpr float tape_row_dy = tape_cell + 16.0f;   // room for the wrap line

constexpr float key_w = 148.0f;
constexpr float key_h = 49.0f;
constexpr float key_gap_x = 10.0f;
constexpr float key_gap_y = 8.0f;
constexpr float key_x0 = 24.0f;
constexpr float key_y0 = 280.0f;

Rectangle tape_rect(int index) {
    const float col = static_cast<float>(index % tape_per_row);
    const float row = static_cast<float>(index / tape_per_row);
    return Rectangle{tape_x0 + col * (tape_cell + tape_gap),
                     tape_y0 + row * tape_row_dy, tape_cell, tape_cell};
}

Rectangle key_rect(int index) {
    const float col = static_cast<float>(index % 3);
    const float row = static_cast<float>(index / 3);
    return Rectangle{key_x0 + col * (key_w + key_gap_x),
                     key_y0 + row * (key_h + key_gap_y), key_w, key_h};
}

// Keys 0-8 are digits 1-9, then CLR, 0, ENT.
const char* key_label(int index) {
    static const char* labels[TapeReaderPuzzle::key_count] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "ENT"};
    return labels[index];
}

// -1 for the two command keys.
int key_digit(int index) {
    if (index <= 8) return index + 1;
    if (index == 10) return 0;
    return -1;
}

void draw_instr_icon(int op, Vector2 c, Color col, Color bg) {
    switch (static_cast<Instr>(op)) {
        case Instr::INSTR_CELLS:
            DrawCircleV(c, 15.0f, col);
            break;
        case Instr::INSTR_FORK: {
            // A lightning bolt: the instruction that branches on the bomb.
            DrawLineEx(Vector2{c.x + 7, c.y - 16}, Vector2{c.x - 8, c.y - 1},
                       6.0f, col);
            DrawLineEx(Vector2{c.x - 8, c.y - 1}, Vector2{c.x + 4, c.y + 1},
                       6.0f, col);
            DrawLineEx(Vector2{c.x + 4, c.y + 1}, Vector2{c.x - 7, c.y + 16},
                       6.0f, col);
            break;
        }
        case Instr::INSTR_HALVE:
            // A disc with its right half masked off.
            DrawCircleV(c, 15.0f, col);
            DrawRectangleV(Vector2{c.x, c.y - 17}, Vector2{19.0f, 34.0f}, bg);
            DrawCircleLinesV(c, 15.0f, col);
            break;
        case Instr::INSTR_DOUBLE:
            DrawLineEx(Vector2{c.x - 13, c.y - 13}, Vector2{c.x + 13, c.y + 13},
                       6.0f, col);
            DrawLineEx(Vector2{c.x - 13, c.y + 13}, Vector2{c.x + 13, c.y - 13},
                       6.0f, col);
            break;
        case Instr::INSTR_STAMP:
            // A square rather than a disc: at bay size a slashed circle was
            // too easily mistaken for Halve.
            DrawRectangleLinesEx(Rectangle{c.x - 14, c.y - 14, 28.0f, 28.0f},
                                 4.0f, col);
            DrawRectangleV(Vector2{c.x - 6, c.y - 6}, Vector2{12.0f, 12.0f},
                           col);
            break;
        case Instr::INSTR_LIFT: {
            const float s = 15.0f;
            DrawTriangle(Vector2{c.x - s, c.y + s}, Vector2{c.x + s, c.y + s},
                         Vector2{c.x, c.y - s}, col);
            break;
        }
        case Instr::INSTR_DROP: {
            const float s = 15.0f;
            DrawTriangle(Vector2{c.x - s, c.y - s}, Vector2{c.x, c.y + s},
                         Vector2{c.x + s, c.y - s}, col);
            break;
        }
        case Instr::INSTR_STASH: {
            // Arrow pressing down onto a bar: the value goes into the spare.
            const float s = 11.0f;
            DrawTriangle(Vector2{c.x - s, c.y - 6}, Vector2{c.x, c.y + 6},
                         Vector2{c.x + s, c.y - 6}, col);
            DrawRectangleV(Vector2{c.x - 15, c.y + 10}, Vector2{30.0f, 6.0f},
                           col);
            break;
        }
        default: {   // Recall: arrow lifting off the bar
            const float s = 11.0f;
            DrawTriangle(Vector2{c.x - s, c.y + 6}, Vector2{c.x + s, c.y + 6},
                         Vector2{c.x, c.y - 6}, col);
            DrawRectangleV(Vector2{c.x - 15, c.y - 16}, Vector2{30.0f, 6.0f},
                           col);
            break;
        }
    }
}

} // namespace

void TapeReaderPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    // Stated once in the manual: a serial with a vowel in it is read backwards.
    reverse_ = attrs.serial_has_vowel();

    std::uniform_int_distribution<int> len(min_tape, max_tape);
    std::uniform_int_distribution<int> op(0, instr_count - 1);
    std::uniform_int_distribution<int> start(8, 60);

    bool ok = false;
    for (int attempt = 0; attempt < 600 && !ok; ++attempt) {
        const int length = len(rng);
        tape_.assign(static_cast<size_t>(length), 0);
        for (int i = 0; i < length; ++i) {
            tape_[i] = static_cast<uint8_t>(op(rng));
        }
        start_value_ = start(rng);

        if (!stash_before_recall(tape_.data(), length, reverse_)) continue;

        // At least two instructions must reach into the bomb, so the tape can
        // never be executed without the casing being read out first.
        int bomb_reads = 0;
        for (int i = 0; i < length; ++i) {
            if (reads_the_bomb(static_cast<Instr>(tape_[i]))) ++bomb_reads;
        }
        if (bomb_reads < 2) continue;

        bool in_range = false;
        const int result = run_tape(tape_.data(), length, reverse_,
                                    start_value_, attrs, &in_range);
        if (!in_range) continue;
        if (result == start_value_) continue;

        // The important check: dropping any single glyph must land somewhere
        // else, so a mis-read is always a wrong answer rather than a lucky one.
        bool omission_safe = true;
        std::vector<uint8_t> shortened;
        shortened.reserve(static_cast<size_t>(length - 1));
        for (int skip = 0; skip < length && omission_safe; ++skip) {
            shortened.clear();
            for (int i = 0; i < length; ++i) {
                if (i != skip) shortened.push_back(tape_[i]);
            }
            bool sub_ok = false;
            const int alt =
                run_tape(shortened.data(), length - 1, reverse_, start_value_,
                         attrs, &sub_ok);
            if (sub_ok && alt == result) omission_safe = false;
        }
        if (!omission_safe) continue;

        answer_ = result;
        ok = true;
    }

    if (!ok) {
        // Fallback: stays inside 0..999 either way round and reads the same on
        // any bomb, so it is always solvable even if the search above gives up.
        tape_ = {static_cast<uint8_t>(Instr::INSTR_LIFT),
                 static_cast<uint8_t>(Instr::INSTR_DOUBLE),
                 static_cast<uint8_t>(Instr::INSTR_DROP),
                 static_cast<uint8_t>(Instr::INSTR_LIFT),
                 static_cast<uint8_t>(Instr::INSTR_HALVE),
                 static_cast<uint8_t>(Instr::INSTR_DOUBLE)};
        start_value_ = 10;
        answer_ = run_tape(tape_.data(), static_cast<int>(tape_.size()),
                           reverse_, start_value_, attrs, nullptr);
    }

    entry_len_ = 0;
}

int TapeReaderPuzzle::key_at_pixel(Vector2 p) const {
    for (int i = 0; i < key_count; ++i) {
        if (CheckCollisionPointRec(p, key_rect(i))) return i;
    }
    return -1;
}

void TapeReaderPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                              float dt) {
    (void)ctx;
    (void)dt;
    if (is_solved() || !in.tapped) return;

    const int key = key_at_pixel(in.tap_pos);
    if (key < 0) return;

    if (key == 9) {          // CLR
        entry_len_ = 0;
        return;
    }
    if (key == 11) {         // ENT: the module's only signal, no partial hints
        if (entry_len_ == 0) return;
        int value = 0;
        for (int i = 0; i < entry_len_; ++i) {
            value = value * 10 + (entry_[i] - '0');
        }
        if (value == answer_) {
            mark_solved();
        } else {
            raise_strike();
            entry_len_ = 0;
        }
        return;
    }

    if (entry_len_ >= max_digits) return;
    entry_[entry_len_++] = static_cast<char>('0' + key_digit(key));
}

void TapeReaderPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    const Color outline = Color{16, 16, 20, 255};
    const Color glyph = Color{226, 228, 234, 255};

    // ---- the tape ----
    const int length = static_cast<int>(tape_.size());
    for (int i = 0; i < length; ++i) {
        const Rectangle r = tape_rect(i);
        // Every third cell is a shade lighter, so a long read-aloud has
        // somewhere to keep its place.
        const Color face = (i % 3 == 2) ? Color{58, 62, 72, 255}
                                        : Color{44, 47, 55, 255};
        DrawRectangleRounded(r, 0.22f, 6, face);
        DrawRectangleRoundedLines(r, 0.22f, 6, outline);
        draw_instr_icon(tape_[i], Vector2{r.x + r.width * 0.5f,
                                         r.y + r.height * 0.5f},
                        glyph, face);
    }

    // Start marker at the leading end of the first row.
    const Rectangle first = tape_rect(0);
    const float my = first.y + first.height * 0.5f;
    DrawTriangle(Vector2{first.x - 20, my - 11}, Vector2{first.x - 20, my + 11},
                 Vector2{first.x - 4, my}, Color{240, 180, 60, 255});

    // Wrap indicator, so the reading order across the two rows is never in
    // doubt. Drawn in display order; which end the Expert starts from is the
    // manual's business.
    if (length > tape_per_row) {
        const Rectangle last_top = tape_rect(tape_per_row - 1);
        const Rectangle first_low = tape_rect(tape_per_row);
        const float y0 = last_top.y + last_top.height * 0.5f;
        const float y1 = first_low.y + first_low.height * 0.5f;
        const float xr = last_top.x + last_top.width + 12.0f;
        const float xl = first_low.x - 12.0f;
        // Routed through the gap between the rows rather than across the
        // second one, so it never draws over an instruction.
        const float ym = last_top.y + last_top.height + 8.0f;
        const Color wrap = Color{110, 116, 130, 255};
        DrawLineEx(Vector2{last_top.x + last_top.width + 2, y0},
                   Vector2{xr, y0}, 3.0f, wrap);
        DrawLineEx(Vector2{xr, y0}, Vector2{xr, ym}, 3.0f, wrap);
        DrawLineEx(Vector2{xr, ym}, Vector2{xl, ym}, 3.0f, wrap);
        DrawLineEx(Vector2{xl, ym}, Vector2{xl, y1}, 3.0f, wrap);
        DrawTriangle(Vector2{xl - 7, y1 - 12}, Vector2{xl + 7, y1 - 12},
                     Vector2{xl, y1}, wrap);
    }

    // ---- the register window ----
    const Rectangle well{112.0f, 212.0f, 288.0f, 56.0f};
    DrawRectangleRec(well, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(well, 5, outline);

    std::string shown;
    if (entry_len_ > 0) {
        shown.assign(entry_.data(), static_cast<size_t>(entry_len_));
    } else {
        shown = TextFormat("%03d", is_solved() ? answer_ : start_value_);
    }
    const int fs = 44;
    DrawText(shown.c_str(),
             static_cast<int>(well.x + (well.width -
                                        MeasureText(shown.c_str(), fs)) * 0.5f),
             static_cast<int>(well.y + (well.height - fs) * 0.5f), fs,
             Color{120, 240, 150, 255});

    // ---- the keypad ----
    for (int i = 0; i < key_count; ++i) {
        const Rectangle r = key_rect(i);
        const bool command = (i == 9 || i == 11);
        DrawRectangleRec(r, command ? Color{58, 62, 72, 255}
                                    : Color{206, 202, 194, 255});
        DrawRectangleLinesEx(r, 3, outline);

        const char* text = key_label(i);
        const int kfs = 30;
        DrawText(text,
                 static_cast<int>(r.x + (r.width - MeasureText(text, kfs)) *
                                            0.5f),
                 static_cast<int>(r.y + (r.height - kfs) * 0.5f), kfs,
                 command ? Color{226, 228, 234, 255} : Color{22, 22, 26, 255});
    }
}
