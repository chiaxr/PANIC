#include "game.h"

#include <algorithm>
#include <cmath>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr float rot_speed = 0.008f;     // radians per pixel dragged
constexpr float drag_threshold = 8.0f;  // px of motion before a press is a drag
constexpr float pitch_limit = 1.45f;
constexpr float menu_spin_speed = 0.25f;  // radians per second on the menu

// Free-look camera, and the module-focus move that swings away from it.
constexpr Vector3 camera_free_pos{0.0f, 0.0f, 8.0f};
constexpr Vector3 camera_free_target{0.0f, 0.0f, 0.0f};
constexpr float focus_time = 0.32f;         // seconds for the focus move
constexpr float focus_zoom_margin = 1.55f;  // >1 leaves air around the module

// Shortest signed way round to an angle, so focusing never takes the long way.
float wrap_pi(float radians) {
    while (radians > PI) radians -= 2.0f * PI;
    while (radians < -PI) radians += 2.0f * PI;
    return radians;
}

float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

// The Expert's manual. raylib's OpenURL is implemented per platform (desktop
// opens the system browser, web opens a new tab), so the call site stays
// platform-agnostic; the URL is absolute because a relative path means nothing
// to a native build.
constexpr const char* manual_url = "https://chiaxr.github.io/PANIC/manual.html";

// Shared palette.
constexpr Color col_text{232, 234, 240, 255};
constexpr Color col_text_dim{150, 158, 170, 255};
constexpr Color col_hint{110, 116, 128, 255};
constexpr Color col_accent{230, 70, 70, 255};
constexpr Color col_amber{240, 200, 90, 255};
constexpr Color col_good{90, 220, 120, 255};

// Menu buttons: same geometry as QWAS so the three entries sit where the
// author's other game puts them.
constexpr int menu_btn_w = 400;
constexpr int menu_btn_h = 52;
constexpr int menu_btn_gap = 66;
constexpr int menu_count = 3;

// End-of-round buttons: a centred pair.
constexpr int end_btn_w = 280;
constexpr int end_btn_h = 52;
constexpr int end_btn_gap = 24;
constexpr int end_count = 2;

// Modal dialog chrome (Settings, Instructions): a BACK button and a footer hint
// occupy a fixed-height block at the bottom of every panel, so both dialogs
// look and behave identically.
constexpr int dialog_back_btn_w = 220;
constexpr int dialog_back_btn_h = 52;
constexpr int dialog_back_btn_margin = 20;
constexpr int dialog_footer_gap = 24;
constexpr int dialog_footer_block_h =
    dialog_back_btn_margin + dialog_back_btn_h + dialog_footer_gap;
constexpr int dialog_title_size = 28;
constexpr int dialog_title_top_pad = 14;

// Instructions body metrics.
constexpr int instr_body_x_pad = 40;
constexpr int instr_body_top = 66;
constexpr int instr_header_size = 20;
constexpr int instr_line_size = 17;
constexpr int instr_header_step = 28;
constexpr int instr_line_step = 21;
constexpr int instr_section_gap = 14;
constexpr int instr_link_dy = 456;  // link row top, relative to panel top
constexpr int instr_link_h = 46;

struct DialogLayout {
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
};

// Unified pointer over mouse and (single-)touch, so the same path serves
// desktop and web/mobile.
bool pointer_down() {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0;
}

Vector2 pointer_pos() {
    if (GetTouchPointCount() > 0) return GetTouchPosition(0);
    return GetMousePosition();
}

Rectangle menu_button_rect(int idx, int sw, int sh) {
    const int x = (sw - menu_btn_w) / 2;
    const int y = static_cast<int>(sh * 0.43f) + idx * menu_btn_gap;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(menu_btn_w),
                     static_cast<float>(menu_btn_h)};
}

Rectangle end_button_rect(int idx, int sw, int top_y) {
    const int span = end_btn_w * end_count + end_btn_gap * (end_count - 1);
    const int x = (sw - span) / 2 + idx * (end_btn_w + end_btn_gap);
    return Rectangle{static_cast<float>(x), static_cast<float>(top_y),
                     static_cast<float>(end_btn_w),
                     static_cast<float>(end_btn_h)};
}

void draw_button(const char* label, Rectangle rect, bool selected) {
    const Color bg =
        selected ? Color{58, 30, 32, 235} : Color{24, 25, 30, 215};
    const Color border =
        selected ? col_accent : Color{60, 63, 72, 220};
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 2.0f, border);

    const int fs = 26;
    const int tw = MeasureText(label, fs);
    DrawText(label, static_cast<int>(rect.x + (rect.width - tw) * 0.5f),
             static_cast<int>(rect.y + (rect.height - fs) * 0.5f), fs,
             selected ? RAYWHITE : Color{180, 186, 196, 255});
}

void draw_centered_text(const char* text, int y, int font_size, Color color) {
    DrawText(text, (GetScreenWidth() - MeasureText(text, font_size)) / 2, y,
             font_size, color);
}

DialogLayout centered_layout(int bw, int bh, int sw, int sh) {
    return DialogLayout{(sw - bw) / 2, (sh - bh) / 2, bw, bh};
}

DialogLayout settings_layout(int sw, int sh) {
    return centered_layout(520, 260, sw, sh);
}

DialogLayout instructions_layout(int sw, int sh) {
    return centered_layout(760, 630, sw, sh);
}

Rectangle dialog_back_button_rect(const DialogLayout& l) {
    const int x = l.bx + (l.bw - dialog_back_btn_w) / 2;
    const int y = l.by + l.bh - dialog_back_btn_margin - dialog_back_btn_h;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(dialog_back_btn_w),
                     static_cast<float>(dialog_back_btn_h)};
}

Rectangle manual_link_rect(const DialogLayout& l) {
    return Rectangle{static_cast<float>(l.bx + instr_body_x_pad),
                     static_cast<float>(l.by + instr_link_dy),
                     static_cast<float>(l.bw - instr_body_x_pad * 2),
                     static_cast<float>(instr_link_h)};
}

// Dimmed backdrop + panel + title, shared by every modal dialog.
void draw_dialog_panel(const DialogLayout& l, const char* title) {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  Color{0, 0, 0, 180});
    DrawRectangle(l.bx, l.by, l.bw, l.bh, Color{12, 13, 16, 232});
    DrawRectangleLinesEx(Rectangle{static_cast<float>(l.bx),
                                   static_cast<float>(l.by),
                                   static_cast<float>(l.bw),
                                   static_cast<float>(l.bh)},
                         2.0f, Color{70, 72, 80, 255});
    draw_centered_text(title, l.by + dialog_title_top_pad, dialog_title_size,
                       RAYWHITE);
}

// Footer hint + BACK button, inside the block reserved by
// dialog_footer_block_h.
void draw_dialog_footer(const DialogLayout& l, const char* hint,
                        bool back_selected) {
    draw_centered_text(hint, l.by + l.bh - dialog_footer_block_h, 16,
                       col_hint);
    draw_button("BACK", dialog_back_button_rect(l), back_selected);
}

bool rect_hovered(Rectangle rect) {
    return CheckCollisionPointRec(GetMousePosition(), rect);
}

}   // namespace

void Game::setup() {
    camera_.position = camera_free_pos;
    camera_.target = camera_free_target;
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
        // Held: distinguish drag (rotate) from tap. Free-look only — a focused
        // module stays square to the camera.
        const Vector2 delta = Vector2Subtract(pos, last_pos_);
        if (Vector2Distance(pos, press_pos_) > drag_threshold) dragging_ = true;
        if (dragging_ && focused_slot_ < 0) {
            yaw_ += delta.x * rot_speed;
            // Vertical drag is inverted: dragging down tips the top of the bomb away.
            pitch_ = Clamp(pitch_ + delta.y * rot_speed, -pitch_limit, pitch_limit);
            free_yaw_ = yaw_;
            free_pitch_ = pitch_;
        }
        last_pos_ = pos;
    } else if (!down && pointer_down_) {
        // Release. A tap first focuses a module; only once the camera has
        // settled on it does a tap reach the module's own components.
        pointer_down_ = false;
        if (!dragging_) {
            if (focus_settled()) {
                if (press_slot_ == focused_slot_) {
                    ModuleInput in;
                    in.tapped = true;
                    in.tap_pos = press_pixel_;
                    bomb_.send_input(press_slot_, in);
                } else if (press_slot_ >= 0) {
                    begin_focus(press_slot_);   // straight to a neighbouring bay
                } else {
                    end_focus();                // tapped off the module
                }
            } else if (focused_slot_ < 0 && press_slot_ >= 0) {
                begin_focus(press_slot_);
            }
            // Mid-move taps are ignored.
        }
        press_slot_ = -1;
        dragging_ = false;
    }
}

bool Game::focus_settled() const {
    return focused_slot_ >= 0 && focusing_ && focus_t_ >= 1.0f;
}

void Game::begin_focus(int slot_index) {
    const auto& slots = bomb_.slots();
    if (slot_index < 0 || static_cast<size_t>(slot_index) >= slots.size()) return;
    const FaceQuad& q = slots[slot_index].quad;

    // Only remember the free-look rotation on the way in, so hopping between
    // bays still returns to where the player was looking.
    if (focused_slot_ < 0) {
        free_yaw_ = yaw_;
        free_pitch_ = pitch_;
    }

    focused_slot_ = slot_index;
    focusing_ = true;

    // Turn the bay's outward normal towards the camera: front bays need no
    // yaw, back bays a half turn, and the pitch always flattens out.
    focus_pitch_ = 0.0f;
    const float face_yaw = (q.normal.z >= 0.0f) ? 0.0f : PI;
    focus_yaw_ = yaw_ + wrap_pi(face_yaw - yaw_);

    // Where that leaves the module in world space, and how far back the camera
    // has to sit for the face to fill most of the viewport height.
    const Matrix m =
        MatrixMultiply(MatrixRotateX(focus_pitch_), MatrixRotateY(focus_yaw_));
    focus_cam_target_ = Vector3Transform(q.center, m);
    const float dist = q.half_h / std::tan(camera_.fovy * 0.5f * DEG2RAD) *
                       focus_zoom_margin;
    focus_cam_pos_ =
        Vector3Add(focus_cam_target_, Vector3{0.0f, 0.0f, dist});

    // Re-aim the interpolation from wherever the camera currently is.
    if (focus_t_ <= 0.0f) focus_t_ = 0.0f;
}

void Game::end_focus() {
    // Keep focused_slot_ set until the move home finishes; update_focus clears
    // it, which is also what keeps free-look locked until then.
    focusing_ = false;
}

void Game::update_focus(float dt) {
    const float target = focusing_ ? 1.0f : 0.0f;
    if (focus_t_ < target) {
        focus_t_ = std::min(target, focus_t_ + dt / focus_time);
    } else if (focus_t_ > target) {
        focus_t_ = std::max(target, focus_t_ - dt / focus_time);
    }

    if (focused_slot_ < 0) return;

    const float s = smoothstep01(focus_t_);
    yaw_ = Lerp(free_yaw_, focus_yaw_, s);
    pitch_ = Lerp(free_pitch_, focus_pitch_, s);
    camera_.position = Vector3Lerp(camera_free_pos, focus_cam_pos_, s);
    camera_.target = Vector3Lerp(camera_free_target, focus_cam_target_, s);

    if (!focusing_ && focus_t_ <= 0.0f) focused_slot_ = -1;
}

bool Game::consume_tap(Vector2& out_pos) {
    const bool down = pointer_down();
    const Vector2 pos = pointer_pos();
    bool tapped = false;

    if (down && !pointer_down_) {
        press_pos_ = pos;
        last_pos_ = pos;
        dragging_ = false;
    } else if (down && pointer_down_) {
        if (Vector2Distance(pos, press_pos_) > drag_threshold) dragging_ = true;
        last_pos_ = pos;
    } else if (!down && pointer_down_) {
        tapped = !dragging_;
        out_pos = press_pos_;
        dragging_ = false;
    }

    pointer_down_ = down;
    return tapped;
}

void Game::start_round() {
    bomb_.unload();
    bomb_.setup(rng_);

    state_ = State::PLAYING;
    time_left_ = round_seconds;
    strikes_ = 0;
    yaw_ = 0.5f;
    pitch_ = -0.15f;
    end_selected_idx_ = 0;

    focused_slot_ = -1;
    focusing_ = false;
    focus_t_ = 0.0f;
    free_yaw_ = yaw_;
    free_pitch_ = pitch_;
    camera_.position = camera_free_pos;
    camera_.target = camera_free_target;

    pointer_down_ = false;
    dragging_ = false;
    press_slot_ = -1;
}

void Game::activate_menu_button(int idx) {
    switch (idx) {
        case 0: start_round(); break;
        case 1: state_ = State::SETTINGS; break;
        case 2: state_ = State::INSTRUCTIONS; break;
        default: break;
    }
}

void Game::update_menu() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    if (IsKeyPressed(KEY_UP)) {
        menu_selected_idx_ = (menu_selected_idx_ - 1 + menu_count) % menu_count;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        menu_selected_idx_ = (menu_selected_idx_ + 1) % menu_count;
    }

    // Mouse hover / touch contact moves the selection.
    for (int i = 0; i < menu_count; ++i) {
        const Rectangle r = menu_button_rect(i, sw, sh);
        if (rect_hovered(r)) menu_selected_idx_ = i;
        for (int t = 0; t < GetTouchPointCount(); ++t) {
            if (CheckCollisionPointRec(GetTouchPosition(t), r)) {
                menu_selected_idx_ = i;
            }
        }
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        activate_menu_button(menu_selected_idx_);
        return;
    }

    Vector2 tap{};
    if (consume_tap(tap)) {
        for (int i = 0; i < menu_count; ++i) {
            if (CheckCollisionPointRec(tap, menu_button_rect(i, sw, sh))) {
                activate_menu_button(i);
                return;
            }
        }
    }
}

void Game::update_settings() {
    const DialogLayout l = settings_layout(GetScreenWidth(), GetScreenHeight());
    const Rectangle back = dialog_back_button_rect(l);

    Vector2 tap{};
    const bool tapped_back = consume_tap(tap) &&
                             CheckCollisionPointRec(tap, back);
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ENTER) || tapped_back) {
        menu_selected_idx_ = 1;
        state_ = State::MENU;
    }
}

void Game::update_instructions() {
    const DialogLayout l =
        instructions_layout(GetScreenWidth(), GetScreenHeight());
    const Rectangle back = dialog_back_button_rect(l);
    const Rectangle link = manual_link_rect(l);

    SetMouseCursor(rect_hovered(link) ? MOUSE_CURSOR_POINTING_HAND
                                      : MOUSE_CURSOR_DEFAULT);

    Vector2 tap{};
    const bool tapped = consume_tap(tap);

    if (tapped && CheckCollisionPointRec(tap, link)) {
        OpenURL(manual_url);
        return;
    }

    const bool tapped_back = tapped && CheckCollisionPointRec(tap, back);
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ENTER) || tapped_back) {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        menu_selected_idx_ = 2;
        state_ = State::MENU;
    }
}

void Game::update_end_screen() {
    const int sw = GetScreenWidth();
    const int top_y = GetScreenHeight() / 2 + 10;

    if (IsKeyPressed(KEY_LEFT)) {
        end_selected_idx_ = (end_selected_idx_ - 1 + end_count) % end_count;
    }
    if (IsKeyPressed(KEY_RIGHT)) {
        end_selected_idx_ = (end_selected_idx_ + 1) % end_count;
    }
    for (int i = 0; i < end_count; ++i) {
        if (rect_hovered(end_button_rect(i, sw, top_y))) end_selected_idx_ = i;
    }

    if (IsKeyPressed(KEY_R)) {
        start_round();
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (end_selected_idx_ == 0) {
            start_round();
        } else {
            menu_selected_idx_ = 0;
            state_ = State::MENU;
        }
        return;
    }

    Vector2 tap{};
    if (!consume_tap(tap)) return;
    for (int i = 0; i < end_count; ++i) {
        if (!CheckCollisionPointRec(tap, end_button_rect(i, sw, top_y))) continue;
        if (i == 0) {
            start_round();
        } else {
            menu_selected_idx_ = 0;
            state_ = State::MENU;
        }
        return;
    }
}

void Game::update(float dt) {
    if (paused_) return;

    // Runs in every state so a focus move started mid-round still finishes
    // (and hands the camera back) after the round ends.
    update_focus(dt);

    switch (state_) {
        case State::MENU:
            yaw_ += menu_spin_speed * dt;
            update_menu();
            break;

        case State::SETTINGS:
            yaw_ += menu_spin_speed * dt;
            update_settings();
            break;

        case State::INSTRUCTIONS:
            yaw_ += menu_spin_speed * dt;
            update_instructions();
            break;

        case State::PLAYING: {
            if (focused_slot_ >= 0 && focusing_ &&
                    (IsKeyPressed(KEY_BACKSPACE) ||
                     IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
                end_focus();
            }

            handle_pointer(dt);
            bomb_.update(dt);
            strikes_ += bomb_.take_strike_events();

            time_left_ -= dt;

            const bool defused = bomb_.all_solved();
            if (defused || strikes_ >= max_strikes || time_left_ <= 0.0f) {
                state_ = defused ? State::DEFUSED : State::EXPLODED;
                if (time_left_ < 0.0f) time_left_ = 0.0f;
                end_selected_idx_ = 0;
                end_focus();   // hand the camera back for the end screen
                // Discard any press still in flight so the gesture that ended
                // the round cannot also press an end-screen button.
                dragging_ = true;
                press_slot_ = -1;
            }
            break;
        }

        case State::DEFUSED:
        case State::EXPLODED:
            update_end_screen();
            break;
    }
}

void Game::draw_menu() const {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    // Dim the bomb behind the menu without hiding it.
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 140});

    const char* title = "PANIC";
    const int title_size = 104;
    const int tx = (sw - MeasureText(title, title_size)) / 2;
    const int ty = static_cast<int>(sh * 0.12f);
    DrawText(title, tx + 4, ty + 4, title_size, Color{0, 0, 0, 160});
    DrawText(title, tx, ty, title_size, col_accent);

    draw_centered_text("Puzzles Always Need Immediate Communication",
                       ty + title_size + 12, 28, col_text_dim);

    const char* labels[menu_count] = {"START", "SETTINGS", "INSTRUCTIONS"};
    for (int i = 0; i < menu_count; ++i) {
        draw_button(labels[i], menu_button_rect(i, sw, sh),
                    i == menu_selected_idx_);
    }

    draw_centered_text("Arrow keys to navigate   -   Enter or click to select",
                       sh - 32, 16, col_hint);
}

void Game::draw_settings() const {
    const DialogLayout l = settings_layout(GetScreenWidth(), GetScreenHeight());
    draw_dialog_panel(l, "SETTINGS");

    draw_centered_text("Nothing to configure yet.", l.by + 92, 20,
                       col_text_dim);
    draw_centered_text("Options will land here as the game grows.",
                       l.by + 120, 17, col_hint);

    draw_dialog_footer(l, "Back, Enter or Backspace to return",
                       rect_hovered(dialog_back_button_rect(l)));
}

void Game::draw_instructions() const {
    const DialogLayout l =
        instructions_layout(GetScreenWidth(), GetScreenHeight());
    draw_dialog_panel(l, "HOW TO PLAY");

    const int lx = l.bx + instr_body_x_pad;
    int y = l.by + instr_body_top;

    auto header = [&](const char* text) {
        DrawText(text, lx, y, instr_header_size, col_amber);
        y += instr_header_step;
    };
    auto line = [&](const char* text) {
        DrawText(text, lx, y, instr_line_size, col_text);
        y += instr_line_step;
    };

    header("Objective");
    line("Disarm every module before the countdown reaches zero.");
    line("Three strikes and the bomb detonates.");
    y += instr_section_gap;

    header("Two players, one manual");
    line("Defuser  -  holds the bomb, has no instructions.");
    line("Expert   -  reads the manual, cannot see the bomb.");
    line("Talking to each other is the whole game.");
    y += instr_section_gap;

    header("Controls");
    line("Drag (mouse or one finger) to rotate the bomb.");
    line("Click or tap a module to zoom in on it, then click its");
    line("components. Tap away or press Backspace to step back.");
    y += instr_section_gap;

    header("Read the casing out loud");
    line("The rim prints the serial number, batteries and");
    line("indicators. Module rules depend on them.");

    // Manual link row.
    const Rectangle link = manual_link_rect(l);
    const bool hover = rect_hovered(link);
    DrawRectangleRec(link, hover ? Color{40, 24, 26, 235}
                                 : Color{24, 25, 30, 215});
    DrawRectangleLinesEx(link, 2.0f,
                         hover ? col_accent : Color{60, 63, 72, 220});

    const int link_size = 17;
    const char* label = "Expert, open the manual:";
    const int label_w = MeasureText(label, link_size);
    const int url_w = MeasureText(manual_url, link_size);
    const int gap = 10;
    const int text_x =
        static_cast<int>(link.x + (link.width - (label_w + gap + url_w)) * 0.5f);
    const int text_y =
        static_cast<int>(link.y + (link.height - link_size) * 0.5f);
    DrawText(label, text_x, text_y, link_size, col_text_dim);
    DrawText(manual_url, text_x + label_w + gap, text_y, link_size,
             hover ? RAYWHITE : col_good);
    DrawLine(text_x + label_w + gap, text_y + link_size + 2,
             text_x + label_w + gap + url_w, text_y + link_size + 2,
             hover ? RAYWHITE : col_good);

    draw_dialog_footer(l, "Click the link to open it in a browser",
                       rect_hovered(dialog_back_button_rect(l)));
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
        (state_ == State::EXPLODED) ? col_accent : RAYWHITE;
    DrawText(clock, sw / 2 - MeasureText(clock, clock_size) / 2, 16, clock_size,
             clock_color);

    // Strikes, top-left.
    DrawText("STRIKES", 20, 20, 22, col_text_dim);
    for (int i = 0; i < max_strikes; ++i) {
        const Color c = (i < strikes_) ? col_accent : Color{60, 63, 70, 255};
        DrawCircle(30 + i * 34, 66, 12, c);
    }

    // Module progress, top-right.
    const char* prog = TextFormat("MODULES    %d / %d", bomb_.solved_module_count(),
                                  bomb_.puzzle_module_count());
    DrawText(prog, sw - 20 - MeasureText(prog, 22), 24, 22, col_text_dim);

    // Controls hint, bottom. What a tap does depends on the focus state.
    const char* hint =
        (focused_slot_ >= 0)
            ? "Tap the module to work on it   -   tap away, right-click or "
              "Backspace to step back"
            : "Drag to rotate the bomb   -   tap a module to zoom in on it";
    DrawText(hint, sw / 2 - MeasureText(hint, 20) / 2, sh - 34, 20, col_hint);

    // End-of-round banner.
    if (state_ == State::DEFUSED || state_ == State::EXPLODED) {
        DrawRectangle(0, sh / 2 - 110, sw, 230, Color{0, 0, 0, 180});

        const char* msg = (state_ == State::DEFUSED) ? "BOMB DEFUSED" : "BOOM";
        const Color col = (state_ == State::DEFUSED) ? col_good : col_accent;
        DrawText(msg, sw / 2 - MeasureText(msg, 72) / 2, sh / 2 - 96, 72, col);

        const int top_y = sh / 2 + 10;
        const char* labels[end_count] = {"NEW BOMB", "MENU"};
        for (int i = 0; i < end_count; ++i) {
            draw_button(labels[i], end_button_rect(i, sw, top_y),
                        i == end_selected_idx_);
        }

        draw_centered_text("R for a new bomb   -   arrow keys and Enter to choose",
                           top_y + end_btn_h + 20, 16, col_hint);
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

    switch (state_) {
        case State::MENU:         draw_menu();         break;
        case State::SETTINGS:     draw_settings();     break;
        case State::INSTRUCTIONS: draw_instructions(); break;
        default:                  draw_hud();          break;
    }
    EndDrawing();
}
