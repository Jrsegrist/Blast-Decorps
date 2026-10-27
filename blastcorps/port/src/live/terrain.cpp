/* bc.exe's terrain tools, their model (see terrain.h). */
#include "terrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_set>

namespace live {
namespace terrain {

namespace {

int clamp16(double v) {
    const long r = std::lround(v);
    return int(std::max(-32768L, std::min(32767L, r)));
}

/* Python's (p + q) >> 1: the floor of the mean (geometry.py _mid) */
int midOf(int a, int b) {
    const int s = a + b;
    return s >= 0 ? s / 2 : -((-s + 1) / 2);
}

uint64_t edgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return uint64_t(uint32_t(a)) << 32 | uint32_t(b);
}

/* the height of triangle (p0, p1, p2) at (x, z) seen from above (false: outside or on its side) */
bool triY(const P3 &a, const P3 &b, const P3 &c, double x, double z, double &y) {
    const double d = double(b.z - c.z) * (a.x - c.x) + double(c.x - b.x) * (a.z - c.z);
    if (std::fabs(d) < 1e-9) return false;
    const double l0 = (double(b.z - c.z) * (x - c.x) + double(c.x - b.x) * (z - c.z)) / d;
    const double l1 = (double(c.z - a.z) * (x - c.x) + double(a.x - c.x) * (z - c.z)) / d;
    const double l2 = 1.0 - l0 - l1;
    const double e = -1e-9;
    if (l0 < e || l1 < e || l2 < e) return false;
    y = l0 * a.y + l1 * b.y + l2 * c.y;
    return true;
}

}  // namespace

uint64_t key3(const P3 &p) {
    return uint64_t(uint16_t(p.x)) << 32 | uint64_t(uint16_t(p.y)) << 16 | uint64_t(uint16_t(p.z));
}

double brushWeight(const Brush &b, double d) {
    if (!(b.radius > 0) || d > b.radius) return 0.0;
    const double f = std::max(0.0, std::min(1.0, b.falloff));
    const double inner = b.radius * (1.0 - f);
    if (d <= inner) return 1.0;
    const double t = (d - inner) / std::max(1e-9, b.radius - inner);   /* 0 .. 1 */
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

/* ------------------------------------------------------------------ opening */
Model::~Model() {
    close();
}

void Model::close() {
    pos0_.clear(), draw0_.clear(), floor0_.clear(), wall0_.clear(), levelPos0_.clear();
    lg_mesh_free(mesh_);
    mesh_ = nullptr;
    pos_.clear(), draw_.clear(), floor_.clear(), wall_.clear(), settimg_.clear();
    levelPos_.clear(), mid_.clear(), foreignMoves_.clear();
    nLevelWalls_[0] = nLevelWalls_[1] = 0;
    lastSplit_.clear(), lastInfo_ = SplitInfo();
    grp_.clear(), grpMembers_.clear(), rels_.clear(), hosts_.clear(), lastWeld_ = WeldInfo();
    indexOk_ = false;
}

int Model::addPos(const P3 &v, bool level) {
    Pos p;
    p.orig = p.cur = v;
    p.level = level;
    pos_.push_back(p);
    return int(pos_.size()) - 1;
}

bool Model::open(const uint8_t *file, size_t size, std::string &err) {
    close();
    char e[512];
    mesh_ = lg_mesh_open(file, size, e, sizeof e);
    if (mesh_ == nullptr) {
        err = e;
        return false;
    }
    auto corner = [&](const int16_t *p) {
        P3 v;
        v.x = p[0], v.y = p[1], v.z = p[2];
        const uint64_t k = key3(v);
        auto it = levelPos_.find(k);
        if (it != levelPos_.end()) return it->second;
        const int id = addPos(v, true);
        pos_[id].prot = lg_mesh_protected(mesh_, p) != 0;
        levelPos_[k] = id;
        return id;
    };
    for (int i = 0; i < lg_mesh_count(mesh_, LG_RENDER); i++) {
        const LgTri *t = lg_mesh_tri(mesh_, LG_RENDER, i);
        DrawTri d;
        d.origId = i, d.state = i, d.flag = t->flag;
        for (int c = 0; c < 3; c++) {
            d.pos[c] = corner(t->p[c]);
            d.st[c][0] = t->st[c][0], d.st[c][1] = t->st[c][1];
            memcpy(d.rgba[c], t->rgba[c], 4);
        }
        draw_.push_back(d);
    }
    for (int i = 0; i < lg_mesh_count(mesh_, LG_GROUND); i++) {
        const LgTri *t = lg_mesh_tri(mesh_, LG_GROUND, i);
        FloorTri f;
        f.origId = i, f.surface = t->flag, f.flag = t->b;
        for (int c = 0; c < 3; c++) f.pos[c] = corner(t->p[c]);
        floor_.push_back(f);
    }
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < lg_mesh_count(mesh_, LG_WALLS_A + s); i++) {
            const LgTri *t = lg_mesh_tri(mesh_, LG_WALLS_A + s, i);
            WallTri w;
            w.set = s, w.origId = i;
            w.heading = t->flag, w.side = t->b, w.fail = t->c;
            for (int c = 0; c < 3; c++) w.pos[c] = corner(t->p[c]);
            wall_.push_back(w);
            nLevelWalls_[s]++;
        }
    buildWelds();
    markFollowers();
    pos0_ = pos_, draw0_ = draw_, floor0_ = floor_, wall0_ = wall_, levelPos0_ = levelPos_;
    indexOk_ = false;
    return true;
}

void Model::reset() {
    if (!isOpen()) return;
    pos_ = pos0_, draw_ = draw0_, floor_ = floor0_, wall_ = wall0_;
    levelPos_ = levelPos0_;
    settimg_.clear(), mid_.clear(), foreignMoves_.clear();
    lastSplit_.clear(), lastInfo_ = SplitInfo(), lastWeld_ = WeldInfo();
    markFollowers();
    indexOk_ = false;
}

/* ------------------------------------------------------------------ welds */
static int cheb(const P3 &a, const P3 &b) {
    return std::max(std::abs(a.x - b.x), std::max(std::abs(a.y - b.y), std::abs(a.z - b.z)));
}

static bool onEdge(const P3 &P, const P3 &A, const P3 &B, double tol2 = 1.0);

/* P on segment A-B as the level's meshes meet: within kWeldTol of it (Rare's long edges run a unit or two off the
 * corners along them), more than 2 from either end */
static bool nearEdge(const P3 &P, const P3 &A, const P3 &B) {
    return onEdge(P, A, B, double(kWeldTol) * kWeldTol);
}

void Model::buildWelds() {
    const int nl = int(pos_.size());   /* (opening: every corner is the level's) */
    grp_.assign(size_t(nl), -1);
    grpMembers_.clear(), rels_.clear(), hosts_.clear();
    /* groups: corners within kWeldTol on every axis, chained (union-find; a group's id is its lowest member) */
    std::vector<int> par(static_cast<size_t>(nl));
    for (int i = 0; i < nl; i++) par[size_t(i)] = i;
    auto find = [&](int a) {
        while (par[size_t(a)] != a) par[size_t(a)] = par[size_t(par[size_t(a)])], a = par[size_t(a)];
        return a;
    };
    auto cellKey = [](int cx, int cz) { return uint64_t(uint32_t(cx)) << 32 | uint32_t(cz); };
    std::unordered_map<uint64_t, std::vector<int>> cells;
    for (int i = 0; i < nl; i++) cells[cellKey(pos_[size_t(i)].orig.x >> 4, pos_[size_t(i)].orig.z >> 4)].push_back(i);
    for (int i = 0; i < nl; i++) {
        const P3 &v = pos_[size_t(i)].orig;
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                auto it = cells.find(cellKey((v.x >> 4) + dx, (v.z >> 4) + dz));
                if (it == cells.end()) continue;
                for (int j : it->second) {
                    if (j <= i || cheb(v, pos_[size_t(j)].orig) > kWeldTol) continue;
                    const int ra = find(i), rb = find(j);
                    if (ra < rb) par[size_t(rb)] = ra;
                    else if (rb < ra) par[size_t(ra)] = rb;
                }
            }
    }
    for (int i = 0; i < nl; i++) {
        grp_[size_t(i)] = find(i);
        if (grp_[size_t(i)] != i) {
            std::vector<int> &m = grpMembers_[grp_[size_t(i)]];
            if (m.empty()) m.push_back(grp_[size_t(i)]);
            m.push_back(i);   /* (ascending: i grows) */
        }
    }
    /* the level's corners lying on another triangle's edge (any set: drawn, floor, walls), not protected, not of
     * the edge's own groups */
    std::unordered_map<uint64_t, std::vector<int>> big;
    for (int i = 0; i < nl; i++)
        if (!pos_[size_t(i)].prot) big[cellKey(pos_[size_t(i)].orig.x >> 6, pos_[size_t(i)].orig.z >> 6)].push_back(i);
    /* the drawn and floor meshes' open edges (one triangle of the set has them) and their corners; every edge (a
     * wider seam is between meshes, not across a thin triangle of one) */
    std::unordered_map<uint64_t, int> uses[2];
    std::unordered_set<uint64_t> triEdges, openEdges;
    std::unordered_set<int> openEnds;
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++) {
            const int *c = corners(kind, i);
            for (int k = 0; k < 3; k++) {
                const uint64_t key = edgeKey(c[k], c[(k + 1) % 3]);
                triEdges.insert(key);
                if (kind < 2) uses[kind][key]++;
            }
        }
    for (int kind = 0; kind < 2; kind++)
        for (const auto &u : uses[kind])
            if (u.second == 1 && (u.first >> 32) != (u.first & 0xFFFFFFFFu)) {
                openEdges.insert(u.first);
                openEnds.insert(int(u.first >> 32)), openEnds.insert(int(u.first & 0xFFFFFFFFu));
            }
    std::unordered_set<uint64_t> done;
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++) {
            const int *c = corners(kind, i);
            for (int k = 0; k < 3; k++) {
                const int a = std::min(c[k], c[(k + 1) % 3]), b = std::max(c[k], c[(k + 1) % 3]);
                if (a == b || !done.insert(edgeKey(a, b)).second) continue;
                const P3 &A = pos_[size_t(a)].orig, &B = pos_[size_t(b)].orig;
                const double ex = B.x - A.x, ey = B.y - A.y, ez = B.z - A.z, l2 = ex * ex + ey * ey + ez * ez;
                if (l2 < 25) continue;
                const bool seam = openEdges.count(edgeKey(a, b)) != 0;
                const int x0 = (std::min(A.x, B.x) - kSeamTol) >> 6, x1 = (std::max(A.x, B.x) + kSeamTol) >> 6;
                const int z0 = (std::min(A.z, B.z) - kSeamTol) >> 6, z1 = (std::max(A.z, B.z) + kSeamTol) >> 6;
                for (int cx = x0; cx <= x1; cx++)
                    for (int cz = z0; cz <= z1; cz++) {
                        auto it = big.find(cellKey(cx, cz));
                        if (it == big.end()) continue;
                        for (int p : it->second) {
                            if (p == a || p == b || grp_[size_t(p)] == grp_[size_t(a)] || grp_[size_t(p)] == grp_[size_t(b)]) continue;
                            const P3 &P = pos_[size_t(p)].orig;
                            bool wide = false;
                            if (!nearEdge(P, A, B)) {
                                /* a wider seam: an open edge's corner along another mesh's open edge */
                                if (!seam || !openEnds.count(p) || triEdges.count(edgeKey(p, a)) || triEdges.count(edgeKey(p, b)) ||
                                    !onEdge(P, A, B, double(kSeamTol) * kSeamTol))
                                    continue;
                                wide = true;
                            }
                            Rel r;
                            r.p = p, r.a = a, r.b = b, r.wide = wide;
                            r.t = ((P.x - A.x) * ex + (P.y - A.y) * ey + (P.z - A.z) * ez) / l2;
                            rels_.push_back(r);
                        }
                    }
            }
        }
    std::sort(rels_.begin(), rels_.end(), [](const Rel &x, const Rel &y) {
        return x.p != y.p ? x.p < y.p : x.a != y.a ? x.a < y.a : x.b < y.b;
    });
}

void Model::markFollowers() {
    for (Pos &p : pos_) p.host = -1;
    hosts_.clear();
    if (rels_.empty()) return;
    std::unordered_set<uint64_t> live;
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++) {
            if (!alive(kind, i)) continue;
            const int *c = corners(kind, i);
            for (int k = 0; k < 3; k++) live.insert(edgeKey(c[k], c[(k + 1) % 3]));
        }
    /* a group follows the first of its members' edges still there (one it lies on before one a wider seam away) */
    std::unordered_set<int> seen;
    for (int widePass = 0; widePass < 2; widePass++)
        for (size_t r = 0; r < rels_.size(); r++) {
            if (int(rels_[r].wide) != widePass || !live.count(edgeKey(rels_[r].a, rels_[r].b))) continue;
            const int g = grp_[size_t(rels_[r].p)];
            if (!seen.insert(g).second) continue;
            hosts_.push_back({g, int(r)});
        }
    std::sort(hosts_.begin(), hosts_.end());
    for (const auto &h : hosts_)
        for (int q : groupMembers(h.first))
            if (!pos_[size_t(q)].prot) pos_[size_t(q)].host = h.second;
}

std::vector<int> Model::groupMembers(int p) const {
    const int g = groupOf(p);
    if (g < 0) return {};
    auto it = grpMembers_.find(g);
    if (it == grpMembers_.end()) return {g};
    return it->second;
}

int Model::followerCount() const {
    int n = 0;
    for (const Pos &p : pos_) n += p.host >= 0;
    return n;
}

std::vector<std::pair<int, P3>> Model::resolve(const std::vector<std::pair<int, P3>> &moves, WeldInfo *info) const {
    WeldInfo wi;
    std::map<int, P3> N;
    for (const auto &m : moves) N[m.first] = m.second;
    auto clampP = [](int x, int y, int z) {
        P3 v;
        v.x = std::max(-32768, std::min(32767, x)), v.y = std::max(-32768, std::min(32767, y));
        v.z = std::max(-32768, std::min(32767, z));
        return v;
    };
    std::unordered_set<int> protSeen;
    /* groups: every member by the move of the lowest-numbered member moved */
    std::vector<int> expl;
    for (const auto &kv : N) expl.push_back(kv.first);
    std::unordered_set<int> doneG;
    for (int p : expl) {
        const int g = groupOf(p);
        if (g < 0 || !grpMembers_.count(g) || !doneG.insert(g).second) continue;
        const P3 &o = pos_[size_t(p)].orig, v = N[p];
        for (int q : grpMembers_.at(g)) {
            if (q == p) continue;
            if (pos_[size_t(q)].prot) {
                if (protSeen.insert(q).second) wi.protSkipped++;
                continue;
            }
            const P3 &oq = pos_[size_t(q)].orig;
            if (!std::binary_search(expl.begin(), expl.end(), q)) wi.partners++;
            N[q] = clampP(oq.x + v.x - o.x, oq.y + v.y - o.y, oq.z + v.z - o.z);
        }
    }
    /* followers: a group on an edge whose ends moved keeps its place along it (until nothing changes) */
    std::unordered_set<int> followed;
    for (int pass = 0; pass < 8; pass++) {
        bool changed = false;
        for (const auto &h : hosts_) {
            const Rel &r = rels_[size_t(h.second)];
            auto ia = N.find(r.a), ib = N.find(r.b);
            if (ia == N.end() && ib == N.end()) continue;
            const P3 &oa = pos_[size_t(r.a)].orig, &ob = pos_[size_t(r.b)].orig, &op = pos_[size_t(r.p)].orig;
            const P3 a1 = ia != N.end() ? ia->second : pos_[size_t(r.a)].cur;
            const P3 b1 = ib != N.end() ? ib->second : pos_[size_t(r.b)].cur;
            const double t = r.t;
            const int dx = int(std::lround((1.0 - t) * (a1.x - oa.x) + t * (b1.x - ob.x)));
            const int dy = int(std::lround((1.0 - t) * (a1.y - oa.y) + t * (b1.y - ob.y)));
            const int dz = int(std::lround((1.0 - t) * (a1.z - oa.z) + t * (b1.z - ob.z)));
            const P3 np = clampP(op.x + dx, op.y + dy, op.z + dz);
            const int ddx = np.x - op.x, ddy = np.y - op.y, ddz = np.z - op.z;
            for (int q : groupMembers(r.p)) {
                if (pos_[size_t(q)].prot) {
                    if (protSeen.insert(q).second) wi.protSkipped++;
                    continue;
                }
                const P3 &oq = pos_[size_t(q)].orig;
                const P3 v = clampP(oq.x + ddx, oq.y + ddy, oq.z + ddz);
                auto it = N.find(q);
                if (it == N.end() || it->second != v) {
                    N[q] = v;
                    followed.insert(q);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
    wi.followers = int(followed.size());
    if (info) *info = wi;
    return std::vector<std::pair<int, P3>>(N.begin(), N.end());
}

/* ------------------------------------------------------------------ corners on two edges */
P3 Model::predict(const Rel &r, const std::map<int, P3> &n) const {
    const P3 &oa = pos_[size_t(r.a)].orig, &ob = pos_[size_t(r.b)].orig, &op = pos_[size_t(r.p)].orig;
    auto ia = n.find(r.a), ib = n.find(r.b);
    const P3 a1 = ia != n.end() ? ia->second : pos_[size_t(r.a)].cur;
    const P3 b1 = ib != n.end() ? ib->second : pos_[size_t(r.b)].cur;
    const double t = r.t;
    const int dx = int(std::lround((1.0 - t) * (a1.x - oa.x) + t * (b1.x - ob.x)));
    const int dy = int(std::lround((1.0 - t) * (a1.y - oa.y) + t * (b1.y - ob.y)));
    const int dz = int(std::lround((1.0 - t) * (a1.z - oa.z) + t * (b1.z - ob.z)));
    P3 d;
    d.x = std::max(-32768, std::min(32767, op.x + dx)) - op.x;
    d.y = std::max(-32768, std::min(32767, op.y + dy)) - op.y;
    d.z = std::max(-32768, std::min(32767, op.z + dz)) - op.z;
    return d;
}

/* A corner lying on two (or more) edges of other meshes (crossing ones: overlapping meshes) follows only one; when
 * the move n would take the edges apart there (more than kSplitGap), every edge it lies on is split at it (a
 * T-junction fixed: the corner becomes theirs), and so is each half at the other corners following that edge (so
 * none is left on a half).  Edges a triangle that can't split has (a bridge's, a protected corner's) stay whole.
 * (terrain.py _split_crossings, line by line) */
int Model::splitCrossings(const std::map<int, P3> &n, std::map<int, P3> &extra) {
    extra.clear();
    if (rels_.empty()) return 0;
    std::unordered_set<uint64_t> live;
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++)
            if (alive(kind, i)) {
                const int *c = corners(kind, i);
                for (int k = 0; k < 3; k++) live.insert(edgeKey(c[k], c[(k + 1) % 3]));
            }
    std::map<int, std::vector<int>> byP;
    std::unordered_map<uint64_t, std::vector<int>> onEdgeR;
    for (size_t ri = 0; ri < rels_.size(); ri++) {
        const Rel &r = rels_[ri];
        if (!live.count(edgeKey(r.a, r.b))) continue;
        byP[r.p].push_back(int(ri));
        onEdgeR[edgeKey(r.a, r.b)].push_back(int(ri));
    }
    std::unordered_map<uint64_t, std::vector<std::pair<int, int>>> ids;
    auto buildIds = [&]() {
        ids.clear();
        for (int kind = 0; kind < 3; kind++)
            for (int i = 0; i < count(kind); i++) {
                if (!alive(kind, i)) continue;
                const int *c = corners(kind, i);
                for (int k = 0; k < 3; k++) ids[edgeKey(c[k], c[(k + 1) % 3])].push_back({kind, i});
            }
    };
    buildIds();
    std::set<int> groups;
    for (const auto &kv : byP) groups.insert(grp_[size_t(kv.first)]);
    struct Cross {
        int g;
        std::vector<int> rs;
        P3 mean;
    };
    std::vector<Cross> cross;
    for (int g : groups) {
        std::vector<int> rs;
        std::unordered_set<uint64_t> seenE;
        for (int q : groupMembers(g)) {
            auto it = byP.find(q);
            if (it == byP.end()) continue;
            for (int ri : it->second)
                if (seenE.insert(edgeKey(rels_[size_t(ri)].a, rels_[size_t(ri)].b)).second) rs.push_back(ri);
        }
        if (rs.size() < 2) continue;
        std::vector<P3> ds;
        for (int ri : rs) ds.push_back(predict(rels_[size_t(ri)], n));
        int gap = 0;
        for (const P3 &x : ds)
            for (const P3 &y : ds)
                gap = std::max(gap, std::max(std::abs(x.x - y.x), std::max(std::abs(x.y - y.y), std::abs(x.z - y.z))));
        if (gap <= kSplitGap) continue;
        double sx = 0, sy = 0, sz = 0;
        for (const P3 &d : ds) sx += d.x, sy += d.y, sz += d.z;
        Cross c;
        c.g = g, c.rs = rs;
        c.mean.x = int(std::lround(sx / double(ds.size())));
        c.mean.y = int(std::lround(sy / double(ds.size())));
        c.mean.z = int(std::lround(sz / double(ds.size())));
        cross.push_back(c);
    }
    if (cross.empty()) return 0;
    /* the crossing corners first, on each of their edges; then the halves, at the corners following those edges */
    struct Job {
        uint64_t e;
        int piv;
        double tp;
        std::vector<std::pair<double, int>> others;   /* (t along e from its lower-numbered end, corner) */
    };
    std::vector<Job> jobs;
    for (const Cross &c : cross)
        for (int ri : c.rs) {
            const Rel &r = rels_[size_t(ri)];
            Job j;
            j.e = edgeKey(r.a, r.b), j.piv = r.p, j.tp = r.t;
            for (int rj : onEdgeR[j.e])
                if (rj != ri) j.others.push_back({rels_[size_t(rj)].t, rels_[size_t(rj)].p});
            jobs.push_back(j);
        }
    const SplitInfo keepInfo = lastInfo_;   /* (what the last Split did: a move's splits aren't one) */
    std::set<int> taken;
    std::unordered_map<int, char> none;
    auto grpOf = [&](int q) { return q >= 0 && q < int(grp_.size()) ? grp_[size_t(q)] : q; };
    for (int round = 0; round < 32 && !jobs.empty(); round++) {
        bool did = false;
        std::vector<Job> next;
        for (const Job &j : jobs) {
            const int ea = int(j.e >> 32), eb = int(j.e & 0xFFFFFFFFu);
            auto it = ids.find(j.e);
            if (mid_.count(j.e) || j.piv == ea || j.piv == eb || it == ids.end() || it->second.empty()) continue;
            bool ok = true;
            for (const auto &u : it->second) ok = ok && splittable(u.first, u.second);
            if (!ok) continue;
            mid_[j.e] = j.piv;
            taken.insert(j.piv);
            did = true;
            const int gp = grpOf(j.piv);
            std::vector<std::pair<double, int>> lo, hi;
            for (const auto &o : j.others) {
                if (o.second == j.piv || grpOf(o.second) == gp) continue;
                if (o.first < j.tp) lo.push_back({j.tp > 0 ? o.first / j.tp : 0.0, o.second});
                else hi.push_back({j.tp < 1 ? (o.first - j.tp) / (1.0 - j.tp) : 1.0, o.second});
            }
            /* (each half's corners along it from its lower-numbered end, as a rel's t) */
            const int origins[2] = {ea, j.piv}, others2[2] = {j.piv, eb};
            std::vector<std::pair<double, int>> *lists[2] = {&lo, &hi};
            for (int h = 0; h < 2; h++) {
                std::vector<std::pair<double, int>> lst = *lists[h];
                if (lst.empty()) continue;
                const uint64_t half = edgeKey(origins[h], others2[h]);
                if (int(half >> 32) != origins[h])
                    for (auto &x : lst) x.first = 1.0 - x.first;
                std::stable_sort(lst.begin(), lst.end(), [](const std::pair<double, int> &x, const std::pair<double, int> &y) {
                    const double ax = std::fabs(x.first - 0.5), ay = std::fabs(y.first - 0.5);
                    return ax != ay ? ax < ay : x.second < y.second;
                });
                Job nj;
                nj.e = half, nj.piv = lst[0].second, nj.tp = lst[0].first;
                const int g0 = grpOf(nj.piv);
                for (size_t k = 1; k < lst.size(); k++)
                    if (lst[k].second != nj.piv && grpOf(lst[k].second) != g0) nj.others.push_back(lst[k]);
                next.push_back(nj);
            }
        }
        if (!did) break;
        splitAll(none);
        buildIds();
        jobs.swap(next);
    }
    lastInfo_ = keepInfo;
    if (taken.empty()) return 0;
    markFollowers();
    for (const Cross &c : cross) {
        bool in = taken.count(c.g) != 0;
        for (int q : groupMembers(c.g)) in = in || taken.count(q) != 0;
        if (!in || !movable(c.g)) continue;
        const P3 &o = pos_[size_t(c.g)].orig;
        P3 v;
        v.x = std::max(-32768, std::min(32767, o.x + c.mean.x));
        v.y = std::max(-32768, std::min(32767, o.y + c.mean.y));
        v.z = std::max(-32768, std::min(32767, o.z + c.mean.z));
        extra[c.g] = v;
    }
    for (int q : taken) {
        const int g = grpOf(q);
        if (extra.count(g) || extra.count(q)) continue;
        auto it = n.find(q);
        if (it != n.end() && movable(q)) extra[q] = it->second;
    }
    return int(taken.size());
}

const LgTri *Model::stateOf(int t) const {
    if (t < 0 || t >= int(draw_.size())) return nullptr;
    return lg_mesh_tri(mesh_, LG_RENDER, draw_[t].state);
}

int Model::posAt(const P3 &v) const {
    auto it = levelPos_.find(key3(v));
    if (it != levelPos_.end() && pos_[it->second].cur == v) return it->second;
    for (size_t i = 0; i < pos_.size(); i++)
        if (pos_[i].cur == v) return int(i);
    return -1;
}

/* ------------------------------------------------------------------ the index */
void Model::rebuildIndex() const {
    nb_.assign(pos_.size(), std::vector<int>());
    live_.assign(pos_.size(), 0);
    std::unordered_set<uint64_t> seen;
    auto edge = [&](int a, int b) {
        if (a < 0 || b < 0 || a == b) return;
        if (!seen.insert(edgeKey(a, b)).second) return;
        nb_[a].push_back(b), nb_[b].push_back(a);
    };
    for (const DrawTri &d : draw_) {
        if (d.removed) continue;
        for (int c = 0; c < 3; c++) live_[d.pos[c]] = 1, edge(d.pos[c], d.pos[(c + 1) % 3]);
    }
    for (const FloorTri &f : floor_) {
        if (f.removed) continue;
        for (int c = 0; c < 3; c++) live_[f.pos[c]] = 1, edge(f.pos[c], f.pos[(c + 1) % 3]);
    }
    for (auto &v : nb_) std::sort(v.begin(), v.end());
    indexOk_ = true;
}

const std::vector<int> &Model::neighbours(int p) const {
    static const std::vector<int> none;
    if (!indexOk_) rebuildIndex();
    return p >= 0 && p < int(nb_.size()) ? nb_[p] : none;
}

std::vector<int> Model::nearby(double x, double z, double r) const {
    if (!indexOk_) rebuildIndex();
    std::vector<int> out;
    for (size_t i = 0; i < pos_.size(); i++) {
        if (!live_[i]) continue;
        const double dx = pos_[i].cur.x - x, dz = pos_[i].cur.z - z;
        if (dx * dx + dz * dz <= r * r) out.push_back(int(i));
    }
    return out;
}

/* the followers to their edges' middles, in the order they were made (one may follow another) */
void Model::settle() {
    std::unordered_set<uint64_t> live;
    for (const DrawTri &d : draw_)
        if (!d.removed)
            for (int c = 0; c < 3; c++) live.insert(edgeKey(d.pos[c], d.pos[(c + 1) % 3]));
    for (const FloorTri &f : floor_)
        if (!f.removed)
            for (int c = 0; c < 3; c++) live.insert(edgeKey(f.pos[c], f.pos[(c + 1) % 3]));
    std::vector<std::pair<int, uint64_t>> order;
    for (const auto &kv : mid_) order.push_back({kv.second, kv.first});
    std::sort(order.begin(), order.end());
    for (const auto &m : order) {
        const int a = int(m.second >> 32), b = int(m.second & 0xFFFFFFFFu), p = m.first;
        if (live.count(m.second)) {
            pos_[p].pa = a, pos_[p].pb = b;
            pos_[p].cur.x = midOf(pos_[a].cur.x, pos_[b].cur.x);
            pos_[p].cur.y = midOf(pos_[a].cur.y, pos_[b].cur.y);
            pos_[p].cur.z = midOf(pos_[a].cur.z, pos_[b].cur.z);
        } else {
            pos_[p].pa = pos_[p].pb = -1;
        }
    }
    markFollowers();
    indexOk_ = false;
}

/* ------------------------------------------------------------------ splitting */
int Model::count(int kind) const {
    return kind == 0 ? int(draw_.size()) : kind == 1 ? int(floor_.size()) : int(wall_.size());
}

const int *Model::corners(int kind, int i) const {
    return kind == 0 ? draw_[i].pos : kind == 1 ? floor_[i].pos : wall_[i].pos;
}

bool Model::alive(int kind, int i) const {
    return !(kind == 0 ? draw_[i].removed : kind == 1 ? floor_[i].removed : wall_[i].removed);
}

bool Model::splittable(int kind, int i) const {
    if (!alive(kind, i)) return true;   /* (gone: nothing to keep whole) */
    const int *c = corners(kind, i);
    for (int k = 0; k < 3; k++)
        if (pos_[c[k]].prot) return false;
    if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]) return false;   /* (a degenerate triangle: left as it is) */
    if (kind == 0) {
        const LgTri *st = stateOf(i);
        if (st == nullptr || st->frozen) return false;
    }
    return true;
}

void Model::splitAll(std::unordered_map<int, char> &selected) {
    for (int pass = 0; pass < 64; pass++) {
        bool changed = false;
        for (int kind = 0; kind < 3; kind++) {
            const int n = count(kind);
            for (int i = 0; i < n; i++) {
                if (!alive(kind, i) || !splittable(kind, i)) continue;
                const int *c = corners(kind, i);
                bool has = false;
                for (int k = 0; k < 3 && !has; k++) has = mid_.count(edgeKey(c[k], c[(k + 1) % 3])) != 0;
                if (!has) continue;
                splitOne(kind, i, selected);
                changed = true;
            }
        }
        if (!changed) break;
    }
    indexOk_ = false;
}

/* triangle i of `kind` split at the midpoints its edges have (mid_): 1 -> two triangles, 2 -> three, 3 -> four
 * (the four-way split as before); corner order (winding) kept */
void Model::splitOne(int kind, int i, std::unordered_map<int, char> &selected) {
    int c[3];
    memcpy(c, corners(kind, i), sizeof c);
    int m[3];   /* m[k]: the midpoint of edge c[k] - c[k+1] (-1: whole) */
    int nm = 0;
    for (int k = 0; k < 3; k++) {
        auto it = mid_.find(edgeKey(c[k], c[(k + 1) % 3]));
        m[k] = it != mid_.end() ? it->second : -1;
        nm += m[k] >= 0;
    }
    /* the pieces as triples of slots: 0..2 the corners, 3..5 the midpoints of edges 0..2 */
    std::vector<std::array<int, 3>> parts;
    if (nm == 3) {
        parts = {{0, 3, 5}, {3, 1, 4}, {5, 4, 2}, {3, 4, 5}};
    } else if (nm == 1) {
        const int j = m[0] >= 0 ? 0 : m[1] >= 0 ? 1 : 2;
        parts = {{j, 3 + j, (j + 2) % 3}, {3 + j, (j + 1) % 3, (j + 2) % 3}};
    } else {
        /* edges (i0, i1) and (i1, i2) split, (i2, i0) whole: the corner triangle (m1, i1, m2), the quad
         * (i0, m1, m2, i2) cut along its shorter diagonal (a tie: the lower corner ids; the same whatever the
         * triangle's corner order, so a floor twin is cut as its drawn triangle is) */
        const int e = m[0] < 0 ? 1 : m[1] < 0 ? 2 : 0;   /* the first split edge after the whole one */
        const int i0 = e, i1 = (e + 1) % 3, i2 = (e + 2) % 3;
        const int s1 = 3 + i0, s2 = 3 + i1;
        const int pi0 = c[i0], pm2 = m[i1], pm1 = m[i0], pi2 = c[i2];
        auto d2 = [&](int a, int b) {
            const double dx = pos_[a].cur.x - pos_[b].cur.x, dy = pos_[a].cur.y - pos_[b].cur.y, dz = pos_[a].cur.z - pos_[b].cur.z;
            return dx * dx + dy * dy + dz * dz;
        };
        const double dA = d2(pi0, pm2), dB = d2(pm1, pi2);
        bool useA = dA < dB;
        if (dA == dB) useA = std::min(pi0, pm2) < std::min(pm1, pi2) ||
                             (std::min(pi0, pm2) == std::min(pm1, pi2) && std::max(pi0, pm2) < std::max(pm1, pi2));
        parts = {{s1, i1, s2}};
        if (useA) parts.push_back({i0, s1, s2}), parts.push_back({i0, s2, i2});
        else parts.push_back({i0, s1, i2}), parts.push_back({s1, s2, i2});
    }
    auto slotPos = [&](int s) { return s < 3 ? c[s] : m[s - 3]; };
    for (int k = 0; k < 3; k++)
        if (m[k] >= 0) subEdges_[edgeKey(c[k], m[k])] = 1, subEdges_[edgeKey(m[k], c[(k + 1) % 3])] = 1;
    if (kind == 0) {
        const DrawTri d = draw_[i];
        /* the values at a slot: a corner's own; a midpoint's as geometry.py _split splits them (floors of the sums),
         * or along the edge where the corner isn't its middle (a seam closed) */
        auto val = [&](int s, int q) -> int {   /* q 0..1 s, t; 2..5 rgba */
            auto own = [&](int k) { return q < 2 ? int(d.st[k][q]) : int(d.rgba[k][q - 2]); };
            if (s < 3) return own(s);
            const int k = s - 3, a = c[k], b = c[(k + 1) % 3], p = m[k];
            const P3 &A = pos_[a].cur, &B = pos_[b].cur, &P = pos_[p].cur;
            if (P.x == midOf(A.x, B.x) && P.y == midOf(A.y, B.y) && P.z == midOf(A.z, B.z))
                return midOf(own(k), own((k + 1) % 3));
            const double ex = B.x - A.x, ey = B.y - A.y, ez = B.z - A.z, l2 = ex * ex + ey * ey + ez * ez;
            double t = l2 > 0 ? ((P.x - A.x) * ex + (P.y - A.y) * ey + (P.z - A.z) * ez) / l2 : 0.5;
            t = std::max(0.0, std::min(1.0, t));
            return int(std::lround(own(k) + (own((k + 1) % 3) - own(k)) * t));
        };
        const bool sel = selected.count(i) != 0;
        if (stepSel_.count(i)) lastInfo_.selected++;
        else if (!sel) lastInfo_.neighbours++;
        draw_[i].removed = true;
        for (const auto &q : parts) {
            DrawTri n;
            n.after = d.origId >= 0 ? d.origId : d.after;
            n.state = d.state;
            n.flag = 0;
            for (int k = 0; k < 3; k++) {
                n.pos[k] = slotPos(q[k]);
                n.st[k][0] = int16_t(val(q[k], 0)), n.st[k][1] = int16_t(val(q[k], 1));
                for (int r = 0; r < 4; r++) n.rgba[k][r] = uint8_t(std::max(0, std::min(255, val(q[k], 2 + r))));
            }
            draw_.push_back(n);
            if (sel) selected[int(draw_.size()) - 1] = 1;
        }
    } else if (kind == 1) {
        const FloorTri g = floor_[i];
        floor_[i].removed = true;
        lastInfo_.floors++;
        for (const auto &q : parts) {
            FloorTri n;
            n.surface = g.surface, n.flag = g.flag;
            for (int k = 0; k < 3; k++) n.pos[k] = slotPos(q[k]);
            floor_.push_back(n);
        }
    } else {
        const WallTri w = wall_[i];
        wall_[i].removed = true;
        lastInfo_.walls++;
        for (const auto &q : parts) {
            WallTri n;
            n.set = w.set, n.heading = w.heading, n.side = w.side, n.fail = w.fail;
            for (int k = 0; k < 3; k++) n.pos[k] = slotPos(q[k]);
            wall_.push_back(n);
        }
    }
}

std::vector<Model::Seam> Model::seams() const {
    return seamsOf(nullptr);
}

/* p on segment a-b: within a map unit of it (tol2: the square of another distance), more than 2 from either end
 * (Rare's meshes have corners a unit or two apart that stand for one: not a seam, and splitting there would only
 * make slivers) */
static bool onEdge(const P3 &P, const P3 &A, const P3 &B, double tol2) {
    const double ex = B.x - A.x, ey = B.y - A.y, ez = B.z - A.z, l2 = ex * ex + ey * ey + ez * ez;
    if (l2 < 25 || P == A || P == B) return false;
    const double t = ((P.x - A.x) * ex + (P.y - A.y) * ey + (P.z - A.z) * ez) / l2, len = std::sqrt(l2);
    if (t * len <= 2.0 || (1 - t) * len <= 2.0) return false;
    const double qx = A.x + ex * t - P.x, qy = A.y + ey * t - P.y, qz = A.z + ez * t - P.z;
    return qx * qx + qy * qy + qz * qz <= tol2;
}

std::vector<Model::Seam> Model::seamsOf(const std::vector<char> *only) const {
    std::vector<Seam> out;
    /* the corners the edits made that a live triangle uses (or those in `only`), in 256-unit x-z cells */
    std::vector<char> used(pos_.size(), 0);
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++)
            if (alive(kind, i))
                for (int k = 0; k < 3; k++) used[corners(kind, i)[k]] = 1;
    std::unordered_map<uint64_t, std::vector<int>> cells;
    auto cellKey = [](int cx, int cz) { return uint64_t(uint32_t(cx)) << 32 | uint32_t(cz); };
    /* (with `only`: also any corner on a half of an edge the step split (subEdges_): a corner the level had on the
     * whole edge is on one of the halves) */
    auto isOnly = [&](int p) { return only && size_t(p) < only->size() && (*only)[p]; };
    for (size_t p = 0; p < pos_.size(); p++)
        if (used[p] && (only ? true : !pos_[p].level)) cells[cellKey(pos_[p].cur.x >> 8, pos_[p].cur.z >> 8)].push_back(int(p));
    if (cells.empty()) return out;
    std::unordered_set<uint64_t> done;
    struct Hit {
        Seam s;
        uint64_t edge;
        double off;
    };
    std::vector<Hit> hits;
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < count(kind); i++) {
            if (!alive(kind, i)) continue;
            const int *c = corners(kind, i);
            for (int k = 0; k < 3; k++) {
                const int a = c[k], b = c[(k + 1) % 3];
                if (a == b || !done.insert(edgeKey(a, b)).second) continue;
                const bool newEdge = only && subEdges_.count(edgeKey(a, b)) != 0;
                const P3 &A = pos_[a].cur, &B = pos_[b].cur;
                const double ex = B.x - A.x, ey = B.y - A.y, ez = B.z - A.z, l2 = ex * ex + ey * ey + ez * ez;
                if (l2 < 25) continue;
                const int x0 = (std::min(A.x, B.x) - 1) >> 8, x1 = (std::max(A.x, B.x) + 1) >> 8;
                const int z0 = (std::min(A.z, B.z) - 1) >> 8, z1 = (std::max(A.z, B.z) + 1) >> 8;
                for (int cx = x0; cx <= x1; cx++)
                    for (int cz = z0; cz <= z1; cz++) {
                        auto it = cells.find(cellKey(cx, cz));
                        if (it == cells.end()) continue;
                        for (int p : it->second) {
                            if (p == a || p == b || (only && !newEdge && !isOnly(p))) continue;
                            const P3 &P = pos_[p].cur;
                            if (!onEdge(P, A, B)) continue;
                            const double t = ((P.x - A.x) * ex + (P.y - A.y) * ey + (P.z - A.z) * ez) / l2;
                            hits.push_back({{p, a, b}, edgeKey(a, b), std::fabs(t - 0.5)});
                        }
                    }
            }
        }
    std::sort(hits.begin(), hits.end(), [](const Hit &x, const Hit &y) {
        return x.edge != y.edge ? x.edge < y.edge : x.off != y.off ? x.off < y.off : x.s.p < y.s.p;
    });
    for (const Hit &h : hits) out.push_back(h.s);
    return out;
}

bool Model::closeSeams(const std::vector<char> *fresh, std::unordered_map<int, char> &selected) {
    bool any = false;
    for (int round = 0; round < 16; round++) {
        std::vector<char> only;
        if (fresh) {
            only = *fresh;
            only.resize(pos_.size(), 0);
        }
        const std::vector<Seam> ss = seamsOf(fresh ? &only : nullptr);
        if (ss.empty()) break;
        std::unordered_map<uint64_t, std::vector<std::pair<int, int>>> ids;
        for (int kind = 0; kind < 3; kind++)
            for (int i = 0; i < count(kind); i++) {
                if (!alive(kind, i)) continue;
                const int *c = corners(kind, i);
                for (int k = 0; k < 3; k++) ids[edgeKey(c[k], c[(k + 1) % 3])].push_back({kind, i});
            }
        bool added = false;
        for (const Seam &m : ss) {
            const uint64_t key = edgeKey(m.a, m.b);
            if (mid_.count(key)) continue;   /* (two corners on one edge: the nearer the middle first, the next round
                                                the other on its half) */
            bool ok = true;
            for (const auto &u : ids[key]) ok = ok && splittable(u.first, u.second);
            if (!ok) continue;
            mid_[key] = m.p;
            added = true;
            lastInfo_.seamsClosed++;
            if (lastInfo_.detail.size() < 600) {
                const P3 &P = pos_[m.p].cur, &A = pos_[m.a].cur, &B = pos_[m.b].cur;
                char b[160];
                snprintf(b, sizeof b, " [r%d (%d,%d,%d) on (%d,%d,%d)-(%d,%d,%d) x%u]", round, P.x, P.y, P.z, A.x, A.y, A.z,
                         B.x, B.y, B.z, unsigned(ids[key].size()));
                lastInfo_.detail += b;
            }
        }
        if (!added) break;
        lastInfo_.seamRounds++;
        splitAll(selected);
        any = true;
    }
    return any;
}

/* ------------------------------------------------------------------ steps */
bool Model::apply(const Step &s, std::string &err) {
    if (!isOpen()) {
        err = "no level";
        return false;
    }
    switch (s.kind) {
    case ST_MOVE: {
        for (const auto &m : s.moves)
            if (m.first < 0 || m.first >= int(pos_.size()) || !movable(m.first)) {
                err = "a corner of a bridge, a moving part, a switch or a gate can't be moved";
                return false;
            }
        lastWeld_ = WeldInfo();
        std::vector<std::pair<int, P3>> full = resolve(s.moves, &lastWeld_);
        {
            /* a corner on two edges the move pulls apart: the edges split at it, and the move again with it */
            std::map<int, P3> n(full.begin(), full.end()), extra;
            const int taken = splitCrossings(n, extra);
            if (taken) {
                std::map<int, P3> m2(s.moves.begin(), s.moves.end());
                for (const auto &kv : extra) m2[kv.first] = kv.second;
                full = resolve(std::vector<std::pair<int, P3>>(m2.begin(), m2.end()), &lastWeld_);
            }
            lastWeld_.tjunctions = taken;
        }
        for (const auto &m : full) pos_[size_t(m.first)].cur = m.second;
        settle();
        return true;
    }
    case ST_SUBDIVIDE: {
        for (int t : s.tris) {
            if (t < 0 || t >= int(draw_.size()) || draw_[t].removed) {
                err = "a triangle that isn't there";
                return false;
            }
            const LgTri *st = stateOf(t);
            if (st == nullptr || st->frozen) {
                err = "a triangle of a list kept as it is (a bridge's): it can't be split";
                return false;
            }
            for (int c = 0; c < 3; c++)
                if (pos_[draw_[t].pos[c]].prot) {
                    err = "a triangle with a corner of a bridge, a moving part, a switch or a gate: it can't be split";
                    return false;
                }
        }
        std::vector<int> tris = s.tris;
        std::sort(tris.begin(), tris.end());
        tris.erase(std::unique(tris.begin(), tris.end()), tris.end());
        lastInfo_ = SplitInfo();
        subEdges_.clear(), stepSel_.clear();
        for (int t : tris) stepSel_[t] = 1;
        const size_t np0 = pos_.size();
        /* the triangles on every edge (drawn, floor, walls): an edge gets a midpoint only if all of them can split,
         * and only if the midpoint doesn't lie on an edge of one that can't (a bridge's, a protected part's) */
        std::unordered_map<uint64_t, std::vector<std::pair<int, int>>> onEdgeIds;
        std::vector<std::pair<int, int>> fixedEdges;
        for (int kind = 0; kind < 3; kind++)
            for (int i = 0; i < count(kind); i++) {
                if (!alive(kind, i)) continue;
                const int *c = corners(kind, i);
                const bool canSplit = splittable(kind, i);
                for (int k = 0; k < 3; k++) {
                    onEdgeIds[edgeKey(c[k], c[(k + 1) % 3])].push_back({kind, i});
                    if (!canSplit) fixedEdges.push_back({c[k], c[(k + 1) % 3]});
                }
            }
        /* the level's corners live triangles use now, not protected, not a midpoint already (16-unit x-z cells): one
         * of another mesh lying on a split edge within kWeldTol of its middle is the midpoint (one corner, no sliver
         * beside it) */
        std::unordered_map<uint64_t, std::vector<int>> usedCells;
        {
            std::vector<char> used(pos_.size(), 0);
            for (int kind = 0; kind < 3; kind++)
                for (int i = 0; i < count(kind); i++)
                    if (alive(kind, i))
                        for (int k = 0; k < 3; k++) used[size_t(corners(kind, i)[k])] = 1;
            for (const auto &kv : mid_) used[size_t(kv.second)] = 0;
            for (size_t p = 0; p < pos_.size(); p++)
                if (used[p] && pos_[p].level && !pos_[p].prot)
                    usedCells[uint64_t(uint32_t(pos_[p].cur.x >> 4)) << 32 | uint32_t(pos_[p].cur.z >> 4)].push_back(int(p));
        }
        std::unordered_map<int, char> selected;
        for (int t : tris) {
            selected[t] = 1;
            for (int k = 0; k < 3; k++) {
                const int a = draw_[t].pos[k], b = draw_[t].pos[(k + 1) % 3];
                const uint64_t key = edgeKey(a, b);
                if (mid_.count(key)) continue;
                bool ok = a != b;
                for (const auto &u : onEdgeIds[key]) ok = ok && splittable(u.first, u.second);
                P3 v;
                v.x = midOf(pos_[a].cur.x, pos_[b].cur.x), v.y = midOf(pos_[a].cur.y, pos_[b].cur.y);
                v.z = midOf(pos_[a].cur.z, pos_[b].cur.z);
                int snap = -1, sd = kWeldTol + 1;
                for (int dx = -1; dx <= 1 && ok; dx++)
                    for (int dz = -1; dz <= 1; dz++) {
                        auto it = usedCells.find(uint64_t(uint32_t((v.x >> 4) + dx)) << 32 | uint32_t((v.z >> 4) + dz));
                        if (it == usedCells.end()) continue;
                        for (int p : it->second) {
                            if (p == a || p == b) continue;
                            const int d = cheb(pos_[size_t(p)].cur, v);
                            if (d > kWeldTol || !nearEdge(pos_[size_t(p)].cur, pos_[a].cur, pos_[b].cur)) continue;
                            if (d < sd || (d == sd && p < snap)) sd = d, snap = p;
                        }
                    }
                if (snap >= 0) v = pos_[size_t(snap)].cur;
                for (size_t q = 0; ok && q < fixedEdges.size(); q++)
                    ok = !onEdge(v, pos_[fixedEdges[q].first].cur, pos_[fixedEdges[q].second].cur);
                if (!ok) {
                    lastInfo_.keptEdges++;
                    continue;
                }
                if (snap >= 0) {
                    mid_[key] = snap;
                    lastInfo_.snapped++;
                } else {
                    mid_[key] = addPos(v, false);
                }
            }
        }
        splitAll(selected);
        /* a midpoint on an edge of a triangle that doesn't share its corners (Rare's meshes aren't always welded:
         * walls along a road, a long edge beside two short ones): that triangle split at it too */
        std::vector<char> fresh(pos_.size(), 0);
        for (size_t p = np0; p < pos_.size(); p++) fresh[p] = 1;
        closeSeams(&fresh, selected);
        lastSplit_.clear();
        for (const auto &kv : selected)
            if (!draw_[kv.first].removed) lastSplit_.push_back(kv.first);
        std::sort(lastSplit_.begin(), lastSplit_.end());
        settle();
        return true;
    }
    case ST_SEAL: {
        lastInfo_ = SplitInfo();
        subEdges_.clear(), stepSel_.clear();
        std::unordered_map<int, char> none;
        const bool any = closeSeams(nullptr, none);
        lastSplit_.clear();
        settle();
        if (!any) {
            err = "no seam to close (no corner lies on another triangle's edge, or the triangles there can't be split)";
            return false;
        }
        return true;
    }    case ST_PAINT: {
        const LgTri *run = lg_mesh_tri(mesh_, LG_RENDER, s.run);
        if (run == nullptr || run->frozen || run->tex < 0) {
            err = "that run can't take triangles (a list kept as it is, or untextured)";
            return false;
        }
        for (int t : s.tris) {
            if (t < 0 || t >= int(draw_.size()) || draw_[t].removed) {
                err = "a triangle that isn't there";
                return false;
            }
            const LgTri *st = stateOf(t);
            if (st == nullptr || st->frozen) {
                err = "a triangle of a list kept as it is (a bridge's) can't be painted";
                return false;
            }
        }
        /* planar: s = x * ds, t = z * dt, moved by whole texture periods so the selection stays near 0 */
        double ds = s.density, dt = s.density;
        if (s.planar && !(ds > 0)) {
            /* the run's own density: its triangle's (s, t) over (x, z) */
            const double x0 = run->p[0][0], z0 = run->p[0][2];
            const double ax = run->p[1][0] - x0, az = run->p[1][2] - z0, bx = run->p[2][0] - x0, bz = run->p[2][2] - z0;
            const double det = ax * bz - az * bx;
            if (std::fabs(det) > 1e-6) {
                const double s1 = run->st[1][0] - run->st[0][0], s2 = run->st[2][0] - run->st[0][0];
                const double t1 = run->st[1][1] - run->st[0][1], t2 = run->st[2][1] - run->st[0][1];
                const double sa = (s1 * bz - s2 * az) / det, sb = (ax * s2 - bx * s1) / det;
                const double ta = (t1 * bz - t2 * az) / det, tb = (ax * t2 - bx * t1) / det;
                ds = std::hypot(sa, sb), dt = std::hypot(ta, tb);
            }
            if (!(ds > 0.01)) ds = 8;
            if (!(dt > 0.01)) dt = 8;
        }
        long offS = 0, offT = 0;
        if (s.planar) {
            const long us = long(std::max(1, run->w)) * 32 * 4, ut = long(std::max(1, run->h)) * 32 * 4;
            double ms = 1e30, mt = 1e30;
            for (int t : s.tris)
                for (int k = 0; k < 3; k++) {
                    ms = std::min(ms, pos_[draw_[t].pos[k]].cur.x * ds);
                    mt = std::min(mt, pos_[draw_[t].pos[k]].cur.z * dt);
                }
            offS = long(std::floor(ms / us)) * us, offT = long(std::floor(mt / ut)) * ut;
        }
        std::vector<int> tris = s.tris;
        std::sort(tris.begin(), tris.end());
        tris.erase(std::unique(tris.begin(), tris.end()), tris.end());
        for (int t : tris) {
            DrawTri n = draw_[t];
            if (s.planar)
                for (int k = 0; k < 3; k++) {
                    n.st[k][0] = int16_t(clamp16(pos_[n.pos[k]].cur.x * ds - double(offS)));
                    n.st[k][1] = int16_t(clamp16(pos_[n.pos[k]].cur.z * dt - double(offT)));
                }
            n.after = s.run, n.state = s.run;
            if (draw_[t].origId >= 0) {
                draw_[t].removed = true;
                n.origId = -1;
                draw_.push_back(n);
            } else {
                draw_[t] = n;
            }
        }
        indexOk_ = false;
        return true;
    }
    case ST_RETEX: {
        bool found = false;
        for (int i = 0; i < lg_mesh_count(mesh_, LG_RENDER) && !found; i++) {
            const LgTri *t = lg_mesh_tri(mesh_, LG_RENDER, i);
            found = t->piece == s.piece && t->timg == s.k && !t->frozen;
        }
        if (!found) {
            err = "no such run (a list's G_SETTIMG) to change";
            return false;
        }
        for (auto &x : settimg_)
            if (x.piece == s.piece && x.k == s.k) {
                x.tex = s.tex;
                return true;
            }
        LgSettimg x;
        x.piece = s.piece, x.k = s.k, x.tex = s.tex;
        settimg_.push_back(x);
        return true;
    }
    default:
        err = "a step not known";
        return false;
    }
}

/* ------------------------------------------------------------------ the edit */
bool Model::import(const LgEdit &e, std::string &err) {
    if (!isOpen()) {
        err = "no level";
        return false;
    }
    for (int i = 0; i < e.nmove; i++) {
        P3 f, t;
        f.x = e.move[i].from[0], f.y = e.move[i].from[1], f.z = e.move[i].from[2];
        t.x = e.move[i].to[0], t.y = e.move[i].to[1], t.z = e.move[i].to[2];
        auto it = levelPos_.find(key3(f));
        if (it != levelPos_.end() && !pos_[it->second].prot) pos_[it->second].cur = t;
        else foreignMoves_.push_back(e.move[i]);
    }
    for (int i = 0; i < e.nrem[0]; i++) {
        const int id = e.rem[0][i];
        if (id < 0 || id >= lg_mesh_count(mesh_, LG_RENDER)) {
            err = "the geometry removes drawn triangle " + std::to_string(id) + ": the level has no such triangle";
            return false;
        }
        draw_[id].removed = true;
    }
    for (int i = 0; i < e.nrem[1]; i++) {
        const int id = e.rem[1][i];
        if (id < 0 || id >= lg_mesh_count(mesh_, LG_GROUND)) {
            err = "the geometry removes floor triangle " + std::to_string(id) + ": the level has no such triangle";
            return false;
        }
        floor_[id].removed = true;
    }
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < e.nrem[2 + k]; i++) {
            const int id = e.rem[2 + k][i];
            if (id < 0 || id >= nLevelWalls_[k]) {
                err = "the geometry removes wall triangle " + std::to_string(id) + ": the level has no such triangle";
                return false;
            }
            wall_[size_t(k ? nLevelWalls_[0] : 0) + size_t(id)].removed = true;
        }
    std::unordered_map<uint64_t, int> at;   /* corner values now -> corner (the level's first) */
    for (size_t i = 0; i < pos_.size(); i++) at.emplace(key3(pos_[i].cur), int(i));
    auto bind = [&](int x, int y, int z) {
        P3 v;
        v.x = x, v.y = y, v.z = z;
        auto it = at.find(key3(v));
        if (it != at.end()) return it->second;
        const int id = addPos(v, false);
        at.emplace(key3(v), id);
        return id;
    };
    for (int i = 0; i < e.nrender; i++) {
        const LgAddRender &r = e.render[i];
        if (r.after < 0 || r.after >= lg_mesh_count(mesh_, LG_RENDER)) {
            err = "the geometry adds a triangle after drawn triangle " + std::to_string(r.after) + ": no such triangle";
            return false;
        }
        DrawTri d;
        d.after = d.state = r.after;
        d.flag = r.flag;
        for (int c = 0; c < 3; c++) {
            d.pos[c] = bind(r.v[c][0], r.v[c][1], r.v[c][2]);
            d.st[c][0] = r.v[c][3], d.st[c][1] = r.v[c][4];
            for (int q = 0; q < 4; q++) d.rgba[c][q] = uint8_t(std::max(0, std::min(255, int(r.v[c][5 + q]))));
        }
        draw_.push_back(d);
    }
    for (int i = 0; i < e.nground; i++) {
        const LgAddTri &g = e.ground[i];
        FloorTri f;
        f.surface = g.a, f.flag = g.b;
        for (int c = 0; c < 3; c++) f.pos[c] = bind(g.v[3 * c], g.v[3 * c + 1], g.v[3 * c + 2]);
        floor_.push_back(f);
    }
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < e.nwall[k]; i++) {
            const LgAddTri &g = e.wall[k][i];
            WallTri w;
            w.set = k, w.heading = g.a, w.side = g.b, w.fail = g.c;
            for (int c = 0; c < 3; c++) w.pos[c] = bind(g.v[3 * c], g.v[3 * c + 1], g.v[3 * c + 2]);
            wall_.push_back(w);
        }
    for (int i = 0; i < e.nsettimg; i++) {
        bool done = false;
        for (auto &x : settimg_)
            if (x.piece == e.settimg[i].piece && x.k == e.settimg[i].k) x.tex = e.settimg[i].tex, done = true;
        if (!done) settimg_.push_back(e.settimg[i]);
    }
    markFollowers();
    indexOk_ = false;
    return true;
}

bool Model::edited() const {
    LgEdit e;
    if (!exportEdit(e)) return true;
    const bool r = !lg_edit_empty(&e);
    lg_edit_free(&e);
    return r;
}

bool Model::exportEdit(LgEdit &out) const {
    memset(&out, 0, sizeof out);
    std::vector<LgMove> mv = foreignMoves_;
    for (const Pos &p : pos_)
        if (p.level && p.cur != p.orig) {
            LgMove m;
            m.from[0] = int16_t(p.orig.x), m.from[1] = int16_t(p.orig.y), m.from[2] = int16_t(p.orig.z);
            m.to[0] = int16_t(p.cur.x), m.to[1] = int16_t(p.cur.y), m.to[2] = int16_t(p.cur.z);
            mv.push_back(m);
        }
    std::vector<int> rem[4];
    std::vector<LgAddRender> ar;
    std::vector<LgAddTri> ag;
    for (const DrawTri &d : draw_) {
        if (d.origId >= 0) {
            if (d.removed) rem[0].push_back(d.origId);
            continue;
        }
        if (d.removed) continue;
        LgAddRender r;
        memset(&r, 0, sizeof r);
        r.after = d.after, r.flag = d.flag;
        for (int c = 0; c < 3; c++) {
            const P3 &v = pos_[d.pos[c]].cur;
            r.v[c][0] = int16_t(v.x), r.v[c][1] = int16_t(v.y), r.v[c][2] = int16_t(v.z);
            r.v[c][3] = d.st[c][0], r.v[c][4] = d.st[c][1];
            for (int q = 0; q < 4; q++) r.v[c][5 + q] = d.rgba[c][q];
        }
        ar.push_back(r);
    }
    for (const FloorTri &f : floor_) {
        if (f.origId >= 0) {
            if (f.removed) rem[1].push_back(f.origId);
            continue;
        }
        if (f.removed) continue;
        LgAddTri g;
        memset(&g, 0, sizeof g);
        for (int c = 0; c < 3; c++) {
            const P3 &v = pos_[f.pos[c]].cur;
            g.v[3 * c] = int16_t(v.x), g.v[3 * c + 1] = int16_t(v.y), g.v[3 * c + 2] = int16_t(v.z);
        }
        g.a = f.surface, g.b = f.flag;
        ag.push_back(g);
    }
    std::vector<LgAddTri> aw[2];
    for (const WallTri &w : wall_) {
        if (w.origId >= 0) {
            if (w.removed) rem[2 + w.set].push_back(w.origId);
            continue;
        }
        if (w.removed) continue;
        LgAddTri g;
        memset(&g, 0, sizeof g);
        for (int c = 0; c < 3; c++) {
            const P3 &v = pos_[w.pos[c]].cur;
            g.v[3 * c] = int16_t(v.x), g.v[3 * c + 1] = int16_t(v.y), g.v[3 * c + 2] = int16_t(v.z);
        }
        g.a = w.heading, g.b = w.side, g.c = w.fail;
        aw[w.set].push_back(g);
    }
    auto dup = [](const void *p, size_t n) {
        void *q = malloc(n ? n : 1);
        if (q && n) memcpy(q, p, n);
        return q;
    };
    out.nmove = int(mv.size());
    out.move = (LgMove *) dup(mv.data(), sizeof(LgMove) * mv.size());
    for (int k = 0; k < 4; k++) {
        out.nrem[k] = int(rem[k].size());
        out.rem[k] = (int *) dup(rem[k].data(), sizeof(int) * rem[k].size());
    }
    out.nrender = int(ar.size());
    out.render = (LgAddRender *) dup(ar.data(), sizeof(LgAddRender) * ar.size());
    out.nground = int(ag.size());
    out.ground = (LgAddTri *) dup(ag.data(), sizeof(LgAddTri) * ag.size());
    for (int k = 0; k < 2; k++) {
        out.nwall[k] = int(aw[k].size());
        out.wall[k] = (LgAddTri *) dup(aw[k].data(), sizeof(LgAddTri) * aw[k].size());
    }
    out.nsettimg = int(settimg_.size());
    out.settimg = (LgSettimg *) dup(settimg_.data(), sizeof(LgSettimg) * settimg_.size());
    bool ok = out.move && out.render && out.ground && out.settimg && out.wall[0] && out.wall[1];
    for (int k = 0; k < 4; k++) ok = ok && out.rem[k];
    if (!ok) lg_edit_free(&out);
    return ok;
}

/* ------------------------------------------------------------------ queries */
bool Model::heightAtWith(const std::vector<P3> &vals, double x, double z, double &y) const {
    double best = -1e30, h;
    bool any = false;
    for (int pass = 0; pass < 2 && !any; pass++) {
        if (pass == 0) {
            for (const FloorTri &f : floor_)
                if (!f.removed && triY(vals[f.pos[0]], vals[f.pos[1]], vals[f.pos[2]], x, z, h))
                    best = std::max(best, h), any = true;
        } else {
            for (const DrawTri &d : draw_)
                if (!d.removed && triY(vals[d.pos[0]], vals[d.pos[1]], vals[d.pos[2]], x, z, h))
                    best = std::max(best, h), any = true;
        }
    }
    if (any) y = best;
    return any;
}

bool Model::heightAt(double x, double z, double &y) const {
    std::vector<P3> v(pos_.size());
    for (size_t i = 0; i < pos_.size(); i++) v[i] = pos_[i].cur;
    return heightAtWith(v, x, z, y);
}

int Model::pick(const double o[3], const double d[3], double &tBest, double hit[3]) const {
    int best = -1;
    tBest = 1e30;
    for (size_t i = 0; i < draw_.size(); i++) {
        const DrawTri &t = draw_[i];
        if (t.removed) continue;
        const P3 &A = pos_[t.pos[0]].cur, &B = pos_[t.pos[1]].cur, &C = pos_[t.pos[2]].cur;
        const double e1[3] = {double(B.x - A.x), double(B.y - A.y), double(B.z - A.z)};
        const double e2[3] = {double(C.x - A.x), double(C.y - A.y), double(C.z - A.z)};
        const double p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
        const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (std::fabs(det) < 1e-9) continue;
        const double inv = 1.0 / det;
        const double s[3] = {o[0] - A.x, o[1] - A.y, o[2] - A.z};
        const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
        if (u < 0 || u > 1) continue;
        const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
        const double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
        if (v < 0 || u + v > 1) continue;
        const double t0 = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
        if (t0 > 1e-6 && t0 < tBest) tBest = t0, best = int(i);
    }
    if (best >= 0)
        for (int k = 0; k < 3; k++) hit[k] = o[k] + d[k] * tBest;
    return best;
}

/* ------------------------------------------------------------------ the brushes */
void Model::brush(BrushTool tool, const Brush &b, double cx, double cz, double flattenY,
                  std::unordered_map<int, double> &cur, int &skipped) const {
    const std::vector<int> ps = nearby(cx, cz, b.radius);
    auto yOf = [&](int p) {
        auto it = cur.find(p);
        return it != cur.end() ? it->second : double(pos_[p].cur.y);
    };
    std::vector<std::pair<int, double>> out;
    const double frac = std::max(0.0, std::min(1.0, b.strength));
    for (int p : ps) {
        if (!movable(p)) {
            if (pos_[p].prot) skipped++;
            continue;
        }
        const double dx = pos_[p].cur.x - cx, dz = pos_[p].cur.z - cz;
        const double w = brushWeight(b, std::sqrt(dx * dx + dz * dz));
        if (w <= 0) continue;
        double y = yOf(p);
        switch (tool) {
        case B_RAISE: y += b.strength * w; break;
        case B_LOWER: y -= b.strength * w; break;
        case B_SMOOTH: {
            const std::vector<int> &nb = neighbours(p);
            if (nb.empty()) break;
            double s = 0;
            for (int q : nb) s += yOf(q);
            y += (s / double(nb.size()) - y) * frac * w;
            break;
        }
        case B_FLATTEN: y += (flattenY - y) * frac * w; break;
        }
        out.push_back({p, std::max(-32768.0, std::min(32767.0, y))});
    }
    for (const auto &v : out) cur[v.first] = v.second;   /* (all read before any is written: in any order the same) */
}

Step Model::ramp(const double a[3], const double b[3], const Brush &br, int &skipped) const {
    Step s;
    s.kind = ST_MOVE;
    const double sx = b[0] - a[0], sz = b[2] - a[2], len2 = sx * sx + sz * sz;
    const double reach = br.radius + std::sqrt(len2);
    const std::vector<int> ps = nearby((a[0] + b[0]) / 2, (a[2] + b[2]) / 2, reach);
    const double frac = std::max(0.0, std::min(1.0, br.strength));
    for (int p : ps) {
        const double px = pos_[p].cur.x - a[0], pz = pos_[p].cur.z - a[2];
        double t = len2 > 1e-9 ? (px * sx + pz * sz) / len2 : 0.0;
        t = std::max(0.0, std::min(1.0, t));
        const double dx = px - t * sx, dz = pz - t * sz, d = std::sqrt(dx * dx + dz * dz);
        const double w = brushWeight(br, d);
        if (w <= 0) continue;
        if (!movable(p)) {
            if (pos_[p].prot) skipped++;
            continue;
        }
        const double target = a[1] + (b[1] - a[1]) * t, y0 = pos_[p].cur.y;
        const int y = clamp16(y0 + (target - y0) * frac * w);
        if (y != pos_[p].cur.y) {
            P3 v = pos_[p].cur;
            v.y = y;
            s.moves.push_back({p, v});
        }
    }
    s.what = "ramp";
    return s;
}

Step Model::strokeStep(const std::unordered_map<int, double> &cur, const std::string &what) const {
    Step s;
    s.kind = ST_MOVE;
    s.what = what;
    std::vector<int> ids;
    for (const auto &kv : cur) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    for (int p : ids) {
        const int y = clamp16(cur.at(p));
        if (y == pos_[p].cur.y) continue;
        P3 v = pos_[p].cur;
        v.y = y;
        s.moves.push_back({p, v});
    }
    return s;
}

}  // namespace terrain
}  // namespace live
