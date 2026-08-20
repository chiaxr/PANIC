// Native desktop entry point.

#include "raylib.h"

#include "app.h"

int main() {
    App app;
    app.init();

    while (!WindowShouldClose()) {
        app.frame();
    }

    app.shutdown();
    return 0;
}
