#!/usr/bin/env python3
"""Diff every printed rule table in manual/index.html against the C++ it documents.

The Expert plays from the manual and the game plays from the tables in
src/puzzles/. Nothing in the build ties the two together, so a one-symbol edit
on either side silently makes a module unsolvable. This script is that tie.

    python3 scripts/verify_manual.py            # check everything
    python3 scripts/verify_manual.py keypads simon

Exit status is 0 when every check passes, 1 otherwise.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panic_parse as P     # noqa: E402

ROOT = P.ROOT
FAILURES = []
CHECKS = []


def check(name):
    def wrap(fn):
        CHECKS.append((name, fn))
        return fn
    return wrap


def fail(module, detail):
    FAILURES.append("%s: %s" % (module, detail))


def compare(module, what, manual, source):
    if manual == source:
        return True
    fail(module, "%s differs\n    manual: %r\n    source: %r"
                 % (what, manual, source))
    return False


# --- table-for-table diffs --------------------------------------------------
@check("keypads")
def keypads():
    compare("keypads", "symbol columns",
            P.keypad_columns_manual(), P.keypad_columns_source())


@check("passwords")
def passwords():
    compare("passwords", "word list",
            P.password_words_manual(), P.password_words_source())


@check("whos-on-first")
def whos_on_first():
    m_display, m_priority = P.wof_manual()
    s_display, s_priority = P.wof_source()
    compare("whos-on-first", "display word table", m_display, s_display)
    compare("whos-on-first", "priority lists", m_priority, s_priority)


@check("simon")
def simon():
    compare("simon", "colour mapping", P.simon_manual(), P.simon_source())


@check("morse")
def morse():
    m_alpha, m_words = P.morse_manual()
    s_alpha, s_words = P.morse_source()
    compare("morse", "alphabet", m_alpha, s_alpha)
    compare("morse", "word frequencies", m_words, s_words)


@check("complicated-wires")
def complicated_wires():
    compare("complicated-wires", "cut table",
            P.complicated_wires_manual(), P.complicated_wires_source())


@check("wire-sequences")
def wire_sequences():
    compare("wire-sequences", "occurrence tables",
            P.wire_sequences_manual(), P.wire_sequences_source())


@check("mazes")
def mazes():
    manual = P.mazes_manual()
    grids, markers = P.mazes_source()
    if len(manual) != len(grids):
        fail("mazes", "manual draws %d mazes, the source holds %d"
                      % (len(manual), len(grids)))
        return
    for i, (walls, marks) in enumerate(manual):
        compare("mazes", "maze %d walls" % (i + 1), walls, grids[i])
        compare("mazes", "maze %d markers" % (i + 1),
                sorted(marks), sorted(markers[i]))


@check("knobs")
def knobs():
    compare("knobs", "light patterns", P.knobs_manual(), P.knobs_source())


@check("pipeworks")
def pipeworks():
    compare("pipeworks", "tile vocabulary",
            P.pipeworks_vocab_manual(), P.pipeworks_shapes_source())

    src = P.cpp("pipeworks_puzzle.cpp")
    plates = P.pipeworks_rules_manual()
    expected = {
        "A": "pass through exactly one tee",
        "B": "pass through exactly two tees",
        "C": "enter the outlet from the tile directly above it",
        "D": "pass through every rivetted tile",
    }
    compare("pipeworks", "rule plates", plates, expected)
    # rule_code() has to letter the plates the same way round.
    if "tee_target_ == 1 ? 'A' : 'B'" not in src:
        fail("pipeworks", "rule_code() no longer letters one tee 'A'")
    frag = P.section(P.manual_html(), "pipeworks")
    if "remainder on division by three" not in frag:
        fail("pipeworks", "the outlet rule is missing from the manual")
    if "% outlet_count" not in src:
        fail("pipeworks", "the outlet rule is missing from the source")


@check("memory")
def memory():
    # The source is a switch rather than a table, so it is transcribed here in
    # the manual's own phrasing. Keep this in step with solve_correct_button().
    src = P.cpp("memory_puzzle.cpp")
    position = "the button in <strong>position %d</strong>"
    same_pos = "the button in the <strong>same position</strong> as stage %d"
    label = "the button labelled <strong>%d</strong>"
    same_label = "the button with the <strong>same label</strong> as stage %d"
    transcription = {
        (1, 1): position % 2, (1, 2): position % 2,
        (1, 3): position % 3, (1, 4): position % 4,
        (2, 1): label % 4,    (2, 2): same_pos % 1,
        (2, 3): position % 1, (2, 4): same_pos % 1,
        (3, 1): same_label % 2, (3, 2): same_label % 1,
        (3, 3): position % 3,   (3, 4): label % 4,
        (4, 1): same_pos % 1, (4, 2): position % 1,
        (4, 3): same_pos % 2, (4, 4): same_pos % 2,
        (5, 1): same_label % 1, (5, 2): same_label % 2,
        (5, 3): same_label % 4, (5, 4): same_label % 3,
    }
    clean = {k: P.strip_tags(v) for k, v in transcription.items()}
    compare("memory", "stage tables", P.memory_manual(), clean)

    # Guard the transcription itself: these are the switch arms it stands for.
    for fragment in ("case 1: return by_label(pressed_label_[1]);",
                     "case 3: return by_position(pressed_position_[1]);",
                     "case 3: return by_label(pressed_label_[3]);"):
        if fragment not in src:
            fail("memory", "the switch changed shape near %r -- re-check the "
                           "transcription in this script" % fragment)


@check("button")
def button():
    src = P.cpp("button_puzzle.cpp")
    frag = P.section(P.manual_html(), "button")
    strips = {row[0]: row[1] for row in P.rows(frag)[1:]}
    expected = {
        "Blue": "a 4 in any position",
        "White": "a 1 in any position",
        "Yellow": "a 5 in any position",
        "Any other colour": "a 1 in any position",
    }
    compare("button", "strip release digits", strips, expected)
    for colour, digit in (("BLUE", 4), ("YELLOW", 5), ("WHITE", 1),
                          ("OTHER", 1)):
        line = "case StripColor::STRIP_%s:" % colour
        at = src.index(line)
        if "return %d;" % digit not in src[at:at + 120]:
            fail("button", "strip %s no longer releases on %d" % (colour, digit))

    # The seven rules, in the order the manual prints them.
    rules = [P.strip_tags(li) for li in
             P.re.findall(r"<li>(.*?)</li>", frag, P.re.S)]
    wanted = [
        ("blue", "ABORT", "hold"),
        ("more than 1 battery", "DETONATE", "tap"),
        ("white", "CAR", "hold"),
        ("more than 2 batteries", "FRK", "tap"),
        ("yellow", None, "hold"),
        ("red", "HOLD", "tap"),
        (None, None, "hold"),
    ]
    if len(rules) != len(wanted):
        fail("button", "the manual prints %d rules, this script knows %d"
                       % (len(rules), len(wanted)))
        return
    for i, (a, b, action) in enumerate(wanted):
        for needle in (a, b, action):
            if needle and needle not in rules[i]:
                fail("button", "rule %d no longer mentions %r: %r"
                               % (i + 1, needle, rules[i]))


@check("wires")
def wires():
    """The four rule lists, checked phrase by phrase against solve_correct_wire."""
    frag = P.section(P.manual_html(), "wires")
    lists = P.re.findall(r'<ol class="rules">(.*?)</ol>', frag, P.re.S)
    printed = [[P.strip_tags(li) for li in
                P.re.findall(r"<li>(.*?)</li>", block, P.re.S)]
               for block in lists]
    expected = [
        ["no red wires.*cut the second",
         "last wire is white.*cut the last",
         "more than one blue.*cut the last blue",
         "cut the last"],
        ["more than one red.*odd.*cut the last red",
         "last wire is yellow.*no red.*cut the first",
         "exactly one blue.*cut the first",
         "more than one yellow.*cut the last",
         "cut the second"],
        ["last wire is black.*odd.*cut the fourth",
         "exactly one red.*more than one yellow.*cut the first",
         "no black wires.*cut the second",
         "cut the first"],
        ["no yellow wires.*odd.*cut the third",
         "exactly one yellow.*more than one white.*cut the fourth",
         "no red wires.*cut the last",
         "cut the fourth"],
    ]
    if len(printed) != 4:
        fail("wires", "expected four rule lists, found %d" % len(printed))
        return
    for count, (rules, patterns) in enumerate(zip(printed, expected), start=3):
        if len(rules) != len(patterns):
            fail("wires", "%d-wire list has %d rules, expected %d"
                          % (count, len(rules), len(patterns)))
            continue
        for rule, pattern in zip(rules, patterns):
            if not P.re.search(pattern, rule, P.re.I | P.re.S):
                fail("wires", "%d-wire rule %r does not read as %r"
                              % (count, rule, pattern))


@check("fold-out")
def fold_out():
    table = P.fold_out_targets_manual()
    expected = [
        ["the bomb has any lit indicator", "the anchored one"],
        ["otherwise, the serial's last digit is odd", "the star"],
        ["otherwise", "the circle"],
    ]
    compare("fold-out", "target table", table, expected)
    src = P.cpp("fold_out_puzzle.cpp")
    for needle in ("attrs.lit_indicator_count() > 0) return anchor_",
                   "Symbol::SYM_STAR", "Symbol::SYM_CIRCLE",
                   "answer_ = partner_of(keyed_cell(attrs))"):
        if needle not in src:
            fail("fold-out", "keyed_cell() no longer contains %r" % needle)


@check("tape-reader")
def tape_reader():
    frag = P.section(P.manual_html(), "tape-reader")
    table = {row[1]: row[2] for row in P.rows(frag)[1:]}
    expected = {
        "Cells": "Add the number of batteries.",
        "Fork": "If any indicator is lit, subtract 3. Otherwise add 3.",
        "Halve": "Halve the value, rounding down.",
        "Double": "Double the value.",
        "Stamp": "Replace the value with the last digit of the serial.",
        "Lift": "Add 7.",
        "Drop": "Subtract 4.",
        "Stash": ("Copy the value into the spare register. The value itself "
                  "does not change."),
        "Recall": "Replace the value with whatever is in the spare register.",
    }
    compare("tape-reader", "instruction table", table, expected)

    src = P.cpp("tape_reader_puzzle.cpp")
    for needle in ("value += attrs.battery_count;",
                   "value += attrs.lit_indicator_count() > 0 ? -3 : 3;",
                   "value /= 2;", "value *= 2;",
                   "value += 7;", "value -= 4;",
                   "stash = value; stashed = true;",
                   "reverse_ = attrs.serial_has_vowel();"):
        if needle not in src:
            fail("tape-reader", "the interpreter no longer contains %r" % needle)
    if "read the tape" not in P.strip_tags(frag):
        fail("tape-reader", "the reading-direction rule is missing")


@check("star-chart")
def star_chart():
    identify, press = P.star_chart_tables_manual()
    if identify is None or press is None:
        fail("star-chart", "the identify/press tables are missing")
        return
    if set(identify) != set(press):
        fail("star-chart", "identify and press tables name different charts")

    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import star_chart_catalogue as SC
    by_name = {c["name"]: c for c in SC.load_catalogue()}
    if set(by_name) != set(identify):
        fail("star-chart", "the manual lists %s, the catalogue holds %s"
                           % (sorted(identify), sorted(by_name)))
        return
    for name, (count, bright, _desc) in identify.items():
        entry = by_name[name]
        if len(entry["pts"]) != count:
            fail("star-chart", "%s: manual says %d stars, catalogue has %d"
                               % (name, count, len(entry["pts"])))
        got = sum(1 for t in entry["tiers"] if t == SC.BRIGHT)
        if got != bright:
            fail("star-chart", "%s: manual says %d bright, catalogue has %d"
                               % (name, bright, got))

    drawn = P.star_chart_figures_manual()
    generated = SC.figures_markup()
    compare("star-chart", "constellation figures", drawn, generated)


@check("star-chart-geometry")
def star_chart_geometry():
    """Defer to the catalogue script's own geometric checks."""
    result = subprocess.run(
        [sys.executable, os.path.join(ROOT, "scripts",
                                      "star_chart_catalogue.py"), "verify"],
        capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        fail("star-chart-geometry",
             "star_chart_catalogue.py verify failed:\n" + result.stdout[-2000:])


@check("color-match")
def color_match():
    """The manual's patches must be the colours the module actually draws."""
    keys, grid = P.color_match_manual()
    src_keys, columns, hexes = P.color_match_source()
    if not compare("color-match", "key words", keys, src_keys):
        return
    expected = [[hexes[col[k]] for col in columns] for k in range(len(keys))]
    compare("color-match", "patch grid", grid, expected)


@check("color-match-palette")
def color_match_palette():
    """Defer to the palette script's own colour-space checks."""
    result = subprocess.run(
        [sys.executable, os.path.join(ROOT, "scripts", "color_palette.py"),
         "verify"],
        capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        fail("color-match-palette",
             "color_palette.py verify failed:\n" + result.stdout[-2000:])


@check("registry")
def registry():
    """Every module in the manual's contents is in the bomb's template pool."""
    doc = P.manual_html()
    toc = P.re.findall(r'<li><a href="#([\w-]+)">(.*?)</a></li>', doc)
    modules = [(ident, P.strip_tags(title)) for ident, title in toc
               if ident not in ("before", "widgets")]

    bomb = P.read(os.path.join("src", "bomb.cpp"))
    pool = P.re.findall(r'"([^"]+)"',
                        P.array_body(bomb, "module_templates"))
    registered = set(P.re.findall(r'reg\.add\("([^"]+)"', bomb))

    if set(pool) - registered:
        fail("registry", "in module_templates but never registered: %s"
                         % sorted(set(pool) - registered))
    if registered - set(pool):
        fail("registry", "registered but not in module_templates: %s"
                         % sorted(registered - set(pool)))
    if len(pool) != len(set(pool)):
        fail("registry", "module_templates has duplicates")
    if len(modules) != len(pool):
        fail("registry", "the manual documents %d modules, the pool holds %d:"
                         "\n    manual: %s\n    pool:   %s"
                         % (len(modules), len(pool),
                            [t for _, t in modules], pool))


def main(argv):
    wanted = set(argv[1:])
    ran = 0
    for name, fn in CHECKS:
        if wanted and name not in wanted:
            continue
        ran += 1
        before = len(FAILURES)
        fn()
        status = "ok" if len(FAILURES) == before else "FAIL"
        print("%-22s %s" % (name, status))
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
