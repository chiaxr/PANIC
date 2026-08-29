#!/usr/bin/env python3
"""Feasibility checks: can the Expert always get to a unique right answer?

verify_manual.py proves the manual and the game hold the same tables.
This script asks the other question -- whether those tables, and the data the
generators draw from, can ever leave the pair stuck: a keypad whose symbols name
two columns, a maze with a cell you cannot reach, a fold-out net the manual's
folding rules cannot resolve, two constellations or two knob patterns that read
the same.

    python3 scripts/verify_puzzles.py           # everything
    python3 scripts/verify_puzzles.py mazes keypads

Exit status is 0 when every check passes, 1 otherwise.
"""

import itertools
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panic_parse as P     # noqa: E402

FAILURES = []
CHECKS = []


def check(name):
    def wrap(fn):
        CHECKS.append((name, fn))
        return fn
    return wrap


def fail(module, detail):
    FAILURES.append("%s: %s" % (module, detail))


# --- Keypads ----------------------------------------------------------------
@check("keypads")
def keypads():
    """Any four symbols off one column must name that column and no other.

    The module shows four of a column's seven symbols. If those four also sit
    together in a second column the Expert cannot tell which press order is
    wanted, and the module becomes a coin toss.
    """
    columns = P.keypad_columns_source()
    for i, col in enumerate(columns):
        if len(set(col)) != len(col):
            fail("keypads", "column %d repeats a symbol: %s" % (i + 1, col))
    for i, col in enumerate(columns):
        for four in itertools.combinations(col, 4):
            owners = [j for j, other in enumerate(columns)
                      if set(four) <= set(other)]
            if owners != [i]:
                fail("keypads", "symbols %s appear in columns %s"
                                % ("".join(four), [o + 1 for o in owners]))
                return


# --- Passwords --------------------------------------------------------------
@check("passwords")
def passwords():
    """Five wheels of six letters, and the deal has to leave exactly one word.

    init() re-deals until exactly one word is spellable, so the only way to
    hang is a list where that is unreachable. Simulating the deal shows how hard
    it has to work.
    """
    words = P.password_words_source()
    if len(set(words)) != len(words):
        fail("passwords", "the word list repeats a word")
    for w in words:
        if len(w) != 5 or not w.isalpha():
            fail("passwords", "%r is not a five-letter word" % w)

    rng = random.Random(20260829)
    worst = 0
    for _ in range(2000):
        for attempt in range(1, 200):
            answer = rng.choice(words)
            wheels = []
            for c in range(5):
                wheel = {answer[c]}
                while len(wheel) < 6:
                    wheel.add(chr(ord("a") + rng.randrange(26)))
                wheels.append(wheel)
            spellable = [w for w in words
                         if all(w[c] in wheels[c] for c in range(5))]
            if len(spellable) == 1:
                worst = max(worst, attempt)
                break
        else:
            fail("passwords", "200 deals without a unique password")
            return
    if worst > 20:
        fail("passwords", "a deal took %d attempts to become unique" % worst)


# --- Wires ------------------------------------------------------------------
COLOURS = ["red", "blue", "yellow", "white", "black"]


def wires_answer(wires, serial_odd):
    """The manual's Wires rules. Returns a 1-based wire number."""
    n = len(wires)
    count = {c: wires.count(c) for c in COLOURS}
    last = wires[-1]

    def last_of(colour):
        return max(i for i, c in enumerate(wires) if c == colour) + 1

    if n == 3:
        if count["red"] == 0:
            return 2
        if last == "white":
            return 3
        if count["blue"] > 1:
            return last_of("blue")
        return 3
    if n == 4:
        if count["red"] > 1 and serial_odd:
            return last_of("red")
        if last == "yellow" and count["red"] == 0:
            return 1
        if count["blue"] == 1:
            return 1
        if count["yellow"] > 1:
            return 4
        return 2
    if n == 5:
        if last == "black" and serial_odd:
            return 4
        if count["red"] == 1 and count["yellow"] > 1:
            return 1
        if count["black"] == 0:
            return 2
        return 1
    if count["yellow"] == 0 and serial_odd:
        return 3
    if count["yellow"] == 1 and count["white"] > 1:
        return 4
    if count["red"] == 0:
        return 6
    return 4


@check("wires")
def wires():
    """Every panel the module can deal has exactly one answer, and it exists."""
    for n in range(3, 7):
        for wires_ in itertools.product(COLOURS, repeat=n):
            for odd in (False, True):
                answer = wires_answer(list(wires_), odd)
                if not 1 <= answer <= n:
                    fail("wires", "%d wires %s odd=%s -> wire %d"
                                  % (n, wires_, odd, answer))
                    return


# --- The Button -------------------------------------------------------------
@check("button")
def button():
    """The strip digits have to actually turn up on the clock.

    A held button is released when the countdown shows the strip's digit
    somewhere in MM:SS. If a digit never appears in the round's clock range the
    module cannot be solved at all.
    """
    src = P.read(os.path.join("include", "game.h"))
    seconds = P.re.search(r"round_seconds\s*=\s*([\d.]+)f", src)
    if not seconds:
        fail("button", "could not find round_seconds in game.h")
        return
    total = int(float(seconds.group(1)))
    reachable = set()
    for t in range(total + 1):
        reachable.update("%02d%02d" % (t // 60, t % 60))
    for digit in "145":
        if digit not in reachable:
            fail("button", "digit %s never appears on a %ds clock"
                           % (digit, total))


# --- Memory -----------------------------------------------------------------
@check("memory")
def memory():
    """Every stage rule names a button that is on the module."""
    rng = random.Random(4)
    for _ in range(20000):
        labels = [1, 2, 3, 4]
        history = []          # (position 1-based, label)
        for stage in range(1, 6):
            rng.shuffle(labels)
            display = rng.randrange(1, 5)
            pos_of = {lab: i + 1 for i, lab in enumerate(labels)}
            rule = {
                1: {1: ("p", 2), 2: ("p", 2), 3: ("p", 3), 4: ("p", 4)},
                2: {1: ("l", 4), 2: ("s", 1), 3: ("p", 1), 4: ("s", 1)},
                3: {1: ("sl", 2), 2: ("sl", 1), 3: ("p", 3), 4: ("l", 4)},
                4: {1: ("s", 1), 2: ("p", 1), 3: ("s", 2), 4: ("s", 2)},
                5: {1: ("sl", 1), 2: ("sl", 2), 3: ("sl", 4), 4: ("sl", 3)},
            }[stage][display]
            kind, arg = rule
            if kind == "p":
                press = arg
            elif kind == "l":
                press = pos_of[arg]
            elif kind == "s":
                press = history[arg - 1][0]
            else:
                press = pos_of[history[arg - 1][1]]
            if not 1 <= press <= 4:
                fail("memory", "stage %d display %d names button %d"
                               % (stage, display, press))
                return
            history.append((press, labels[press - 1]))


# --- Who's on First ---------------------------------------------------------
@check("whos-on-first")
def whos_on_first():
    """Each priority list must cover every label, or a deal has no answer."""
    display, priority = P.wof_source()
    labels = set(priority)
    for label, order in priority.items():
        if sorted(order) != sorted(labels):
            fail("whos-on-first", "%s's list is not a permutation of the "
                                  "labels: %s" % (label, order))
    for word, position in display.items():
        if position not in P.WOF_POSITIONS:
            fail("whos-on-first", "%s names %r, not a button" % (word, position))
    # Six labels are dealt out of fourteen; the first list entry that is on the
    # module always exists because the lists are permutations, checked above.


# --- Simon Says -------------------------------------------------------------
@check("simon")
def simon():
    table = P.simon_source()
    for (vowel, strikes), row in table.items():
        if sorted(row) != ["Blue", "Green", "Red", "Yellow"]:
            fail("simon", "vowel=%s strikes=%d does not cover four colours: %s"
                          % (vowel, strikes, row))
    # Three strikes detonates, so only 0-2 need a column.
    for vowel in (True, False):
        for strikes in range(3):
            if (vowel, strikes) not in table:
                fail("simon", "no column for vowel=%s strikes=%d"
                              % (vowel, strikes))


# --- Morse Code -------------------------------------------------------------
@check("morse")
def morse():
    """The manual leans on every word starting with a different letter."""
    alphabet, freqs = P.morse_source()
    words = list(freqs)
    firsts = [w[0] for w in words]
    if len(set(firsts)) != len(firsts):
        fail("morse", "two words share a first letter: %s" % sorted(firsts))
    codes = [alphabet[c.upper()] for c in set("".join(words))]
    if len(set(codes)) != len(codes):
        fail("morse", "two letters share a Morse code")
    if len(set(freqs.values())) != len(freqs):
        fail("morse", "two words share a frequency")


# --- Complicated Wires ------------------------------------------------------
@check("complicated-wires")
def complicated_wires():
    """init() re-deals until a wire needs cutting -- that must be reachable."""
    table = P.complicated_wires_source()
    for batteries in range(5):
        for lit_frk in (False, True):
            for even in (False, True):
                for vowel in (False, True):
                    cuttable = [k for k, action in table.items()
                                if resolve_cwire(action, batteries, lit_frk,
                                                 even, vowel)]
                    if not cuttable:
                        fail("complicated-wires",
                             "no wire is ever cut with batteries=%d frk=%s "
                             "even=%s vowel=%s"
                             % (batteries, lit_frk, even, vowel))


def resolve_cwire(action, batteries, lit_frk, serial_even, serial_vowel):
    if action == "Cut the wire":
        return True
    if action == "Do not cut the wire":
        return False
    if "2 or more batteries" in action:
        return batteries >= 2
    if "FRK" in action:
        return lit_frk
    if "last digit of the serial is even" in action:
        return serial_even
    if "contains a vowel" in action:
        return serial_vowel
    raise AssertionError("unknown action %r" % action)


# --- Wire Sequences ---------------------------------------------------------
@check("wire-sequences")
def wire_sequences():
    """Two things have to line up for the Expert's count to mean anything.

    (1) The terminal the Defuser reads off the module must be the terminal the
        cut rule uses.
    (2) Occurrences must be numbered in the order the Defuser reads the wires,
        which is top to bottom within a panel and panel by panel after that.
    """
    src = P.cpp("wire_sequences_puzzle.cpp")
    table = P.wire_sequences_source()
    for colour, entries in table.items():
        if len(entries) != 9:
            fail("wire-sequences", "%s has %d occurrence rows, expected 9"
                                   % (colour, len(entries)))
        if all(not e for e in entries):
            fail("wire-sequences", "%s never cuts anything" % colour)

    # (1) The cut rule reads Wire::connection, so the wire has to be drawn
    # ending at that terminal -- not at the one lettered by the row it starts in.
    rule_uses_connection = "w.connection == 0 ? conn_a" in src
    drawn_to_connection = "const Vector2 b = wire_end(w.connection);" in src
    if rule_uses_connection and not drawn_to_connection:
        fail("wire-sequences",
             "the cut rule reads Wire::connection but the wire is not drawn to "
             "that terminal, so the letter the Defuser reads out is not the "
             "one the module grades against")

    # (2) Occurrences must be handed out top to bottom. The rows a panel's
    # wires land on are shuffled, so numbering them as they are dealt puts the
    # count out of step with the order the panel is read aloud.
    numbering = src[src.index("for (int r = 0; r < rows_per_panel; ++r) {"):]
    numbering = numbering[:numbering.index("panel_ = 0;")]
    if "w.occurrence = ++seen[ci];" not in numbering:
        fail("wire-sequences",
             "occurrences are no longer numbered in a top-to-bottom pass over "
             "the panel, so they can fall out of step with the reading order")


# --- Mazes ------------------------------------------------------------------
WALL_N, WALL_E, WALL_S, WALL_W = 1, 2, 4, 8


@check("mazes")
def mazes():
    grids, markers = P.mazes_source()
    n = 6
    for index, walls in enumerate(grids, start=1):
        # Walls have to agree from both sides or a move is legal one way only.
        for r in range(n):
            for c in range(n):
                if c + 1 < n:
                    east = bool(walls[r][c] & WALL_E)
                    west = bool(walls[r][c + 1] & WALL_W)
                    if east != west:
                        fail("mazes", "maze %d: the wall between (%d,%d) and "
                                      "(%d,%d) exists on one side only"
                                      % (index, r, c, r, c + 1))
                if r + 1 < n:
                    south = bool(walls[r][c] & WALL_S)
                    north = bool(walls[r + 1][c] & WALL_N)
                    if south != north:
                        fail("mazes", "maze %d: the wall between (%d,%d) and "
                                      "(%d,%d) exists on one side only"
                                      % (index, r, c, r + 1, c))

        # Every start the module can roll has to reach every target.
        seen = {(0, 0)}
        queue = [(0, 0)]
        while queue:
            r, c = queue.pop()
            for wall, dr, dc in ((WALL_N, -1, 0), (WALL_E, 0, 1),
                                 (WALL_S, 1, 0), (WALL_W, 0, -1)):
                if walls[r][c] & wall:
                    continue
                nr, nc = r + dr, c + dc
                if 0 <= nr < n and 0 <= nc < n and (nr, nc) not in seen:
                    seen.add((nr, nc))
                    queue.append((nr, nc))
        if len(seen) != n * n:
            fail("mazes", "maze %d reaches only %d of %d cells"
                          % (index, len(seen), n * n))

    pairs = [frozenset(m) for m in markers]
    if len(set(pairs)) != len(pairs):
        fail("mazes", "two mazes share the same pair of marker circles")
    for index, pair in enumerate(markers, start=1):
        if len(set(pair)) != 2:
            fail("mazes", "maze %d puts both markers on one cell" % index)


# --- Fold-Out ---------------------------------------------------------------
FOLD_DIRS = [(-1, 0), (1, 0), (0, 1), (0, -1)]   # north, south, east, west


def roll(faces, direction):
    b, t, n, s, e, w = faces
    if direction == 0:      # north
        return (n, s, t, b, e, w)
    if direction == 1:      # south
        return (s, n, b, t, e, w)
    if direction == 2:      # east
        return (e, w, n, s, t, b)
    return (w, e, n, s, b, t)   # west


def face_map(cells):
    """Roll a die over the net; {cell: face} or None when it is not a net."""
    start = min(cells)
    arrangement = {start: (0, 1, 2, 3, 4, 5)}
    order = [start]
    seen = {start}
    while order:
        cur = order.pop()
        for d, (dr, dc) in enumerate(FOLD_DIRS):
            nxt = (cur[0] + dr, cur[1] + dc)
            if nxt not in cells or nxt in seen:
                continue
            arrangement[nxt] = roll(arrangement[cur], d)
            seen.add(nxt)
            order.append(nxt)
    if seen != set(cells):
        return None
    faces = {cell: arr[0] for cell, arr in arrangement.items()}
    if len(set(faces.values())) != 6:
        return None
    return faces


def rule_pairs(cells, faces):
    """Pairs the manual's rules (a) and (b) settle, as a set of face pairs."""
    found = set()
    for (r, c) in cells:
        for dr, dc in ((0, 1), (1, 0)):     # rule (a): three in a line
            if (r + dr, c + dc) in cells and (r + 2 * dr, c + 2 * dc) in cells:
                found.add(faces[(r, c)] // 2)
        for d1 in range(4):                 # rule (b): a four-square staircase
            p1 = (r + FOLD_DIRS[d1][0], c + FOLD_DIRS[d1][1])
            if p1 not in cells:
                continue
            for d2 in range(4):
                if (d1 < 2) == (d2 < 2):
                    continue
                p2 = (p1[0] + FOLD_DIRS[d2][0], p1[1] + FOLD_DIRS[d2][1])
                if p2 not in cells:
                    continue
                for d3 in range(4):
                    if (d2 < 2) == (d3 < 2):
                        continue
                    p3 = (p2[0] + FOLD_DIRS[d3][0], p2[1] + FOLD_DIRS[d3][1])
                    if p3 not in cells or p3 == (r, c):
                        continue
                    found.add(faces[(r, c)] // 2)
    return found


@check("fold-out")
def fold_out():
    """Every net folds, and the manual's three rules always finish the job."""
    nets = P.fold_out_nets_source()
    if len(nets) != len(set(tuple(sorted(n)) for n in nets)):
        fail("fold-out", "the catalogue repeats a net")

    # The manual's rule (b) is a claim about cubes, not about this catalogue:
    # check it on its own before leaning on it.
    for shape in (
            [(0, 0), (1, 0), (1, 1), (2, 1)],     # a plain staircase
            [(0, 1), (0, 0), (1, 0), (1, -1)]):
        cells = set(shape)
        padded = cells | _complete_to_net(cells)
        faces = face_map(padded) if len(padded) == 6 else None
        if faces and faces[shape[0]] // 2 != faces[shape[3]] // 2:
            fail("fold-out", "rule (b) is false for the staircase %s" % shape)

    for index, net in enumerate(nets):
        for turns in range(4):
            for flip in (False, True):
                cells = []
                for r, c in net:
                    if flip:
                        c = -c
                    for _ in range(turns):
                        r, c = c, -r
                    cells.append((r, c))
                min_r = min(r for r, _ in cells)
                min_c = min(c for _, c in cells)
                cells = {(r - min_r, c - min_c) for r, c in cells}
                span_r = max(r for r, _ in cells) + 1
                span_c = max(c for _, c in cells) + 1
                if span_r > 4 or span_c > 4:
                    fail("fold-out", "net %d turned %d flip=%s spans %dx%d, "
                                     "too big for the lattice"
                                     % (index, turns, flip, span_r, span_c))
                    continue
                faces = face_map(cells)
                if faces is None:
                    fail("fold-out", "net %d turned %d flip=%s does not fold "
                                     "into a cube" % (index, turns, flip))
                    continue
                pairs = rule_pairs(cells, faces)
                if len(pairs) < 2:
                    fail("fold-out", "net %d turned %d flip=%s: the manual's "
                                     "rules settle only %d of the three pairs"
                                     % (index, turns, flip, len(pairs)))


def _complete_to_net(cells):
    """Grow a four-cell run into some six-cell net, for the rule (b) check."""
    for extra in itertools.combinations(
            [(r + dr, c + dc) for r, c in cells for dr, dc in FOLD_DIRS
             if (r + dr, c + dc) not in cells], 2):
        if face_map(cells | set(extra)):
            return set(extra)
    return set()


# --- Knobs ------------------------------------------------------------------
@check("knobs")
def knobs():
    """Sixteen patterns, all different, or the same lights want two answers."""
    entries = P.knobs_source()
    patterns = [p for p, _ in entries]
    if len(set(patterns)) != len(patterns):
        clashes = [p for p in set(patterns) if patterns.count(p) > 1]
        fail("knobs", "repeated light patterns: %s"
                      % ["%03X" % p for p in clashes])
    for pattern, direction in entries:
        if pattern >> 12:
            fail("knobs", "pattern %03X has bits past the twelve lights"
                          % pattern)
        if direction not in ("Up", "Down", "Left", "Right"):
            fail("knobs", "%03X points %r" % (pattern, direction))


# --- Pipeworks --------------------------------------------------------------
@check("pipeworks")
def pipeworks():
    """The scramble must always be undoable, or the grid has no solution.

    init() lays a route, checks commit_passes(), then scrambles. That is only
    safe while rivetted tiles -- the ones pinned at their solution angle -- are
    skipped by the scramble, since every other tile can simply be turned back.
    """
    src = P.cpp("pipeworks_puzzle.cpp")
    scramble = src[src.index("// ---- scramble"):src.index("if (!ok) {")]
    if "if (tiles_[i].rivet) continue;" not in scramble:
        fail("pipeworks", "the scramble no longer skips rivetted tiles, so the "
                          "solution orientation may be unreachable")
    if "if (!commit_passes()) continue;" not in src:
        fail("pipeworks", "the generator no longer proves the route passes "
                          "before scrambling it")
    if "if (!on_path[i]) off_path[off_count++] = i;" not in src:
        fail("pipeworks", "burst seals are no longer kept off the route")

    shapes = P.pipeworks_shapes_source()
    if len(shapes) != 10:
        fail("pipeworks", "the tile vocabulary has %d entries, expected 10"
                          % len(shapes))
    for name in shapes:
        if len(set(name)) != len(name) or not set(name) <= set("NESW"):
            fail("pipeworks", "%r is not a set of distinct edges" % name)
        if len(name) not in (2, 3):
            fail("pipeworks", "%r is a dead end or a cross" % name)
    # Every rotation of a listed shape has to be listed too, or turning a tile
    # produces something the Expert has no name for.
    def rotate(name):
        step = {"N": "E", "E": "S", "S": "W", "W": "N"}
        return "".join(sorted((step[ch] for ch in name),
                              key="NESW".index))
    canon = {"".join(sorted(s, key="NESW".index)) for s in shapes}
    for name in canon:
        if rotate(name) not in canon:
            fail("pipeworks", "%s turns into %s, which is not in the "
                              "vocabulary" % (name, rotate(name)))


# --- Tape Reader ------------------------------------------------------------
@check("tape-reader")
def tape_reader():
    """A three-key entry has to be able to express every answer."""
    src = P.cpp("tape_reader_puzzle.cpp")
    header = P.read(os.path.join("include", "puzzles", "tape_reader_puzzle.h"))
    digits = P.re.search(r"max_digits\s*=\s*(\d+)", header)
    value_max = P.re.search(r"value_max\s*=\s*(\d+)", src)
    if not digits or not value_max:
        fail("tape-reader", "could not find max_digits / value_max")
        return
    if 10 ** int(digits.group(1)) - 1 < int(value_max.group(1)):
        fail("tape-reader", "the keypad takes %s digits but answers run to %s"
                            % (digits.group(1), value_max.group(1)))
    if "if (value < 0 || value > value_max)" not in src:
        fail("tape-reader", "the generator no longer keeps every intermediate "
                            "value inside the range the manual promises")
    if "if (sub_ok && alt == result) omission_safe = false;" not in src:
        fail("tape-reader", "the generator no longer rejects tapes where "
                            "dropping a glyph lands on the same answer")


# --- Needy modules ----------------------------------------------------------
@check("needy")
def needy():
    """A needy module has to give the pair long enough to answer."""
    src = P.read(os.path.join("src", "puzzles", "needy_puzzle.cpp"))
    active = float(P.re.search(r"active_seconds = ([\d.]+)f", src).group(1))
    dormant_min = float(
        P.re.search(r"dormant_min_seconds = ([\d.]+)f", src).group(1))
    if active < 15.0:
        fail("needy", "an awake needy module allows only %.0fs" % active)
    if dormant_min < active / 4.0:
        fail("needy", "needy modules wake up again after only %.0fs"
                      % dormant_min)

    cap = P.cpp("capacitor_puzzle.cpp")
    charge = float(P.re.search(r"charge_per_second = ([\d.]+)f", cap).group(1))
    drain = float(
        P.re.search(r"discharge_per_second = ([\d.]+)f", cap).group(1))
    if 1.0 / charge < 30.0:
        fail("needy", "the capacitor fills in %.0fs, too fast to leave alone"
                      % (1.0 / charge))
    if drain <= charge:
        fail("needy", "holding the capacitor's lever does not drain it")


def main(argv):
    wanted = set(argv[1:])
    ran = 0
    for name, fn in CHECKS:
        if wanted and name not in wanted:
            continue
        ran += 1
        before = len(FAILURES)
        fn()
        print("%-22s %s" % (name, "ok" if len(FAILURES) == before else "FAIL"))
    if wanted and ran == 0:
        print("no such check: %s" % ", ".join(sorted(wanted)))
        print("available: %s" % ", ".join(n for n, _ in CHECKS))
        return 2
    print()
    for line in FAILURES:
        print("  " + line)
    print("%d check%s, %d problem%s"
          % (ran, "" if ran == 1 else "s",
             len(FAILURES), "" if len(FAILURES) == 1 else "s"))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
