#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr float rot_speed = 0.008f;     // radians per pixel dragged
constexpr float drag_threshold = 8.0f;  // px of motion before a press is a drag
// Just short of straight up/down. The pose keeps roll at zero by turning
// pitch about the camera's horizontal axis last, which holds the bomb's up
// axis vertical on screen only while it stays this side of vertical: past it
// the bomb reads as upside down and both drags invert.
constexpr float pitch_limit = PI * 0.5f - 0.05f;
constexpr float menu_spin_speed = 0.25f;  // radians per second on the menu

// Free-look camera, and the module-focus move that swings away from it.
constexpr Vector3 camera_free_pos{0.0f, 0.0f, 8.0f};
constexpr Vector3 camera_free_target{0.0f, 0.0f, 0.0f};
constexpr float focus_time = 0.32f;         // seconds for the focus move
constexpr float round_yaw = 0.5f;           // pose a round starts in
constexpr float round_pitch = -0.15f;
constexpr float settle_time = 0.7f;         // seconds to settle into that pose
constexpr float focus_zoom_margin = 1.55f;  // >1 leaves air around the module

// Shortest signed way round to an angle, so focusing never takes the long way.
float wrap_pi(float radians) {
    while (radians > PI) radians -= 2.0f * PI;
    while (radians < -PI) radians += 2.0f * PI;
    return radians;
}

float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

// The bomb's pose. Yaw turns first about the bomb's own up axis, then pitch
// tips the result about the camera's horizontal axis — so a vertical drag
// always tilts the bomb the same way on screen, whichever face is towards the
// player. (Pitching first would tilt about the bomb's local X axis, which
// points the other way once the back is turned to the camera.)
Matrix pose_matrix(float yaw, float pitch) {
    return MatrixMultiply(MatrixRotateY(yaw), MatrixRotateX(pitch));
}

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
constexpr int menu_count = 2;

// End-of-round buttons: a centred pair.
constexpr int end_btn_w = 280;
constexpr int end_btn_h = 52;
constexpr int end_btn_gap = 24;
constexpr int end_count = 2;

// Serial entry and difficulty slider, sat above the menu buttons.
constexpr int seed_row_w = 400;          // same width as the menu buttons
constexpr int serial_box_h = 62;
constexpr int slider_h = 26;
constexpr int slider_knob_w = 22;
constexpr int seed_block_gap = 18;
constexpr int seed_label_h = 26;   // room above each control for its caption
constexpr int min_difficulty = 1;
constexpr int max_difficulty = 6;

// Modal dialog chrome (Instructions): a BACK button and a footer hint
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
constexpr int instr_link_dy = 480;  // link row top, relative to panel top
constexpr int instr_link_h = 46;

// A debug round's seed is the serial alone; this stands in for the difficulty
// the picker deliberately does not offer.
constexpr int debug_seed_difficulty = 0;

// Debug mode's way in: a small, quiet button in the title screen's bottom-right
// corner. It is a development tool, so it stays clear of the two things a
// player actually needs.
constexpr int debug_corner_w = 110;
constexpr int debug_corner_h = 30;
constexpr int debug_corner_margin = 16;

// Debug module picker: its own seed row, then a two-column grid of every
// module template, then the two controls a debug round carries on screen.
constexpr int debug_cols = 2;
constexpr int debug_entry_h = 44;
constexpr int debug_entry_gap_x = 16;
constexpr int debug_entry_gap_y = 10;
constexpr int debug_seed_label_y = 70;
constexpr int debug_seed_top = 88;
constexpr int debug_seed_h = 44;
constexpr int debug_roll_w = 130;
constexpr int debug_seed_gap = 12;
constexpr int debug_body_top = 146;
// The list outgrew the panel once the template pool passed a dozen entries, so
// the body is a scrolling viewport between the seed row and the footer block.
constexpr int debug_body_bottom_pad = dialog_footer_block_h + 40;
constexpr float debug_scroll_step = 54.0f;   // one entry and its gap
constexpr int debug_scrollbar_w = 6;
constexpr int debug_btn_w = 240;
constexpr int debug_btn_h = 44;
constexpr int debug_btn_gap = 20;
constexpr int debug_btn_count = 2;
constexpr int debug_bar_bottom_gap = 62;   // clear of the controls hint

struct DialogLayout {
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
};

// Build the bomb's engine from the run's seed. Mixing the difficulty in means
// the same serial at a different module count is a genuinely different bomb,
// which is what players expect from a difficulty setting.
std::mt19937 seeded_engine(const std::string& serial, int difficulty) {
    std::vector<std::uint32_t> data;
    data.reserve(serial.size() + 1);
    for (char c : serial) data.push_back(static_cast<std::uint32_t>(c));
    data.push_back(static_cast<std::uint32_t>(difficulty));
    std::seed_seq seq(data.begin(), data.end());
    return std::mt19937(seq);
}

// Serials are upper-case letters and digits only.
bool is_serial_char(int c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

// Unified pointer over mouse and (single-)touch, so the same path serves
// desktop and web/mobile.
bool pointer_down() {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0;
}

Vector2 pointer_pos() {
    if (GetTouchPointCount() > 0) return GetTouchPosition(0);
    return GetMousePosition();
}

// The title screen stacks title, subtitle, the run's seed controls, then the
// buttons. Everything below the subtitle hangs off this one anchor so the
// blocks cannot drift into each other.
float menu_seed_top(int sh) {
    // Never let the seed block ride up into the subtitle on a short window.
    const float below_subtitle = sh * 0.12f + 104.0f + 12.0f + 28.0f + 24.0f;
    const float proportional = sh * 0.34f;
    return proportional > below_subtitle ? proportional : below_subtitle;
}

// Height of the seed block: two labelled controls plus the gap under them.
constexpr int seed_block_h = 196;

Rectangle menu_button_rect(int idx, int sw, int sh) {
    const int x = (sw - menu_btn_w) / 2;
    const int y = static_cast<int>(menu_seed_top(sh)) + seed_block_h +
                  idx * menu_btn_gap;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(menu_btn_w),
                     static_cast<float>(menu_btn_h)};
}

// Serial box, and the slider below it. Both are captioned, so each leaves
// room above itself for its label.
Rectangle serial_box_rect(int sw, int sh) {
    const int x = (sw - seed_row_w) / 2;
    const int y = static_cast<int>(menu_seed_top(sh)) + seed_label_h;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(seed_row_w),
                     static_cast<float>(serial_box_h)};
}

Rectangle slider_rect(int sw, int sh) {
    const Rectangle box = serial_box_rect(sw, sh);
    return Rectangle{box.x,
                     box.y + box.height + seed_block_gap +
                         static_cast<float>(seed_label_h),
                     static_cast<float>(seed_row_w),
                     static_cast<float>(slider_h)};
}

// Where the knob sits for a given difficulty.
float slider_knob_x(Rectangle track, int difficulty) {
    const float t = static_cast<float>(difficulty - min_difficulty) /
                    static_cast<float>(max_difficulty - min_difficulty);
    return track.x + t * (track.width - slider_knob_w);
}

// Difficulty under a pointer at x, clamped to the slider's range.
int difficulty_at_x(Rectangle track, float x) {
    const float usable = track.width - slider_knob_w;
    float t = (x - track.x - slider_knob_w * 0.5f) / usable;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return min_difficulty +
           static_cast<int>(t * (max_difficulty - min_difficulty) + 0.5f);
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

// A left-aligned row for the debug picker: the module name, plus a tag on the
// right for the modules that behave differently (the needy ones).
void draw_list_entry(const char* label, const char* tag, Rectangle rect,
                     bool selected) {
    DrawRectangleRec(rect, selected ? Color{58, 30, 32, 235}
                                    : Color{24, 25, 30, 215});
    DrawRectangleLinesEx(rect, 2.0f,
                         selected ? col_accent : Color{60, 63, 72, 220});

    const int fs = 22;
    DrawText(label, static_cast<int>(rect.x) + 14,
             static_cast<int>(rect.y + (rect.height - fs) * 0.5f), fs,
             selected ? RAYWHITE : Color{180, 186, 196, 255});

    if (tag == nullptr) return;
    const int tag_fs = 14;
    DrawText(tag,
             static_cast<int>(rect.x + rect.width) - 14 -
                 MeasureText(tag, tag_fs),
             static_cast<int>(rect.y + (rect.height - tag_fs) * 0.5f), tag_fs,
             col_amber);
}

void draw_centered_text(const char* text, int y, int font_size, Color color) {
    DrawText(text, (GetScreenWidth() - MeasureText(text, font_size)) / 2, y,
             font_size, color);
}

DialogLayout centered_layout(int bw, int bh, int sw, int sh) {
    return DialogLayout{(sw - bw) / 2, (sh - bh) / 2, bw, bh};
}

DialogLayout instructions_layout(int sw, int sh) {
    return centered_layout(760, 654, sw, sh);
}

Rectangle dialog_back_button_rect(const DialogLayout& l) {
    const int x = l.bx + (l.bw - dialog_back_btn_w) / 2;
    const int y = l.by + l.bh - dialog_back_btn_margin - dialog_back_btn_h;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(dialog_back_btn_w),
                     static_cast<float>(dialog_back_btn_h)};
}

DialogLayout debug_menu_layout(int sw, int sh) {
    return centered_layout(760, 660, sw, sh);
}

Rectangle debug_corner_rect(int sw, int sh) {
    return Rectangle{
        static_cast<float>(sw - debug_corner_margin - debug_corner_w),
        static_cast<float>(sh - debug_corner_margin - debug_corner_h),
        static_cast<float>(debug_corner_w),
        static_cast<float>(debug_corner_h)};
}

// The picker's seed row: the serial the bomb is built from, and a button that
// rolls a fresh one.
Rectangle debug_serial_rect(const DialogLayout& l) {
    const int w = l.bw - instr_body_x_pad * 2 - debug_roll_w - debug_seed_gap;
    return Rectangle{static_cast<float>(l.bx + instr_body_x_pad),
                     static_cast<float>(l.by + debug_seed_top),
                     static_cast<float>(w), static_cast<float>(debug_seed_h)};
}

Rectangle debug_roll_rect(const DialogLayout& l) {
    const Rectangle box = debug_serial_rect(l);
    return Rectangle{box.x + box.width + debug_seed_gap, box.y,
                     static_cast<float>(debug_roll_w),
                     static_cast<float>(debug_seed_h)};
}

// Entries fill each column top to bottom before starting the next one.
int debug_grid_rows(int count) {
    return (count + debug_cols - 1) / debug_cols;
}

// The window the list is seen through. Everything outside it is clipped away.
Rectangle debug_viewport_rect(const DialogLayout& l) {
    return Rectangle{
        static_cast<float>(l.bx + instr_body_x_pad),
        static_cast<float>(l.by + debug_body_top),
        static_cast<float>(l.bw - instr_body_x_pad * 2),
        static_cast<float>(l.bh - debug_body_top - debug_body_bottom_pad)};
}

int debug_row_pitch() { return debug_entry_h + debug_entry_gap_y; }

int debug_content_h(int rows) {
    if (rows <= 0) return 0;
    return rows * debug_entry_h + (rows - 1) * debug_entry_gap_y;
}

float debug_max_scroll(const DialogLayout& l, int rows) {
    const float over = static_cast<float>(debug_content_h(rows)) -
                       debug_viewport_rect(l).height;
    return over > 0.0f ? over : 0.0f;
}

Rectangle debug_entry_rect(int idx, const DialogLayout& l, int rows,
                           float scroll) {
    const int col = rows > 0 ? idx / rows : 0;
    const int row = rows > 0 ? idx % rows : 0;
    const int w = (l.bw - instr_body_x_pad * 2 -
                   debug_entry_gap_x * (debug_cols - 1)) / debug_cols;
    const int x = l.bx + instr_body_x_pad + col * (w + debug_entry_gap_x);
    const int y = l.by + debug_body_top + row * debug_row_pitch();
    return Rectangle{static_cast<float>(x), static_cast<float>(y) - scroll,
                     static_cast<float>(w), static_cast<float>(debug_entry_h)};
}

// An entry scrolled out of the viewport is only half drawn, so hover and click
// tests run against the part still on screen. A fully clipped entry ends up
// with no height and answers nothing.
Rectangle clip_to_viewport(Rectangle r, const Rectangle& view) {
    const float top = r.y > view.y ? r.y : view.y;
    const float view_bottom = view.y + view.height;
    const float bottom =
        (r.y + r.height) < view_bottom ? (r.y + r.height) : view_bottom;
    r.y = top;
    r.height = bottom > top ? bottom - top : 0.0f;
    return r;
}

// The debug round's own buttons, sat above the controls hint.
Rectangle debug_button_rect(int idx, int sw, int sh) {
    const int span =
        debug_btn_w * debug_btn_count + debug_btn_gap * (debug_btn_count - 1);
    const int x = (sw - span) / 2 + idx * (debug_btn_w + debug_btn_gap);
    const int y = sh - debug_bar_bottom_gap - debug_btn_h;
    return Rectangle{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(debug_btn_w),
                     static_cast<float>(debug_btn_h)};
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

    // The menu's backdrop bomb is built from the serial shown in the box, so
    // what the players see is already the bomb they would get by pressing START.
    serial_rng_.seed(std::random_device{}());
    randomize_serial();
    rebuild_bomb();

    // The debug picker's list: every module template, with the needy ones
    // flagged, since a needy module never reaches a solved state. Probing each
    // template once here keeps the picker from instantiating puzzles per frame.
    register_builtin_puzzles();
    for (const std::string& name : Bomb::module_template_names()) {
        const std::unique_ptr<Puzzle> probe =
            PuzzleRegistry::instance().create(name);
        if (!probe) continue;
        debug_modules_.push_back(name);
        debug_needy_.push_back(probe->is_needy());
    }
}

void Game::rebuild_bomb() {
    bomb_.unload();
    // A debug round replaces the whole layout with the one module under test,
    // and takes the difficulty out of its seed: the slider is not on the debug
    // picker, so leaving it in would make the same serial build a different
    // module depending on a setting that screen never shows.
    const bool debug = !debug_module_.empty();
    rng_ = seeded_engine(serial_, debug ? debug_seed_difficulty : difficulty_);
    bomb_.setup(rng_, serial_, difficulty_,
                debug ? debug_module_.c_str() : nullptr);

    built_serial_ = serial_;
    built_difficulty_ = difficulty_;
    built_debug_module_ = debug_module_;
    bomb_pristine_ = true;
}

void Game::refresh_menu_bomb() {
    // A half-typed serial has no bomb to show; keep the last good one until it
    // is complete again.
    if (!serial_is_valid()) return;
    if (bomb_pristine_ && built_serial_ == serial_ &&
            built_difficulty_ == difficulty_ &&
            built_debug_module_ == debug_module_) {
        return;
    }
    rebuild_bomb();
}

void Game::unload() { bomb_.unload(); }

Matrix Game::bomb_transform() const { return pose_matrix(yaw_, pitch_); }

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
        if (focus_settled() && press_slot_ == focused_slot_) {
            ModuleInput in;
            in.pressed = true;
            in.held = true;
            in.pointer_pos = press_pixel_;
            bomb_.send_input(focused_slot_, in);
        }
    } else if (down && pointer_down_) {
        // Held: distinguish drag (rotate) from tap. Free-look only — a focused
        // module stays square to the camera.
        const Vector2 delta = Vector2Subtract(pos, last_pos_);
        if (Vector2Distance(pos, press_pos_) > drag_threshold) dragging_ = true;
        if (dragging_ && focused_slot_ < 0) {
            intro_t_ = 1.0f;   // the player is steering now
            // Dragging down rolls the top of the bomb away and its top face
            // towards the player. Pitch turns about the camera's horizontal
            // axis, so it reads the same whichever face is towards the
            // player, and stops short of vertical so the bomb never tips over
            // into reading upside down (which would invert both drags). Yaw
            // wraps instead, so spinning on and on never winds it out of
            // range.
            yaw_ = wrap_pi(yaw_ + delta.x * rot_speed);
            pitch_ = Clamp(pitch_ + delta.y * rot_speed, -pitch_limit,
                           pitch_limit);
            free_yaw_ = yaw_;
            free_pitch_ = pitch_;
        }
        last_pos_ = pos;

        // A press that started on the focused module keeps reporting as held,
        // which is what the press-and-hold modules watch.
        if (focus_settled() && press_slot_ == focused_slot_) {
            Vector2 pixel{};
            ModuleInput in;
            in.held = true;
            in.pointer_pos =
                (pick_module(pos, pixel) == focused_slot_) ? pixel : press_pixel_;
            bomb_.send_input(focused_slot_, in);
        }
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
                    in.released = true;
                    in.pointer_pos = press_pixel_;
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
        } else if (focus_settled() && press_slot_ == focused_slot_) {
            // Dragged rather than tapped, but the module still needs to know
            // the hold ended (The Button releases on a moved pointer too).
            ModuleInput in;
            in.released = true;
            in.pointer_pos = press_pixel_;
            bomb_.send_input(press_slot_, in);
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
    intro_t_ = 1.0f;

    // Turn the bay's outward normal towards the camera: front bays need no
    // yaw, back bays a half turn, and the pitch always flattens out.
    focus_pitch_ = 0.0f;
    const float face_yaw = (q.normal.z >= 0.0f) ? 0.0f : PI;
    focus_yaw_ = yaw_ + wrap_pi(face_yaw - yaw_);

    // Where that leaves the module in world space, and how far back the camera
    // has to sit for the face to fill most of the viewport height.
    const Matrix m = pose_matrix(focus_yaw_, focus_pitch_);
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

// A screen's highlight belongs to whichever device is steering it. Moving or
// clicking the pointer hands it over to the pointer, so the lit button is the
// one under the cursor and nothing is lit when the cursor is off them all;
// pressing an arrow key hands it back to the keyboard selection.
void Game::update_nav_device() {
    const Vector2 delta = GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f || GetTouchPointCount() > 0 ||
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pointer_nav_ = true;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN) ||
            IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) {
        pointer_nav_ = false;
    }
}

bool Game::button_lit(int idx, int selected_idx, Rectangle rect) const {
    return pointer_nav_ ? rect_hovered(rect) : idx == selected_idx;
}

void Game::randomize_serial() {
    serial_ = BombAttributes::random_serial(serial_rng_);
}

// A serial has to be the full length and carry at least one digit, because
// several modules key off "the last digit of the serial".
bool Game::serial_is_valid() const {
    if (static_cast<int>(serial_.size()) != BombAttributes::serial_length) {
        return false;
    }
    for (char c : serial_) {
        if (c >= '0' && c <= '9') return true;
    }
    return false;
}

void Game::start_round() {
    // Always rebuild: the bomb on the menu may have been played already. The
    // seed makes this deterministic, so when it is the same bomb the players
    // were just looking at, it comes back looking exactly the same.
    rebuild_bomb();
    bomb_pristine_ = false;

    state_ = State::PLAYING;
    time_left_ = round_seconds;
    strikes_ = 0;
    end_selected_idx_ = 0;

    // Settle into the round's pose from wherever the title screen's spin left
    // the bomb, taking the short way round on both axes rather than unwinding
    // the turns the menu spin (or a drag) put in.
    intro_from_yaw_ = yaw_;
    intro_from_pitch_ = pitch_;
    intro_to_yaw_ = yaw_ + wrap_pi(round_yaw - yaw_);
    intro_to_pitch_ = pitch_ + wrap_pi(round_pitch - pitch_);
    intro_t_ = 0.0f;

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
        case 0:
            // A short serial, or one with no digit, would leave several
            // modules' rules undefined, so refuse rather than build it.
            if (serial_is_valid()) start_round();
            break;
        case 1: state_ = State::INSTRUCTIONS; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Debug mode. A debug round puts one module, alone, on an otherwise empty
// bomb: the clock wraps instead of running out, strikes are counted but never
// detonate, and solving the module leaves the round running so it can be
// poked further. debug_module_ names the module and is what makes a round a
// debug round.
// ---------------------------------------------------------------------------

const Puzzle* Game::debug_puzzle() const {
    for (const auto& slot : bomb_.slots()) {
        if (slot.puzzle) return slot.puzzle.get();
    }
    return nullptr;
}

void Game::start_debug_round(const std::string& module_name) {
    serial_focused_ = false;
    debug_module_ = module_name;
    debug_ui_press_ = false;
    debug_press_idx_ = -1;
    start_round();
}

void Game::leave_debug() {
    debug_module_.clear();
    debug_ui_press_ = false;
    debug_press_idx_ = -1;
    end_focus();   // hand the camera back before the picker comes up

    // Discard any press still in flight so the gesture that left the round
    // cannot also pick the next module.
    pointer_down_ = pointer_down();
    dragging_ = true;
    press_slot_ = -1;

    state_ = State::DEBUG_MENU;
    scroll_debug_entry_into_view();
}

void Game::update_debug_menu() {
    const DialogLayout l =
        debug_menu_layout(GetScreenWidth(), GetScreenHeight());
    const int count = static_cast<int>(debug_modules_.size());
    const int rows = debug_grid_rows(count);
    const Rectangle view = debug_viewport_rect(l);
    const float max_scroll = debug_max_scroll(l, rows);

    // Scrolling the list. The wheel and a drag inside the viewport both work,
    // so the picker behaves the same with a mouse and with a finger. This runs
    // before consume_tap, which is what advances last_pos_.
    debug_scroll_ -= GetMouseWheelMove() * debug_scroll_step;
    if (pointer_down() && pointer_down_ &&
            CheckCollisionPointRec(press_pos_, view)) {
        debug_scroll_ -= pointer_pos().y - last_pos_.y;
    }
    debug_scroll_ = Clamp(debug_scroll_, 0.0f, max_scroll);

    // The seed box is the same control the title screen has, editing the same
    // serial: a debug round is built from a seed like any other round.
    update_serial_entry();
    if (serial_focused_ && IsKeyPressed(KEY_ENTER)) {
        serial_focused_ = false;
        return;
    }

    if (count > 0 && !serial_focused_) {
        if (IsKeyPressed(KEY_DOWN)) {
            debug_selected_idx_ = (debug_selected_idx_ + 1) % count;
        }
        if (IsKeyPressed(KEY_UP)) {
            debug_selected_idx_ = (debug_selected_idx_ - 1 + count) % count;
        }
        // Left and right step a whole column, which is how the grid is filled.
        if (IsKeyPressed(KEY_RIGHT)) {
            debug_selected_idx_ = (debug_selected_idx_ + rows) % count;
        }
        if (IsKeyPressed(KEY_LEFT)) {
            debug_selected_idx_ = (debug_selected_idx_ - rows + count) % count;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP) ||
                IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_LEFT)) {
            scroll_debug_entry_into_view();
        }
        for (int i = 0; i < count; ++i) {
            const Rectangle hit =
                clip_to_viewport(debug_entry_rect(i, l, rows, debug_scroll_),
                                 view);
            if (hit.height > 0.0f && rect_hovered(hit)) debug_selected_idx_ = i;
        }
        // A serial that no bomb can be built from is refused here, exactly as
        // START refuses it on the title screen.
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) &&
                serial_is_valid()) {
            start_debug_round(debug_modules_[debug_selected_idx_]);
            return;
        }
    }

    Vector2 tap{};
    const bool tapped = consume_tap(tap);

    // Backspace edits the serial while the box has focus, so it only backs out
    // of the picker when it does not.
    const bool tapped_back =
        tapped && CheckCollisionPointRec(tap, dialog_back_button_rect(l));
    if ((!serial_focused_ && IsKeyPressed(KEY_BACKSPACE)) || tapped_back) {
        serial_focused_ = false;
        menu_selected_idx_ = 0;
        state_ = State::MENU;
        return;
    }

    if (!tapped) return;

    serial_focused_ = CheckCollisionPointRec(tap, debug_serial_rect(l));
    if (serial_focused_) return;

    if (CheckCollisionPointRec(tap, debug_roll_rect(l))) {
        randomize_serial();
        return;
    }

    if (!serial_is_valid()) return;
    for (int i = 0; i < count; ++i) {
        const Rectangle hit = clip_to_viewport(
            debug_entry_rect(i, l, rows, debug_scroll_), view);
        if (hit.height > 0.0f && CheckCollisionPointRec(tap, hit)) {
            start_debug_round(debug_modules_[i]);
            return;
        }
    }
}

void Game::scroll_debug_entry_into_view() {
    const DialogLayout l =
        debug_menu_layout(GetScreenWidth(), GetScreenHeight());
    const int rows = debug_grid_rows(static_cast<int>(debug_modules_.size()));
    if (rows <= 0) return;

    const float height = debug_viewport_rect(l).height;
    const float top =
        static_cast<float>((debug_selected_idx_ % rows) * debug_row_pitch());
    const float bottom = top + static_cast<float>(debug_entry_h);

    if (top < debug_scroll_) debug_scroll_ = top;
    if (bottom > debug_scroll_ + height) debug_scroll_ = bottom - height;
    debug_scroll_ = Clamp(debug_scroll_, 0.0f, debug_max_scroll(l, rows));
}

bool Game::update_debug_controls() {
    // A new seed rebuilds the same module with fresh variables, which is the
    // quick way to walk a module through many of its cases.
    if (IsKeyPressed(KEY_R)) {
        randomize_serial();
        start_debug_round(debug_module_);
        return true;
    }
    if (IsKeyPressed(KEY_M)) {
        leave_debug();
        return true;
    }

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const bool down = pointer_down();
    const Vector2 pos = pointer_pos();

    // Claim a press that starts on a button, and keep it until it is released,
    // so the same gesture never reaches the bomb behind.
    if (down && !debug_ui_press_ && !pointer_down_) {
        for (int i = 0; i < debug_btn_count; ++i) {
            if (CheckCollisionPointRec(pos, debug_button_rect(i, sw, sh))) {
                debug_ui_press_ = true;
                debug_press_idx_ = i;
                break;
            }
        }
    }
    if (!debug_ui_press_) return false;

    if (!down) {
        const int idx = debug_press_idx_;
        debug_ui_press_ = false;
        debug_press_idx_ = -1;
        if (CheckCollisionPointRec(pos, debug_button_rect(idx, sw, sh))) {
            if (idx == 0) {
                randomize_serial();
                start_debug_round(debug_module_);
            } else {
                leave_debug();
            }
        }
    }
    return true;
}

// Typing into the serial box. Only accepts serial characters, and lower case
// is folded up so the box always shows what the casing will print.
void Game::update_serial_entry() {
    if (!serial_focused_) return;

    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!is_serial_char(c)) continue;
        if (static_cast<int>(serial_.size()) >= BombAttributes::serial_length) {
            continue;
        }
        serial_ += static_cast<char>(c);
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !serial_.empty()) serial_.pop_back();
}

void Game::update_difficulty_slider() {
    const Rectangle track = slider_rect(GetScreenWidth(), GetScreenHeight());
    const Vector2 pos = pointer_pos();
    const bool down = pointer_down();

    // Grab anywhere on the track, then keep following the pointer.
    if (down && !dragging_slider_ && CheckCollisionPointRec(pos, track)) {
        dragging_slider_ = true;
    }
    if (!down) dragging_slider_ = false;
    if (dragging_slider_) difficulty_ = difficulty_at_x(track, pos.x);

    // Keyboard nudges, so the slider is reachable without a pointer.
    if (!serial_focused_) {
        if (IsKeyPressed(KEY_LEFT) && difficulty_ > min_difficulty) --difficulty_;
        if (IsKeyPressed(KEY_RIGHT) && difficulty_ < max_difficulty) ++difficulty_;
    }
}

void Game::update_menu() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    update_serial_entry();
    update_difficulty_slider();
    refresh_menu_bomb();

    if (IsKeyPressed(KEY_UP)) {
        menu_selected_idx_ = (menu_selected_idx_ - 1 + menu_count) % menu_count;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        menu_selected_idx_ = (menu_selected_idx_ + 1) % menu_count;
    }
    // While typing, Enter just closes the box rather than starting a round.
    if (serial_focused_ && IsKeyPressed(KEY_ENTER)) {
        serial_focused_ = false;
        return;
    }

    // Hover and touch contact also move the selection, so pressing Enter picks
    // the button the pointer is lighting up rather than one somewhere else.
    for (int i = 0; i < menu_count; ++i) {
        const Rectangle r = menu_button_rect(i, sw, sh);
        if (rect_hovered(r)) menu_selected_idx_ = i;
        for (int t = 0; t < GetTouchPointCount(); ++t) {
            if (CheckCollisionPointRec(GetTouchPosition(t), r)) {
                menu_selected_idx_ = i;
            }
        }
    }

    // Not while typing: space is not a serial character, but it would
    // otherwise fall through and press START.
    if (!serial_focused_ &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
        activate_menu_button(menu_selected_idx_);
        return;
    }

    Vector2 tap{};
    if (!consume_tap(tap)) return;

    // Clicking the box starts editing; clicking anywhere else stops.
    serial_focused_ = CheckCollisionPointRec(tap, serial_box_rect(sw, sh));
    if (serial_focused_) return;

    if (CheckCollisionPointRec(tap, debug_corner_rect(sw, sh))) {
        debug_selected_idx_ = 0;
        debug_scroll_ = 0.0f;
        state_ = State::DEBUG_MENU;
        return;
    }

    for (int i = 0; i < menu_count; ++i) {
        if (CheckCollisionPointRec(tap, menu_button_rect(i, sw, sh))) {
            activate_menu_button(i);
            return;
        }
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
        menu_selected_idx_ = 1;
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

    // A new bomb means a new seed. The serial stays on the HUD throughout, so
    // the one just played can still be written down before moving on.
    auto new_bomb = [this] {
        randomize_serial();
        start_round();
    };
    auto to_menu = [this] {
        menu_selected_idx_ = 0;
        state_ = State::MENU;
    };

    if (IsKeyPressed(KEY_R)) {
        new_bomb();
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (end_selected_idx_ == 0) {
            new_bomb();
        } else {
            to_menu();
        }
        return;
    }

    Vector2 tap{};
    if (!consume_tap(tap)) return;
    for (int i = 0; i < end_count; ++i) {
        if (!CheckCollisionPointRec(tap, end_button_rect(i, sw, top_y))) continue;
        if (i == 0) {
            new_bomb();
        } else {
            to_menu();
        }
        return;
    }
}

void Game::update(float dt) {
    if (paused_) return;

    update_nav_device();

    // Runs in every state so a focus move started mid-round still finishes
    // (and hands the camera back) after the round ends.
    update_focus(dt);

    switch (state_) {
        case State::MENU:
            yaw_ = wrap_pi(yaw_ + menu_spin_speed * dt);
            update_menu();
            break;

        case State::INSTRUCTIONS:
            yaw_ = wrap_pi(yaw_ + menu_spin_speed * dt);
            update_instructions();
            break;

        case State::DEBUG_MENU:
            yaw_ = wrap_pi(yaw_ + menu_spin_speed * dt);
            update_debug_menu();
            break;

        case State::PLAYING: {
            // Focusing a module takes over the rotation, so the two tweens
            // never run against each other.
            if (settling() && focused_slot_ < 0) {
                intro_t_ = std::min(1.0f, intro_t_ + dt / settle_time);
                const float s = smoothstep01(intro_t_);
                yaw_ = Lerp(intro_from_yaw_, intro_to_yaw_, s);
                pitch_ = Lerp(intro_from_pitch_, intro_to_pitch_, s);
                free_yaw_ = yaw_;
                free_pitch_ = pitch_;
            }

            if (focused_slot_ >= 0 && focusing_ &&
                    (IsKeyPressed(KEY_BACKSPACE) ||
                     IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
                end_focus();
            }

            // The debug bar owns the pointer while a press sits on it, and
            // can end the round outright (back to the picker).
            const bool debug_bar_active = debug_mode() && update_debug_controls();
            if (state_ != State::PLAYING) break;
            if (!debug_bar_active) handle_pointer(dt);

            BombContext ctx;
            ctx.strikes = strikes_;
            ctx.time_left = time_left_;
            bomb_.update(dt, ctx);
            strikes_ += bomb_.take_strike_events();

            time_left_ -= dt;

            if (debug_mode()) {
                // No time limit. The clock still runs, because the modules
                // that read it (The Button's release digit, the needy wake-up
                // timers) have to behave as they do in a real round, but it
                // wraps round instead of running out. Strikes are counted and
                // never detonate, and a solved module is left on screen.
                if (time_left_ <= 0.0f) time_left_ += round_seconds;
                break;
            }

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

    // ---- the run's seed: serial number and module count ----
    const Rectangle box = serial_box_rect(sw, sh);
    const bool valid = serial_is_valid();

    draw_centered_text("BOMB SERIAL", static_cast<int>(box.y) - 26, 18,
                       col_text_dim);
    DrawRectangleRec(box, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(box, 3,
                         serial_focused_ ? col_accent
                                         : (valid ? Color{70, 72, 80, 255}
                                                  : Color{150, 90, 60, 255}));

    const int serial_size = 40;
    const int serial_w = MeasureText(serial_.c_str(), serial_size);
    const int serial_x = static_cast<int>(box.x + (box.width - serial_w) * 0.5f);
    const int serial_y =
        static_cast<int>(box.y + (box.height - serial_size) * 0.5f);
    DrawText(serial_.c_str(), serial_x, serial_y, serial_size,
             Color{120, 240, 150, 255});

    // Caret, blinking, while the box has focus.
    if (serial_focused_ &&
        static_cast<int>(GetTime() * 2.0f) % 2 == 0 &&
        static_cast<int>(serial_.size()) < BombAttributes::serial_length) {
        DrawRectangle(serial_x + serial_w + 4, serial_y, 3, serial_size,
                      Color{120, 240, 150, 255});
    }

    // ---- difficulty ----
    const Rectangle track = slider_rect(sw, sh);
    const char* diff_label =
        TextFormat("DIFFICULTY   %d MODULE%s", difficulty_,
                   difficulty_ == 1 ? "" : "S");
    draw_centered_text(diff_label, static_cast<int>(track.y) - 26, 18,
                       col_text_dim);

    DrawRectangleRec(track, Color{24, 25, 30, 255});
    const float knob_x = slider_knob_x(track, difficulty_);
    DrawRectangleRec(Rectangle{track.x, track.y, knob_x - track.x + slider_knob_w,
                               track.height},
                     Color{78, 34, 36, 255});
    DrawRectangleLinesEx(track, 3, Color{70, 72, 80, 255});
    DrawRectangleRec(
        Rectangle{knob_x, track.y - 5.0f, static_cast<float>(slider_knob_w),
                  track.height + 10.0f},
        col_accent);

    // Notches, so the discrete steps are visible.
    for (int d = min_difficulty; d <= max_difficulty; ++d) {
        const float x = slider_knob_x(track, d) + slider_knob_w * 0.5f;
        DrawRectangle(static_cast<int>(x) - 1,
                      static_cast<int>(track.y + track.height) + 8, 2, 6,
                      Color{70, 72, 80, 255});
    }

    // ---- buttons ----
    const char* labels[menu_count] = {"START", "INSTRUCTIONS"};
    for (int i = 0; i < menu_count; ++i) {
        const Rectangle r = menu_button_rect(i, sw, sh);
        if (i == 0 && !valid) {
            // START stays visible but reads as unavailable.
            DrawRectangleRec(r, Color{22, 22, 26, 200});
            DrawRectangleLinesEx(r, 2, Color{56, 48, 48, 200});
            const int fs = 26;
            DrawText(labels[i],
                     static_cast<int>(r.x + (r.width -
                                             MeasureText(labels[i], fs)) * 0.5f),
                     static_cast<int>(r.y + (r.height - fs) * 0.5f), fs,
                     Color{110, 100, 100, 255});
            continue;
        }
        draw_button(labels[i], r, button_lit(i, menu_selected_idx_, r));
    }

    if (!valid) {
        draw_centered_text(
            "The serial needs 6 characters and at least one digit",
            static_cast<int>(menu_button_rect(menu_count - 1, sw, sh).y) +
                menu_btn_h + 16,
            16, Color{200, 130, 90, 255});
    }

    draw_centered_text(
        "Click the serial to edit   -   the same serial and difficulty "
        "always builds the same bomb",
        sh - 32, 16, col_hint);

    // Quiet corner button into debug mode.
    const Rectangle dbg = debug_corner_rect(sw, sh);
    const bool dbg_hover = rect_hovered(dbg);
    DrawRectangleRec(dbg, dbg_hover ? Color{30, 32, 38, 220}
                                    : Color{20, 21, 25, 150});
    DrawRectangleLinesEx(dbg, 1.0f, dbg_hover ? col_text_dim
                                              : Color{52, 55, 62, 200});
    const int dbg_fs = 15;
    DrawText("DEBUG",
             static_cast<int>(dbg.x + (dbg.width -
                                       MeasureText("DEBUG", dbg_fs)) * 0.5f),
             static_cast<int>(dbg.y + (dbg.height - dbg_fs) * 0.5f), dbg_fs,
             dbg_hover ? col_text : col_hint);
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
    line("The serial and difficulty on the title screen build the bomb:");
    line("note them down to replay the same one.");
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

void Game::draw_debug_menu() const {
    const DialogLayout l =
        debug_menu_layout(GetScreenWidth(), GetScreenHeight());
    draw_dialog_panel(l, "DEBUG - PLAY ONE MODULE");

    draw_centered_text(
        "One module on an empty bomb: no time limit, no detonation",
        l.by + 46, 16, col_text_dim);

    // ---- the seed this module will be built from ----
    const bool valid = serial_is_valid();
    const Rectangle box = debug_serial_rect(l);
    DrawText("SEED   -   the same seed always builds the same module",
             l.bx + instr_body_x_pad, l.by + debug_seed_label_y, 14,
             col_text_dim);

    DrawRectangleRec(box, Color{18, 30, 24, 255});
    DrawRectangleLinesEx(box, 2.0f,
                         serial_focused_ ? col_accent
                                         : (valid ? Color{70, 72, 80, 255}
                                                  : Color{150, 90, 60, 255}));
    const int serial_fs = 28;
    const int serial_x = static_cast<int>(box.x) + 14;
    const int serial_y =
        static_cast<int>(box.y + (box.height - serial_fs) * 0.5f);
    DrawText(serial_.c_str(), serial_x, serial_y, serial_fs,
             Color{120, 240, 150, 255});
    if (serial_focused_ && static_cast<int>(GetTime() * 2.0f) % 2 == 0 &&
            static_cast<int>(serial_.size()) < BombAttributes::serial_length) {
        DrawRectangle(serial_x + MeasureText(serial_.c_str(), serial_fs) + 4,
                      serial_y, 3, serial_fs, Color{120, 240, 150, 255});
    }

    const Rectangle roll = debug_roll_rect(l);
    const bool roll_hover = rect_hovered(roll);
    DrawRectangleRec(roll, roll_hover ? Color{58, 30, 32, 235}
                                      : Color{24, 25, 30, 215});
    DrawRectangleLinesEx(roll, 2.0f,
                         roll_hover ? col_accent : Color{60, 63, 72, 220});
    const int roll_fs = 18;
    DrawText("RANDOM",
             static_cast<int>(roll.x + (roll.width -
                                        MeasureText("RANDOM", roll_fs)) * 0.5f),
             static_cast<int>(roll.y + (roll.height - roll_fs) * 0.5f), roll_fs,
             roll_hover ? RAYWHITE : Color{180, 186, 196, 255});

    // ---- the modules ----
    const int count = static_cast<int>(debug_modules_.size());
    const int rows = debug_grid_rows(count);
    const Rectangle view = debug_viewport_rect(l);
    const float max_scroll = debug_max_scroll(l, rows);

    // The list is taller than the panel, so it is drawn through a window and
    // whatever falls outside is clipped rather than spilling over the footer.
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                     static_cast<int>(view.width),
                     static_cast<int>(view.height));
    for (int i = 0; i < count; ++i) {
        const Rectangle r = debug_entry_rect(i, l, rows, debug_scroll_);
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        // An unusable serial builds nothing, so nothing lights up either.
        draw_list_entry(debug_modules_[static_cast<size_t>(i)].c_str(),
                        debug_needy_[static_cast<size_t>(i)] ? "NEEDY" : nullptr,
                        r, valid && button_lit(i, debug_selected_idx_, r));
    }
    EndScissorMode();

    if (max_scroll > 0.0f) {
        // A slim track down the right-hand gutter, so it is obvious there is
        // more list than the panel is showing.
        const float x = view.x + view.width + 10.0f;
        DrawRectangleRounded(
            Rectangle{x, view.y, static_cast<float>(debug_scrollbar_w),
                      view.height},
            0.5f, 4, Color{40, 42, 50, 220});
        const float span = static_cast<float>(debug_content_h(rows));
        const float thumb_h = view.height * (view.height / span);
        const float travel = view.height - thumb_h;
        const float y = view.y + travel * (debug_scroll_ / max_scroll);
        DrawRectangleRounded(
            Rectangle{x, y, static_cast<float>(debug_scrollbar_w), thumb_h},
            0.5f, 4, Color{120, 126, 140, 235});
    }

    if (!valid) {
        // Every module reads the serial, so nothing can be built until it is
        // one a bomb could actually carry.
        draw_centered_text("The serial needs 6 characters and at least one digit",
                           l.by + l.bh - dialog_footer_block_h - 24, 16,
                           Color{200, 130, 90, 255});
    }

    draw_dialog_footer(
        l, "Click a module to play it   -   R rolls a new seed while playing",
        rect_hovered(dialog_back_button_rect(l)));
}

void Game::draw_debug_hud() const {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    // The clock keeps ticking (modules read it) but never ends the round.
    const int total =
        static_cast<int>(std::ceil(time_left_ < 0 ? 0 : time_left_));
    const char* clock = TextFormat("%02d:%02d", total / 60, total % 60);
    const int clock_size = 48;
    DrawText(clock, sw / 2 - MeasureText(clock, clock_size) / 2, 16, clock_size,
             RAYWHITE);
    draw_centered_text("NO TIME LIMIT   -   THE CLOCK JUST WRAPS ROUND",
                       16 + clock_size + 6, 16, col_hint);

    // What is under test, top-left, with the strikes it has raised.
    DrawText("DEBUG MODULE", 20, 20, 20, col_accent);
    DrawText(debug_module_.c_str(), 20, 46, 26, col_text);
    DrawText(TextFormat("STRIKES   %d", strikes_), 20, 82, 20, col_text_dim);
    DrawText("(counted, never detonates)", 20, 106, 16, col_hint);

    // Whether the module has reached its solved state, top-right.
    const Puzzle* puzzle = debug_puzzle();
    const char* status = "MODULE MISSING";
    Color status_color = col_accent;
    if (puzzle != nullptr && puzzle->is_needy()) {
        status = "NEEDY   -   NEVER DISARMS";
        status_color = col_amber;
    } else if (puzzle != nullptr) {
        status = puzzle->is_solved() ? "SOLVED" : "UNSOLVED";
        status_color = puzzle->is_solved() ? col_good : col_text_dim;
    }
    DrawText(status, sw - 20 - MeasureText(status, 22), 24, 22, status_color);

    const char* seed = TextFormat("SEED   %s", serial_.c_str());
    DrawText(seed, sw - 20 - MeasureText(seed, 18), 56, 18, col_hint);

    const char* labels[debug_btn_count] = {"NEW SEED  (R)", "MODULES  (M)"};
    for (int i = 0; i < debug_btn_count; ++i) {
        draw_button(labels[i], debug_button_rect(i, sw, sh),
                    debug_press_idx_ == i ||
                        rect_hovered(debug_button_rect(i, sw, sh)));
    }

    const char* hint =
        (focused_slot_ >= 0)
            ? "Tap the module to work on it   -   tap away, right-click or "
              "Backspace to step back"
            : "Drag to rotate the bomb   -   tap the module to zoom in on it";
    DrawText(hint, sw / 2 - MeasureText(hint, 20) / 2, sh - 34, 20, col_hint);
}

void Game::draw_hud() const {
    if (debug_mode()) {
        draw_debug_hud();
        return;
    }

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

    // The seed that built this bomb, so it can be noted down and replayed. Both
    // halves are needed: the serial alone does not identify the bomb.
    const char* seed = TextFormat("SEED   %s  /  %d", serial_.c_str(),
                                  difficulty_);
    DrawText(seed, sw - 20 - MeasureText(seed, 18), 56, 18, col_hint);

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
            const Rectangle r = end_button_rect(i, sw, top_y);
            draw_button(labels[i], r, button_lit(i, end_selected_idx_, r));
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
        case State::INSTRUCTIONS: draw_instructions(); break;
        case State::DEBUG_MENU:   draw_debug_menu();   break;
        default:                  draw_hud();          break;
    }
    EndDrawing();
}
