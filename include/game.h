#pragma once

// Game owns the 3D scene: the camera, the bomb, the rotation/interaction input,
// and the round state (timer, strikes, win/lose). It converts unified
// mouse/touch pointer events into either a bomb rotation (drag) or a module tap
// (short press that lands on a module face).

#include <random>

#include "raylib.h"

#include "bomb.h"

class Game {
public:
    void setup(); // after window/GL init
    void unload();
    void update(float dt);
    void draw();
    void set_paused(bool paused) { paused_ = paused; }

private:
    enum class State { PLAYING, DEFUSED, EXPLODED };

    void handle_pointer(float dt);
    void draw_hud() const;

    // Ray-cast a screen point into bomb-local space; returns the hit slot index
    // or -1, filling pixel coords within the module when hit.
    int pick_module(Vector2 screen_pos, Vector2& out_module_pixel) const;

    // Current bomb rotation as a matrix (its inverse is used for ray-casting).
    Matrix bomb_transform() const;

    Bomb bomb_;
    Camera3D camera_{};
    std::mt19937 rng_;

    // Rotation (radians).
    float yaw_ = 0.5f;
    float pitch_ = -0.15f;

    // Pointer tracking (unified mouse/touch).
    bool pointer_down_ = false;
    bool dragging_ = false;
    Vector2 press_pos_{};
    Vector2 last_pos_{};
    int press_slot_ = -1;
    Vector2 press_pixel_{};

    // Round state.
    State state_ = State::PLAYING;
    float time_left_ = 300.0f; // seconds
    int strikes_ = 0;
    static constexpr int max_strikes = 3;

    bool paused_ = false;
};
