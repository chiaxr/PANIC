#!/usr/bin/env python3
"""Search and check the Colour Match module's palette and colour wheel.

The sixteen colours in src/puzzles/color_match_puzzle.cpp are not hand-picked.
The module asks the Expert to *describe* an unnamed patch and the Defuser to
find it again on a colour wheel, so the palette has to sit in a narrow band: far
enough apart that a careful description resolves them, close enough that one
word ("red") never does.

The wheel is built in **Oklab**, not CIELAB: it handles saturated blues far
better, which matters once the colours are vivid. Angle is Oklab hue; radius
runs from a neutral grey at the centre out to that hue's strongest colour --
the gamut cusp's lightness, with chroma capped so the rim is rich rather than
fluorescent. Lightness therefore varies with hue, which is what saturated
palettes need: a vivid yellow is light and a vivid blue is dark. An earlier
design pinned every colour to one lightness so that screen distance would equal
perceptual distance; that bought a clean grading rule at the cost of a washed-
out palette, and cost colour-blind players almost everything, since dichromats
separate colours largely by lightness.

Two things have to stay apart, and optimising either alone quietly destroys the
other:

  * how different the colours look, measured as CIEDE2000 and as Oklab distance
  * how far apart they sit on the drawn wheel, which is the Defuser's aim slack

    python3 scripts/color_palette.py verify
        Check the committed table: cached sRGB matches the wheel coordinates,
        every entry is in gamut, the pairs are far enough apart in colour and on
        the wheel, the three columns are mutually deranged. Exit 1 on failure.

    python3 scripts/color_palette.py search [--count N] [--cap C] [--arc LO HI]
        Pack N colours onto the wheel and print the C++ table.

    python3 scripts/color_palette.py cvd
        The accessibility audit: how close the palette's closest pair comes
        under simulated protanopia, deuteranopia and tritanopia.

    python3 scripts/color_palette.py swatches
        Emit the manual's chip styles and the key-word grid.

    python3 scripts/color_palette.py cusp
        Recompute and print the C++ gamut-cusp table the wheel is built on.
"""

import argparse
import math
import os
import random
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = "src/puzzles/color_match_puzzle.cpp"

# Mirrors of the C++ constants. verify() reads the real values back out of the
# source and fails if these drift, so they are a convenience, not a duplicate.
NEUTRAL_L = 0.62        # color_match_puzzle.cpp: neutral_lightness
CHROMA_CAP = 0.20       # color_match_puzzle.cpp: chroma_cap
PALETTE_COUNT = 16      # color_match_puzzle.h: palette_count
COLUMN_COUNT = 3        # color_match_puzzle.h: column_count
CUSP_STEP = 2.0         # color_match_puzzle.cpp: cusp_step_degrees
DISC_RADIUS = 148.0     # color_match_puzzle.cpp: disc_radius

# Acceptance thresholds.
MIN_DE_00 = 9.0         # closest pair, CIEDE2000 -- describably different
MIN_WHEEL_GAP = 0.28    # closest pair on the wheel, as a fraction of radius
ROUNDING = 5e-4         # the table is printed to four decimals

COLOUR_WORDS = ("RED", "ROSE", "BLUE", "GREEN", "JADE", "GOLD", "AMBER",
                "TEAL", "RUST", "SAGE", "PLUM", "LIME", "CYAN", "OLIVE",
                "IVORY", "CORAL", "SLATE", "OCHRE", "MAUVE", "PEARL",
                "SEPIA", "UMBER", "TAUPE", "INDIGO", "VIOLET", "SCARLET",
                "KHAKI", "AZURE", "CRIMSON", "MAGENTA", "SALMON")


# --- sRGB -------------------------------------------------------------------
def _encode(v):
    """Linear light -> sRGB, unclipped so gamut tests can see the overflow."""
    if v <= 0.0031308:
        return 12.92 * v
    return 1.055 * (abs(v) ** (1.0 / 2.4)) * (1 if v > 0 else -1) - 0.055


def _decode(v):
    if v <= 0.04045:
        return v / 12.92
    return ((v + 0.055) / 1.055) ** 2.4


def lin_to_rgb8(lin):
    return tuple(int(round(255.0 * min(1.0, max(0.0, _encode(v))))) for v in lin)


def rgb8_to_lin(rgb):
    return [_decode(c / 255.0) for c in rgb]


def hex_of(rgb):
    return "#%02X%02X%02X" % tuple(rgb)


# --- Oklab ------------------------------------------------------------------
def lin_to_oklab(r, g, b):
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l_, m_, s_ = (math.copysign(abs(v) ** (1.0 / 3.0), v) for v in (l, m, s))
    return (0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
            1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
            0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_)


def oklab_to_lin(lightness, a, b):
    l_ = lightness + 0.3963377774 * a + 0.2158037573 * b
    m_ = lightness - 0.1055613458 * a - 0.0638541728 * b
    s_ = lightness - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    return (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)


def oklch_to_lin(lightness, chroma, hue):
    rad = math.radians(hue)
    return oklab_to_lin(lightness, chroma * math.cos(rad), chroma * math.sin(rad))


def in_gamut(lin, slack=1e-4):
    return all(-slack <= v <= 1.0 + slack for v in lin)


def max_chroma(lightness, hue):
    """Largest in-gamut Oklab chroma at this lightness and hue."""
    lo, hi = 0.0, 0.45
    for _ in range(30):
        mid = 0.5 * (lo + hi)
        if in_gamut(oklch_to_lin(lightness, mid, hue)):
            lo = mid
        else:
            hi = mid
    return lo


def compute_cusp(hue):
    """(lightness, chroma) of the most chromatic in-gamut colour at this hue."""
    best = (0.0, 0.0)
    for i in range(101):
        lightness = i / 100.0
        chroma = max_chroma(lightness, hue)
        if chroma > best[1]:
            best = (lightness, chroma)
    return best


def computed_cusp_table():
    return [compute_cusp(i * CUSP_STEP)
            for i in range(int(round(360.0 / CUSP_STEP)))]


# --- the wheel --------------------------------------------------------------
def cusp_at(table, hue):
    """The cusp between table samples, interpolated exactly as the C++ does."""
    hue %= 360.0
    span = float(len(table))
    pos = hue / CUSP_STEP
    lo = int(pos) % len(table)
    hi = (lo + 1) % len(table)
    f = pos - int(pos)
    (l0, c0), (l1, c1) = table[lo], table[hi]
    return l0 + (l1 - l0) * f, c0 + (c1 - c0) * f


def surface(table, hue, radius, cap=CHROMA_CAP, neutral=NEUTRAL_L):
    """A point on the wheel, as Oklch.

    Lightness ramps from the neutral centre to the cusp's lightness; chroma
    ramps from zero to the cusp's chroma, capped. Reducing chroma at a fixed
    lightness never leaves the gamut, but the lightness ramp can overshoot near
    the cusp, so the result is clamped to what that lightness can hold.
    """
    cusp_l, cusp_c = cusp_at(table, hue)
    lightness = neutral + (cusp_l - neutral) * radius
    chroma = radius * min(cusp_c, cap)
    return lightness, min(chroma, max_chroma(lightness, hue)), hue


def surface_rgb8(table, hue, radius, cap=CHROMA_CAP):
    return lin_to_rgb8(oklch_to_lin(*surface(table, hue, radius, cap)))


def wheel_xy(hue, radius):
    rad = math.radians(hue)
    return radius * math.cos(rad), radius * math.sin(rad)


def wheel_gap(a, b):
    ax, ay = wheel_xy(*a)
    bx, by = wheel_xy(*b)
    return math.hypot(ax - bx, ay - by)


# --- CIELAB, for the distinctness audit -------------------------------------
WHITE = (0.95047, 1.00000, 1.08883)


def lin_to_lab(r, g, b):
    x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b
    y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b
    z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b

    def f(t):
        t = max(t, 0.0)
        return t ** (1.0 / 3.0) if t > 0.008856 else 7.787 * t + 16.0 / 116.0

    fx, fy, fz = f(x / WHITE[0]), f(y / WHITE[1]), f(z / WHITE[2])
    return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)


def rgb8_to_lab(rgb):
    return lin_to_lab(*rgb8_to_lin(rgb))


def delta_e00(lab1, lab2):
    """CIEDE2000, the full formula (Sharma et al. 2005)."""
    l1, a1, b1 = lab1
    l2, a2, b2 = lab2
    c1, c2 = math.hypot(a1, b1), math.hypot(a2, b2)
    cbar = 0.5 * (c1 + c2)
    g = 0.5 * (1.0 - math.sqrt(cbar ** 7 / (cbar ** 7 + 25.0 ** 7))) if cbar else 0.0
    a1p, a2p = (1.0 + g) * a1, (1.0 + g) * a2
    c1p, c2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360.0 if (a1p or b1) else 0.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360.0 if (a2p or b2) else 0.0

    dlp, dcp = l2 - l1, c2p - c1p
    if c1p * c2p == 0.0:
        dhp = 0.0
    else:
        dhp = h2p - h1p
        if dhp > 180.0:
            dhp -= 360.0
        elif dhp < -180.0:
            dhp += 360.0
    dbig = 2.0 * math.sqrt(c1p * c2p) * math.sin(math.radians(dhp) / 2.0)

    lbar, cbarp = 0.5 * (l1 + l2), 0.5 * (c1p + c2p)
    if c1p * c2p == 0.0:
        hbarp = h1p + h2p
    elif abs(h1p - h2p) <= 180.0:
        hbarp = 0.5 * (h1p + h2p)
    elif h1p + h2p < 360.0:
        hbarp = 0.5 * (h1p + h2p + 360.0)
    else:
        hbarp = 0.5 * (h1p + h2p - 360.0)

    t = (1.0
         - 0.17 * math.cos(math.radians(hbarp - 30.0))
         + 0.24 * math.cos(math.radians(2.0 * hbarp))
         + 0.32 * math.cos(math.radians(3.0 * hbarp + 6.0))
         - 0.20 * math.cos(math.radians(4.0 * hbarp - 63.0)))
    sl = 1.0 + (0.015 * (lbar - 50.0) ** 2) / math.sqrt(20.0 + (lbar - 50.0) ** 2)
    sc = 1.0 + 0.045 * cbarp
    sh = 1.0 + 0.015 * cbarp * t
    rt = (-2.0 * math.sqrt(cbarp ** 7 / (cbarp ** 7 + 25.0 ** 7))
          * math.sin(math.radians(60.0 * math.exp(-(((hbarp - 275.0) / 25.0) ** 2)))))
    return math.sqrt((dlp / sl) ** 2 + (dcp / sc) ** 2 + (dbig / sh) ** 2
                     + rt * (dcp / sc) * (dbig / sh))


# Vienot, Brettel & Mollon (1999) dichromat simulation, on linear RGB.
DICHROMAT = {
    "protanopia": ((0.11238, 0.88762, 0.00000),
                   (0.11238, 0.88762, 0.00000),
                   (0.00401, -0.00401, 1.00000)),
    "deuteranopia": ((0.29275, 0.70725, 0.00000),
                     (0.29275, 0.70725, 0.00000),
                     (-0.02234, 0.02234, 1.00000)),
    "tritanopia": ((1.00000, 0.14461, -0.14461),
                   (0.00000, 0.85653, 0.14347),
                   (0.00000, 0.85653, 0.14347)),
}


def simulate(rgb8, kind):
    lin = rgb8_to_lin(rgb8)
    m = DICHROMAT[kind]
    return [min(1.0, max(0.0, sum(m[i][j] * lin[j] for j in range(3))))
            for i in range(3)]


def dichromat_lab(rgb8, kind):
    return lin_to_lab(*simulate(rgb8, kind))


# --- the committed table ----------------------------------------------------
ENTRY = re.compile(r"\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*"
                   r"Color\{\s*(\d+),\s*(\d+),\s*(\d+),\s*255\}\}")
CUSP_ENTRY = re.compile(r"\{\s*([\d.]+)f,\s*([\d.]+)f\}")


def source(path=SRC):
    with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
        return fh.read()


def _float_const(src, name):
    m = re.search(r"constexpr float %s = (-?[\d.]+)f;" % name, src)
    if not m:
        raise KeyError("no constexpr float %s in %s" % (name, SRC))
    return float(m.group(1))


def _block(src, declaration):
    start = src.index(declaration)
    return src[start:src.index("\n};", start)]


def load_palette(src=None):
    """[(hue, radius, (r, g, b)), ...] in table order."""
    src = source() if src is None else src
    return [(float(m.group(1)), float(m.group(2)),
             (int(m.group(3)), int(m.group(4)), int(m.group(5))))
            for m in ENTRY.finditer(_block(src, "constexpr Swatch palette["))]


def load_cusp_table(src=None):
    src = source() if src is None else src
    return [(float(m.group(1)), float(m.group(2)))
            for m in CUSP_ENTRY.finditer(_block(src, "constexpr Cusp cusp_table["))]


def load_columns(src=None):
    src = source() if src is None else src
    return [[int(n) for n in re.findall(r"\d+", row)]
            for row in re.findall(r"\{([^{}]*)\}",
                                  _block(src, "constexpr int column_map["))]


def load_keys(src=None):
    src = source() if src is None else src
    return re.findall(r'"([A-Z]+)"',
                      _block(src, "constexpr const char* key_words["))


def load_constants(src=None):
    src = source() if src is None else src
    return (_float_const(src, "neutral_lightness"),
            _float_const(src, "chroma_cap"))


# --- commands ---------------------------------------------------------------
def cmd_cusp(args):
    table = computed_cusp_table()
    print("constexpr int cusp_count = %d;" % len(table))
    print("constexpr Cusp cusp_table[cusp_count] = {")
    for i in range(0, len(table), 3):
        row = ", ".join("{%.4ff, %.4ff}" % e for e in table[i:i + 3])
        print("    %s," % row)
    print("};")
    return 0


def _views(table, hue, radius, cap):
    rgb = surface_rgb8(table, hue, radius, cap)
    return [lin_to_oklab(*rgb8_to_lin(rgb)),
            lin_to_oklab(*simulate(rgb, "protanopia")),
            lin_to_oklab(*simulate(rgb, "deuteranopia"))]


def _pair_score(pi, pj, ci, cj, targets):
    ok_t, wheel_t, cvd_t = targets
    return min(math.dist(ci[0], cj[0]) / ok_t,
               wheel_gap(pi, pj) / wheel_t,
               math.dist(ci[1], cj[1]) / cvd_t,
               math.dist(ci[2], cj[2]) / cvd_t)


def cmd_search(args):
    """Pack colours onto the wheel, balancing colour against aim slack.

    The score is the worst *fraction of target* across every metric at once --
    colour separation, separation on the drawn wheel, and what survives the two
    common kinds of red-green colour blindness -- so no one of them can be
    traded away to nothing.
    """
    table = computed_cusp_table()
    targets = (args.ok_target, args.wheel_target, args.cvd_target)
    rng = random.Random(args.seed)
    lo, hi = (args.arc if args.arc else (0.0, 360.0))
    wraps = lo > hi

    def sample_hue():
        span = (360.0 - lo + hi) if wraps else (hi - lo)
        return (lo + rng.uniform(0.0, span)) % 360.0

    def in_arc(hue):
        return (hue >= lo or hue <= hi) if wraps else (lo <= hue <= hi)

    best = None
    for _ in range(args.restarts):
        pts = [[sample_hue(), rng.uniform(args.r_min, 1.0)]
               for _ in range(args.count)]
        cols = [_views(table, p[0], p[1], args.cap) for p in pts]

        def worst(i):
            return min(_pair_score(pts[i], pts[j], cols[i], cols[j], targets)
                       for j in range(args.count) if j != i)

        for step in range(args.steps):
            scale = 1.0 - step / float(args.steps)
            i = rng.randrange(args.count)
            before = worst(i)
            old_p, old_c = pts[i], cols[i]
            hue = (old_p[0] + rng.gauss(0.0, 40.0 * scale)) % 360.0
            if not in_arc(hue):
                continue
            pts[i] = [hue, min(1.0, max(args.r_min,
                                        old_p[1] + rng.gauss(0.0, 0.2 * scale)))]
            cols[i] = _views(table, pts[i][0], pts[i][1], args.cap)
            if worst(i) <= before:
                pts[i], cols[i] = old_p, old_c
        score = min(_pair_score(pts[i], pts[j], cols[i], cols[j], targets)
                    for i in range(args.count) for j in range(i + 1, args.count))
        if best is None or score > best[0]:
            best = (score, [tuple(p) for p in pts])

    pts = sorted(best[1], key=lambda p: p[0])
    print("constexpr Swatch palette[palette_count] = {")
    for hue, radius in pts:
        rgb = surface_rgb8(table, hue, radius, args.cap)
        print("    {%8.3ff, %6.4ff, Color{%3d, %3d, %3d, 255}},  // %s"
              % (hue, radius, rgb[0], rgb[1], rgb[2], hex_of(rgb)))
    print("};")
    return 0


def cmd_verify(args):
    failures = []

    def fail(detail):
        failures.append(detail)

    palette = load_palette()
    table = load_cusp_table()
    columns = load_columns()
    keys = load_keys()
    neutral, cap = load_constants()

    if (neutral, cap) != (NEUTRAL_L, CHROMA_CAP):
        fail("this script mirrors neutral %.2f cap %.2f but the source says "
             "%.2f / %.2f" % (NEUTRAL_L, CHROMA_CAP, neutral, cap))
    if len(palette) != PALETTE_COUNT:
        fail("palette has %d entries, expected %d" % (len(palette), PALETTE_COUNT))
    if len(table) != int(round(360.0 / CUSP_STEP)):
        fail("cusp table has %d entries, expected %d"
             % (len(table), int(round(360.0 / CUSP_STEP))))
    if len(keys) != PALETTE_COUNT:
        fail("%d key words for %d colours" % (len(keys), PALETTE_COUNT))
    if len(set(keys)) != len(keys):
        fail("key words repeat: %s" % sorted(keys))
    for word in keys:
        if word in COLOUR_WORDS:
            fail("key word %r names a colour, which leaks the answer" % word)
    if len(set(w[0] for w in keys)) != len(keys):
        fail("two key words share an initial letter, which is a mishearing "
             "waiting to happen: %s" % sorted(keys))

    # The embedded cusp table is what the wheel is drawn from, so it has to be
    # the real gamut cusp and not something that has drifted.
    computed = computed_cusp_table()
    worst_cusp = max(max(abs(a[0] - b[0]), abs(a[1] - b[1]))
                     for a, b in zip(table, computed))
    if worst_cusp > 0.002:
        fail("the embedded cusp table is off the real gamut cusp by %.4f; "
             "regenerate it with `color_palette.py cusp`" % worst_cusp)

    # Every patch the manual prints must be what the wheel actually draws at
    # that point, or the Expert and the Defuser are looking at different
    # colours.
    for i, (hue, radius, rgb) in enumerate(palette):
        if not 0.0 <= radius <= 1.0 + ROUNDING:
            fail("entry %d sits at radius %.4f, off the wheel" % (i, radius))
        lin = oklch_to_lin(*surface(table, hue, radius, cap, neutral))
        if not in_gamut(lin, slack=2e-3):
            fail("entry %d (hue %.2f, radius %.3f) is out of the sRGB gamut"
                 % (i, hue, radius))
        expect = lin_to_rgb8(lin)
        if max(abs(x - y) for x, y in zip(expect, rgb)) > 1:
            fail("entry %d caches %s but hue %.2f at radius %.4f is %s"
                 % (i, hex_of(rgb), hue, radius, hex_of(expect)))

    # Distinct enough to describe apart, and far enough apart on the wheel that
    # finding one is a description rather than a pixel hunt.
    for i in range(len(palette)):
        for j in range(i + 1, len(palette)):
            p, q = palette[i], palette[j]
            e00 = delta_e00(rgb8_to_lab(p[2]), rgb8_to_lab(q[2]))
            if e00 < MIN_DE_00:
                fail("entries %d and %d are only dE00 %.1f apart (min %.1f)"
                     % (i, j, e00, MIN_DE_00))
            gap = wheel_gap(p[:2], q[:2])
            if gap < MIN_WHEEL_GAP:
                fail("entries %d and %d sit %.2f of the radius apart on the "
                     "wheel (min %.2f)" % (i, j, gap, MIN_WHEEL_GAP))

    # The battery column has to matter: reading it wrong must always be wrong.
    if len(columns) != COLUMN_COUNT:
        fail("%d columns, expected %d" % (len(columns), COLUMN_COUNT))
    for c, column in enumerate(columns):
        if sorted(column) != list(range(PALETTE_COUNT)):
            fail("column %d is not a permutation of the palette: %s" % (c, column))
    for c in range(len(columns)):
        for d in range(c + 1, len(columns)):
            shared = [k for k in range(PALETTE_COUNT)
                      if columns[c][k] == columns[d][k]]
            if shared:
                fail("columns %d and %d give key(s) %s the same colour, so "
                     "misreading the battery count would still solve it"
                     % (c, d, [keys[k] for k in shared]))

    for detail in failures:
        print("color-match: %s" % detail)
    print("%-22s %s" % ("color-match palette", "FAIL" if failures else "ok"))
    if not failures:
        e00 = min(delta_e00(rgb8_to_lab(palette[i][2]), rgb8_to_lab(palette[j][2]))
                  for i in range(len(palette)) for j in range(i + 1, len(palette)))
        gap = min(wheel_gap(palette[i][:2], palette[j][:2])
                  for i in range(len(palette)) for j in range(i + 1, len(palette)))
        print("  %d colours on an Oklab wheel, chroma capped at %.2f"
              % (len(palette), cap))
        print("  closest pair: dE00 %.1f, %.2f of the radius apart" % (e00, gap))
        print("  that is +-%.0f px of aim on a %.0f px wheel"
              % (0.5 * gap * DISC_RADIUS, DISC_RADIUS))
    return 1 if failures else 0


def cmd_cvd(args):
    palette = load_palette()
    print("Colour-vision audit. The module is read by colour, so this is a")
    print("measurement, not a pass/fail: it records how much of the palette")
    print("survives dichromacy.\n")
    normal = min((delta_e00(rgb8_to_lab(palette[i][2]), rgb8_to_lab(palette[j][2])),
                  i, j)
                 for i in range(len(palette)) for j in range(i + 1, len(palette)))
    print("  normal vision      closest pair dE00  %5.1f  (entries %d, %d)" % normal)
    for kind in ("protanopia", "deuteranopia", "tritanopia"):
        labs = [dichromat_lab(p[2], kind) for p in palette]
        worst = min((math.dist(labs[i], labs[j]), i, j)
                    for i in range(len(labs)) for j in range(i + 1, len(labs)))
        print("  %-18s closest pair dE*ab %5.1f  (entries %d, %d)"
              % (kind, worst[0], worst[1], worst[2]))
    print("\nPairs under dE*ab 5 are effectively the same colour to that "
          "viewer.\nThe manual says so in the Colour Match section.")
    return 0


def swatch_styles():
    """The .cm1 .. .cmN background rules the manual's chips refer to."""
    return ["        .cm%-2d { background: %s; }" % (i + 1, hex_of(rgb))
            for i, (_h, _r, rgb) in enumerate(load_palette())]


def swatch_rows():
    """The manual's <tr> markup for the key grid, one row per key word."""
    columns = load_columns()
    keys = load_keys()
    rows = []
    for k, word in enumerate(keys):
        cells = "".join(
            '<td class="chipcell"><span class="chip cm%d"></span></td>'
            % (columns[c][k] + 1) for c in range(len(columns)))
        rows.append('                <tr><td class="k">%s</td>%s</tr>'
                    % (word, cells))
    return rows


def cmd_swatches(args):
    for line in swatch_styles():
        print(line)
    print()
    for line in swatch_rows():
        print(line)
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subs = parser.add_subparsers(dest="command")
    subs.add_parser("verify").set_defaults(fn=cmd_verify)
    subs.add_parser("cvd").set_defaults(fn=cmd_cvd)
    subs.add_parser("cusp").set_defaults(fn=cmd_cusp)
    subs.add_parser("swatches").set_defaults(fn=cmd_swatches)
    search = subs.add_parser("search")
    search.add_argument("--count", type=int, default=PALETTE_COUNT)
    search.add_argument("--cap", type=float, default=CHROMA_CAP)
    search.add_argument("--r-min", type=float, default=0.45)
    search.add_argument("--arc", type=float, nargs=2, default=None,
                        metavar=("LO", "HI"),
                        help="confine the colours to a hue arc, in degrees")
    search.add_argument("--ok-target", type=float, default=0.15)
    search.add_argument("--wheel-target", type=float, default=0.45)
    search.add_argument("--cvd-target", type=float, default=0.11)
    search.add_argument("--restarts", type=int, default=45)
    search.add_argument("--steps", type=int, default=9000)
    search.add_argument("--seed", type=int, default=20250829)
    search.set_defaults(fn=cmd_search)
    args = parser.parse_args(argv)
    if not getattr(args, "fn", None):
        parser.print_help()
        return 2
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
