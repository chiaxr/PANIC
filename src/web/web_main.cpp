// Emscripten web entry point. Mirrors the QWAS web driver: a single App is
// created and stepped by emscripten_set_main_loop, pausing while the device is
// in portrait orientation (the bomb wants a landscape canvas).

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "app.h"

namespace {

App g_app;

// Prefer a JS-supplied hook if present, otherwise compare viewport dimensions.
EM_JS(int, panic_web_is_portrait, (), {
    if (typeof Module !== 'undefined' && Module.__panicIsPortrait) {
        return Module.__panicIsPortrait() ? 1 : 0;
    }
    return (window.innerHeight > window.innerWidth) ? 1 : 0;
});

void frame() {
    g_app.set_paused(panic_web_is_portrait() != 0);
    g_app.frame();
}

} // namespace

int main() {
    g_app.init();
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
