#!/usr/bin/env python3
"""Search and check the Star Chart module's constellation catalogue.

The catalogue in src/puzzles/star_chart_puzzle.cpp is not hand-placed: every
entry has to hold a set of promises the manual makes to the Defuser, and all of
them are geometric. This script is where those promises live in executable
form.

    python3 scripts/star_chart_catalogue.py verify
        Parse the shipped catalogue and check every promise:
        structure, the DESCRIPTION line, the press rules, and that no two
        constellations sharing a (stars, bright) signature match each other's
        description.

    python3 scripts/star_chart_catalogue.py search [name ...]
        Search fresh coordinates for a constellation and print the C++ literal.

    python3 scripts/star_chart_catalogue.py figures [name ...]
        Print the manual's <figure> markup for a constellation.

Everything here is in catalogue units, the same numbers the C++ stores; the
module multiplies them by unit_px. Rotation is the only transform applied at
run time, and it preserves every distance these checks compare, so a
constellation that passes here is correct at any angle on the bomb.
"""

import math
import random
import re
import sys

# --- constants shared with the module ---------------------------------------
UNIT_PX = 420.0            # star_chart_puzzle.cpp: unit_px
CELL_PX = 72.0             # star_chart_puzzle.cpp: cell_size, one grid square
SRC = "src/puzzles/star_chart_puzzle.cpp"

MIN_SEP = 38.0 / UNIT_PX   # two stars stay countable and separately tappable
NEAR = CELL_PX / UNIT_PX   # a chain link: neighbours inside one grid square
STRUCT_NEAR = 80.0 / UNIT_PX   # "a neighbour within about one grid square"
LONELY = 160.0 / UNIT_PX   # what the module keeps clear around a field star
MAX_R = 180.0 / UNIT_PX    # fits the lattice whatever the rotation
MARGIN = 1.30              # a press rule's winner beats the runner-up by 30%

BRIGHT, MEDIUM, DIM = 0, 1, 2

# --- geometry ---------------------------------------------------------------
def d(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])

def cen(ps):
    return (sum(p[0] for p in ps) / len(ps), sum(p[1] for p in ps) / len(ps))

def nn(pts, i):
    return min(d(pts[i], pts[j]) for j in range(len(pts)) if j != i)

def diameter(pts):
    return max(d(pts[i], pts[j])
               for i in range(len(pts)) for j in range(i + 1, len(pts)))

def brights(tiers):
    return [i for i, t in enumerate(tiers) if t == BRIGHT]

def dline(p, a, b):
    return abs((b[0] - a[0]) * (a[1] - p[1]) -
               (a[0] - p[0]) * (b[1] - a[1])) / d(a, b)

def ang(o, p):
    return math.atan2(p[1] - o[1], p[0] - o[0])

def angle_at(o, p, q):
    v = ang(o, p) - ang(o, q)
    return abs((v + math.pi) % (2 * math.pi) - math.pi)

def argbest(vals, want_max, margin=MARGIN):
    """Winning index, and whether it beats the runner-up by `margin`."""
    order = sorted(range(len(vals)), key=lambda i: vals[i], reverse=want_max)
    w, r = order[0], order[1]
    ok = (vals[r] > 1e-9 and vals[w] / vals[r] >= margin) if want_max else \
         (vals[w] > 1e-9 and vals[r] / vals[w] >= margin)
    return w, ok

def path_order(pts, near=NEAR):
    """Star order along a single chain, or None if these stars are not one."""
    n = len(pts)
    adj = [[j for j in range(n) if j != i and d(pts[i], pts[j]) <= near]
           for i in range(n)]
    if any(len(a) > 2 for a in adj):
        return None
    ends = [i for i in range(n) if len(adj[i]) == 1]
    if len(ends) != 2:
        return None
    order, prev, cur = [ends[0]], -1, ends[0]
    while True:
        nxt = [j for j in adj[cur] if j != prev]
        if not nxt:
            break
        prev, cur = cur, nxt[0]
        order.append(cur)
    return order if len(order) == n else None

def chain_unambiguous(pts, order, ratio=1.20):
    """Non-links stand clear of the links, so the chain reads only one way."""
    links = {frozenset((order[i], order[i + 1])) for i in range(len(order) - 1)}
    link_len = [d(pts[order[i]], pts[order[i + 1]]) for i in range(len(order) - 1)]
    other = [d(pts[i], pts[j])
             for i in range(len(pts)) for j in range(i + 1, len(pts))
             if frozenset((i, j)) not in links]
    return min(other) >= ratio * max(link_len)

# --- structure every constellation must hold --------------------------------
def structure_ok(pts, tiers, lonely_bright=False):
    n = len(pts)
    for i in range(n):
        for j in range(i + 1, n):
            if d(pts[i], pts[j]) < MIN_SEP:
                return False, "stars %d and %d too close" % (i, j)
    b = brights(tiers)
    for i in range(n):
        if lonely_bright and i == b[0]:
            continue
        if nn(pts, i) > STRUCT_NEAR:
            return False, "star %d has no neighbour" % i
    c = cen(pts)
    if max(d(p, c) for p in pts) > MAX_R:
        return False, "too wide for the chart"
    return True, ""

# ---------------------------------------------------------------------------
# The manual's DESCRIPTION column, one predicate per constellation. Each is a
# fact about the stars that a rotation cannot change, and within a shared
# (stars, bright) signature it is what tells the two entries apart.
# ---------------------------------------------------------------------------
def fp_ladle(pts, tiers):        # 5/1  a chain, the bright one at one end
    b = brights(tiers)[0]
    o = path_order(pts)
    return bool(o) and chain_unambiguous(pts, o) and b in (o[0], o[-1])

def fp_lantern(pts, tiers):      # 5/1  the bright one ringed by the other four
    b = brights(tiers)[0]
    others = [i for i in range(len(pts)) if i != b]
    if any(d(pts[i], pts[b]) > STRUCT_NEAR for i in others):
        return False
    a = sorted(ang(pts[b], pts[i]) for i in others)
    gaps = [(a[(k + 1) % len(a)] - a[k]) % (2 * math.pi) for k in range(len(a))]
    return max(gaps) <= math.radians(150)

def fp_hook(pts, tiers):         # 6/1  the bright one sits well apart
    b = brights(tiers)[0]
    rest = max(nn(pts, i) for i in range(len(pts)) if i != b)
    return nn(pts, b) >= 1.5 * rest

def fp_serpent(pts, tiers):      # 6/1  one chain, the bright one splitting 2|3
    b = brights(tiers)[0]
    o = path_order(pts)
    if not o or not chain_unambiguous(pts, o):
        return False
    k = o.index(b)
    return {k, len(o) - 1 - k} == {2, 3}

def fp_anvil(pts, tiers):        # 6/2  the brights are the closest pair
    b = brights(tiers)
    bp = d(pts[b[0]], pts[b[1]])
    other = [d(pts[i], pts[j])
             for i in range(len(pts)) for j in range(i + 1, len(pts))
             if {i, j} != set(b)]
    return min(other) >= 1.25 * bp

def fp_scales(pts, tiers):       # 6/2  the brights are the furthest-apart pair
    b = brights(tiers)
    bp = d(pts[b[0]], pts[b[1]])
    other = [d(pts[i], pts[j])
             for i in range(len(pts)) for j in range(i + 1, len(pts))
             if {i, j} != set(b)]
    if bp < 1.25 * max(other):
        return False
    rest = [i for i in range(len(pts)) if i not in b]
    if max(d(pts[i], pts[j]) for i in rest for j in rest) > 0.55 * bp:
        return False            # the others really are one tight cluster
    a, z = pts[b[0]], pts[b[1]]
    for i in rest:              # ... and it sits between the two bright ones
        t = ((pts[i][0] - a[0]) * (z[0] - a[0]) +
             (pts[i][1] - a[1]) * (z[1] - a[1])) / (bp * bp)
        if not 0.2 <= t <= 0.8:
            return False
        if dline(pts[i], a, z) > 0.28 * bp:
            return False
    return True

def fp_kite(pts, tiers):         # 7/3  the three brights lie in a line
    b = brights(tiers)
    for k in range(3):
        m, a, z = b[k], b[(k + 1) % 3], b[(k + 2) % 3]
        if dline(pts[m], pts[a], pts[z]) <= 0.02 and \
           abs(d(pts[a], pts[m]) - d(pts[z], pts[m])) <= 0.10 * d(pts[a], pts[z]):
            return True
    return False

def fp_talon(pts, tiers):        # 7/3  a close bright pair plus a lone bright
    b = brights(tiers)
    s = sorted(d(pts[b[i]], pts[b[j]]) for i in range(3) for j in range(i + 1, 3))
    return s[1] >= 2.0 * s[0]

def fp_twins(pts, tiers):        # 8/2  brights far apart, each with a dim star
    b = brights(tiers)
    if d(pts[b[0]], pts[b[1]]) < 0.45:
        return False
    for x in b:
        o = sorted((d(pts[k], pts[x]), k) for k in range(len(pts)) if k != x)
        if tiers[o[0][1]] != DIM:
            return False
        if o[1][0] < 1.30 * o[0][0]:
            return False
    return True

def fp_comet(pts, tiers):        # 8/2  the brights head a single chain
    b = brights(tiers)
    o = path_order(pts)
    if not o or not chain_unambiguous(pts, o):
        return False
    return set(o[:2]) == set(b) or set(o[-2:]) == set(b)

def fp_crown(pts, tiers):        # 8/3  a wide, roughly equal-sided bright triangle
    b = brights(tiers)
    s = sorted(d(pts[b[i]], pts[b[j]]) for i in range(3) for j in range(i + 1, 3))
    return s[2] / s[0] <= 1.20 and s[0] >= 0.50 * diameter(pts)

def fp_furnace(pts, tiers):      # 8/3  the three brights huddle in a knot
    b = brights(tiers)
    s = sorted(d(pts[b[i]], pts[b[j]]) for i in range(3) for j in range(i + 1, 3))
    return s[2] <= 0.40 * diameter(pts) and s[2] / s[0] >= 1.50

FINGERPRINT = {
    "The Ladle": fp_ladle,   "The Lantern": fp_lantern,
    "The Hook": fp_hook,     "The Serpent": fp_serpent,
    "The Anvil": fp_anvil,   "The Scales": fp_scales,
    "The Kite": fp_kite,     "The Talon": fp_talon,
    "The Twins": fp_twins,   "The Comet": fp_comet,
    "The Crown": fp_crown,   "The Furnace": fp_furnace,
}

DESCRIPTION = {
    "The Ladle":   "the other four trail away from the bright one in a chain",
    "The Lantern": "the bright one is in the middle, the other four around it",
    "The Hook":    "the bright one sits well apart from all the others",
    "The Serpent": "all six wind in one chain, the bright one inside it",
    "The Anvil":   "the two bright ones are the closest pair on the chart",
    "The Scales":  "the two bright ones are the furthest-apart pair",
    "The Kite":    "the three bright ones lie in a straight line",
    "The Talon":   "two bright ones sit close, the third far off alone",
    "The Twins":   "the brights are far apart, each with a dim star beside it",
    "The Comet":   "the two bright ones head a single chain",
    "The Crown":   "the three bright ones form a wide equal-sided triangle",
    "The Furnace": "the three bright ones huddle in a tight, lopsided knot",
}

# (stars, bright) signature of each entry. Two constellations share every
# signature, so counting alone never identifies one -- the description does.
SIGNATURE = {
    "The Ladle": (5, 1),  "The Lantern": (5, 1),
    "The Hook": (6, 1),   "The Serpent": (6, 1),
    "The Anvil": (6, 2),  "The Scales": (6, 2),
    "The Kite": (7, 3),   "The Talon": (7, 3),
    "The Twins": (8, 2),  "The Comet": (8, 2),
    "The Crown": (8, 3),  "The Furnace": (8, 3),
}

# The Hook is the one entry with a deliberate outlier star.
LONELY_BRIGHT = {"The Hook"}

# ---------------------------------------------------------------------------
# The manual's "which star to press" rules, returning (odd, even, margins_ok).
# ---------------------------------------------------------------------------
def tg_ladle(pts, tiers):        # nearest the bright one | furthest from it
    n, b = len(pts), brights(tiers)[0]
    o, k1 = argbest([d(pts[i], pts[b]) if i != b else 9e9 for i in range(n)], False)
    e, k2 = argbest([d(pts[i], pts[b]) for i in range(n)], True)
    return o, e, k1 and k2

def tg_lantern(pts, tiers):      # nearest the bright one | across from that one
    n, b = len(pts), brights(tiers)[0]
    o, k1 = argbest([d(pts[i], pts[b]) if i != b else 9e9 for i in range(n)], False)
    a = [angle_at(pts[b], pts[i], pts[o]) if i not in (b, o) else -1.0
         for i in range(n)]
    e = max(range(n), key=lambda i: a[i])
    k2 = a[e] >= math.radians(150) and sorted(a)[-2] <= math.radians(115)
    return o, e, k1 and k2

def tg_hook(pts, tiers):         # furthest from the bright one | nearest middle
    n, b = len(pts), brights(tiers)[0]
    c = cen(pts)
    o, k1 = argbest([d(pts[i], pts[b]) for i in range(n)], True)
    e, k2 = argbest([d(pts[i], c) if i != b else 9e9 for i in range(n)], False)
    return o, e, k1 and k2

def tg_serpent(pts, tiers):      # far end of the chain | near end
    b = brights(tiers)[0]
    o = path_order(pts)
    k = o.index(b)
    far, near = (o[-1], o[0]) if len(o) - 1 - k > k else (o[0], o[-1])
    return far, near, True       # structural: three stars one side, two the other

def tg_anvil(pts, tiers):        # dim star furthest from the pair | nearest star
    n, b = len(pts), brights(tiers)
    m = ((pts[b[0]][0] + pts[b[1]][0]) / 2, (pts[b[0]][1] + pts[b[1]][1]) / 2)
    o, k1 = argbest([d(pts[i], m) if tiers[i] == DIM else -1.0 for i in range(n)],
                    True)
    e, k2 = argbest([d(pts[i], m) if i not in b else 9e9 for i in range(n)], False)
    return o, e, k1 and k2

def tg_scales(pts, tiers):       # nearest the midpoint | furthest from it
    n, b = len(pts), brights(tiers)
    m = ((pts[b[0]][0] + pts[b[1]][0]) / 2, (pts[b[0]][1] + pts[b[1]][1]) / 2)
    o, k1 = argbest([d(pts[i], m) if i not in b else 9e9 for i in range(n)], False)
    e, k2 = argbest([d(pts[i], m) if i not in b else -1.0 for i in range(n)], True)
    return o, e, k1 and k2

def tg_kite(pts, tiers):         # middle of the bright line | furthest from it
    n, b = len(pts), brights(tiers)
    mid = min(b, key=lambda m: dline(pts[m], *[pts[x] for x in b if x != m]))
    a, z = [pts[x] for x in b if x != mid]
    e, ok = argbest([dline(pts[i], a, z) for i in range(n)], True)
    return mid, e, ok

def tg_talon(pts, tiers):        # the lone bright one | the star nearest it
    n, b = len(pts), brights(tiers)
    lone = max(b, key=lambda x: min(d(pts[x], pts[y]) for y in b if y != x))
    e, ok = argbest([d(pts[i], pts[lone]) if i not in b else 9e9
                     for i in range(n)], False)
    return lone, e, ok

def tg_twins(pts, tiers):        # companion of the inner bright | the other one
    n, b = len(pts), brights(tiers)
    c = cen(pts)
    comp = [min((k for k in range(n) if k != x), key=lambda k: d(pts[k], pts[x]))
            for x in b]
    dc = [d(pts[x], c) for x in b]
    if max(dc) / min(dc) < MARGIN:
        return 0, 0, False
    near = 0 if dc[0] < dc[1] else 1
    return comp[near], comp[1 - near], True

def tg_comet(pts, tiers):        # far end of the tail | first star of the tail
    b = brights(tiers)
    o = path_order(pts)
    if set(o[:2]) != set(b):
        o = o[::-1]
    return o[-1], o[2], True     # structural: the chain reads only one way

def tg_furnace(pts, tiers):      # nearest the knot | furthest from it
    n, b = len(pts), brights(tiers)
    k = cen([pts[x] for x in b])
    o, k1 = argbest([d(pts[i], k) if i not in b else 9e9 for i in range(n)], False)
    e, k2 = argbest([d(pts[i], k) if i not in b else -1.0 for i in range(n)], True)
    return o, e, k1 and k2

def tg_crown(pts, tiers):        # nearest the triangle's centre | furthest
    n, b = len(pts), brights(tiers)
    c3 = cen([pts[x] for x in b])
    o, k1 = argbest([d(pts[i], c3) if i not in b else 9e9 for i in range(n)], False)
    e, k2 = argbest([d(pts[i], c3) for i in range(n)], True)
    return o, e, k1 and k2

TARGETS = {
    "The Ladle": tg_ladle,   "The Lantern": tg_lantern,
    "The Hook": tg_hook,     "The Serpent": tg_serpent,
    "The Anvil": tg_anvil,   "The Scales": tg_scales,
    "The Kite": tg_kite,     "The Talon": tg_talon,
    "The Twins": tg_twins,   "The Comet": tg_comet,
    "The Crown": tg_crown,   "The Furnace": tg_furnace,
}

# ---------------------------------------------------------------------------
# Builders: rough shapes to sample from. The checks, not these, decide what is
# acceptable -- a builder only has to make the right kind of candidate often
# enough to find one.
# ---------------------------------------------------------------------------
def blob(rng, n, lo, hi, around=(0.0, 0.0)):
    p = []
    for _ in range(n * 300):
        q = (around[0] + rng.uniform(-hi, hi), around[1] + rng.uniform(-hi, hi))
        if all(d(q, r) >= lo for r in p):
            p.append(q)
        if len(p) == n:
            break
    return p if len(p) == n else None

def chain(rng, n, lo, hi, spread):
    p = [(0.0, 0.0)]
    a = rng.uniform(0, 2 * math.pi)
    for _ in range(n - 1):
        a += rng.uniform(-spread, spread)
        s = rng.uniform(lo, hi)
        p.append((p[-1][0] + s * math.cos(a), p[-1][1] + s * math.sin(a)))
    return p

def grow(rng, seed, n, lo, hi):
    """Hang n more stars off an existing group, each within a link of one.

    Growing outwards like this is what keeps a spread-out constellation
    connected: every star it adds has a neighbour inside one grid square, so
    none of them can be mistaken for a field star.
    """
    pts = list(seed)
    for _ in range(n * 400):
        base = pts[rng.randrange(len(pts))]
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(lo, hi)
        q = (base[0] + r * math.cos(a), base[1] + r * math.sin(a))
        if all(d(q, p) >= MIN_SEP for p in pts):
            pts.append(q)
        if len(pts) == len(seed) + n:
            return pts[len(seed):]
    return None

def build(name, rng):
    if name == "The Ladle":
        return chain(rng, 5, 0.145, 0.168, 0.85), \
               [BRIGHT, MEDIUM, MEDIUM, MEDIUM, DIM]

    if name == "The Lantern":
        base = rng.uniform(0, 2 * math.pi)
        pts = [(0.0, 0.0)]
        for k in range(4):
            a = base + 2 * math.pi * k / 4 + rng.uniform(-0.45, 0.45)
            r = rng.uniform(0.105, 0.168)
            pts.append((r * math.cos(a), r * math.sin(a)))
        return pts, [BRIGHT, MEDIUM, MEDIUM, DIM, MEDIUM]

    if name == "The Hook":
        core = blob(rng, 5, 0.115, 0.20)
        if not core:
            return None
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.42, 0.52)
        c = cen(core)
        return [(c[0] + r * math.cos(a), c[1] + r * math.sin(a))] + core, \
               [BRIGHT, MEDIUM, MEDIUM, MEDIUM, DIM, DIM]

    if name == "The Serpent":
        k = rng.choice([2, 3])
        tiers = [MEDIUM, DIM, MEDIUM, MEDIUM, DIM, MEDIUM]
        tiers[k] = BRIGHT
        return chain(rng, 6, 0.130, 0.168, 1.15), tiers

    if name == "The Anvil":
        b0 = (0.0, 0.0)
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.106, 0.112)
        b1 = (s * math.cos(a), s * math.sin(a))
        rest = blob(rng, 4, 0.145, 0.26, cen([b0, b1]))
        if not rest:
            return None
        return [b0, b1] + rest, [BRIGHT, BRIGHT, MEDIUM, DIM, MEDIUM, DIM]

    if name == "The Scales":
        span = rng.uniform(0.50, 0.60)
        a = rng.uniform(0, 2 * math.pi)
        b0 = (-0.5 * span * math.cos(a), -0.5 * span * math.sin(a))
        b1 = (+0.5 * span * math.cos(a), +0.5 * span * math.sin(a))
        rest = blob(rng, 4, 0.100, 0.145)
        if not rest:
            return None
        return [b0, b1] + rest, [BRIGHT, BRIGHT, MEDIUM, MEDIUM, DIM, DIM]

    if name == "The Kite":
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.135, 0.175)
        line = [(k * s * math.cos(a), k * s * math.sin(a)) for k in (-1, 0, 1)]
        rest = blob(rng, 4, 0.13, 0.26)
        if not rest:
            return None
        return line + rest, \
               [BRIGHT, BRIGHT, BRIGHT, MEDIUM, MEDIUM, DIM, DIM]

    if name == "The Talon":
        body = blob(rng, 4, 0.105, 0.185)
        if not body:
            return None
        c = cen(body)
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.098, 0.125)
        p0 = (c[0] + rng.uniform(-0.05, 0.05), c[1] + rng.uniform(-0.05, 0.05))
        p1 = (p0[0] + s * math.cos(a), p0[1] + s * math.sin(a))
        anchor = body[rng.randrange(4)]
        t = rng.uniform(0, 2 * math.pi)
        q = rng.uniform(0.115, 0.165)
        lone = (anchor[0] + q * math.cos(t), anchor[1] + q * math.sin(t))
        return [p0, p1, lone] + body, \
               [BRIGHT, BRIGHT, BRIGHT, MEDIUM, MEDIUM, DIM, MEDIUM]

    if name == "The Twins":
        b0 = (0.0, 0.0)
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.46, 0.56)
        b1 = (r * math.cos(a), r * math.sin(a))

        def companion(b):
            t = rng.uniform(0, 2 * math.pi)
            q = rng.uniform(0.108, 0.135)
            return (b[0] + q * math.cos(t), b[1] + q * math.sin(t))

        rest = blob(rng, 4, 0.14, 0.24, cen([b0, b1]))
        if not rest:
            return None
        return [b0, b1, companion(b0), companion(b1)] + rest, \
               [BRIGHT, BRIGHT, DIM, DIM, MEDIUM, MEDIUM, MEDIUM, MEDIUM]

    if name == "The Comet":
        tail = chain(rng, 7, 0.130, 0.168, 1.05)
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.092, 0.115)
        head = (tail[0][0] + s * math.cos(a), tail[0][1] + s * math.sin(a))
        return [head] + tail, \
               [BRIGHT, BRIGHT, MEDIUM, DIM, MEDIUM, MEDIUM, DIM, MEDIUM]

    if name == "The Crown":
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.17, 0.21)
        tri = [(r * math.cos(a + k * 2 * math.pi / 3),
                r * math.sin(a + k * 2 * math.pi / 3)) for k in range(3)]
        rest = blob(rng, 5, 0.13, 0.26)
        if not rest:
            return None
        return tri + rest, \
               [BRIGHT, BRIGHT, BRIGHT, MEDIUM, MEDIUM, MEDIUM, DIM, DIM]

    if name == "The Furnace":
        knot = None
        for _ in range(200):
            k = blob(rng, 3, 0.095, 0.155)
            if not k:
                continue
            s = sorted(d(k[i], k[j]) for i in range(3) for j in range(i + 1, 3))
            if s[2] / s[0] >= 1.60 and s[2] <= 0.170:
                knot = k
                break
        if not knot:
            return None
        rest = grow(rng, knot, 5, 0.125, 0.168)
        if not rest:
            return None
        return knot + rest, \
               [BRIGHT, BRIGHT, BRIGHT, MEDIUM, MEDIUM, DIM, MEDIUM, DIM]

    raise SystemExit("no builder for %r" % name)

def matches_other(name, pts, tiers, same_signature_only=True):
    """Names of other constellations whose description also fits these stars.

    Only a clash inside the same (stars, bright) signature is a real problem:
    the Defuser counts first, so the Comet's bright pair being the closest pair
    -- the Anvil's description -- can never be mistaken for a six-star Anvil.
    """
    sig = SIGNATURE[name]
    hits = []
    for other, fp in FINGERPRINT.items():
        if other == name:
            continue
        if same_signature_only and SIGNATURE[other] != sig:
            continue
        try:
            if fp(pts, tiers):
                hits.append(other)
        except (IndexError, ValueError, ZeroDivisionError):
            pass    # a description that cannot even be evaluated does not fit
    return hits

def acceptable(name, pts, tiers):
    lonely = name in LONELY_BRIGHT
    ok, _ = structure_ok(pts, tiers, lonely)
    if not ok:
        return None
    if min(d(pts[i], pts[j]) for i in range(len(pts))
           for j in range(i + 1, len(pts))) < 1.05 * MIN_SEP:
        return None                       # keep headroom over the bare minimum
    outlier = brights(tiers)[0] if lonely else -1
    if max(nn(pts, i) for i in range(len(pts)) if i != outlier) > NEAR:
        return None                       # a fresh entry holds the tighter bound
    if not FINGERPRINT[name](pts, tiers):
        return None
    if matches_other(name, pts, tiers):
        return None
    t = TARGETS[name](pts, tiers)
    if not t or not t[2] or t[0] == t[1]:
        return None
    return t

# --- reading the shipped catalogue back out of the C++ ----------------------
def load_catalogue(path=SRC):
    src = open(path).read()
    body = src[src.index("constexpr Catalogue catalogue["):]
    body = body[:body.index("\n};")]
    out = []
    for m in re.finditer(
            r'\{"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*\{(.*?)\}\}', body, re.S):
        name, count, odd, even, stars = m.groups()
        pts, tiers = [], []
        for s in re.finditer(r'\{([-+][\d.]+)f,\s*([-+][\d.]+)f,\s*(\d)\}', stars):
            pts.append((float(s.group(1)), float(s.group(2))))
            tiers.append(int(s.group(3)))
        if len(pts) != int(count):
            raise SystemExit("%s: count says %s, %d stars listed"
                             % (name, count, len(pts)))
        out.append(dict(name=name, pts=pts, tiers=tiers,
                        odd=int(odd), even=int(even)))
    return out

# --- output helpers ---------------------------------------------------------
def cpp_entry(name, pts, tiers, odd, even):
    lines = ['    {"%s", %d, %d, %d, {' % (name, len(pts), odd, even)]
    for (x, y), t in zip(pts, tiers):
        lines.append("        {%+.4ff, %+.4ff, %d}," % (x, y, t))
    lines.append("    }},")
    return "\n".join(lines)

SVG_SCALE = 183.86      # catalogue units -> the manual's 200x200 figure box
SVG_R = {BRIGHT: 7.0, MEDIUM: 4.5, DIM: 2.5}

def svg_figure(name, pts, tiers, odd, even):
    out = ['<figure><figcaption>%s</figcaption>' % name,
           '<svg viewBox="0 0 200 200" width="190" height="190" role="img" '
           'aria-label="%s">' % name]
    for o in (50, 100, 150):
        out.append('<line class="gr" x1="%d" y1="0" x2="%d" y2="200"/>' % (o, o))
        out.append('<line class="gr" x1="0" y1="%d" x2="200" y2="%d"/>' % (o, o))
    for i, ((x, y), t) in enumerate(zip(pts, tiers)):
        cx, cy = 100 + x * SVG_SCALE, 100 + y * SVG_SCALE
        if i == odd:
            out.append('<circle class="r1" cx="%.1f" cy="%.1f" r="13"/>' % (cx, cy))
        if i == even:
            out.append('<circle class="r2" cx="%.1f" cy="%.1f" r="13"/>' % (cx, cy))
        out.append('<circle class="st" cx="%.1f" cy="%.1f" r="%.1f"/>'
                   % (cx, cy, SVG_R[t]))
    out.append('</svg></figure>')
    return "".join(out)

# --- commands ---------------------------------------------------------------
def spectrum(pts):
    """Sorted pairwise distances: a shape fingerprint rotation cannot change."""
    return sorted(d(pts[i], pts[j])
                  for i in range(len(pts)) for j in range(i + 1, len(pts)))

def shape_gap(a, b):
    """How far apart two same-size constellations are as shapes, 0 = identical.

    RMS difference of their distance spectra, as a fraction of the average
    distance in the two. This is only a backstop -- what actually separates a
    pair is the description -- but it is what says the two are not near-copies
    of one another.
    """
    va, vb = spectrum(a), spectrum(b)
    mean = sum(va + vb) / len(va + vb)
    return (sum((x - y) ** 2 for x, y in zip(va, vb)) / len(va)) ** 0.5 / mean

MIN_SHAPE_GAP = 0.10

def cmd_verify(argv):
    cat = load_catalogue(argv[0] if argv else SRC)
    problems = 0
    print("%-13s %-5s %-6s %-8s %-8s %s"
          % ("name", "stars", "bright", "radius", "max nn", "checks"))
    for c in cat:
        name, pts, tiers = c["name"], c["pts"], c["tiers"]
        lonely = name in LONELY_BRIGHT
        notes = []
        if name not in FINGERPRINT:
            print("%-13s unknown to this script" % name)
            problems += 1
            continue
        ok, why = structure_ok(pts, tiers, lonely)
        if not ok:
            notes.append("STRUCTURE: " + why)
        if not FINGERPRINT[name](pts, tiers):
            notes.append("DESCRIPTION does not fit")
        odd, even, margins = TARGETS[name](pts, tiers)
        if not margins:
            notes.append("PRESS RULE too close to call")
        if odd == even:
            notes.append("both press rules give the same star")
        if (odd, even) != (c["odd"], c["even"]):
            notes.append("stored %d/%d but the rules say %d/%d"
                         % (c["odd"], c["even"], odd, even))
        b = brights(tiers)
        ctr = cen(pts)
        print("%-13s %-5d %-6d %-8.1f %-8.1f %s"
              % (name, len(pts), len(b),
                 max(d(p, ctr) for p in pts) * UNIT_PX,
                 max(nn(pts, i) for i in range(len(pts))
                     if not (lonely and i == b[0])) * UNIT_PX,
                 "; ".join(notes) if notes else "ok"))
        problems += len(notes)

    groups = {}
    for c in cat:
        groups.setdefault((len(c["pts"]), len(brights(c["tiers"]))), []).append(c)
    print("\n(stars, bright) signatures -- the description is what separates a pair:")
    for sig, members in sorted(groups.items()):
        print("  %-8s %s" % (str(sig), ", ".join(m["name"] for m in members)))
    print("\ndescription cross-check (its own must fit, its partner's must not):")
    for c in cat:
        own = FINGERPRINT[c["name"]](c["pts"], c["tiers"])
        clash = matches_other(c["name"], c["pts"], c["tiers"])
        loose = matches_other(c["name"], c["pts"], c["tiers"], False)
        if clash or not own:
            problems += 1
        print("  %-13s own=%-5s same signature: %-14s other signatures: %s"
              % (c["name"], own, ", ".join(clash) if clash else "none",
                 ", ".join(loose) if loose else "none"))

    print("\nclosest shapes (RMS distance-spectrum gap; %.2f is the floor):"
          % MIN_SHAPE_GAP)
    gaps = []
    for i in range(len(cat)):
        for j in range(i + 1, len(cat)):
            a, b = cat[i], cat[j]
            if len(a["pts"]) != len(b["pts"]):
                continue
            gaps.append((shape_gap(a["pts"], b["pts"]), a["name"], b["name"],
                         SIGNATURE.get(a["name"]) == SIGNATURE.get(b["name"])))
    gaps.sort()
    for gap, a, b, same in gaps[:6]:
        if gap < MIN_SHAPE_GAP:
            problems += 1
        print("  %.3f  %-13s %-13s %s"
              % (gap, a, b, "(same signature)" if same else ""))

    print("\n%d entries, %d problems" % (len(cat), problems))
    return 1 if problems else 0

def cmd_search(argv):
    names = argv or list(FINGERPRINT)
    for name in names:
        rng = random.Random(20260829 ^ (sum(map(ord, name)) * 7919))
        for tries in range(2000000):
            r = build(name, rng)
            if not r:
                continue
            pts, tiers = r
            c = cen(pts)
            # Check the rounded coordinates: those, not the full-precision
            # ones, are what the C++ table stores and the module draws.
            pts = [(round(p[0] - c[0], 4), round(p[1] - c[1], 4)) for p in pts]
            t = acceptable(name, pts, tiers)
            if t:
                print("// %s -- %d tries, radius %.0fpx"
                      % (name, tries, max(math.hypot(*p) for p in pts) * UNIT_PX))
                print(cpp_entry(name, pts, tiers, t[0], t[1]))
                sys.stdout.flush()
                break
        else:
            print("// %s -- no candidate found" % name)
    return 0

def figures_markup(names=None):
    """The manual's <figure> markup for the whole catalogue, in order.

    verify_manual.py diffs this against what manual/index.html actually prints,
    so the drawings can never drift from the coordinates the module uses.
    """
    cat = {c["name"]: c for c in load_catalogue()}
    return [svg_figure(n, cat[n]["pts"], cat[n]["tiers"], cat[n]["odd"],
                       cat[n]["even"])
            for n in (names or list(cat))]


def cmd_figures(argv):
    cat = {c["name"]: c for c in load_catalogue()}
    for name in (argv or list(cat)):
        c = cat[name]
        print(svg_figure(name, c["pts"], c["tiers"], c["odd"], c["even"]))
    return 0

if __name__ == "__main__":
    cmds = {"verify": cmd_verify, "search": cmd_search, "figures": cmd_figures}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        raise SystemExit(__doc__)
    sys.exit(cmds[sys.argv[1]](sys.argv[2:]))
