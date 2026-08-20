#include "game.h"

#include <cmath>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr float rot_speed = 0.008f;     // radians per pixel dragged
constexpr float drag_threshold = 8.0f;  // px of motion before a press is a drag
constexpr float pitch_limit = 1.45f;

// Unified pointer over mouse and (single-)touch, so the same path serves
// desktop and web/mobile.
bool pointer_down() {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0;
}

Vector2 pointer_pos() {
    if (GetTouchPointCount() > 0) return GetTouchPosition(0);
    return GetMousePosition();
}

}   // namespace

void Game::setup() {
    camera_.position = Vector3{0.0f, 0.0f, 8.0f};
    camera_.target = Vector3{0.0f, 0.0f, 0.0f};
    camera_.up = Vector3{0.0f, 1.0f, 0.0f};
    camera_.fovy = 45.0f;
    camera_.projection = CAMERA_PERSPECTIVE;

    rng_.seed(std::random_device{}());
    bomb_.setup(rng_);
}

void Game::unload() { bomb_.unload(); }

Matrix Game::bomb_transform() const {
    return MatrixMultiply(MatrixRotateX(pitch_), MatrixRotateY(yaw_));
}

int Game::pick_module(Vector2 screen_pos, Vector2& out_module_pixel) const {
    const Ray ray = GetScreenToWorldRay(screen_pos, camera_);

    // Move the ray into bomb-local space (same transform used for drawing).
    const Matrix inv = MatrixInvert(bomb_transform());
    const Vector3 lo = Vector3Transform(ray.position, inv);
    const Vector3 ld = Vector3Normalize(Vector3Subtract(
            Vector3Transform(Vector3Add(ray.position, ray.direction), inv), lo));

    int best = -1;
    float best_t = 1e30f;

    const auto& slots = bomb_.slots();
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].puzzle) continue;   // only interactive modules are pickable
        const FaceQuad& q = slots[i].quad;

        const float denom = Vector3DotProduct(ld, q.normal);
        if (std::fabs(denom) < 1e-6f) continue;
        const float t =
                Vector3DotProduct(Vector3Subtract(q.center, lo), q.normal) / denom;
        if (t <= 0.0f || t >= best_t) continue;

        const Vector3 hit = Vector3Add(lo, Vector3Scale(ld, t));
        const Vector3 d = Vector3Subtract(hit, q.center);
        const float u = Vector3DotProduct(d, q.right);
        const float v = Vector3DotProduct(d, q.up);
        if (std::fabs(u) > q.half_w || std::fabs(v) > q.half_h) continue;

        best_t = t;
        best = static_cast<int>(i);
        out_module_pixel = Vector2{(u / q.half_w * 0.5f + 0.5f) * module_tex_size,
                                   (0.5f - v / q.half_h * 0.5f) * module_tex_size};
    }
    return best;
}

void Game::handle_pointer(float dt) {
    (void)dt;
    const bool down = pointer_down();
    const Vector2 pos = pointer_pos();

    if (down && !pointer_down_) {
        // Press.
        pointer_down_ = true;
        dragging_ = false;
        press_pos_ = pos;
        last_pos_ = pos;
        press_slot_ = pick_module(pos, press_pixel_);
    } else if (down && pointer_down_) {
        // Held: distinguish drag (rotate) from tap.
        const Vector2 delta = Vector2Subtract(pos, last_pos_);
        if (Vector2Distance(pos, press_pos_) > drag_threshold) dragging_ = true;
        if (dragging_) {
            yaw_ += delta.x * rot_speed;
            // Vertical drag is inverted: dragging down tips the top of the bomb away.
            pitch_ = Clamp(pitch_ + delta.y * rot_speed, -pitch_limit, pitch_limit);
        }
        last_pos_ = pos;
    } else if (!down && pointer_down_) {
        // Release: a short press on a module counts as a tap.
        pointer_down_ = false;
        if (!dragging_ && press_slot_ >= 0) {
            ModuleInput in;
            in.tapped = true;
            in.tap_pos = press_pixel_;
            bomb_.send_input(press_slot_, in);
        }
        press_slot_ = -1;
        dragging_ = false;
    }
}

void Game::update(float dt) {
    if (paused_) return;

    if (state_ == State::PLAYING) {
        handle_pointer(dt);
        bomb_.update(dt);
        strikes_ += bomb_.take_strike_events();

        time_left_ -= dt;

        if (bomb_.all_solved()) {
            state_ = State::DEFUSED;
        } else if (strikes_ >= max_strikes || time_left_ <= 0.0f) {
            state_ = State::EXPLODED;
            if (time_left_ < 0.0f) time_left_ = 0.0f;
        }
    } else {
        // Restart with a fresh bomb.
        if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER) ||
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0) {
            bomb_.unload();
            bomb_.setup(rng_);
            state_ = State::PLAYING;
            time_left_ = 300.0f;
            strikes_ = 0;
            yaw_ = 0.5f;
            pitch_ = -0.15f;
            pointer_down_ = false;
            dragging_ = false;
            press_slot_ = -1;
        }
    }
}

void Game::draw_hud() const {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    // Countdown timer, top centre.
    const int total =
        static_cast<int>(std::ceil(time_left_ < 0 ? 0 : time_left_));
    const char* clock = TextFormat("%02d:%02d", total / 60, total % 60);
    const int clock_size = 48;
    const Color clock_color =
        (state_ == State::EXPLODED) ? Color{230, 70, 70, 255} : RAYWHITE;
    DrawText(clock, sw / 2 - MeasureText(clock, clock_size) / 2, 16, clock_size,
             clock_color);

    // Strikes, top-left.
    DrawText("STRIKES", 20, 20, 22, Color{150, 158, 170, 255});
    for (int i = 0; i < max_strikes; ++i) {
        const Color c =
            (i < strikes_) ? Color{230, 70, 70, 255} : Color{60, 63, 70, 255};
        DrawCircle(30 + i * 34, 66, 12, c);
    }

    // Module progress, top-right.
    const char* prog = TextFormat("MODULES    %d / %d", bomb_.solved_module_count(),
                                  bomb_.puzzle_module_count());
    DrawText(prog, sw - 20 - MeasureText(prog, 22), 24, 22,
             Color{150, 158, 170, 255});

    // Controls hint, bottom.
    const char* hint = "Drag to rotate the bomb   -   tap a module to interact";
    DrawText(hint, sw / 2 - MeasureText(hint, 20) / 2, sh - 34, 20,
             Color{110, 116, 128, 255});

    // End-of-round banner.
    if (state_ != State::PLAYING) {
        DrawRectangle(0, sh / 2 - 80, sw, 160, Color{0, 0, 0, 170});
        const char* msg = (state_ == State::DEFUSED) ? "BOMB DEFUSED" : "BOOM";
        const Color col = (state_ == State::DEFUSED) ? Color{90, 220, 120, 255} : Color{230, 70, 70, 255};
        DrawText(msg, sw / 2 - MeasureText(msg, 72) / 2, sh / 2 - 60, 72, col);
        const char* again = "Press R (or tap) for a new bomb";
        DrawText(again, sw / 2 - MeasureText(again, 24) / 2, sh / 2 + 24, 24,
            Color{200, 202, 208, 255});
    }
}

void Game::draw() {
    // Render each module's 2D content into its texture first (before drawing).
    bomb_.render_module_textures();

    BeginDrawing();
    ClearBackground(Color{18, 19, 22, 255});

    BeginMode3D(camera_);
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(bomb_transform()));
    bomb_.draw_faces_3d();
    rlPopMatrix();
    EndMode3D();

    draw_hud();
    EndDrawing();
}
