#pragma once

// Game owns the 3D scene: the camera, the bomb, the rotation/interaction input,
// and the round state (timer, strikes, win/lose). It converts unified
// mouse/touch pointer events into either a bomb rotation (drag) or a module tap
// (short press that lands on a module face).
//
// Modules are interacted with through a focus step: a tap on a module swings
// the bomb flat and zooms the camera onto that bay, and only the focused module
// accepts taps on its components.
//
// It also owns the front-end states drawn over that scene: the title menu, the
// Instructions dialog and the debug module picker. The bomb keeps rendering
// (and slowly spinning) behind them as a backdrop.
//
// Debug mode plays one chosen module on an otherwise empty bomb, with no time
// limit and no detonation, so a module can be exercised on its own.
//
// The title screen carries the run's seed: a serial number the players can type
// and a difficulty slider setting the module count. Together those seed every
// random choice the bomb makes, so the same pair replays the same bomb exactly.

#include <random>
#include <string>
#include <vector>

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
    enum class State {
        MENU, INSTRUCTIONS, DEBUG_MENU, PLAYING, DEFUSED, EXPLODED
    };

    void handle_pointer(float dt);
    void draw_hud() const;

    // Module focus: align the bomb so a bay faces the camera and zoom in on it.
    void begin_focus(int slot_index);
    void end_focus();
    void update_focus(float dt);
    // True once the focus move has settled, i.e. components accept input.
    bool focus_settled() const;

    // Front-end states.
    void update_menu();
    // Title-screen serial entry and difficulty slider.
    void update_serial_entry();
    void update_difficulty_slider();
    void randomize_serial();
    bool serial_is_valid() const;
    // Build the bomb from the current serial and difficulty.
    void rebuild_bomb();
    // True while the round-opening rotation tween is still running.
    bool settling() const { return intro_t_ < 1.0f; }
    // Keep the title screen's backdrop showing the bomb the current seed would
    // build, so pressing START changes nothing about how the bomb looks.
    void refresh_menu_bomb();
    void update_instructions();
    void update_end_screen();
    void activate_menu_button(int idx);
    void draw_menu() const;
    void draw_instructions() const;

    // Which device is steering the menus, and so what a screen draws lit: the
    // button under the pointer while the pointer is being used, the arrow-key
    // selection once the keys are. Called once a frame.
    void update_nav_device();
    // True when this button should be drawn lit.
    bool button_lit(int idx, int selected_idx, Rectangle rect) const;

    // ---- Debug mode: one module, no time limit, no detonation ----
    // A round is a debug round exactly when it names a module to exercise.
    bool debug_mode() const { return !debug_module_.empty(); }
    void update_debug_menu();
    void draw_debug_menu() const;
    // Build a bomb carrying only this module and start playing it.
    void start_debug_round(const std::string& module_name);
    void leave_debug();
    // The debug round's own controls (new seed / back to the picker). Returns
    // true while the pointer belongs to them, so the bomb never sees the press.
    bool update_debug_controls();
    void draw_debug_hud() const;
    // The single module a debug round is running, or null.
    const Puzzle* debug_puzzle() const;

    // Fresh bomb + reset round state, then enter State::PLAYING.
    void start_round();

    // True on the frame a press is released without having become a drag;
    // out_pos is where the press started.
    bool consume_tap(Vector2& out_pos);

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

    // Round-opening tween: the bomb settles from wherever the title screen left
    // it spinning to the pose a round starts in, rather than snapping there.
    float intro_t_ = 1.0f;   // 1 = settled
    float intro_from_yaw_ = 0.0f;
    float intro_from_pitch_ = 0.0f;
    float intro_to_yaw_ = 0.0f;

    // Module focus. focused_slot_ stays set while the move animates back out,
    // so free-look rotation stays locked until the camera is home again.
    int focused_slot_ = -1;
    bool focusing_ = false;
    float focus_t_ = 0.0f;      // 0 = free look, 1 = fully focused
    float free_yaw_ = 0.5f;     // rotation to return to when unfocusing
    float free_pitch_ = -0.15f;
    float focus_yaw_ = 0.0f;
    float focus_pitch_ = 0.0f;
    Vector3 focus_cam_pos_{};
    Vector3 focus_cam_target_{};

    // Round state.
    State state_ = State::MENU;
    static constexpr float round_seconds = 300.0f;
    static constexpr int max_strikes = 3;
    float time_left_ = round_seconds;
    int strikes_ = 0;

    // Menu / end-screen selection.
    int menu_selected_idx_ = 0;
    int end_selected_idx_ = 0;
    // Set while the pointer is the thing steering the menus, so its highlight
    // follows the cursor (and leaves with it) instead of the arrow keys'.
    bool pointer_nav_ = false;

    // Debug mode. debug_module_ names the module being exercised and is empty
    // for a normal round; it is also part of what the bomb was built from.
    std::string debug_module_;
    std::vector<std::string> debug_modules_;   // every template, for the picker
    std::vector<bool> debug_needy_;            // which of those are needy
    int debug_selected_idx_ = 0;
    bool debug_ui_press_ = false;   // a press that started on a debug button
    int debug_press_idx_ = -1;

    // The run's seed. The serial the players type, together with the module
    // count, seeds everything about the bomb, so the same pair always replays
    // the same run.
    std::string serial_;
    bool serial_focused_ = false;
    int difficulty_ = 3;          // number of modules to spawn
    bool dragging_slider_ = false;

    // Engine used to roll a fresh serial; separate from the bomb's seeded one.
    std::mt19937 serial_rng_;

    // What the bomb currently on screen was built from, and whether it is still
    // untouched. A bomb that has been played has solved modules and strikes on
    // it, so it cannot go back to being the menu backdrop.
    std::string built_serial_;
    int built_difficulty_ = -1;
    std::string built_debug_module_;
    bool bomb_pristine_ = false;

    bool paused_ = false;
};
