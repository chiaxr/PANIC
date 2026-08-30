#pragma once

// Colour Match: the Expert describes a colour the manual refuses to name.
//
// The manual holds a grid of colour patches with no names beside them, so the
// only way the Expert can hand one over is to describe it -- "a burnt orange,
// browner than it is red". The Defuser dials that description in on a colour
// wheel and submits.
//
// The wheel is built in Oklab. Angle is hue; radius runs from a neutral grey at
// the centre out to that hue's strongest colour -- the sRGB gamut cusp's
// lightness, with chroma capped so the rim reads rich rather than fluorescent.
// Lightness therefore varies with hue, which is what a saturated palette needs:
// a vivid yellow is light and a vivid blue is dark. It is also what keeps the
// colours apart for a colour-blind player, who separates them largely by
// lightness rather than by hue.
//
// A submitted pick is scored by which palette entry it lands nearest to on the
// wheel -- a Voronoi cell, not an exact match. The palette search is what
// guarantees every entry a cell wide enough to aim at; see
// scripts/color_palette.py, which generates the palette and the cusp table and
// checks both.

#include <array>
#include <random>

#include "raylib.h"

#include "puzzle.h"

class ColorMatchPuzzle : public Puzzle {
public:
    static constexpr int palette_count = 16;   // colours, and manual rows
    static constexpr int column_count = 3;     // battery buckets
    static constexpr int stage_count = 2;      // keys per round

    // The wheel is drawn as a fan of flat cells, fine enough that the banding
    // sits below what the eye picks out at this chroma.
    static constexpr int disc_sectors = 120;
    static constexpr int disc_rings = 18;
    static constexpr int disc_cells = disc_sectors * disc_rings;

    const char* name() const override { return "Colour Match"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;

    // A printed colour chart. Paper is the flattest, least specular surface in
    // the palette, which is what a colour-critical face wants: a highlight
    // sweeping across the wheel would recolour whatever it touched.
    SurfaceMaterial material() const override { return materials::paper; }

private:
    // Module pixels -> unit-disc coordinates on the wheel, y already flipped
    // and the length clamped to the rim. A pick at the rim is full strength,
    // and several palette colours live exactly there, so this must never
    // reject a point for landing a rounding error outside the circle.
    void disc_coords(Vector2 p, float& x, float& y) const;

    // As above, but false when p is not on the wheel at all -- what decides
    // whether a press starts a drag.
    bool disc_at_pixel(Vector2 p, float& x, float& y) const;

    // Which palette entry a point on the wheel belongs to.
    int nearest_swatch(float x, float y) const;

    // Both keys, drawn fresh; called on init and after a strike.
    void deal_keys();

    int column_ = 0;
    int keys_[stage_count] = {0, 0};
    int stage_ = 0;
    bool has_pick_ = false;
    bool dragging_ = false;
    float pick_x_ = 0.0f;
    float pick_y_ = 0.0f;
    std::array<Color, disc_cells> disc_{};
    std::mt19937 key_rng_;
};
