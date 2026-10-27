#!/usr/bin/env python3
"""The level editor's Split, twin test: port/src/live/terrain.cpp (bc_cameratest --subdiv DIR) against
tools_port/bcfiles terrain.py (geometry.subdivide_edit) on the levels of the player's ROM.

    python3 port/tools/subdiv_cross.py BC_CAMERATEST.EXE ROM [WORKDIR] [LEVELS]

For each level (default: every tenth one and a few with walls, bridges and moving parts), patches of drawn
triangles around random splittable ones are split once and twice by both (the second patch's pieces then
moved up 8), and the geometry edits (moves, removes, added drawn / floor / wall triangles, in order) compared.
The welds across meshes get their own cases: L0's triangle 113 moved up 8 (its edge at z 2020 carries
another list's corner), and random raise strokes (hard and soft edges) on each level, both twins' edits and
weld counts compared, and the result checked for cracks: open edges of the drawn and the floor triangles near
the edit that weren't open before (a sample every 4 units with no other edge within 2; not within 3 of the
edge's ends, and an edge from the same corner counts only when it runs the same way: two meshes welded at a
corner aren't a crack), with the welds and, for comparison, without them (the editor before).  The edges'
first measure (every sample, an edge from the same corner never counting) is reported too, and with
SUBDIV_FACES=1 a surface one (no triangle within 2), which also counts a mesh lifted off another's face.
And the corners lying on another mesh's edge (the welds' T-junctions, the wider seams' corners too) that an
edit took more than a unit off it are counted: none may be (main before the crossing splits: 17 in these
cases, 29 units at L10's crossing).
The level files are written to WORKDIR (default: a temporary folder) from the ROM at test time; nothing from
the ROM is kept.  Exit 0: every edit the same, L0's case closed, and the strokes' leftover cracks (reported)
fewer than without the welds."""
import json
import math
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools_port"))
from bcfiles import Rom, level, geometry as G, terrain as T  # noqa: E402

DEFAULT_LEVELS = (0, 1, 4, 10, 12, 20, 22, 26, 30, 40, 43, 50)
STROKES = 6          # random raise strokes per level


def run_ops(geo, lv, nlev, ids, ops, weld=True):
    """the Python twin of bc_cameratest --subdiv's case: (model, edit or "ERR ...", weld counts)"""
    m = T.Model(geo, geo.protected_positions(lv), weld=weld)
    sel = list(ids)
    wsum = [0, 0, 0, 0]
    try:
        for _ in range(nlev):
            m.subdivide(sel)
            sel = m.last_split
        for op in [o for o in ops.split(";") if o]:
            if op.startswith("sel:"):
                dy = int(op[4:])
                moves = {}
                for p in sorted({p for t in sel for p in m.tris[T.DRAWN][t].pos}):
                    if m.movable(p):
                        x, y, z = m.pos[p].cur
                        moves[p] = (x, T.clamp16(y + dy), z)
            elif op.startswith("disc:"):
                x, z, r, s, f = (float(v) for v in op[5:].split(","))
                moves = m.raise_step(x, z, r, s, f)
            else:
                return m, "ERR bad op " + op, wsum
            if not moves:
                continue
            m.move(moves)
            wsum[0] += m.last_weld["partners"]
            wsum[1] += m.last_weld["followers"]
            wsum[2] += m.last_weld["prot_skipped"]
            wsum[3] += m.last_weld.get("tjunctions", 0)
    except T.TerrainError as e:
        return m, "ERR %s" % e, wsum
    return m, norm(m.export()), wsum


# ------------------------------------------------------------------ cracks
def _seg_dist(P, A, B):
    ex, ey, ez = B[0] - A[0], B[1] - A[1], B[2] - A[2]
    l2 = ex * ex + ey * ey + ez * ez
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / l2))
    return math.sqrt((A[0] + ex * t - P[0]) ** 2 + (A[1] + ey * t - P[1]) ** 2 + (A[2] + ez * t - P[2]) ** 2)


def _tri_dist(P, A, B, C):
    """the distance from P to triangle A B C (its closest point: Ericson, Real-Time Collision Detection 5.1.5)"""
    def sub(u, v):
        return (u[0] - v[0], u[1] - v[1], u[2] - v[2])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    def at(Q):
        return math.sqrt(dot(sub(P, Q), sub(P, Q)))
    ab, ac, ap = sub(B, A), sub(C, A), sub(P, A)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return at(A)
    bp = sub(P, B)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return at(B)
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return at((A[0] + ab[0] * v, A[1] + ab[1] * v, A[2] + ab[2] * v))
    cp = sub(P, C)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return at(C)
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return at((A[0] + ac[0] * w, A[1] + ac[1] * w, A[2] + ac[2] * w))
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return at((B[0] + (C[0] - B[0]) * w, B[1] + (C[1] - B[1]) * w, B[2] + (C[2] - B[2]) * w))
    den = va + vb + vc
    if den == 0:
        return min(_seg_dist(P, A, B), _seg_dist(P, B, C), _seg_dist(P, A, C))
    v, w = vb / den, vc / den
    return at((A[0] + ab[0] * v + ac[0] * w, A[1] + ab[1] * v + ac[1] * w, A[2] + ab[2] * v + ac[2] * w))


def _parallel(A, B, C, D, cos_min=0.98):
    """segments A-B and C-D run the same way (either direction)"""
    u = [B[k] - A[k] for k in range(3)]
    v = [D[k] - C[k] for k in range(3)]
    lu = math.sqrt(sum(x * x for x in u))
    lv = math.sqrt(sum(x * x for x in v))
    if lu == 0 or lv == 0:
        return False
    return abs(sum(u[k] * v[k] for k in range(3))) / (lu * lv) >= cos_min


def open_samples_faces(m, kind, region, tol=2.0):
    """open edges' samples with no other triangle of that kind within tol (any surface, not just edges: this also
    counts a mesh lying on another one's face - a decal, a building's foot - that a brush lifts off it, which the
    welds don't hold: reported, not a pass condition)"""
    x0, z0, x1, z1 = region
    cnt = {}
    for t in m.tris[kind]:
        if not t.removed:
            for k in range(3):
                a, b = t.pos[k], t.pos[(k + 1) % 3]
                if a != b:
                    e = T.edge_key(a, b)
                    cnt[e] = cnt.get(e, 0) + 1
    cells = {}
    for ti, t in enumerate(m.tris[kind]):
        if t.removed:
            continue
        P = [m.pos[q].cur for q in t.pos]
        lx, hx = min(p[0] for p in P), max(p[0] for p in P)
        lz, hz = min(p[2] for p in P), max(p[2] for p in P)
        if hx < x0 - 8 or lx > x1 + 8 or hz < z0 - 8 or lz > z1 + 8:
            continue
        for cx in range((lx - 3) >> 5, ((hx + 3) >> 5) + 1):
            for cz in range((lz - 3) >> 5, ((hz + 3) >> 5) + 1):
                cells.setdefault((cx, cz), []).append(ti)
    out = []
    for e, c in cnt.items():
        if c != 1:
            continue
        A, B = m.pos[e[0]].cur, m.pos[e[1]].cur
        ln = math.sqrt(sum((A[i] - B[i]) ** 2 for i in range(3)))
        n = max(1, int(ln / 4))
        prot = m.pos[e[0]].prot or m.pos[e[1]].prot
        for i in range(n):
            s = (i + 0.5) / n
            S = tuple(A[k] + (B[k] - A[k]) * s for k in range(3))
            if not (x0 <= S[0] <= x1 and z0 <= S[2] <= z1):
                continue
            sealed = False
            for ti in cells.get((int(math.floor(S[0])) >> 5, int(math.floor(S[2])) >> 5), ()):
                q = m.tris[kind][ti].pos
                if e[0] in q and e[1] in q:
                    continue   # (the edge's own triangle)
                if _tri_dist(S, m.pos[q[0]].cur, m.pos[q[1]].cur, m.pos[q[2]].cur) <= tol:
                    sealed = True
                    break
            if not sealed:
                out.append((S[0], S[1], S[2], prot))
    return out


def open_samples(m, kind, region, tol=2.0):
    """[(x, y, z, protected)]: samples (every 4 units) of the live `kind` triangles' open edges inside region
    (x0, z0, x1, z1) with no other edge of that kind within tol (protected: an end of the edge is).  An edge from
    the same corner seals nothing near that corner (there it is always close) unless it runs the same way (two
    meshes' edges along one line, welded there: what a T-junction fix leaves), and the samples within 3 units of an
    edge's ends aren't taken (where several meshes' corners meet, near is always ambiguous: a crack is longer)"""
    return _open_edges(m, kind, region, tol, True)


def open_samples_edges(m, kind, region, tol=2.0):
    """the edges' measure as the welds were first checked (rough1): every sample, an edge from the same corner never
    counted near it (two meshes welded at a corner read as open there)"""
    return _open_edges(m, kind, region, tol, False)


def _open_edges(m, kind, region, tol, seam):
    x0, z0, x1, z1 = region
    cnt = {}
    for t in m.tris[kind]:
        if not t.removed:
            for k in range(3):
                a, b = t.pos[k], t.pos[(k + 1) % 3]
                if a != b:
                    e = T.edge_key(a, b)
                    cnt[e] = cnt.get(e, 0) + 1
    cells = {}
    for e in cnt:
        A, B = m.pos[e[0]].cur, m.pos[e[1]].cur
        if A == B:
            continue
        if max(A[0], B[0]) < x0 - 8 or min(A[0], B[0]) > x1 + 8 or max(A[2], B[2]) < z0 - 8 or min(A[2], B[2]) > z1 + 8:
            continue
        for cx in range((min(A[0], B[0]) - 3) >> 5, ((max(A[0], B[0]) + 3) >> 5) + 1):
            for cz in range((min(A[2], B[2]) - 3) >> 5, ((max(A[2], B[2]) + 3) >> 5) + 1):
                cells.setdefault((cx, cz), []).append(e)
    out = []
    for e, c in cnt.items():
        if c != 1:
            continue
        A, B = m.pos[e[0]].cur, m.pos[e[1]].cur
        ln = math.sqrt(sum((A[i] - B[i]) ** 2 for i in range(3)))
        n = max(1, int(ln / 4))
        prot = m.pos[e[0]].prot or m.pos[e[1]].prot
        for i in range(n):
            s = (i + 0.5) / n
            S = tuple(A[k] + (B[k] - A[k]) * s for k in range(3))
            if not (x0 <= S[0] <= x1 and z0 <= S[2] <= z1):
                continue
            if seam and min(s, 1 - s) * ln < 3.0:
                continue
            sealed = False
            for f in cells.get((int(math.floor(S[0])) >> 5, int(math.floor(S[2])) >> 5), ()):
                skip = False
                for q in f:
                    Q = m.pos[q].cur
                    if (q in e or Q == A or Q == B) and (Q[0] - S[0]) ** 2 + (Q[1] - S[1]) ** 2 + (Q[2] - S[2]) ** 2 <= 36:
                        skip = True
                if skip and seam and f != e and _parallel(A, B, m.pos[f[0]].cur, m.pos[f[1]].cur):
                    skip = False
                if skip:
                    continue
                if _seg_dist(S, m.pos[f[0]].cur, m.pos[f[1]].cur) <= tol:
                    sealed = True
                    break
            if not sealed:
                out.append((S[0], S[1], S[2], prot))
    return out


def pulled_off(m):
    """(corners, largest units): the level's corners lying on another mesh's edge (the welds' rels, wider seams too)
    that the edits took more than a unit further off that edge, while it is still there (a corner taken into a split
    edge is on it)"""
    live = set()
    for kind in (T.DRAWN, T.FLOOR, T.WALL):
        for t in m.tris[kind]:
            if not t.removed:
                for k in range(3):
                    live.add(T.edge_key(t.pos[k], t.pos[(k + 1) % 3]))
    n, big = 0, 0.0
    for r in m.rels:
        p, a, b = r[0], r[1], r[2]
        if (a, b) not in live:
            continue
        P, A, B = m.pos[p], m.pos[a], m.pos[b]
        if P.cur == P.orig and A.cur == A.orig and B.cur == B.orig:
            continue
        d = _seg_dist(P.cur, A.cur, B.cur) - _seg_dist(P.orig, A.orig, B.orig)
        if d > 1.0:
            n += 1
            big = max(big, d)
    return n, big


def new_cracks(m0, m1, measure=None):
    """(units, units at a protected corner, first) of open edge in m1 (edited) near its edit that m0 (the level)
    didn't have open there (no open sample of m0 within 3 units in x-z); measure: open_samples (the default: no
    other triangle within 2) or open_samples_edges (no other edge within 2: the measure before T-junction fixes,
    which counts two meshes welded at a corner as open near it)"""
    open_samples_fn = measure or open_samples
    moved = [p.cur for p in m1.pos if p.level and p.cur != p.orig] + [p.cur for p in m1.pos if not p.level]
    if not moved:
        return 0, 0, None
    region = (min(v[0] for v in moved) - 8, min(v[2] for v in moved) - 8,
              max(v[0] for v in moved) + 8, max(v[2] for v in moved) + 8)
    units = prot = 0
    first = None
    for kind in (T.DRAWN, T.FLOOR):
        before = open_samples_fn(m0, kind, region)
        grid = {}
        for s in before:
            grid.setdefault((int(s[0]) >> 3, int(s[2]) >> 3), []).append(s)
        for s in open_samples_fn(m1, kind, region):
            near = False
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for b in grid.get(((int(s[0]) >> 3) + dx, (int(s[2]) >> 3) + dz), ()):
                        if (b[0] - s[0]) ** 2 + (b[2] - s[2]) ** 2 <= 9:
                            near = True
            if not near:
                units += 4
                prot += 4 if s[3] else 0
                if first is None and not s[3]:
                    first = "%s (%.0f, %.0f, %.0f)" % ("drawn" if kind == T.DRAWN else "floor", s[0], s[1], s[2])
    return units, prot, first


def patches(lv, geo, levelno, n=2):
    """[(levels, ids)]: n patches around random splittable drawn triangles"""
    prot = geo.protected_positions(lv)
    rt = geo.render_tris()
    ok = [i for i, (pi, oi, vs) in enumerate(rt)
          if not geo.pieces[pi].frozen and not any((v[0], v[1], v[2]) in prot for v in vs)]
    if not ok:
        return []
    cen = {i: (sum(v[0] for v in rt[i][2]) / 3.0, sum(v[2] for v in rt[i][2]) / 3.0) for i in ok}
    rnd = random.Random(1000 + levelno)
    out = []
    for k in range(n):
        c = cen[rnd.choice(ok)]
        r = 150 + 250 * k
        ids = sorted(i for i in ok if (cen[i][0] - c[0]) ** 2 + (cen[i][1] - c[1]) ** 2 <= r * r)[:30]
        out.append((1 + k % 2, ids))
    return out


def norm(ed):
    rem = ed.get("remove", {})
    add = ed.get("add", {})
    return {"move": [list(m) for m in ed.get("move", [])],
            "remove": {k: list(rem.get(k, [])) for k in ("render", "ground", "walls_a", "walls_b")},
            "add": {"render": [{"after": a["after"], "flag": a.get("flag", 0), "v": [list(v) for v in a["v"]]}
                               for a in add.get("render", [])],
                    "ground": [{"v": list(a["v"]), "surface": a["surface"], "flag": a["flag"]} for a in add.get("ground", [])],
                    "walls_a": [dict(a) for a in add.get("walls_a", [])],
                    "walls_b": [dict(a) for a in add.get("walls_b", [])]}}


def first_diff(a, b, path=""):
    if type(a) != type(b):
        return "%s: %r vs %r" % (path, a, b)
    if isinstance(a, dict):
        for k in a:
            d = first_diff(a[k], b.get(k), path + "." + k)
            if d:
                return d
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return "%s: %d vs %d entries" % (path, len(a), len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_diff(x, y, "%s[%d]" % (path, i))
            if d:
                return d
        return None
    return None if a == b else "%s: %r vs %r" % (path, a, b)


def strokes(lv, geo, levelno, n=STROKES):
    """n random raise strokes (op strings) at random corners of the level's drawn triangles"""
    rt = geo.render_tris()
    rnd = random.Random(7000 + levelno)
    out = []
    for k in range(n):
        v = rt[rnd.randrange(len(rt))][2][rnd.randrange(3)]
        r = rnd.choice((60, 120, 200, 350))
        s = rnd.choice((8, 16, 40, -24))
        f = (0, 0.5, 1)[k % 3]
        out.append("disc:%d,%d,%d,%d,%s" % (v[0], v[2], r, s, f))
    return out


def main():
    exe, rom_path = sys.argv[1], sys.argv[2]
    work = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix="subdiv_")
    levels = [int(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 else DEFAULT_LEVELS
    os.makedirs(work, exist_ok=True)
    rom = Rom.load(rom_path)
    cases = []
    want = {}
    wwant = {}
    cracks = {}
    hard = set()   # hard-edged strokes (falloff 0): a cliff along the brush's rim; reported, not failed
    for L in levels:
        f = rom.level(L)
        fname = "level_%02d.bin" % L
        with open(os.path.join(work, fname), "wb") as fh:
            fh.write(f.data)
        lv = level.Level.parse(f.data, f.split)
        geo = G.Geometry.from_level(lv)
        base = T.Model(geo, geo.protected_positions(lv))
        mine = []
        for k, (nlev, ids) in enumerate(patches(lv, geo, L)):
            if ids:
                mine.append(("L%02d_%d" % (L, k), nlev, ids, "sel:8" if k == 1 else "", False))
        if L == 0:
            mine.append(("L00_t113", 0, [113], "sel:8", True))
        for k, op in enumerate(strokes(lv, geo, L)):
            mine.append(("L%02d_s%d" % (L, k), 0, [0], op, True))
            if op.endswith(",0"):
                hard.add("L%02d_s%d" % (L, k))
        for name, nlev, ids, ops, check in mine:
            cases.append((name, fname, nlev, ids, ops))
            m, want[name], wwant[name] = run_ops(geo, lv, nlev, ids, ops)
            if check and not isinstance(want[name], str):
                m0, _, _ = run_ops(geo, lv, nlev, ids, ops, weld=False)
                ue = new_cracks(base, m, open_samples_edges)
                uf = new_cracks(base, m, open_samples_faces) if os.environ.get("SUBDIV_FACES") else (0, 0, None)
                cracks[name] = (new_cracks(base, m), new_cracks(T.Model(geo, geo.protected_positions(lv), weld=False), m0),
                                m.follower_count(), ue[0] - ue[1], uf[0] - uf[1], pulled_off(m))
    with open(os.path.join(work, "cases.txt"), "w") as fh:
        for name, fname, nlev, ids, ops in cases:
            fh.write("%s %s %d %s%s\n" % (name, fname, nlev, ",".join(str(i) for i in ids), " " + ops if ops else ""))
    win = work
    if work.startswith("/mnt/") or work.startswith("/home/") or work.startswith("/tmp/"):
        win = subprocess.run(["wslpath", "-w", work], capture_output=True, text=True).stdout.strip()
    r = subprocess.run([exe, "--subdiv", win], capture_output=True, text=True)
    print(r.stdout.strip())
    same = diff = 0
    crack_cases = crack_bad = 0
    cr_new = cr_old = hard_new = hard_old = 0
    cr_e = hard_e = faces = 0
    off_n, off_big = 0, 0.0
    for name, fname, nlev, ids, ops in cases:
        p = os.path.join(work, name + ".c.json")
        got = open(p).read() if os.path.exists(p) else "ERR no output"
        if got.startswith("ERR"):
            ok = isinstance(want[name], str)
            why = "C %s, Python %s" % (got.strip(), "ERR" if ok else "an edit")
        else:
            c = norm(json.loads(got))
            why = first_diff(c, want[name]) if not isinstance(want[name], str) else "C an edit, Python " + want[name]
            ok = why is None
        pw = os.path.join(work, name + ".c.weld")
        cw = [int(v) for v in open(pw).read().split()] if os.path.exists(pw) else None
        if ok and cw != wwant[name]:
            ok, why = False, "weld counts C %s, Python %s" % (cw, wwant[name])
        w = want[name]
        n_add = 0 if isinstance(w, str) else len(w["add"]["render"])
        n_wall = 0 if isinstance(w, str) else len(w["add"]["walls_a"]) + len(w["add"]["walls_b"])
        n_rem = 0 if isinstance(w, str) else len(w["remove"]["render"])
        n_mv = 0 if isinstance(w, str) else len(w["move"])
        extra = ""
        if name in cracks:
            (u, up, first), (u0, up0, _), nf, ue, uf, (po, pobig) = cracks[name]
            off_n += po
            off_big = max(off_big, pobig)
            if name in hard:
                hard_new += u - up
                hard_old += u0 - up0
                hard_e += ue
            else:
                crack_cases += 1
                cr_new += u - up
                cr_old += u0 - up0
                crack_bad += u - up > 0
                cr_e += ue
            faces += uf
            extra = "; cracks %d units (%d at protected corners)%s, without the welds %d%s; welds: %d partners, %d " \
                    "followers, %d protected left, %d T-junctions split; %d corners follow edges; %d pulled off an " \
                    "edge they lie on; the edges' first measure %d" % (
                        u, up, " FIRST " + first if u - up > 0 and first else "", u0,
                        " (hard edge: reported only)" if name in hard else "", wwant[name][0], wwant[name][1],
                        wwant[name][2], wwant[name][3], nf, po, ue)
        if ok:
            same += 1
            print("same %s: %d triangles x%d%s -> %d moved, %d drawn removed, %d added, %d wall triangles added%s"
                  % (name, len(ids), nlev, " " + ops if ops else "", n_mv, n_rem, n_add, n_wall, extra))
        else:
            diff += 1
            print("DIFF %s (%d triangles x%d %s): %s" % (name, len(ids), nlev, ops, why))
    print("subdiv cross: %d cases the same, %d different (%s)" % (same, diff, work))
    print("weld check: %d cases, %d with cracks (%d units of new open edge; without the welds %d); hard-edged "
          "strokes: %d units (without the welds %d)" % (crack_cases, crack_bad, cr_new, cr_old, hard_new, hard_old))
    print("the edges' first measure (two meshes welded at a corner read as open there): %d units, hard-edged %d%s"
          % (cr_e, hard_e, "; open edges off every surface (SUBDIV_FACES: meshes lying on others' faces too): %d units"
             % faces if os.environ.get("SUBDIV_FACES") else ""))
    print("corners on another mesh's edge an edit pulled off it (more than a unit): %d (the largest %.1f units)"
          % (off_n, off_big))
    # (the twins must agree, L0's triangle 113 must close and no edit may pull a corner off an edge it lies on (crossing
    # edges are split at it); the strokes' leftover cracks are reported, and must stay below the editor's without the
    # welds)
    t113 = cracks.get("L00_t113")
    t113_ok = t113 is None or t113[0][0] - t113[0][1] == 0
    return 0 if diff == 0 and same > 0 and t113_ok and cr_new <= cr_old and off_n == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
