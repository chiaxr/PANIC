#pragma once

// Thin application wrapper (mirrors the QWAS app structure): owns the Game and
// exposes the small surface the native and web entry points drive.
//   native (src/main.cpp): init(); while (!WindowShouldClose()) frame(); shutdown();
//   web    (src/web/web_main.cpp): init(); emscripten_set_main_loop -> frame();

#include "game.h"

class App {
public:
    void init();      // create window/GL context, seed the first bomb
    void frame();     // one update + draw step
    void shutdown();  // release resources, close window
    void set_paused(bool paused);

private:
    Game game_;
    bool paused_ = false;
};
