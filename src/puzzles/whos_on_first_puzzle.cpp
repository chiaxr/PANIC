#include "puzzles/whos_on_first_puzzle.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "raylib.h"

namespace {

// ---------------------------------------------------------------------------
// PANIC's own tables. Step 1 maps the display word to the button position to
// read; step 2 maps that button's label to a priority list covering every
// label, so a match is always found. manual/index.html prints both verbatim —
// change one and you must change the other.
// ---------------------------------------------------------------------------
constexpr int label_count = 14;
const char* const labels[label_count] = {
    "READY", "STEADY", "HOLD", "WAIT", "NOW", "LATER", "NEVER",
    "MAYBE", "SURE", "NOPE", "YEP", "STOP", "GO", "NEXT",
};

struct DisplayEntry { const char* word; int position; };
constexpr int display_count = 20;
const DisplayEntry display_table[display_count] = {
    {"BLANK", 5},  {"EMPTY", 2},  {"FILLED", 4}, {"LEFT", 3},
    {"RIGHT", 3},  {"UPPER", 2},  {"LOWER", 5},  {"TOP", 2},
    {"BOTTOM", 1}, {"CENTRE", 2}, {"FIRST", 4},  {"LAST", 4},
    {"SINGLE", 3}, {"DOUBLE", 2}, {"TRIPLE", 3}, {"LIGHT", 0},
    {"DARK", 5},   {"OPEN", 3},   {"SHUT", 2},   {"PLAIN", 1},
};

struct PriorityEntry { const char* label; const char* order[label_count]; };
const PriorityEntry priority_table[label_count] = {
    {"READY",  {"GO", "READY", "YEP", "NEXT", "NEVER", "WAIT", "LATER", "SURE", "STEADY", "STOP", "MAYBE", "NOW", "HOLD", "NOPE"}},
    {"STEADY", {"READY", "GO", "NOW", "LATER", "HOLD", "YEP", "STOP", "SURE", "NEVER", "NEXT", "NOPE", "STEADY", "WAIT", "MAYBE"}},
    {"HOLD",   {"YEP", "GO", "HOLD", "NOPE", "MAYBE", "LATER", "STOP", "NEVER", "NEXT", "WAIT", "SURE", "READY", "NOW", "STEADY"}},
    {"WAIT",   {"NEXT", "READY", "NOPE", "SURE", "LATER", "STEADY", "HOLD", "YEP", "WAIT", "MAYBE", "NEVER", "STOP", "NOW", "GO"}},
    {"NOW",    {"STOP", "NEXT", "NOW", "READY", "GO", "LATER", "HOLD", "NOPE", "WAIT", "MAYBE", "STEADY", "YEP", "NEVER", "SURE"}},
    {"LATER",  {"WAIT", "NOPE", "NOW", "YEP", "LATER", "NEVER", "STEADY", "MAYBE", "SURE", "STOP", "HOLD", "GO", "READY", "NEXT"}},
    {"NEVER",  {"NEXT", "STOP", "NOPE", "SURE", "NEVER", "GO", "HOLD", "NOW", "READY", "YEP", "MAYBE", "WAIT", "STEADY", "LATER"}},
    {"MAYBE",  {"LATER", "SURE", "WAIT", "NEVER", "MAYBE", "NOW", "READY", "STOP", "STEADY", "GO", "HOLD", "NOPE", "NEXT", "YEP"}},
    {"SURE",   {"MAYBE", "READY", "YEP", "NOPE", "LATER", "STOP", "STEADY", "GO", "SURE", "WAIT", "NEVER", "NOW", "HOLD", "NEXT"}},
    {"NOPE",   {"GO", "YEP", "SURE", "NOPE", "HOLD", "READY", "LATER", "NEVER", "NOW", "WAIT", "STOP", "STEADY", "MAYBE", "NEXT"}},
    {"YEP",    {"STEADY", "NEVER", "SURE", "LATER", "STOP", "WAIT", "YEP", "NOPE", "MAYBE", "READY", "NEXT", "NOW", "GO", "HOLD"}},
    {"STOP",   {"NOW", "STOP", "MAYBE", "STEADY", "NEXT", "READY", "SURE", "GO", "NOPE", "WAIT", "YEP", "NEVER", "LATER", "HOLD"}},
    {"GO",     {"GO", "NEVER", "STOP", "NEXT", "SURE", "LATER", "WAIT", "NOPE", "STEADY", "MAYBE", "HOLD", "READY", "NOW", "YEP"}},
    {"NEXT",   {"NEVER", "STOP", "MAYBE", "NOPE", "LATER", "NOW", "SURE", "READY", "NEXT", "GO", "WAIT", "STEADY", "YEP", "HOLD"}},
};

// Module-local layout: two columns of three buttons, display across the top.
constexpr float display_x = 46.0f;
constexpr float display_y = 40.0f;
constexpr float display_w = 420.0f;
constexpr float display_h = 96.0f;
constexpr float btn_w = 200.0f;
constexpr float btn_h = 92.0f;
constexpr float btn_x0 = 46.0f;
constexpr float btn_y0 = 166.0f;
constexpr float btn_gap_x = 20.0f;
constexpr float btn_gap_y = 16.0f;

Rectangle button_rect(int idx) {
    const float x = btn_x0 + static_cast<float>(idx % 2) * (btn_w + btn_gap_x);
    const float y = btn_y0 + static_cast<float>(idx / 2) * (btn_h + btn_gap_y);
    return Rectangle{x, y, btn_w, btn_h};
}

std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

const char* const* priority_for(const char* label) {
    for (const PriorityEntry& e : priority_table) {
        if (std::strcmp(e.label, label) == 0) return e.order;
    }
    return priority_table[0].order;
}

} // namespace

void WhosOnFirstPuzzle::init(const BombAttributes& attrs) {
    (void)attrs;   // the module's own two tables decide everything
    stage_ = 0;
    deal_stage();
}

void WhosOnFirstPuzzle::deal_stage() {
    std::array<int, label_count> pick{};
    for (int i = 0; i < label_count; ++i) pick[static_cast<size_t>(i)] = i;
    std::shuffle(pick.begin(), pick.end(), rng());
    for (int i = 0; i < button_count; ++i) {
        labels_[static_cast<size_t>(i)] = labels[pick[static_cast<size_t>(i)]];
    }

    display_ = display_table[std::uniform_int_distribution<int>(
                                 0, display_count - 1)(rng())]
                   .word;
}

int WhosOnFirstPuzzle::solve_correct_button() const {
    // Step 1: the display word names the button whose label we read.
    int read_position = 0;
    for (const DisplayEntry& e : display_table) {
        if (std::strcmp(e.word, display_) == 0) {
            read_position = e.position;
            break;
        }
    }

    // Step 2: that label's priority list; press the first label present here.
    const char* const* order =
        priority_for(labels_[static_cast<size_t>(read_position)]);
    for (int i = 0; i < label_count; ++i) {
        for (int b = 0; b < button_count; ++b) {
            if (std::strcmp(order[i], labels_[static_cast<size_t>(b)]) == 0) {
                return b;
            }
        }
    }
    return 0;   // unreachable: the list covers every label
}

void WhosOnFirstPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                               float dt) {
    (void)dt;
    (void)ctx;
    if (is_solved() || !in.tapped) return;

    int hit = -1;
    for (int i = 0; i < button_count; ++i) {
        if (CheckCollisionPointRec(in.tap_pos, button_rect(i))) {
            hit = i;
            break;
        }
    }
    if (hit < 0) return;

    if (hit != solve_correct_button()) {
        raise_strike();
        deal_stage();   // a strike re-deals the current stage
        return;
    }

    if (++stage_ >= total_stages) {
        mark_solved();
        return;
    }
    deal_stage();
}

void WhosOnFirstPuzzle::draw() {
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});

    const Rectangle display{display_x, display_y, display_w, display_h};
    DrawRectangleRec(display, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(display, 5, Color{16, 16, 20, 255});
    if (!is_solved()) {
        const int fs = 46;
        DrawText(display_,
                 static_cast<int>(display.x + (display.width -
                                               MeasureText(display_, fs)) * 0.5f),
                 static_cast<int>(display.y + (display.height - fs) * 0.5f), fs,
                 Color{120, 240, 150, 255});
    }

    for (int i = 0; i < button_count; ++i) {
        const Rectangle r = button_rect(i);
        DrawRectangleRec(r, Color{60, 64, 76, 255});
        DrawRectangleLinesEx(r, 4, Color{16, 16, 20, 255});
        const char* text = labels_[static_cast<size_t>(i)];
        const int fs = 34;
        DrawText(text,
                 static_cast<int>(r.x + (r.width - MeasureText(text, fs)) * 0.5f),
                 static_cast<int>(r.y + (r.height - fs) * 0.5f), fs,
                 Color{232, 234, 240, 255});
    }

    for (int i = 0; i < total_stages; ++i) {
        DrawCircle(46 + i * 30, module_tex_size - 18, 10,
                   i < stage_ ? Color{90, 220, 120, 255}
                              : Color{60, 63, 70, 255});
    }
}
