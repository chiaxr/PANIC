#include "puzzles/star_chart_puzzle.h"

#include <cmath>
#include <random>

#include "raylib.h"

namespace {

constexpr int grid_n = StarChartPuzzle::grid;
constexpr int max_stars = StarChartPuzzle::max_stars;

constexpr uint8_t tier_bright = 0;
constexpr uint8_t tier_medium = 1;
constexpr uint8_t tier_dim = 2;

// ---------------------------------------------------------------------------
// Module-local layout. The faint lattice is only ever a way for the Defuser to
// point at a star; it is never a way to identify the constellation, which is
// why it is drawn so far down.
// ---------------------------------------------------------------------------
constexpr float cell_size = 72.0f;
constexpr float grid_x = 40.0f;
constexpr float grid_y = 40.0f;
constexpr float chart_cx = grid_x + grid_n * cell_size * 0.5f;
constexpr float chart_cy = grid_y + grid_n * cell_size * 0.5f;

// One catalogue unit in pixels. Fixed rather than randomised, so the distances
// the manual's rules compare stay exactly as they were checked.
constexpr float unit_px = 420.0f;

constexpr float hit_radius = 26.0f;
// A field star has nothing within this far; a catalogue star always does.
constexpr float lonely_px = 160.0f;

// The solved lamp's corner is not sky: keep field stars out from under it.
constexpr Vector2 lamp_pos{module_tex_size - 54.0f, 48.0f};
constexpr float lamp_clear = 56.0f;

// ---------------------------------------------------------------------------
// The catalogue. Positions are centred on the constellation's own centroid and
// were searched offline (scratchpad stars2.py) against exactly the checks the
// manual promises: no two stars closer than 38px, every star with a neighbour
// inside about one grid square (bar the Hook's deliberate outlier), the shape
// fingerprint holding, and both target rules beating their runner-up by 30%.
//
// The (count, bright) signature is unique across the six, which is what makes
// "count the stars, then count the bright ones" a complete identification.
// ---------------------------------------------------------------------------
struct StarDef { float x, y; uint8_t tier; };

struct Catalogue {
    const char* name;
    int count;
    int target_odd;    // serial's last digit odd
    int target_even;
    StarDef stars[max_stars];
};

constexpr int catalogue_count = 6;
constexpr Catalogue catalogue[catalogue_count] = {
    {"The Ladle", 5, 1, 4, {
        {+0.2257f, +0.2117f, 0},
        {+0.1037f, +0.1114f, 1},
        {+0.0253f, -0.0235f, 1},
        {-0.1152f, -0.1047f, 1},
        {-0.2394f, -0.1948f, 2},
    }},
    {"The Anvil", 6, 5, 3, {
        {-0.0464f, +0.0639f, 0},
        {+0.0132f, -0.0264f, 0},
        {+0.1985f, +0.0045f, 1},
        {-0.1183f, -0.0803f, 2},
        {-0.1547f, +0.1845f, 1},
        {+0.1076f, -0.1462f, 2},
    }},
    {"The Hook", 6, 3, 1, {
        {-0.3513f, +0.2210f, 0},
        {-0.0153f, +0.0444f, 1},
        {+0.1425f, +0.1199f, 1},
        {+0.2120f, -0.1397f, 1},
        {-0.0394f, -0.1628f, 2},
        {+0.0515f, -0.0828f, 2},
    }},
    {"The Kite", 7, 1, 6, {
        {-0.0958f, -0.0712f, 0},
        {+0.0067f, +0.0213f, 0},
        {+0.1092f, +0.1137f, 0},
        {+0.0093f, +0.2019f, 1},
        {-0.1661f, +0.0369f, 1},
        {+0.0119f, -0.1131f, 2},
        {+0.1248f, -0.1894f, 2},
    }},
    {"The Twins", 8, 2, 3, {
        {-0.1955f, +0.0215f, 0},
        {+0.2600f, -0.1117f, 0},
        {-0.2859f, +0.0906f, 2},
        {+0.3285f, -0.0065f, 2},
        {-0.1480f, +0.1653f, 1},
        {-0.0031f, +0.1195f, 1},
        {-0.0223f, -0.0792f, 1},
        {+0.0662f, -0.1995f, 1},
    }},
    {"The Crown", 8, 4, 7, {
        {-0.1370f, +0.1458f, 0},
        {-0.0361f, -0.1808f, 0},
        {+0.1962f, +0.0699f, 0},
        {+0.1267f, -0.0719f, 1},
        {+0.0190f, +0.0940f, 1},
        {-0.0677f, -0.0786f, 1},
        {+0.0915f, +0.2031f, 2},
        {-0.1927f, -0.1814f, 2},
    }},
};

float dist(Vector2 a, Vector2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// One ring of a star's halo, faded by the twinkle.
void draw_glow(Vector2 p, float r, Color c, float t, unsigned char alpha) {
    c.a = static_cast<unsigned char>(static_cast<float>(alpha) * t);
    DrawCircleV(p, r, c);
}

float star_radius(uint8_t tier) {
    switch (tier) {
        case tier_bright: return 14.0f;
        case tier_medium: return 9.0f;
        default:          return 5.0f;
    }
}

} // namespace

void StarChartPuzzle::init(const BombAttributes& attrs, std::mt19937& rng) {
    std::uniform_int_distribution<int> pick(0, catalogue_count - 1);
    const Catalogue& c = catalogue[pick(rng)];

    const float angle =
        std::uniform_real_distribution<float>(0.0f, 2.0f * PI)(rng);
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);

    stars_.clear();
    stars_.reserve(static_cast<size_t>(c.count) + 3);
    for (int i = 0; i < c.count; ++i) {
        const float x = c.stars[i].x * unit_px;
        const float y = c.stars[i].y * unit_px;
        Star s;
        s.pos = Vector2{chart_cx + x * ca - y * sa, chart_cy + x * sa + y * ca};
        s.tier = c.stars[i].tier;
        s.phase =
            std::uniform_real_distribution<float>(0.0f, 2.0f * PI)(rng);
        stars_.push_back(s);
    }

    // The answer is the catalogue's, chosen by a fact only the Defuser can
    // read out. Rotation cannot change it: every rule compares distances, and
    // rotation preserves all of them.
    answer_ = attrs.serial_last_digit_odd() ? c.target_odd : c.target_even;

    // Field stars: dim, and always far enough from everything that the
    // manual's "ignore any dim star with nothing within about one grid square"
    // rule picks them out cleanly. Placed after the answer is fixed, so they
    // can never become it.
    const int wanted = std::uniform_int_distribution<int>(0, 3)(rng);
    for (int placed = 0; placed < wanted; ++placed) {
        bool found = false;
        for (int attempt = 0; attempt < 300 && !found; ++attempt) {
            const Vector2 p{
                std::uniform_real_distribution<float>(grid_x + 30.0f,
                    grid_x + grid_n * cell_size - 30.0f)(rng),
                std::uniform_real_distribution<float>(grid_y + 30.0f,
                    grid_y + grid_n * cell_size - 30.0f)(rng)};
            if (dist(p, lamp_pos) < lamp_clear) continue;
            bool clear = true;
            for (const Star& s : stars_) {
                if (dist(p, s.pos) < lonely_px) {
                    clear = false;
                    break;
                }
            }
            if (!clear) continue;
            Star s;
            s.pos = p;
            s.tier = tier_dim;
            s.phase =
                std::uniform_real_distribution<float>(0.0f, 2.0f * PI)(rng);
            stars_.push_back(s);
            found = true;
        }
        if (!found) break;   // no room left; a smaller sky is still a fair one
    }

    time_ = 0.0f;
}

int StarChartPuzzle::star_at_pixel(Vector2 p) const {
    int best = -1;
    float best_dist = hit_radius;
    for (size_t i = 0; i < stars_.size(); ++i) {
        const float away = dist(p, stars_[i].pos);
        if (away <= best_dist) {   // overlapping boxes: the nearer centre wins
            best_dist = away;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void StarChartPuzzle::update(const ModuleInput& in, const BombContext& ctx,
                             float dt) {
    (void)ctx;
    time_ += dt;
    if (is_solved() || !in.tapped) return;

    const int star = star_at_pixel(in.tap_pos);
    if (star < 0) return;

    if (star == answer_) {
        mark_solved();
    } else {
        raise_strike();   // the chart does not change
    }
}

void StarChartPuzzle::draw() {
    // The night field, drawn over the bay's usual casing grey.
    DrawRectangle(0, 0, module_tex_size, module_tex_size,
                  Color{12, 14, 22, 255});

    // The lattice: readable, never competing with the stars. Any brighter and
    // players start trying to identify the constellation by coordinates.
    const Color rule = Color{110, 130, 175, 34};
    const Color label = Color{92, 110, 145, 255};
    for (int i = 0; i <= grid_n; ++i) {
        const float o = static_cast<float>(i) * cell_size;
        const float far = grid_n * cell_size;
        DrawLineEx(Vector2{grid_x, grid_y + o},
                   Vector2{grid_x + far, grid_y + o}, 1.5f, rule);
        DrawLineEx(Vector2{grid_x + o, grid_y},
                   Vector2{grid_x + o, grid_y + far}, 1.5f, rule);
    }
    const int fs = 22;
    for (int i = 0; i < grid_n; ++i) {
        const char col_text[2] = {static_cast<char>('A' + i), '\0'};
        // Along the bottom: the top-right corner belongs to the solved lamp.
        DrawText(col_text,
                 static_cast<int>(grid_x + i * cell_size +
                                  (cell_size - MeasureText(col_text, fs)) *
                                      0.5f),
                 static_cast<int>(grid_y + grid_n * cell_size) + 6, fs, label);

        const char row_text[2] = {static_cast<char>('1' + i), '\0'};
        DrawText(row_text, static_cast<int>(grid_x) - 26,
                 static_cast<int>(grid_y + i * cell_size +
                                  (cell_size - fs) * 0.5f),
                 fs, label);
    }

    for (const Star& s : stars_) {
        // A slow twinkle, kept far too shallow to move a star between tiers.
        const float t = 0.92f + 0.08f * std::sin(time_ * 1.1f + s.phase);
        const float r = star_radius(s.tier);

        Color core;
        switch (s.tier) {
            case tier_bright: core = Color{246, 248, 253, 255}; break;
            case tier_medium: core = Color{248, 238, 214, 255}; break;
            default:          core = Color{168, 174, 190, 255}; break;
        }

        // Brightness is carried by size and glow, never colour alone, so the
        // tier is nameable without colour vision.
        if (s.tier == tier_bright) {
            draw_glow(s.pos, r * 2.10f, Color{190, 210, 255, 255}, t, 26);
            draw_glow(s.pos, r * 1.60f, Color{210, 226, 255, 255}, t, 52);
            draw_glow(s.pos, r * 1.25f, Color{232, 240, 255, 255}, t, 96);
        } else if (s.tier == tier_medium) {
            draw_glow(s.pos, r * 1.80f, Color{250, 226, 176, 255}, t, 34);
            draw_glow(s.pos, r * 1.35f, Color{252, 236, 200, 255}, t, 66);
        }
        core.a = static_cast<unsigned char>(255.0f * t);
        DrawCircleV(s.pos, r, core);
    }

    // Last, so the lattice never draws across it.
    DrawCircle(module_tex_size - 54, 48, 19,
               is_solved() ? Color{90, 220, 120, 255} : Color{60, 63, 70, 255});
}
