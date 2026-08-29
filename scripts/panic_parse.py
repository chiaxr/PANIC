#!/usr/bin/env python3
"""Parsers that pull the rule tables out of the manual and the C++ sources.

Every module that keys off a printed table stores that table twice: once in
``manual/index.html`` for the Expert and once in ``src/puzzles/*.cpp`` for the
game. The two MUST agree, and nothing in the build enforces it. This module
turns both copies into plain Python so ``verify_manual.py`` can diff them.

Nothing here knows any rules -- it only reads. Keep it that way: a parser that
"fixes up" what it reads cannot catch a divergence.
"""

import html
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANUAL = os.path.join(ROOT, "manual", "index.html")


# --- generic helpers --------------------------------------------------------
def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
        return fh.read()


def manual_html():
    return read(os.path.join("manual", "index.html"))


def section(doc, ident):
    """The raw HTML of one <section id="...">."""
    start = doc.find('<section id="%s">' % ident)
    if start < 0:
        raise KeyError("no section %r in the manual" % ident)
    end = doc.find("</section>", start)
    return doc[start:end]


def strip_tags(fragment):
    """Tag-free text with every run of whitespace (nbsp included) collapsed."""
    text = html.unescape(re.sub(r"<[^>]+>", "", fragment))
    return re.sub(r"\s+", " ", text.replace("\u00a0", " ")).strip()


def rows(fragment, table_index=0):
    """One HTML table as a list of lists of cell text (header row included)."""
    tables = re.findall(r"<table[^>]*>(.*?)</table>", fragment, re.S)
    body = tables[table_index]
    out = []
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", body, re.S):
        cells = re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", tr, re.S)
        out.append([strip_tags(c) for c in cells])
    return out


def all_tables(fragment):
    return [rows(fragment, i)
            for i in range(len(re.findall(r"<table[^>]*>", fragment)))]


def cpp(name):
    return read(os.path.join("src", "puzzles", name))


# A C++ character literal, including the escaped forms. Braces inside one are
# data, not structure -- the Keypads table is full of '{' and '}' -- so every
# brace scan below blanks these out first.
CHAR_LITERAL = re.compile(r"'(?:\\.|[^'\\])'")


def _blank_char_literals(text):
    return CHAR_LITERAL.sub(lambda m: " " * len(m.group(0)), text)


def _unescape_char(literal):
    """'x' or '\\x' -> the single character it denotes."""
    body = literal[1:-1]
    return body[1] if body.startswith("\\") else body


def braced_lists(text):
    """Every top-level {...} group inside a brace-initialised C++ array body."""
    scan = _blank_char_literals(text)
    out = []
    depth = 0
    start = None
    for i, ch in enumerate(scan):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                out.append(text[start:i])
    return out


def array_body(src, declaration):
    """The text between the outermost braces of `declaration ... = { ... };`."""
    at = src.index(declaration)
    scan = _blank_char_literals(src)
    open_brace = scan.index("{", at)
    depth = 0
    for i in range(open_brace, len(scan)):
        if scan[i] == "{":
            depth += 1
        elif scan[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_brace + 1:i]
    raise ValueError("unterminated array %r" % declaration)


# --- Keypads ----------------------------------------------------------------
def keypad_columns_manual():
    """Six columns of seven symbols, read out of the manual's row-wise table."""
    table = rows(section(manual_html(), "keypads"))
    body = table[1:]
    return [[row[c] for row in body] for c in range(6)]


def keypad_columns_source():
    body = array_body(cpp("keypads_puzzle.cpp"),
                      "constexpr char columns[column_count][column_len]")
    cols = []
    for group in braced_lists(body):
        cols.append([_unescape_char(c) for c in CHAR_LITERAL.findall(group)])
    return cols


# --- Passwords --------------------------------------------------------------
def password_words_manual():
    frag = section(manual_html(), "passwords")
    listing = re.search(r'<div class="wordlist">(.*?)</div>', frag, re.S).group(1)
    return [w for w in (x.strip() for x in re.split(r"<br\s*/?>", listing)) if w]


def password_words_source():
    body = array_body(cpp("passwords_puzzle.cpp"),
                      "const char* const words[word_count]")
    return re.findall(r'"([a-z]+)"', body)


# --- Who's on First ---------------------------------------------------------
def wof_manual():
    frag = section(manual_html(), "whos-on-first")
    display_rows, priority_rows = all_tables(frag)[0][1:], all_tables(frag)[1][1:]
    display = {r[0]: r[1] for r in display_rows}
    priority = {r[0]: [w.strip() for w in r[1].split(",")] for r in priority_rows}
    return display, priority


# Button index -> the manual's name for that position. button_rect() in
# whos_on_first_puzzle.cpp lays the six out as two columns of three.
WOF_POSITIONS = ["top left", "top right", "middle left", "middle right",
                 "bottom left", "bottom right"]


def wof_source():
    src = cpp("whos_on_first_puzzle.cpp")
    body = array_body(src, "const DisplayEntry display_table[display_count]")
    display = {w: WOF_POSITIONS[int(p)]
               for w, p in re.findall(r'\{"(\w+)",\s*(\d+)\}', body)}

    body = array_body(src, "const PriorityEntry priority_table[label_count]")
    priority = {}
    for group in braced_lists(body):
        names = re.findall(r'"([A-Z]+)"', group)
        priority[names[0]] = names[1:]
    return display, priority


# --- Simon Says -------------------------------------------------------------
SIMON_ORDER = ["Red", "Blue", "Green", "Yellow"]   # SimonColor enum order


def simon_manual():
    """{(has_vowel, strikes): {flashed: pressed}} from the manual's two tables."""
    frag = section(manual_html(), "simon")
    out = {}
    for vowel, table in zip((True, False), all_tables(frag)):
        for row in table[1:]:
            flashed = row[0]
            for strikes in range(3):
                out.setdefault((vowel, strikes), {})[flashed] = row[1 + strikes]
    return out


def simon_source():
    body = array_body(cpp("simon_puzzle.cpp"),
                      "constexpr SC color_map[mapping_rows]")
    entries = re.findall(r"SC::SIMON_(\w+)", body)
    out = {}
    for row in range(6):
        vowel = row < 3
        strikes = row % 3
        cells = entries[row * 4:(row + 1) * 4]
        out[(vowel, strikes)] = {
            SIMON_ORDER[i]: c.capitalize() for i, c in enumerate(cells)}
    return out


# --- Morse Code -------------------------------------------------------------
def morse_manual():
    frag = section(manual_html(), "morse")
    alpha_table, freq_table = all_tables(frag)[0], all_tables(frag)[1]
    alphabet = {}
    for row in alpha_table:
        for cell in row:
            m = re.match(r"^([A-Z])\s+([.−-]+)$", cell.strip())
            if m:
                alphabet[m.group(1)] = m.group(2).replace("−", "-")
    words = {}
    for row in freq_table[1:]:
        words[row[0]] = row[1]
    return alphabet, words


def morse_source():
    src = cpp("morse_puzzle.cpp")
    words = re.findall(r'"([a-z]{5})"',
                       array_body(src, "const char* const words[word_count]"))
    codes = re.findall(r'"([.-]+)"',
                       array_body(src, "const char* const morse[26]"))
    alphabet = {chr(ord("A") + i): c for i, c in enumerate(codes)}
    base = int(re.search(r"base_frequency_khz = (\d+)", src).group(1))
    step = int(re.search(r"frequency_step_khz = (\d+)", src).group(1))
    freqs = {}
    for i, w in enumerate(words):
        khz = base + i * step
        freqs[w] = "%d.%03d MHz" % (khz // 1000, khz % 1000)
    return alphabet, freqs


# --- Complicated Wires ------------------------------------------------------
CWIRE_ACTIONS = {
    "CUT": "Cut the wire",
    "DONT": "Do not cut the wire",
    "SERIAL": "Cut only if the last digit of the serial is even",
    "VOWEL": "Cut only if the serial contains a vowel",
    "FRK": "Cut only if the bomb has a lit FRK indicator",
    "BATTERY": "Cut only if the bomb has 2 or more batteries",
}


def complicated_wires_manual():
    """[(red, blue, star, led)] -> action text, in the manual's row order."""
    table = rows(section(manual_html(), "complicated-wires"))
    out = {}
    for row in table[1:]:
        key = tuple(cell.strip() == "yes" for cell in row[:4])
        out[key] = row[4]
    return out


def complicated_wires_source():
    body = array_body(cpp("complicated_wires_puzzle.cpp"),
                      "constexpr Instruction wire_table[16]")
    names = re.findall(r"Instruction::(\w+)", body)
    out = {}
    for index, name in enumerate(names):
        key = (bool(index & 1), bool(index & 2), bool(index & 4),
               bool(index & 8))
        out[key] = CWIRE_ACTIONS[name]
    return out


# --- Wire Sequences ---------------------------------------------------------
def wire_sequences_manual():
    frag = section(manual_html(), "wire-sequences")
    out = {}
    for colour, table in zip(("Red", "Blue", "Black"), all_tables(frag)):
        out[colour] = [
            tuple(sorted(t.strip() for t in row[1].split(","))) for row in table[1:]]
    return out


def wire_sequences_source():
    body = array_body(cpp("wire_sequences_puzzle.cpp"),
                      "constexpr int sequence_table[3][max_occurrences]")
    out = {}
    for colour, group in zip(("Red", "Blue", "Black"), braced_lists(body)):
        entries = []
        for cell in group.split(","):
            terms = re.findall(r"conn_(\w)", cell)
            entries.append(tuple(sorted(t.upper() for t in terms)))
        out[colour] = entries
    return out


# --- Mazes ------------------------------------------------------------------
# Wall bits, matching maze_puzzle.cpp: 1 N, 2 E, 4 S, 8 W.
MAZE_CELL = 30
MAZE_PAD = 6


def mazes_manual():
    """[(walls[6][6], marker_pair)] read back out of the manual's SVGs."""
    frag = section(manual_html(), "mazes")
    out = []
    for svg in re.findall(r"<svg[^>]*>(.*?)</svg>", frag, re.S):
        walls = [[0] * 6 for _ in range(6)]
        for x1, y1, x2, y2 in re.findall(
                r'<line x1="(\d+)" y1="(\d+)" x2="(\d+)" y2="(\d+)"/>', svg):
            x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
            if y1 == y2:                       # horizontal: a N/S wall
                col = (min(x1, x2) - MAZE_PAD) // MAZE_CELL
                row = (y1 - MAZE_PAD) // MAZE_CELL
                if 0 <= col < 6:
                    if 0 <= row < 6:
                        walls[row][col] |= 1   # north edge of that cell
                    if 0 <= row - 1 < 6:
                        walls[row - 1][col] |= 4
            else:                              # vertical: a W/E wall
                row = (min(y1, y2) - MAZE_PAD) // MAZE_CELL
                col = (x1 - MAZE_PAD) // MAZE_CELL
                if 0 <= row < 6:
                    if 0 <= col < 6:
                        walls[row][col] |= 8   # west edge of that cell
                    if 0 <= col - 1 < 6:
                        walls[row][col - 1] |= 2
        markers = []
        for cx, cy in re.findall(r'<circle cx="(\d+)" cy="(\d+)"[^>]*class="mk"',
                                 svg):
            centre = MAZE_PAD + MAZE_CELL // 2
            markers.append(((int(cy) - centre) // MAZE_CELL,
                            (int(cx) - centre) // MAZE_CELL))
        out.append((walls, markers))
    return out


def mazes_source():
    src = cpp("maze_puzzle.cpp")
    body = array_body(src, "constexpr int mazes[maze_count]")
    grids = []
    for group in braced_lists(body):
        numbers = [int(n) for n in re.findall(r"-?\d+", group)]
        grids.append([numbers[r * 6:(r + 1) * 6] for r in range(6)])
    body = array_body(src, "constexpr MarkerPair markers[maze_count]")
    markers = [tuple(int(n) for n in m)
               for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}",
                                   body)]
    return grids, [[(m[0], m[1]), (m[2], m[3])] for m in markers]


# --- Knobs ------------------------------------------------------------------
def knobs_manual():
    """[(12-bit pattern, direction)] read off the manual's LED grids."""
    frag = section(manual_html(), "knobs")
    out = []
    table = re.search(r"<table[^>]*>(.*?)</table>", frag, re.S).group(1)
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", table, re.S)[1:]:
        leds = re.findall(r'<span class="led (on|off)">', tr)
        pattern = 0
        for i, state in enumerate(leds):
            if state == "on":
                pattern |= 1 << i
        direction = strip_tags(re.findall(r"<td[^>]*>(.*?)</td>", tr, re.S)[1])
        out.append((pattern, direction))
    return out


def knobs_source():
    body = array_body(cpp("knobs_puzzle.cpp"),
                      "constexpr PatternEntry patterns[pattern_count]")
    out = []
    for value, name in re.findall(r"\{(0x[0-9A-Fa-f]+),\s*KP::KNOB_(\w+)\}", body):
        out.append((int(value, 16), name.capitalize()))
    return out


# --- Memory -----------------------------------------------------------------
def memory_manual():
    """[stage][display] -> instruction text, stages and displays 1-based."""
    frag = section(manual_html(), "memory")
    out = {}
    for stage, table in enumerate(all_tables(frag), start=1):
        for row in table[1:]:
            out[(stage, int(row[0]))] = " ".join(row[1].split())
    return out


# --- Fold-Out ---------------------------------------------------------------
def fold_out_targets_manual():
    return rows(section(manual_html(), "fold-out"))[1:]


def fold_out_nets_source():
    body = array_body(cpp("fold_out_puzzle.cpp"),
                      "constexpr NetShape nets[net_count]")
    nets = []
    for group in braced_lists(body):
        pairs = re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", group)
        nets.append([(int(r), int(c)) for r, c in pairs])
    return nets


# --- Pipeworks --------------------------------------------------------------
def pipeworks_vocab_manual():
    frag = section(manual_html(), "pipeworks")
    figs = re.search(r'<div class="figs">(.*?)</div>', frag, re.S).group(1)
    return re.findall(r"<figcaption>([NESW]+)</figcaption>", figs)


def pipeworks_shapes_source():
    body = array_body(cpp("pipeworks_puzzle.cpp"),
                      "constexpr uint8_t filler_shapes[filler_count]")
    return re.findall(r"//\s*([NESW]+)\s*$", body, re.M)


def pipeworks_rules_manual():
    frag = section(manual_html(), "pipeworks")
    table = [r for r in all_tables(frag) if r and r[0][0] == "Plate"][0]
    return {row[0]: row[1] for row in table[1:]}


# --- Star Chart -------------------------------------------------------------
def star_chart_tables_manual():
    frag = section(manual_html(), "star-chart")
    identify, press = None, None
    for table in all_tables(frag):
        if table[0][:2] == ["Constellation", "Stars"]:
            identify = {r[0]: (int(r[1]), int(r[2]), r[3]) for r in table[1:]}
        elif table[0][0] == "Constellation" and "Odd" in table[0][1]:
            press = {r[0]: (r[1], r[2]) for r in table[1:]}
    return identify, press


def star_chart_figures_manual():
    frag = section(manual_html(), "star-chart")
    return [f.strip() for f in
            re.findall(r"(<figure><figcaption>The .*?</figure>)", frag, re.S)]
