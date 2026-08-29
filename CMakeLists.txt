cmake_minimum_required(VERSION 3.16)

project(PANIC
    VERSION 0.1.0
    DESCRIPTION "Puzzles Always Need Immediate Communication - a bomb defusal game"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Build options: native (desktop) and web (Emscripten) are mutually exclusive.
# ----------------------------------------------------------------------------
option(PANIC_BUILD_NATIVE "Build the native desktop target" ON)
option(PANIC_BUILD_WEB    "Build the Emscripten web target"  OFF)

if(PANIC_BUILD_NATIVE AND PANIC_BUILD_WEB)
    message(FATAL_ERROR "PANIC_BUILD_NATIVE and PANIC_BUILD_WEB are mutually exclusive; enable only one.")
endif()
if(NOT PANIC_BUILD_NATIVE AND NOT PANIC_BUILD_WEB)
    message(FATAL_ERROR "Enable exactly one of PANIC_BUILD_NATIVE or PANIC_BUILD_WEB.")
endif()
if(PANIC_BUILD_WEB AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "PANIC_BUILD_WEB requires configuring with emcmake (Emscripten toolchain).")
endif()

# ----------------------------------------------------------------------------
# raylib (fetched automatically).
# ----------------------------------------------------------------------------
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# ----------------------------------------------------------------------------
# Shared game library: all gameplay code, platform-agnostic.
# ----------------------------------------------------------------------------
add_library(panic_game STATIC
    src/app.cpp
    src/game.cpp
    src/bomb.cpp
    src/puzzles/wires_puzzle.cpp
    src/puzzles/button_puzzle.cpp
    src/puzzles/keypads_puzzle.cpp
    src/puzzles/memory_puzzle.cpp
    src/puzzles/passwords_puzzle.cpp
    src/puzzles/simon_puzzle.cpp
    src/puzzles/whos_on_first_puzzle.cpp
    src/puzzles/morse_puzzle.cpp
    src/puzzles/complicated_wires_puzzle.cpp
    src/puzzles/wire_sequences_puzzle.cpp
    src/puzzles/maze_puzzle.cpp
    src/puzzles/fold_out_puzzle.cpp
    src/puzzles/tape_reader_puzzle.cpp
    src/puzzles/pipeworks_puzzle.cpp
    src/puzzles/star_chart_puzzle.cpp
    src/puzzles/needy_puzzle.cpp
    src/puzzles/venting_gas_puzzle.cpp
    src/puzzles/capacitor_puzzle.cpp
    src/puzzles/knobs_puzzle.cpp
)

target_include_directories(panic_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(panic_game PUBLIC raylib)

# ----------------------------------------------------------------------------
# Native desktop target.
# ----------------------------------------------------------------------------
if(PANIC_BUILD_NATIVE)
    add_executable(PANIC src/main.cpp)
    target_link_libraries(PANIC PRIVATE panic_game)

    if(APPLE)
        target_link_libraries(PANIC PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework OpenGL")
    elseif(UNIX)
        target_link_libraries(PANIC PRIVATE dl pthread)
    endif()
endif()

# ----------------------------------------------------------------------------
# Web (Emscripten) target -> dist/index.{html,js,wasm}
# ----------------------------------------------------------------------------
if(PANIC_BUILD_WEB)
    add_executable(panic_web src/web/web_main.cpp)
    target_link_libraries(panic_web PRIVATE panic_game)

    set_target_properties(panic_web PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

    target_link_options(panic_web PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sGL_ENABLE_GET_PROC_ADDRESS=1"
        "-sASYNCIFY"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/emscripten_shell.html")

    # Bundle the static defuser manual next to the game so it can be served together.
    add_custom_command(TARGET panic_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/manual/index.html"
            "${CMAKE_BINARY_DIR}/dist/manual.html")

    # Social/link-preview card referenced by the og:image tags in both pages.
    add_custom_command(TARGET panic_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/doc/og-image.png"
            "${CMAKE_BINARY_DIR}/dist/og-image.png")
endif()
