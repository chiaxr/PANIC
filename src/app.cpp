#include "app.h"

#include "raylib.h"

namespace {
constexpr int default_width = 1280;
constexpr int default_height = 720;
} // namespace

void App::init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(default_width, default_height,
                         "PANIC - Puzzles Always Need Immediate Communication");
    SetTargetFPS(60);

    game_.setup();
}

void App::frame() {
    const float dt = GetFrameTime();
    if (!paused_) {
        game_.update(dt);
    }
    game_.draw();
}

void App::shutdown() {
    game_.unload();
    CloseWindow();
}

void App::set_paused(bool paused) {
    paused_ = paused;
    game_.set_paused(paused);
}
