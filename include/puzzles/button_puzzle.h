#pragma once

// The Button module: a single big coloured button with a word on it. The seven
// rules in the manual decide whether it must be tapped or held; a held button
// shows a coloured strip, and the release is only correct while the countdown
// shows that strip's digit.
//
// This is the module that needs ModuleInput's press/hold/release trio. It is
// still a single pointer on a single button — nothing is ever held at once.

#include "puzzle.h"

class ButtonPuzzle : public Puzzle {
public:
    enum class ButtonColor { BTN_RED, BTN_BLUE, BTN_YELLOW, BTN_WHITE, BTN_BLACK };
    enum class ButtonLabel { ABORT, DETONATE, HOLD, PRESS };
    enum class StripColor { STRIP_BLUE, STRIP_WHITE, STRIP_YELLOW, STRIP_OTHER };

    const char* name() const override { return "The Button"; }
    void init(const BombAttributes& attrs, std::mt19937& rng) override;
    void update(const ModuleInput& in, const BombContext& ctx,
                float dt) override;
    void draw() override;
    // One big moulded button and its housing.
    SurfaceMaterial material() const override {
        return materials::glossy_plastic;
    }

    // The countdown digit a given strip demands on release.
    static int strip_release_digit(StripColor strip);

private:
    // Applies the manual's seven rules; true means "hold", false means
    // "press and immediately release".
    bool solve_should_hold(const BombAttributes& attrs) const;

    bool button_at_pixel(Vector2 p) const;
    // True when the countdown, as the defuser reads it, shows `digit`.
    static bool timer_shows_digit(float time_left, int digit);

    ButtonColor color_ = ButtonColor::BTN_RED;
    ButtonLabel label_ = ButtonLabel::PRESS;
    StripColor strip_ = StripColor::STRIP_BLUE;

    bool should_hold_ = false;
    bool holding_ = false;      // pointer is down on the button right now
    float hold_time_ = 0.0f;    // seconds the button has been down
    bool strip_lit_ = false;    // the strip only appears once a hold registers
};
