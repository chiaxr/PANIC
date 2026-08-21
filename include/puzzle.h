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
//   2. Read what you need from BombAttributes in init() to pick your variables,
//      taking all randomness from the seeded rng that init() is handed.
//   3. Register a factory in register_builtin_puzzles() (bomb.cpp).

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

#include "bomb_attributes.h"

// Side length, in pixels, of the square render target each module draws into.
// Sized for the text-heavy modules (Who's on First, Passwords, Memory), which
// are unreadable in a bay at anything smaller.
inline constexpr int module_tex_size = 512;

// One frame of interaction for a module, already in module-local space. Only
// the focused module receives these; everything is in module pixels: [0, size).
//
// tapped is the discrete "clicked this thing" event most modules want. The
// press/hold/release trio exists for the two press-and-hold modules (The Button
// and the needy Capacitor Discharge) and always describes a single pointer —
// no module ever asks for two contact points at once.
struct ModuleInput {
    bool tapped = false;   // press+release on the spot, no drag
    Vector2 tap_pos{0.0f, 0.0f};

    bool pressed = false;   // pointer went down this frame
    bool held = false;      // pointer is still down
    bool released = false;  // pointer came up this frame
    Vector2 pointer_pos{0.0f, 0.0f};
};

// Live bomb state a module may need while playing. Distinct from
// BombAttributes, which is fixed when the bomb is built: these change during
// the round, so they are handed to update() rather than init().
struct BombContext {
    int strikes = 0;         // Simon Says picks its colour table from this
    float time_left = 0.0f;  // seconds; The Button's release digit
};

class Puzzle {
public:
    virtual ~Puzzle() = default;

    // Human-readable module name (also the manual heading).
    virtual const char* name() const = 0;

    // Derive puzzle variables from the bomb's attributes. `rng` is the bomb's
    // seeded generator: every module must take its randomness from it (or from
    // a member engine seeded off it) so that one serial always builds the same
    // bomb. Never use std::random_device in a module.
    virtual void init(const BombAttributes& attrs, std::mt19937& rng) = 0;

    // Advance interaction. Called every frame while the game is running, even
    // for modules that are not focused (needy timers depend on this); `in` only
    // carries pointer events when this module is the focused one.
    virtual void update(const ModuleInput& in, const BombContext& ctx,
                        float dt) = 0;

    // Needy modules tick continuously and are never disarmed, so they are
    // excluded from the bomb's solved/total accounting.
    virtual bool is_needy() const { return false; }

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
