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

Every bomb fills its six bays with a random selection of modules:

| Module | What the Defuser does |
| --- | --- |
| Wires | Cuts exactly one of 3-6 coloured wires |
| The Button | Taps or holds a coloured button, releasing on the right countdown digit |
| Keypads | Presses four symbols in the order one manual column lists them |
| Passwords | Cycles five letter wheels to the only spellable word, then submits |
| Memory | Presses one of four numbered buttons across five stages, referring back to earlier ones |
| Simon Says | Repeats a flashing colour sequence, remapped by serial vowel and strike count |
| Who's on First | Reads one button's label, then presses the first label on that label's priority list |
| Morse Code | Decodes a blinking word and transmits its frequency |
| Complicated Wires | Cuts each wire or not, from its colours, star and LED |
| Wire Sequences | Cuts wires across four panels, counting each colour as it goes |
| Mazes | Steers a marker to the goal through walls only the Expert can see |

The Expert looks the rules up in the manual and talks the Defuser through them. A wrong move earns
a strike.

PANIC follows the same module *mechanics* as *Keep Talking and Nobody Explodes*, but every lookup
table — keypad symbol columns, Who's on First words and priority lists, Simon's colour mappings,
Morse frequencies — is PANIC's own, generated and checked for solvability. Use this repository's
manual, not any other.

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
