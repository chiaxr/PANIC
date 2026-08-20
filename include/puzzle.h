#pragma once

// Puzzle template interface.
//
// Every module on the bomb implements Puzzle. A module authors its content in a
// clean 2D coordinate space (a square of side module_tex_size pixels). The Bomb
// binds a per-module RenderTexture, calls draw(), and maps the result onto the
// module's face quad in 3D. Taps are ray-cast into that quad and delivered back
// as module-local pixel coordinates via ModuleInput, so a puzzle never needs to
// know it lives on a 3D object.
//
// Adding a new module:
//   1. Subclass Puzzle (see puzzles/wires_puzzle.h for a worked example).
//   2. Read what you need from BombAttributes in init() to pick your variables.
//   3. Register a factory in register_builtin_puzzles() (bomb.cpp).

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

#include "bomb_attributes.h"

// Side length, in pixels, of the square render target each module draws into.
inline constexpr int module_tex_size = 320;

// One frame of interaction for a module, already in module-local space.
struct ModuleInput {
    bool tapped = false;  // a discrete tap landed on this module this frame
    Vector2 tap_pos{0.0f, 0.0f};  // tap position in module pixels: [0, size)
};

class Puzzle {
public:
    virtual ~Puzzle() = default;

    // Human-readable module name (also the manual heading).
    virtual const char* name() const = 0;

    // Derive puzzle variables from the bomb's attributes.
    virtual void init(const BombAttributes& attrs) = 0;

    // Advance interaction. Called every frame while the game is running.
    virtual void update(const ModuleInput& in, float dt) = 0;

    // Issue 2D draw calls into the currently-bound render target
    // (origin top-left, extent [0, module_tex_size] on both axes).
    virtual void draw() = 0;

    // True once the module has been correctly disarmed.
    bool is_solved() const { return solved_; }

    // Returns true exactly once for each strike the module has raised.
    bool consume_strike() {
      if (pending_strikes_ > 0) {
          --pending_strikes_;
          return true;
      }
      return false;
    }

protected:
    void mark_solved() { solved_ = true; }
    void raise_strike() { ++pending_strikes_; }

private:
    bool solved_ = false;
    int pending_strikes_ = 0;
};

// ---------------------------------------------------------------------------
// Factory registry: name -> function producing a fresh puzzle instance.
// ---------------------------------------------------------------------------
using PuzzleFactory = std::function<std::unique_ptr<Puzzle>()>;

class PuzzleRegistry {
public:
    static PuzzleRegistry& instance() {
      static PuzzleRegistry inst;
      return inst;
    }

    void add(const std::string& name, PuzzleFactory factory) {
      factories_[name] = std::move(factory);
    }

    std::unique_ptr<Puzzle> create(const std::string& name) const {
      auto it = factories_.find(name);
      return it == factories_.end() ? nullptr : it->second();
    }

    std::vector<std::string> names() const {
      std::vector<std::string> result;
      result.reserve(factories_.size());
      for (const auto& kv : factories_) result.push_back(kv.first);
      return result;
    }

private:
    std::unordered_map<std::string, PuzzleFactory> factories_;
};

// Populates the registry with all built-in puzzle templates. Called once by
// Bomb. Defined in bomb.cpp so the linker keeps every puzzle translation unit.
void register_builtin_puzzles();
