# PANIC - Puzzles Always Need Immediate Communication

PANIC is a small 3D bomb-defusal game inspired by *Keep Talking and Nobody Explodes*. One player
(the **Defuser**) sees the bomb but has no instructions; their teammate (the
**Expert**) has access to the [manual](https://chiaxr.github.io/PANIC/manual.html) but cannot see the bomb. They have to communicate with each other to disarm the bomb before the timer runs out.

[Try it here now!](https://chiaxr.github.io/PANIC/)

## Gameplay
Rotate the bomb to inspect its faces and read the widgets aloud to your Expert. Each **module**
must be disarmed before the countdown reaches zero. Three **strikes** and the bomb detonates.

Modules may depend on bomb-wide facts printed on the casing, so the Expert always needs information
only the Defuser can see:

| Widget | What to read out |
| --- | --- |
| Serial # | The six-character code (its last digit's parity is the most-used fact) |
| Batteries | How many battery cells are shown |
| Indicators | Which labelled lights (`CAR`, `FRK`, …) are lit vs unlit |

The **Wires** module shows 3-6 coloured wires; exactly one must be cut. Which one depends on the
wire colours, their count, and the serial number — the Expert looks it up in the manual and
tells the Defuser which to cut. A wrong cut earns a strike.

## Controls

| Input | Action |
| --- | --- |
| Arrow keys | Move the title-screen / end-screen selection |
| Enter / Space / Mouse click / Tap | Activate the selected button |
| Backspace | Leave Settings or Instructions; step back out of a focused module |
| Drag (mouse / one finger) | Rotate the bomb (free look only) |
| Mouse click / Tap on a module | Zoom in on it |
| Mouse click / Tap on a focused module | Interact with it (e.g. cut a wire) |
| Right-click / Tap away from the module | Step back out to free look |
| R | Start a fresh bomb (on the win/loss screen) |
| Esc / window close | Quit the native desktop build |

The web version is landscape-only. In portrait orientation the game pauses and a rotate prompt
is shown until the viewport returns to landscape.

## Native Build

Requirements: CMake 3.16+, a C++17 compiler, and Git. raylib 6.0 is fetched automatically by
CMake.

```bash
cmake -S . -B build -DPANIC_BUILD_NATIVE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the native executable:

```bash
./build/PANIC
```

Platform notes:

- macOS native builds link the Cocoa, IOKit, and OpenGL frameworks.
- Linux native builds use the normal raylib desktop dependencies such as OpenGL and
  X11/Wayland development packages.
- The desktop build does not depend on Emscripten, JavaScript, or web files.

## Local Emscripten Build

Install and activate Emscripten first, then configure with `emcmake`. `PANIC_BUILD_NATIVE` and
`PANIC_BUILD_WEB` are mutually exclusive.

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DPANIC_BUILD_NATIVE=OFF \
  -DPANIC_BUILD_WEB=ON

cmake --build build-web --target panic_web
```

Expected outputs:

```text
build-web/dist/index.html
build-web/dist/index.js
build-web/dist/index.wasm
build-web/dist/manual.html
```

Serve locally with:

```bash
emrun build-web/dist/index.html
```

You can also serve `build-web/dist` with any static file server. The Defuser's manual is bundled
alongside the game as `manual.html`.

## Project Structure

```text
PANIC/
|-- CMakeLists.txt
|-- .github/workflows/pages.yml
|-- include/
|   |-- app.h
|   |-- bomb.h
|   |-- bomb_attributes.h
|   |-- game.h
|   |-- puzzle.h
|   `-- puzzles/
|       `-- wires_puzzle.h
|-- src/
|   |-- app.cpp
|   |-- bomb.cpp
|   |-- game.cpp
|   |-- main.cpp
|   |-- puzzles/
|   |   `-- wires_puzzle.cpp
|   `-- web/
|       `-- web_main.cpp
|-- web/
|   `-- emscripten_shell.html
|-- manual/
|   `-- index.html
`-- doc/
    `-- panic.png
```

`panic_game` contains the shared application, game, bomb, and puzzle code. `PANIC` is the native
launcher. `panic_web` is the Emscripten launcher and uses `emscripten_set_main_loop()`. Puzzle
modules implement the `Puzzle` interface (see `puzzles/wires_puzzle.*`) and read the bomb's
attributes to pick their variables; the in-game logic and `manual/index.html` are kept in sync.
