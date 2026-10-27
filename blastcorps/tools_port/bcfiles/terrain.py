"""The editor's split and seal steps (SUBDIVIDE, SEAL) on a Geometry: the twin of
port/src/live/terrain.cpp's Model, so a script's subdivide_edit makes the same
geometry edit as the level editor in bc.exe (port/tools/subdiv_cross.py checks
the two against each other).

The model: the level's triangles (drawn, floor, walls) over corners shared by
position (welded: a move moves every triangle there); a split puts a midpoint
on each edge of the selected drawn triangles and splits every triangle on such
an edge along it (2, 3 or 4 ways), so no corner is left on another triangle's
edge.  An edge a triangle that can't split shares (a frozen list's, one with a
protected corner) stays whole, and so does an edge whose midpoint would lie on
such a triangle's edge.  A fresh midpoint lying on the edge of a triangle that
doesn't share its corners (Rare's meshes aren't always welded) splits that
triangle too (the seams).  The ids, the loop orders and the arithmetic follow
terrain.cpp line by line: the corners' ids decide ties.

Welds across meshes (terrain.h): the level's corners within WELD_TOL on every
axis are one group (a move moves them as one, by the move of the lowest-numbered
member moved), and a level corner lying on another triangle's edge follows that
edge (it moves only with the edge's ends) while the edge is there; a split takes
it in (the midpoint, when it lies within WELD_TOL of the middle).

    m = Model(geo, geo.protected_positions(lv))
    m.subdivide(render_ids)            # m.last_split: the selected triangles' pieces
    m.move({corner: (x, y, z)})        # m.last_weld: what the welds added
    ed = m.export()                    # the geometry edit (Geometry.apply_edit's form)
"""
import math

DRAWN, FLOOR, WALL = 0, 1, 2
WELD_TOL = 2
SEAM_TOL = 3     # an open edge's corner this far (more than WELD_TOL) from another mesh's open edge follows it too
SPLIT_GAP = 1    # a corner on two edges that would take it more than this far apart: the edges split at it


class TerrainError(ValueError):
    pass


def mid_of(a, b):
    """the floor of the mean (terrain.cpp midOf, geometry.py _mid)"""
    return (a + b) >> 1


def edge_key(a, b):
    return (a, b) if a < b else (b, a)


def lround(x):
    """C's lround: halves away from zero"""
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def s16(v):
    return ((v + 0x8000) & 0xFFFF) - 0x8000


def near_edge(P, A, B):
    """P on segment A-B as the level's meshes meet: within WELD_TOL of it, more than 2 from either end
    (terrain.cpp nearEdge)"""
    return on_edge(P, A, B, float(WELD_TOL) * WELD_TOL)


def on_edge(P, A, B, tol2=1.0):
    """P on segment A-B: within a map unit of it (tol2: the square of another distance), more than 2 from
    either end (terrain.cpp onEdge)"""
    ex, ey, ez = B[0] - A[0], B[1] - A[1], B[2] - A[2]
    l2 = ex * ex + ey * ey + ez * ez
    if l2 < 25 or P == A or P == B:
        return False
    t = ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / l2
    ln = math.sqrt(l2)
    if t * ln <= 2.0 or (1 - t) * ln <= 2.0:
        return False
    qx, qy, qz = A[0] + ex * t - P[0], A[1] + ey * t - P[1], A[2] + ez * t - P[2]
    return qx * qx + qy * qy + qz * qz <= tol2


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def clamp16(v):
    return max(-32768, min(32767, v))


def brush_weight(radius, falloff, d):
    """terrain.cpp brushWeight"""
    if not radius > 0 or d > radius:
        return 0.0
    f = max(0.0, min(1.0, falloff))
    inner = radius * (1.0 - f)
    if d <= inner:
        return 1.0
    t = (d - inner) / max(1e-9, radius - inner)
    return 1.0 - t * t * (3.0 - 2.0 * t)


class Pos:
    __slots__ = ("orig", "cur", "level", "prot", "pa", "pb", "host")

    def __init__(self, v, level):
        self.orig = self.cur = v
        self.level = level
        self.prot = False
        self.pa = self.pb = -1
        self.host = -1


class Tri:
    """a drawn (st, rgba, flag, after, state), floor (surface, flag) or wall (set, heading, side, fail) triangle"""
    __slots__ = ("orig_id", "pos", "st", "rgba", "flag", "after", "state", "removed", "surface", "set",
                 "heading", "side", "fail")

    def __init__(self):
        self.orig_id = -1
        self.pos = [-1, -1, -1]
        self.removed = False
        self.after = -1
        self.state = -1
        self.flag = 0


class Model:
    def __init__(self, geo, protected=(), weld=True):
        """weld=False: no welds across meshes (the editor before them; for comparisons)"""
        self.geo = geo
        self.weld = weld
        self.last_weld = {"partners": 0, "followers": 0, "prot_skipped": 0}
        self.last_snapped = 0
        self.rt = geo.render_tris()
        prot = set(protected)
        self.pos = []
        self.level_pos = {}
        self.mid = {}            # edge (a, b) -> its midpoint corner
        self.sub_edges = set()   # this step's halves of split edges
        self.last_split = []
        self.tris = ([], [], [])

        def corner(v):
            k = (v[0], v[1], v[2])
            if k in self.level_pos:
                return self.level_pos[k]
            i = self._add(k, True)
            self.pos[i].prot = k in prot
            self.level_pos[k] = i
            return i
        for i, (pi, oi, vs) in enumerate(self.rt):
            d = Tri()
            d.orig_id, d.state = i, i
            d.pos = [corner(v) for v in vs]
            d.st = [[v[4], v[5]] for v in vs]
            d.rgba = [list(v[6:10]) for v in vs]
            self.tris[DRAWN].append(d)
        for i, t in enumerate(geo.ground):
            f = Tri()
            f.orig_id = i
            f.surface, f.flag = t[9], t[10]
            f.pos = [corner(t[3 * k:3 * k + 3]) for k in range(3)]
            self.tris[FLOOR].append(f)
        for s, key in enumerate((0x6C, 0x70)):
            for i, t in enumerate(geo.walls[key]):
                w = Tri()
                w.set, w.orig_id = s, i
                w.heading, w.side, w.fail = t[9], t[10], t[11]
                w.pos = [corner(t[3 * k:3 * k + 3]) for k in range(3)]
                self.tris[WALL].append(w)
        self.grp = []
        self.grp_members = {}
        self.rels = []
        self.hosts = []
        if weld:
            self._build_welds()
        self.mark_followers()

    # ---------------------------------------------------------- welds
    def _build_welds(self):
        """the groups (corners within WELD_TOL, chained) and the level's corners on other triangles' edges"""
        nl = len(self.pos)
        par = list(range(nl))

        def find(a):
            while par[a] != a:
                par[a] = par[par[a]]
                a = par[a]
            return a
        cells = {}
        for i in range(nl):
            v = self.pos[i].orig
            cells.setdefault((v[0] >> 4, v[2] >> 4), []).append(i)
        for i in range(nl):
            v = self.pos[i].orig
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for j in cells.get(((v[0] >> 4) + dx, (v[2] >> 4) + dz), ()):
                        if j <= i or cheb(v, self.pos[j].orig) > WELD_TOL:
                            continue
                        ra, rb = find(i), find(j)
                        if ra < rb:
                            par[rb] = ra
                        elif rb < ra:
                            par[ra] = rb
        self.grp = [find(i) for i in range(nl)]
        for i in range(nl):
            g = self.grp[i]
            if g != i:
                self.grp_members.setdefault(g, [g]).append(i)
        big = {}
        for i in range(nl):
            if not self.pos[i].prot:
                v = self.pos[i].orig
                big.setdefault((v[0] >> 6, v[2] >> 6), []).append(i)
        # the drawn and floor meshes' open edges (one triangle of the set has them) and their corners; every edge
        # (two corners of one triangle: a wider seam is between meshes, not across a thin triangle of one)
        uses = ({}, {})
        tri_edges = set()
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                c = t.pos
                for k in range(3):
                    key = edge_key(c[k], c[(k + 1) % 3])
                    tri_edges.add(key)
                    if kind != WALL:
                        uses[kind][key] = uses[kind].get(key, 0) + 1
        open_edges = set(key for u in uses for key, n in u.items() if n == 1 and key[0] != key[1])
        open_ends = set(q for key in open_edges for q in key)
        done = set()
        rels = []
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                c = t.pos
                for k in range(3):
                    a, b = min(c[k], c[(k + 1) % 3]), max(c[k], c[(k + 1) % 3])
                    if a == b or (a, b) in done:
                        continue
                    done.add((a, b))
                    A, B = self.pos[a].orig, self.pos[b].orig
                    ex, ey, ez = B[0] - A[0], B[1] - A[1], B[2] - A[2]
                    l2 = ex * ex + ey * ey + ez * ez
                    if l2 < 25:
                        continue
                    seam = (a, b) in open_edges
                    for cx in range((min(A[0], B[0]) - SEAM_TOL) >> 6, ((max(A[0], B[0]) + SEAM_TOL) >> 6) + 1):
                        for cz in range((min(A[2], B[2]) - SEAM_TOL) >> 6, ((max(A[2], B[2]) + SEAM_TOL) >> 6) + 1):
                            for p in big.get((cx, cz), ()):
                                if p == a or p == b or self.grp[p] == self.grp[a] or self.grp[p] == self.grp[b]:
                                    continue
                                P = self.pos[p].orig
                                wide = False
                                if not near_edge(P, A, B):
                                    # a wider seam: an open edge's corner along another mesh's open edge
                                    if not seam or p not in open_ends or edge_key(p, a) in tri_edges or \
                                            edge_key(p, b) in tri_edges or not on_edge(P, A, B, float(SEAM_TOL * SEAM_TOL)):
                                        continue
                                    wide = True
                                tt = ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / l2
                                rels.append((p, a, b, tt, wide))
        rels.sort(key=lambda r: (r[0], r[1], r[2]))
        self.rels = rels

    def group_members(self, p):
        if p < 0 or p >= len(self.grp):
            return []
        g = self.grp[p]
        return self.grp_members.get(g, [g])

    def mark_followers(self):
        for p in self.pos:
            p.host = -1
        self.hosts = []
        if not self.rels:
            return
        live = set()
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                if not t.removed:
                    c = t.pos
                    for k in range(3):
                        live.add(edge_key(c[k], c[(k + 1) % 3]))
        # (an edge it lies on before one a wider seam away)
        seen = set()
        for wide_pass in (False, True):
            for r, (p, a, b, _, wide) in enumerate(self.rels):
                if wide != wide_pass or (a, b) not in live:
                    continue
                g = self.grp[p]
                if g in seen:
                    continue
                seen.add(g)
                self.hosts.append((g, r))
        self.hosts.sort()
        for g, r in self.hosts:
            for q in self.group_members(g):
                if not self.pos[q].prot:
                    self.pos[q].host = r

    def follower_count(self):
        return sum(1 for p in self.pos if p.host >= 0)

    def resolve(self, moves):
        """{corner: (x, y, z)} -> the same with the welds (terrain.cpp resolve), and what they added"""
        info = {"partners": 0, "followers": 0, "prot_skipped": 0}
        N = dict(moves)
        prot_seen = set()
        expl = sorted(N)
        expl_set = set(expl)
        done_g = set()
        for p in expl:
            g = self.grp[p] if p < len(self.grp) else -1
            if g < 0 or g not in self.grp_members or g in done_g:
                continue
            done_g.add(g)
            o, v = self.pos[p].orig, N[p]
            for q in self.grp_members[g]:
                if q == p:
                    continue
                if self.pos[q].prot:
                    if q not in prot_seen:
                        prot_seen.add(q)
                        info["prot_skipped"] += 1
                    continue
                oq = self.pos[q].orig
                if q not in expl_set:
                    info["partners"] += 1
                N[q] = (clamp16(oq[0] + v[0] - o[0]), clamp16(oq[1] + v[1] - o[1]), clamp16(oq[2] + v[2] - o[2]))
        followed = set()
        for _ in range(8):
            changed = False
            for g, ri in self.hosts:
                p, a, b, t, _ = self.rels[ri]
                if a not in N and b not in N:
                    continue
                oa, ob, op = self.pos[a].orig, self.pos[b].orig, self.pos[p].orig
                a1 = N[a] if a in N else self.pos[a].cur
                b1 = N[b] if b in N else self.pos[b].cur
                d = [lround((1.0 - t) * (a1[k] - oa[k]) + t * (b1[k] - ob[k])) for k in range(3)]
                np_ = tuple(clamp16(op[k] + d[k]) for k in range(3))
                dd = [np_[k] - op[k] for k in range(3)]
                for q in self.group_members(p):
                    if self.pos[q].prot:
                        if q not in prot_seen:
                            prot_seen.add(q)
                            info["prot_skipped"] += 1
                        continue
                    oq = self.pos[q].orig
                    v = (clamp16(oq[0] + dd[0]), clamp16(oq[1] + dd[1]), clamp16(oq[2] + dd[2]))
                    if N.get(q) != v:
                        N[q] = v
                        followed.add(q)
                        changed = True
            if not changed:
                break
        info["followers"] = len(followed)
        return N, info

    # ---------------------------------------------------------- the brushes (a raise's single application)
    def live_corners(self):
        live = [0] * len(self.pos)
        for kind in (DRAWN, FLOOR):
            for t in self.tris[kind]:
                if not t.removed:
                    for p in t.pos:
                        live[p] = 1
        return live

    def raise_step(self, cx, cz, radius, strength, falloff):
        """terrain.cpp brush(B_RAISE) once, then strokeStep: {corner: (x, y, z)}, protected corners skipped"""
        live = self.live_corners()
        out = {}
        for i, p in enumerate(self.pos):
            if not live[i]:
                continue
            dx, dz = p.cur[0] - cx, p.cur[2] - cz
            if not dx * dx + dz * dz <= radius * radius:
                continue
            if not self.movable(i):
                continue
            w = brush_weight(radius, falloff, math.sqrt(dx * dx + dz * dz))
            if w <= 0:
                continue
            y = max(-32768.0, min(32767.0, p.cur[1] + strength * w))
            yi = clamp16(lround(y))
            if yi != p.cur[1]:
                out[i] = (p.cur[0], yi, p.cur[2])
        return out

    def _add(self, v, level):
        self.pos.append(Pos(v, level))
        return len(self.pos) - 1

    def frozen(self, i):
        """the drawn triangle's list is kept as it is (its own, or the run's it joined)"""
        return self.geo.pieces[self.rt[self.tris[DRAWN][i].state][0]].frozen

    # ---------------------------------------------------------- splitting
    def alive(self, kind, i):
        return not self.tris[kind][i].removed

    def splittable(self, kind, i):
        if not self.alive(kind, i):
            return True
        c = self.tris[kind][i].pos
        if any(self.pos[p].prot for p in c):
            return False
        if c[0] == c[1] or c[1] == c[2] or c[0] == c[2]:
            return False
        if kind == DRAWN and self.frozen(i):
            return False
        return True

    def split_all(self, selected):
        for _ in range(64):
            changed = False
            for kind in (DRAWN, FLOOR, WALL):
                n = len(self.tris[kind])
                for i in range(n):
                    if not self.alive(kind, i) or not self.splittable(kind, i):
                        continue
                    c = self.tris[kind][i].pos
                    if not any(edge_key(c[k], c[(k + 1) % 3]) in self.mid for k in range(3)):
                        continue
                    self.split_one(kind, i, selected)
                    changed = True
            if not changed:
                break

    def split_one(self, kind, i, selected):
        src = self.tris[kind][i]
        c = list(src.pos)
        m = [self.mid.get(edge_key(c[k], c[(k + 1) % 3]), -1) for k in range(3)]
        nm = sum(1 for x in m if x >= 0)
        if nm == 3:
            parts = [(0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)]
        elif nm == 1:
            j = 0 if m[0] >= 0 else 1 if m[1] >= 0 else 2
            parts = [(j, 3 + j, (j + 2) % 3), (3 + j, (j + 1) % 3, (j + 2) % 3)]
        else:
            # edges (i0, i1) and (i1, i2) split, (i2, i0) whole: the corner triangle, then the quad
            # (i0, m1, m2, i2) cut along its shorter diagonal (a tie: the lower corner ids)
            e = 1 if m[0] < 0 else 2 if m[1] < 0 else 0
            i0, i1, i2 = e, (e + 1) % 3, (e + 2) % 3
            s1, s2 = 3 + i0, 3 + i1
            pi0, pm2, pm1, pi2 = c[i0], m[i1], m[i0], c[i2]

            def d2(a, b):
                A, B = self.pos[a].cur, self.pos[b].cur
                dx, dy, dz = float(A[0] - B[0]), float(A[1] - B[1]), float(A[2] - B[2])
                return dx * dx + dy * dy + dz * dz
            dA, dB = d2(pi0, pm2), d2(pm1, pi2)
            use_a = dA < dB
            if dA == dB:
                use_a = min(pi0, pm2) < min(pm1, pi2) or (min(pi0, pm2) == min(pm1, pi2) and
                                                          max(pi0, pm2) < max(pm1, pi2))
            parts = [(s1, i1, s2)]
            parts += [(i0, s1, s2), (i0, s2, i2)] if use_a else [(i0, s1, i2), (s1, s2, i2)]

        def slot_pos(s):
            return c[s] if s < 3 else m[s - 3]
        for k in range(3):
            if m[k] >= 0:
                self.sub_edges.add(edge_key(c[k], m[k]))
                self.sub_edges.add(edge_key(m[k], c[(k + 1) % 3]))
        src.removed = True
        if kind == DRAWN:
            def own(k, q):
                return src.st[k][q] if q < 2 else src.rgba[k][q - 2]

            def val(s, q):
                """a corner's own value; a midpoint's as geometry.py _split splits them, or along the edge
                where the corner isn't its middle (a seam closed)"""
                if s < 3:
                    return own(s, q)
                k = s - 3
                A, B, P = self.pos[c[k]].cur, self.pos[c[(k + 1) % 3]].cur, self.pos[m[k]].cur
                if P[0] == mid_of(A[0], B[0]) and P[1] == mid_of(A[1], B[1]) and P[2] == mid_of(A[2], B[2]):
                    return mid_of(own(k, q), own((k + 1) % 3, q))
                ex, ey, ez = float(B[0] - A[0]), float(B[1] - A[1]), float(B[2] - A[2])
                l2 = ex * ex + ey * ey + ez * ez
                t = ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / l2 if l2 > 0 else 0.5
                t = max(0.0, min(1.0, t))
                return lround(own(k, q) + (own((k + 1) % 3, q) - own(k, q)) * t)
            sel = i in selected
            for q in parts:
                n = Tri()
                n.after = src.orig_id if src.orig_id >= 0 else src.after
                n.state = src.state
                n.flag = 0
                n.pos = [slot_pos(s) for s in q]
                n.st = [[s16(val(s, 0)), s16(val(s, 1))] for s in q]
                n.rgba = [[max(0, min(255, val(s, 2 + r))) for r in range(4)] for s in q]
                self.tris[DRAWN].append(n)
                if sel:
                    selected.add(len(self.tris[DRAWN]) - 1)
        elif kind == FLOOR:
            for q in parts:
                n = Tri()
                n.surface, n.flag = src.surface, src.flag
                n.pos = [slot_pos(s) for s in q]
                self.tris[FLOOR].append(n)
        else:
            for q in parts:
                n = Tri()
                n.set, n.heading, n.side, n.fail = src.set, src.heading, src.side, src.fail
                n.pos = [slot_pos(s) for s in q]
                self.tris[WALL].append(n)

    def seams_of(self, only=None):
        """[(corner, a, b)]: corners lying on another live triangle's edge a-b (only: those corners, and any
        corner on a half of an edge this step split; else the corners the edits made)"""
        used = [0] * len(self.pos)
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                if not t.removed:
                    for p in t.pos:
                        used[p] = 1
        cells = {}
        for p in range(len(self.pos)):
            if used[p] and (only is not None or not self.pos[p].level):
                cells.setdefault((self.pos[p].cur[0] >> 8, self.pos[p].cur[2] >> 8), []).append(p)
        if not cells:
            return []
        done = set()
        hits = []
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                if t.removed:
                    continue
                c = t.pos
                for k in range(3):
                    a, b = c[k], c[(k + 1) % 3]
                    if a == b:
                        continue
                    key = edge_key(a, b)
                    if key in done:
                        continue
                    done.add(key)
                    new_edge = only is not None and key in self.sub_edges
                    A, B = self.pos[a].cur, self.pos[b].cur
                    ex, ey, ez = float(B[0] - A[0]), float(B[1] - A[1]), float(B[2] - A[2])
                    l2 = ex * ex + ey * ey + ez * ez
                    if l2 < 25:
                        continue
                    x0, x1 = (min(A[0], B[0]) - 1) >> 8, (max(A[0], B[0]) + 1) >> 8
                    z0, z1 = (min(A[2], B[2]) - 1) >> 8, (max(A[2], B[2]) + 1) >> 8
                    for cx in range(x0, x1 + 1):
                        for cz in range(z0, z1 + 1):
                            for p in cells.get((cx, cz), ()):
                                if p == a or p == b:
                                    continue
                                if only is not None and not new_edge and not (p < len(only) and only[p]):
                                    continue
                                P = self.pos[p].cur
                                if not on_edge(P, A, B):
                                    continue
                                tt = ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / l2
                                hits.append((key, abs(tt - 0.5), p, a, b))
        hits.sort(key=lambda h: (h[0], h[1], h[2]))
        return [(p, a, b) for _, _, p, a, b in hits]

    def close_seams(self, fresh, selected):
        any_ = False
        for _ in range(16):
            only = None
            if fresh is not None:
                only = list(fresh) + [0] * max(0, len(self.pos) - len(fresh))
            ss = self.seams_of(only)
            if not ss:
                break
            ids = {}
            for kind in (DRAWN, FLOOR, WALL):
                for i, t in enumerate(self.tris[kind]):
                    if not t.removed:
                        c = t.pos
                        for k in range(3):
                            ids.setdefault(edge_key(c[k], c[(k + 1) % 3]), []).append((kind, i))
            added = False
            for p, a, b in ss:
                key = edge_key(a, b)
                if key in self.mid:
                    continue
                if not all(self.splittable(kind, i) for kind, i in ids.get(key, [])):
                    continue
                self.mid[key] = p
                added = True
            if not added:
                break
            self.split_all(selected)
            any_ = True
        return any_

    def settle(self):
        """the followers (midpoints on an edge a neighbour keeps whole) to their edges' middles"""
        live = set()
        for kind in (DRAWN, FLOOR):
            for t in self.tris[kind]:
                if not t.removed:
                    c = t.pos
                    for k in range(3):
                        live.add(edge_key(c[k], c[(k + 1) % 3]))
        for p, key in sorted((p, key) for key, p in self.mid.items()):
            a, b = key
            if key in live:
                self.pos[p].pa, self.pos[p].pb = a, b
                A, B = self.pos[a].cur, self.pos[b].cur
                self.pos[p].cur = (mid_of(A[0], B[0]), mid_of(A[1], B[1]), mid_of(A[2], B[2]))
            else:
                self.pos[p].pa = self.pos[p].pb = -1
        self.mark_followers()

    # ---------------------------------------------------------- steps
    def subdivide(self, render_ids):
        """SUBDIVIDE: the drawn triangles render_ids (model indexes: the level's render ids, then the
        triangles the steps added, in order) split four ways; last_split: their pieces"""
        D = self.tris[DRAWN]
        for t in render_ids:
            if not 0 <= t < len(D) or D[t].removed:
                raise TerrainError("a triangle that isn't there")
            if self.frozen(t):
                raise TerrainError("a triangle of a list kept as it is (a bridge's): it can't be split")
            if any(self.pos[p].prot for p in D[t].pos):
                raise TerrainError("a triangle with a corner of a bridge, a moving part, a switch or a gate: "
                                   "it can't be split")
        tris = sorted(set(render_ids))
        self.sub_edges = set()
        np0 = len(self.pos)
        on_edge_ids = {}
        fixed_edges = []
        for kind in (DRAWN, FLOOR, WALL):
            for i, t in enumerate(self.tris[kind]):
                if t.removed:
                    continue
                c = t.pos
                can = self.splittable(kind, i)
                for k in range(3):
                    on_edge_ids.setdefault(edge_key(c[k], c[(k + 1) % 3]), []).append((kind, i))
                    if not can:
                        fixed_edges.append((c[k], c[(k + 1) % 3]))
        # the level's corners live triangles use, not protected, not a midpoint already: one lying on a split
        # edge within WELD_TOL of its middle is the midpoint (terrain.cpp)
        used_cells = {}
        self.last_snapped = 0
        if self.weld:
            used = [0] * len(self.pos)
            for kind in (DRAWN, FLOOR, WALL):
                for t in self.tris[kind]:
                    if not t.removed:
                        for p in t.pos:
                            used[p] = 1
            for p in self.mid.values():
                used[p] = 0
            for p in range(len(self.pos)):
                if used[p] and self.pos[p].level and not self.pos[p].prot:
                    used_cells.setdefault((self.pos[p].cur[0] >> 4, self.pos[p].cur[2] >> 4), []).append(p)
        selected = set()
        for t in tris:
            selected.add(t)
            for k in range(3):
                a, b = D[t].pos[k], D[t].pos[(k + 1) % 3]
                key = edge_key(a, b)
                if key in self.mid:
                    continue
                ok = a != b and all(self.splittable(kind, i) for kind, i in on_edge_ids.get(key, []))
                A, B = self.pos[a].cur, self.pos[b].cur
                v = (mid_of(A[0], B[0]), mid_of(A[1], B[1]), mid_of(A[2], B[2]))
                snap, sd = -1, WELD_TOL + 1
                if ok:
                    for dx in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            for p in used_cells.get(((v[0] >> 4) + dx, (v[2] >> 4) + dz), ()):
                                if p == a or p == b:
                                    continue
                                d = cheb(self.pos[p].cur, v)
                                if d > WELD_TOL or not near_edge(self.pos[p].cur, A, B):
                                    continue
                                if d < sd or (d == sd and p < snap):
                                    sd, snap = d, p
                if snap >= 0:
                    v = self.pos[snap].cur
                q = 0
                while ok and q < len(fixed_edges):
                    ok = not on_edge(v, self.pos[fixed_edges[q][0]].cur, self.pos[fixed_edges[q][1]].cur)
                    q += 1
                if not ok:
                    continue
                if snap >= 0:
                    self.mid[key] = snap
                    self.last_snapped += 1
                else:
                    self.mid[key] = self._add(v, False)
        self.split_all(selected)
        fresh = [0] * len(self.pos)
        for p in range(np0, len(self.pos)):
            fresh[p] = 1
        self.close_seams(fresh, selected)
        self.last_split = sorted(t for t in selected if not D[t].removed)
        self.settle()

    def seal(self):
        """SEAL: corners already on another triangle's edge (the edits' own) closed; False: none"""
        self.sub_edges = set()
        r = self.close_seams(None, set())
        self.last_split = []
        self.settle()
        return r

    def movable(self, p):
        return 0 <= p < len(self.pos) and not self.pos[p].prot and self.pos[p].pa < 0 and self.pos[p].host < 0

    def move(self, moves):
        """MOVE: {corner: (x, y, z)} (with the welds: last_weld says what they added)"""
        for p in moves:
            if not self.movable(p):
                raise TerrainError("a corner of a bridge, a moving part, a switch or a gate can't be moved")
        moves = {p: tuple(v) for p, v in moves.items()}
        full, info = self.resolve(moves)
        taken = 0
        if self.weld:
            extra, taken = self._split_crossings(full)
            if taken:
                m2 = dict(moves)
                m2.update(extra)
                full, info = self.resolve(m2)
        info["tjunctions"] = taken
        self.last_weld = info
        for p, v in full.items():
            self.pos[p].cur = tuple(v)
        self.settle()

    # ---------------------------------------------------------- corners on two edges (terrain.cpp splitCrossings)
    def _live_edges(self):
        live = set()
        for kind in (DRAWN, FLOOR, WALL):
            for t in self.tris[kind]:
                if not t.removed:
                    c = t.pos
                    for k in range(3):
                        live.add(edge_key(c[k], c[(k + 1) % 3]))
        return live

    def _predict(self, ri, N):
        """the displacement following rel ri gives its corner under the moves N (resolve's formula)"""
        p, a, b, t, _ = self.rels[ri]
        oa, ob, op = self.pos[a].orig, self.pos[b].orig, self.pos[p].orig
        a1 = N[a] if a in N else self.pos[a].cur
        b1 = N[b] if b in N else self.pos[b].cur
        d = [lround((1.0 - t) * (a1[k] - oa[k]) + t * (b1[k] - ob[k])) for k in range(3)]
        np_ = tuple(clamp16(op[k] + d[k]) for k in range(3))
        return tuple(np_[k] - op[k] for k in range(3))

    def _split_crossings(self, N):
        """A corner lying on two (or more) edges of other meshes (crossing ones: overlapping meshes) follows only
        one; when the move N would take the edges apart there (more than SPLIT_GAP), every edge it lies on is split
        at it (a T-junction fixed: the corner becomes theirs), and so is each half at the other corners following
        that edge (so none is left on a half).  -> ({corner: place} to add to the move: the crossing corner at the
        mean of the places its edges gave it, the corners taken in where they were following to), corners taken in.
        Edges a triangle that can't split has (a bridge's, a protected corner's) stay whole."""
        if not self.rels:
            return {}, 0
        live = self._live_edges()
        by_p = {}
        on_edge = {}
        for ri, r in enumerate(self.rels):
            if (r[1], r[2]) in live:
                by_p.setdefault(r[0], []).append(ri)
                on_edge.setdefault((r[1], r[2]), []).append(ri)
        ids = {}
        for kind in (DRAWN, FLOOR, WALL):
            for i, t in enumerate(self.tris[kind]):
                if not t.removed:
                    c = t.pos
                    for k in range(3):
                        ids.setdefault(edge_key(c[k], c[(k + 1) % 3]), []).append((kind, i))
        groups = sorted(set(self.grp[p] for p in by_p))
        cross = []          # (group, [rel indexes on distinct edges], mean displacement)
        for g in groups:
            rs = []
            seen_e = set()
            for q in self.group_members(g):
                for ri in by_p.get(q, ()):
                    e = (self.rels[ri][1], self.rels[ri][2])
                    if e not in seen_e:
                        seen_e.add(e)
                        rs.append(ri)
            if len(rs) < 2:
                continue
            ds = [self._predict(ri, N) for ri in rs]
            gap = max(max(abs(x[k] - y[k]) for k in range(3)) for x in ds for y in ds)
            if gap <= SPLIT_GAP:
                continue
            mean = tuple(lround(sum(d[k] for d in ds) / float(len(ds))) for k in range(3))
            cross.append((g, rs, mean))
        if not cross:
            return {}, 0
        # the crossing corners first, on each of their edges; then the halves, at the corners following those edges
        pending = []
        for g, rs, _ in cross:
            for ri in rs:
                pending.append(((self.rels[ri][1], self.rels[ri][2]), ri))
        taken = set()
        jobs = []           # (edge, pivot corner, [(t, corner)] the others on it, along it)
        for e, ri in pending:
            others = [(self.rels[rj][3], self.rels[rj][0]) for rj in on_edge.get(e, ()) if rj != ri]
            jobs.append((e, self.rels[ri][0], self.rels[ri][3], others))
        rounds = 0
        while jobs and rounds < 32:
            rounds += 1
            did = False
            nxt = []
            for e, piv, tp, others in jobs:
                if e in self.mid or piv in e or not all(self.splittable(kind, i) for kind, i in ids.get(e, [])) or \
                        not ids.get(e):
                    continue
                self.mid[e] = piv
                taken.add(piv)
                did = True
                gp = self.grp[piv] if piv < len(self.grp) else piv
                others = [(t, q) for t, q in others if q != piv and (q >= len(self.grp) or self.grp[q] != gp)]
                lo = [(t / tp if tp > 0 else 0.0, q) for t, q in others if t < tp]
                hi = [((t - tp) / (1.0 - tp) if tp < 1 else 1.0, q) for t, q in others if t >= tp]
                # (each half's corners along it from its lower-numbered end, as a rel's t)
                for origin, other, lst in ((e[0], piv, lo), (piv, e[1], hi)):
                    if not lst:
                        continue
                    half = edge_key(origin, other)
                    if half[0] != origin:
                        lst = [(1.0 - t, q) for t, q in lst]
                    lst.sort(key=lambda x: (abs(x[0] - 0.5), x[1]))
                    t0, q0 = lst[0]
                    g0 = self.grp[q0] if q0 < len(self.grp) else q0
                    rest = [x for x in lst[1:] if x[1] != q0 and (x[1] >= len(self.grp) or self.grp[x[1]] != g0)]
                    nxt.append((half, q0, t0, rest))
            if not did:
                break
            self.split_all(set())
            ids = {}
            for kind in (DRAWN, FLOOR, WALL):
                for i, t in enumerate(self.tris[kind]):
                    if not t.removed:
                        c = t.pos
                        for k in range(3):
                            ids.setdefault(edge_key(c[k], c[(k + 1) % 3]), []).append((kind, i))
            jobs = nxt
        if not taken:
            return {}, 0
        self.mark_followers()
        extra = {}
        for g, rs, mean in cross:
            if g in taken or any(q in taken for q in self.group_members(g)):
                o = self.pos[g].orig
                if self.movable(g):
                    extra[g] = tuple(clamp16(o[k] + mean[k]) for k in range(3))
        for q in sorted(taken):
            g = self.grp[q] if q < len(self.grp) else q
            if g in extra or q in extra:
                continue
            if q in N and self.movable(q):
                extra[q] = N[q]
        return extra, len(taken)

    # ---------------------------------------------------------- the edit
    def export(self):
        """the geometry edit (Geometry.apply_edit's form; terrain.cpp exportEdit's order)"""
        mv = [list(p.orig) + list(p.cur) for p in self.pos if p.level and p.cur != p.orig]
        rem = {"render": [], "ground": [], "walls_a": [], "walls_b": []}
        add = {"render": [], "ground": [], "walls_a": [], "walls_b": []}
        for d in self.tris[DRAWN]:
            if d.orig_id >= 0:
                if d.removed:
                    rem["render"].append(d.orig_id)
                continue
            if d.removed:
                continue
            add["render"].append({"after": d.after, "v": [list(self.pos[p].cur) + d.st[k] + d.rgba[k]
                                                          for k, p in enumerate(d.pos)], "flag": d.flag})
        for f in self.tris[FLOOR]:
            if f.orig_id >= 0:
                if f.removed:
                    rem["ground"].append(f.orig_id)
                continue
            if f.removed:
                continue
            add["ground"].append({"v": [q for p in f.pos for q in self.pos[p].cur], "surface": f.surface,
                                  "flag": f.flag})
        for w in self.tris[WALL]:
            key = "walls_b" if w.set else "walls_a"
            if w.orig_id >= 0:
                if w.removed:
                    rem[key].append(w.orig_id)
                continue
            if w.removed:
                continue
            add[key].append({"v": [q for p in w.pos for q in self.pos[p].cur], "heading": w.heading,
                             "side": w.side, "fail": w.fail})
        ed = {}
        if mv:
            ed["move"] = mv
        if any(rem.values()):
            ed["remove"] = rem
        add = {k: v for k, v in add.items() if v}
        if add:
            ed["add"] = add
        return ed
