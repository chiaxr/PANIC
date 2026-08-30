#!/usr/bin/env python3
"""The Expert's half of the manual, as a command line.

Every rule here is a transcription of manual/index.html -- never of the C++ --
so running a module through this and through the game is a real cross-check as
well as a way to defuse a bomb quickly while play-testing.

    python3 scripts/expert_solver.py wires red blue red --serial-odd
    python3 scripts/expert_solver.py button --colour yellow --label PRESS
    python3 scripts/expert_solver.py keypads '!' '&' ';' '}'
    python3 scripts/expert_solver.py passwords tbwsa hqeoi ieprl ncsae kslrt
    python3 scripts/expert_solver.py memory 3 --history 2:4,1:3
    python3 scripts/expert_solver.py simon red blue green --vowel --strikes 1
    python3 scripts/expert_solver.py wof BLANK READY GO NOPE YEP WAIT STOP
    python3 scripts/expert_solver.py morse -- '-.../---/-..-/./...'
    python3 scripts/expert_solver.py cwires --batteries 2 rb.l r... ..s.
    python3 scripts/expert_solver.py sequences red:A blue:C red:B
    python3 scripts/expert_solver.py tape 12 lift double drop --batteries 3
    python3 scripts/expert_solver.py knobs 100001 110101
    python3 scripts/expert_solver.py pipeworks --batteries 2 --lit 3
    python3 scripts/expert_solver.py starchart --stars 6 --bright 2

Run any subcommand with -h for its arguments.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panic_parse as P     # noqa: E402


# --- Wires ------------------------------------------------------------------
def cmd_wires(args):
    wires = [w.lower() for w in args.wires]
    n = len(wires)
    if not 3 <= n <= 6:
        raise SystemExit("Wires shows 3 to 6 wires, not %d" % n)
    count = {c: wires.count(c) for c in
             ("red", "blue", "yellow", "white", "black")}
    last = wires[-1]
    odd = args.serial_odd

    def last_of(colour):
        return max(i for i, c in enumerate(wires) if c == colour) + 1

    if n == 3:
        if count["red"] == 0:
            return say(2, "no red wires")
        if last == "white":
            return say(3, "the last wire is white")
        if count["blue"] > 1:
            return say(last_of("blue"), "more than one blue wire")
        return say(3, "none of the earlier rules apply")
    if n == 4:
        if count["red"] > 1 and odd:
            return say(last_of("red"), "more than one red and the serial is odd")
        if last == "yellow" and count["red"] == 0:
            return say(1, "the last wire is yellow and there are no red wires")
        if count["blue"] == 1:
            return say(1, "exactly one blue wire")
        if count["yellow"] > 1:
            return say(4, "more than one yellow wire")
        return say(2, "none of the earlier rules apply")
    if n == 5:
        if last == "black" and odd:
            return say(4, "the last wire is black and the serial is odd")
        if count["red"] == 1 and count["yellow"] > 1:
            return say(1, "one red wire and more than one yellow")
        if count["black"] == 0:
            return say(2, "no black wires")
        return say(1, "none of the earlier rules apply")
    if count["yellow"] == 0 and odd:
        return say(3, "no yellow wires and the serial is odd")
    if count["yellow"] == 1 and count["white"] > 1:
        return say(4, "one yellow wire and more than one white")
    if count["red"] == 0:
        return say(6, "no red wires")
    return say(4, "none of the earlier rules apply")


def say(wire, because):
    print("Cut wire %d  (%s)" % (wire, because))
    return 0


# --- The Button -------------------------------------------------------------
def cmd_button(args):
    colour = args.colour.lower()
    label = args.label.upper()
    lit = {i.upper() for i in args.lit}

    if colour == "blue" and label == "ABORT":
        hold, why = True, "blue and says ABORT"
    elif args.batteries > 1 and label == "DETONATE":
        hold, why = False, "more than 1 battery and says DETONATE"
    elif colour == "white" and "CAR" in lit:
        hold, why = True, "white with a lit CAR indicator"
    elif args.batteries > 2 and "FRK" in lit:
        hold, why = False, "more than 2 batteries and a lit FRK indicator"
    elif colour == "yellow":
        hold, why = True, "the button is yellow"
    elif colour == "red" and label == "HOLD":
        hold, why = False, "red and says HOLD"
    else:
        hold, why = True, "no earlier rule applies"

    if not hold:
        print("Tap it and release immediately  (%s)" % why)
        return 0
    print("Hold it and read the strip  (%s)" % why)
    print("  blue strip   -> release on a 4 anywhere in the timer")
    print("  white strip  -> release on a 1")
    print("  yellow strip -> release on a 5")
    print("  any other    -> release on a 1")
    if args.strip:
        digit = {"blue": 4, "white": 1, "yellow": 5}.get(args.strip.lower(), 1)
        print("Strip is %s: release when the timer shows a %d"
              % (args.strip, digit))
    return 0


# --- Keypads ----------------------------------------------------------------
def cmd_keypads(args):
    columns = P.keypad_columns_manual()
    wanted = set(args.symbols)
    if len(wanted) != 4:
        raise SystemExit("give the four distinct symbols on the keys")
    hits = [(i, col) for i, col in enumerate(columns) if wanted <= set(col)]
    if not hits:
        raise SystemExit("no column holds all four -- re-read the symbols")
    if len(hits) > 1:
        raise SystemExit("columns %s all hold those four; that should be "
                         "impossible, re-run verify_puzzles.py"
                         % [i + 1 for i, _ in hits])
    index, col = hits[0]
    order = [s for s in col if s in wanted]
    print("Column %d" % (index + 1))
    print("Press: %s" % "  ".join(order))
    return 0


# --- Passwords --------------------------------------------------------------
def cmd_passwords(args):
    wheels = [set(w.lower()) for w in args.wheels]
    if len(wheels) != 5:
        raise SystemExit("give five wheels, each as its letters, e.g. abcdef")
    words = P.password_words_manual()
    hits = [w for w in words if all(w[i] in wheels[i] for i in range(5))]
    if not hits:
        print("No word fits -- re-read the wheels.")
        return 1
    for w in hits:
        print(w)
    if len(hits) > 1:
        print("(more than one fits; read out another wheel)")
    return 0


# --- Memory -----------------------------------------------------------------
def cmd_memory(args):
    history = []
    if args.history:
        for item in args.history.split(","):
            pos, lab = item.split(":")
            history.append((int(pos), int(lab)))
    stage = len(history) + 1
    display = args.display
    if not 1 <= stage <= 5:
        raise SystemExit("Memory has five stages; %d recorded already"
                         % len(history))

    rules = {
        1: {1: ("position", 2), 2: ("position", 2),
            3: ("position", 3), 4: ("position", 4)},
        2: {1: ("label", 4), 2: ("same position as stage", 1),
            3: ("position", 1), 4: ("same position as stage", 1)},
        3: {1: ("same label as stage", 2), 2: ("same label as stage", 1),
            3: ("position", 3), 4: ("label", 4)},
        4: {1: ("same position as stage", 1), 2: ("position", 1),
            3: ("same position as stage", 2), 4: ("same position as stage", 2)},
        5: {1: ("same label as stage", 1), 2: ("same label as stage", 2),
            3: ("same label as stage", 4), 4: ("same label as stage", 3)},
    }
    kind, arg = rules[stage][display]
    print("Stage %d, display %d:" % (stage, display))
    if kind == "position":
        print("  press the button in position %d" % arg)
    elif kind == "label":
        print("  press the button labelled %d" % arg)
    elif kind == "same position as stage":
        if len(history) < arg:
            raise SystemExit("stage %d is not in the history yet" % arg)
        print("  press the button in position %d (stage %d's position)"
              % (history[arg - 1][0], arg))
    else:
        if len(history) < arg:
            raise SystemExit("stage %d is not in the history yet" % arg)
        print("  press the button labelled %d (stage %d's label)"
              % (history[arg - 1][1], arg))
    print("Record both the position and the label of whatever gets pressed.")
    return 0


# --- Simon Says -------------------------------------------------------------
def cmd_simon(args):
    table = P.simon_manual()[(args.vowel, args.strikes)]
    presses = []
    for flash in args.flashes:
        key = flash.capitalize()
        if key not in table:
            raise SystemExit("%r is not one of red, blue, green, yellow" % flash)
        presses.append(table[key])
    print("Press: %s" % "  ".join(presses))
    print("(re-read this after any strike -- the column changes)")
    return 0


# --- Who's on First ---------------------------------------------------------
def cmd_wof(args):
    display, priority = P.wof_manual()
    word = args.display.upper()
    if word not in display:
        raise SystemExit("%r is not a display word" % word)
    labels = [b.upper() for b in args.buttons]
    if len(labels) != 6:
        raise SystemExit("give the six button labels, reading order: "
                         "top left, top right, middle left, middle right, "
                         "bottom left, bottom right")
    position = display[word]
    index = P.WOF_POSITIONS.index(position)
    key = labels[index]
    if key not in priority:
        raise SystemExit("%r is not a button label" % key)
    print("%s names the %s button, which reads %s" % (word, position, key))
    for candidate in priority[key]:
        if candidate in labels:
            print("Press %s (%s in the reading order)"
                  % (candidate, P.WOF_POSITIONS[labels.index(candidate)]))
            return 0
    raise SystemExit("no label on the module is in %s's list" % key)


# --- Morse Code -------------------------------------------------------------
def cmd_morse(args):
    alphabet, freqs = P.morse_manual()
    reverse = {code: letter for letter, code in alphabet.items()}
    signal = args.signal.replace("−", "-")
    letters = []
    for token in signal.replace(" ", "/").split("/"):
        if not token:
            continue
        if token in reverse:
            letters.append(reverse[token])
        else:
            letters.append("?")
    word = "".join(letters).lower()
    print("Decoded: %s" % word.upper())
    hits = [w for w in freqs if w.startswith(word[0])] if word else []
    exact = [w for w in freqs if w == word]
    for w in (exact or hits):
        print("  %s -> %s" % (w, freqs[w]))
    if not (exact or hits):
        print("  no word starts with that letter -- re-read the first letter")
        return 1
    return 0


# --- Complicated Wires ------------------------------------------------------
def cmd_cwires(args):
    table = P.complicated_wires_manual()
    lit = {i.upper() for i in args.lit}
    for n, spec in enumerate(args.wires, start=1):
        spec = spec.lower()
        key = ("r" in spec, "b" in spec, "s" in spec, "l" in spec)
        action = table[key]
        if action == "Cut the wire":
            cut, why = True, "the table says cut"
        elif action == "Do not cut the wire":
            cut, why = False, "the table says leave it"
        elif "batteries" in action:
            cut, why = args.batteries >= 2, "batteries = %d" % args.batteries
        elif "FRK" in action:
            cut, why = "FRK" in lit, "lit FRK = %s" % ("FRK" in lit)
        elif "even" in action:
            cut, why = args.serial_digit % 2 == 0, \
                "last digit = %d" % args.serial_digit
        else:
            cut, why = args.serial_vowel, "serial vowel = %s" % args.serial_vowel
        flags = "".join(c for c, on in zip("RBSL", key) if on) or "plain"
        print("wire %d [%-4s] %-12s (%s)"
              % (n, flags, "CUT" if cut else "leave", why))
    return 0


# --- Wire Sequences ---------------------------------------------------------
def cmd_sequences(args):
    table = P.wire_sequences_manual()
    seen = {"Red": 0, "Blue": 0, "Black": 0}
    for spec in args.wires:
        colour, terminal = spec.split(":")
        colour = colour.capitalize()
        terminal = terminal.upper()
        if colour not in seen:
            raise SystemExit("%r is not red, blue or black" % colour)
        seen[colour] += 1
        n = seen[colour]
        if n > len(table[colour]):
            raise SystemExit("%s occurrence %d is past the table" % (colour, n))
        cut = terminal in table[colour][n - 1]
        print("%-5s #%d -> %s : %s"
              % (colour, n, terminal, "CUT" if cut else "leave"))
    return 0


# --- Tape Reader ------------------------------------------------------------
TAPE_OPS = ("cells", "fork", "halve", "double", "stamp", "lift", "drop",
            "stash", "recall")


def cmd_tape(args):
    value = args.start
    spare = None
    glyphs = [g.lower() for g in args.glyphs]
    if args.serial_vowel:
        glyphs = list(reversed(glyphs))
        print("serial has a vowel -> reading the tape backwards")
    for op in glyphs:
        if op not in TAPE_OPS:
            raise SystemExit("%r is not one of %s" % (op, ", ".join(TAPE_OPS)))
        before = value
        if op == "cells":
            value += args.batteries
        elif op == "fork":
            value += -3 if args.lit else 3
        elif op == "halve":
            value //= 2
        elif op == "double":
            value *= 2
        elif op == "stamp":
            value = args.serial_digit
        elif op == "lift":
            value += 7
        elif op == "drop":
            value -= 4
        elif op == "stash":
            spare = value
        else:
            if spare is None:
                raise SystemExit("Recall before any Stash -- re-read the tape")
            value = spare
        print("  %-7s %4d -> %4d" % (op, before, value))
        if not 0 <= value <= 999:
            print("  value left 0-999: that is a misread, start again")
            return 1
    print("Type %d and press ENT" % value)
    return 0


# --- Knobs ------------------------------------------------------------------
def cmd_knobs(args):
    top, bottom = args.top, args.bottom
    if len(top) != 6 or len(bottom) != 6:
        raise SystemExit("give two rows of six, e.g. 100001 110101")
    bits = 0
    for i, ch in enumerate(top + bottom):
        if ch not in "01":
            raise SystemExit("rows are 1 for lit, 0 for unlit")
        if ch == "1":
            bits |= 1 << i
    for pattern, direction in P.knobs_manual():
        if pattern == bits:
            print("Point the knob %s" % direction)
            return 0
    print("No pattern matches %03X -- re-read the lights" % bits)
    return 1


# --- Pipeworks --------------------------------------------------------------
def cmd_pipeworks(args):
    outlet = "XYZ"[(args.batteries + args.lit) % 3]
    print("Target outlet: %s  ((%d batteries + %d lit) mod 3)"
          % (outlet, args.batteries, args.lit))
    print("Always: the flow must reach no burst seal.")
    if args.plate:
        rules = P.pipeworks_rules_manual()
        plate = args.plate.upper()
        if plate not in rules:
            raise SystemExit("plates are %s" % ", ".join(sorted(rules)))
        print("Plate %s: the flow must %s" % (plate, rules[plate]))
    print("Tile names, clockwise from north: %s"
          % ", ".join(P.pipeworks_vocab_manual()))
    return 0


# --- Star Chart -------------------------------------------------------------
def cmd_starchart(args):
    identify, press = P.star_chart_tables_manual()
    hits = [(name, row) for name, row in identify.items()
            if row[0] == args.stars and row[1] == args.bright]
    if not hits:
        print("No constellation has %d stars with %d bright -- did you throw "
              "away the lone dim field stars first?" % (args.stars, args.bright))
        return 1
    for name, (_c, _b, description) in hits:
        print("%s -- %s" % (name, description))
        odd, even = press[name]
        print("    odd serial:  press %s" % odd)
        print("    even serial: press %s" % even)
    return 0


# --- Colour Match -----------------------------------------------------------
# The manual deliberately names none of its patches, so this reads the printed
# hex back and puts words to it. The words are derived, not authored: a second
# hand-kept table of colour names would be one more thing to drift.
HUE_NAMES = (
    (20.0, "a crimson pink -- red with the pink side showing"),
    (45.0, "a strong red, barely orange"),
    (72.0, "orange"),
    (100.0, "gold, an orange-yellow"),
    (125.0, "yellow"),
    (160.0, "green"),
    (185.0, "a blue-green, mint or jade"),
    (215.0, "cyan, green-blue"),
    (240.0, "a sky blue"),
    (280.0, "blue"),
    (320.0, "violet, blue with red in it"),
    (360.0, "a purple-pink"),
)


def describe(hex_colour):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import color_palette as CP
    rgb = [int(hex_colour[i:i + 2], 16) for i in (1, 3, 5)]
    lightness, a, b = CP.lin_to_oklab(*CP.rgb8_to_lin(rgb))
    hue = math.degrees(math.atan2(b, a)) % 360.0
    chroma = math.hypot(a, b)
    name = next(text for limit, text in HUE_NAMES if hue < limit)
    if chroma < 0.09:
        strength = "washed out -- close to the middle of the wheel"
    elif chroma < 0.15:
        strength = "clearly coloured, but not full strength"
    else:
        strength = "full strength, right out at the rim"
    if lightness < 0.55:
        tone = "dark"
    elif lightness < 0.78:
        tone = "mid-toned"
    else:
        tone = "light"
    return name, strength, tone, hue, chroma, lightness


def cmd_colormatch(args):
    keys, grid = P.color_match_manual()
    column = 0 if args.batteries <= 1 else (1 if args.batteries <= 3 else 2)
    key = args.key.upper()
    if key not in keys:
        raise SystemExit("keys are %s" % ", ".join(keys))
    hex_colour = grid[keys.index(key)][column]
    name, strength, tone, hue, chroma, lightness = describe(hex_colour)
    print("%s with %d batteries -> column %d, patch %s"
          % (key, args.batteries, column + 1, hex_colour))
    print("    hue        %s" % name)
    print("    strength   %s" % strength)
    print("    lightness  %s" % tone)
    print("    (Oklab hue %.0f deg, chroma %.3f, lightness %.2f)"
          % (hue, chroma, lightness))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("wires", help="which wire to cut")
    p.add_argument("wires", nargs="+", help="colours, top to bottom")
    p.add_argument("--serial-odd", action="store_true",
                   help="the serial's last digit is odd")
    p.set_defaults(fn=cmd_wires)

    p = sub.add_parser("button", help="tap or hold")
    p.add_argument("--colour", required=True)
    p.add_argument("--label", required=True,
                   help="ABORT, DETONATE, HOLD or PRESS")
    p.add_argument("--batteries", type=int, default=0)
    p.add_argument("--lit", nargs="*", default=[],
                   help="lit indicator labels, e.g. CAR FRK")
    p.add_argument("--strip", help="the strip colour, once it lights")
    p.set_defaults(fn=cmd_button)

    p = sub.add_parser("keypads", help="which column, and the press order")
    p.add_argument("symbols", nargs=4)
    p.set_defaults(fn=cmd_keypads)

    p = sub.add_parser("passwords", help="which word the wheels can spell")
    p.add_argument("wheels", nargs="+", help="six letters per wheel")
    p.set_defaults(fn=cmd_passwords)

    p = sub.add_parser("memory", help="which button this stage wants")
    p.add_argument("display", type=int)
    p.add_argument("--history", default="",
                   help="earlier stages as position:label, comma separated")
    p.set_defaults(fn=cmd_memory)

    p = sub.add_parser("simon", help="which buttons to press")
    p.add_argument("flashes", nargs="+")
    p.add_argument("--vowel", action="store_true",
                   help="the serial contains a vowel")
    p.add_argument("--strikes", type=int, default=0, choices=(0, 1, 2))
    p.set_defaults(fn=cmd_simon)

    p = sub.add_parser("wof", help="which button to press")
    p.add_argument("display")
    p.add_argument("buttons", nargs=6)
    p.set_defaults(fn=cmd_wof)

    p = sub.add_parser("morse", help="decode and tune")
    # A signal starting with a dash looks like an option, so it is passed after
    # a bare `--`: expert_solver.py morse -- '-.../---/-..-/./...'
    p.add_argument("signal", help="letters separated by / or spaces")
    p.set_defaults(fn=cmd_morse)

    p = sub.add_parser("cwires", help="complicated wires: cut or leave")
    p.add_argument("wires", nargs="+",
                   help="one token per wire; letters r b s l for "
                        "red/blue/star/lit, '.' as filler")
    p.add_argument("--batteries", type=int, default=0)
    p.add_argument("--lit", nargs="*", default=[])
    p.add_argument("--serial-digit", type=int, default=0)
    p.add_argument("--serial-vowel", action="store_true")
    p.set_defaults(fn=cmd_cwires)

    p = sub.add_parser("sequences", help="wire sequences, in reading order")
    p.add_argument("wires", nargs="+", help="colour:terminal, e.g. red:A")
    p.set_defaults(fn=cmd_sequences)

    p = sub.add_parser("tape", help="run the tape by hand")
    p.add_argument("start", type=int)
    p.add_argument("glyphs", nargs="+", help=" ".join(TAPE_OPS))
    p.add_argument("--batteries", type=int, default=0)
    p.add_argument("--lit", action="store_true", help="any indicator is lit")
    p.add_argument("--serial-digit", type=int, default=0)
    p.add_argument("--serial-vowel", action="store_true")
    p.set_defaults(fn=cmd_tape)

    p = sub.add_parser("knobs", help="which way to point the knob")
    p.add_argument("top", help="top row, six of 0/1")
    p.add_argument("bottom", help="bottom row, six of 0/1")
    p.set_defaults(fn=cmd_knobs)

    p = sub.add_parser("pipeworks", help="target outlet and the plate rule")
    p.add_argument("--batteries", type=int, default=0)
    p.add_argument("--lit", type=int, default=0, help="lit indicator count")
    p.add_argument("--plate")
    p.set_defaults(fn=cmd_pipeworks)

    p = sub.add_parser("colormatch", help="describe the key's colour")
    p.add_argument("--key", required=True, help="the key word on the module")
    p.add_argument("--batteries", type=int, required=True)
    p.set_defaults(fn=cmd_colormatch)

    p = sub.add_parser("starchart", help="name the constellation")
    p.add_argument("--stars", type=int, required=True)
    p.add_argument("--bright", type=int, required=True)
    p.set_defaults(fn=cmd_starchart)

    args = ap.parse_args(argv[1:])
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
