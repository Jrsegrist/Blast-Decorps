/* bc.exe's terrain tools, their model (terrain.cpp): a level's triangles as
 * corners shared by position (welded, as a geometry edit moves them: drawn,
 * floor and wall triangles together), the edits the tools make to them, and
 * the geometry edit (level_geo.h LgEdit, a bcmod/2 level's "geometry") they
 * come to.  Plain C++: no renderer, window or game state, so bc_cameratest
 * (edit_test.cpp) tests it on synthetic meshes.
 *
 * Tools (each one a Step, replayed in order from the level as the ROM has it,
 * so undo and redo rebuild the same model):
 *   MOVE       new heights / places for corners (the brushes: raise, lower,
 *              smooth, flatten, ramp; moving a selection by hand)
 *   SUBDIVIDE  drawn triangles (and the floor triangles with the same corners)
 *              split four ways at their edges' midpoints, for detail to sculpt;
 *              every other triangle on a split edge - the drawn neighbour, its
 *              floor twin, a wall standing on it - is split along that edge too
 *              (two or three ways), so no corner is left on another triangle's
 *              edge (a T-junction: a hairline crack in the picture, a gap the
 *              floor or a wall opens once a brush moves the corner).  An edge a
 *              triangle that can't be split shares (a bridge's, a protected
 *              corner's) stays whole: the selected triangle splits around it
 *   SEAL       the same for corners already on another triangle's edge (a mod
 *              made before: its split corners): the triangles split at them
 *   PAINT      drawn triangles drawn with another texture of the level: they
 *              join a list's run that draws it (a texture already used near
 *              them), their texture coordinates kept or projected from above
 *   RETEX      a run's G_SETTIMG loads another texture id (every triangle of it)
 * Protected corners (bridges, moving parts, switches, gates: lg_apply refuses to
 * move them) never move: a brush skips them and says how many.
 *
 * Welds across meshes.  Rare's meshes (the display-list pieces, the floor, the
 * walls) meet without sharing corners: corners a unit or two apart stand for one
 * (a "group": level corners within kWeldTol on every axis, chained), and a mesh's
 * corner often lies on another mesh's edge (a T-junction: within a map unit of it,
 * more than 2 from its ends; a survey of the 60 levels: two thirds of the open edges
 * have another mesh's edge within one unit, then the count drops to the background).
 * A MOVE moves a group as one (every member by the move of its lowest-numbered moved
 * member), and a corner on another triangle's edge follows that edge (keeps its place
 * along it) as long as the edge is there: it moves only with the edge's ends, so no
 * crack opens between the meshes.  A Split of that edge takes the corner in (it
 * becomes the edge's midpoint when it lies within kWeldTol of the middle, or a seam
 * closed on a half otherwise), and from then on it is free.  Protected corners stay
 * where they are: a weld that would move one skips it and says so (lastWeld()). */
#ifndef TERRAIN_H
#define TERRAIN_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "load/level_geo.h"

namespace live {
namespace terrain {

struct P3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const P3 &o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const P3 &o) const { return !(*this == o); }
};

struct Pos {
    P3 orig, cur;
    bool level = false;           /* a corner of the level's own triangles (moves name it by orig) */
    bool prot = false;            /* protected: never moved */
    int pa = -1, pb = -1;         /* a midpoint on an edge a neighbour keeps whole: follows pa-pb's middle */
    int host = -1;                /* a level corner on another mesh's edge (or welded to one): the weld it follows */
};

/* corners within this many map units on every axis are one corner (the level's own, chained) */
const int kWeldTol = 2;
/* a corner of an open edge (a mesh's border) this far from another mesh's open edge (more than kWeldTol: a seam
 * Rare left wider) follows that edge too; the survey's counts of open edges with another one within 0, 1, 2, 3 units
 * are 3778, 2255, 553, 902, then the background: 3 is still the meshes' own seams */
const int kSeamTol = 3;
/* a corner lying on two (or more) other meshes' edges (meshes that overlap: their edges cross there) can follow
 * only one; when a move would take those edges more than this far apart at it, they are split at it */
const int kSplitGap = 1;

/* what the last MOVE's welds did */
struct WeldInfo {
    int partners = 0;             /* corners moved with a welded neighbour (a unit or two away) */
    int followers = 0;            /* corners moved along another mesh's edge they lie on */
    int protSkipped = 0;          /* protected corners a weld would have moved (left: a crack may show there) */
    int tjunctions = 0;           /* corners the edges they lay on were split at (a crossing the move pulled apart) */
};

struct DrawTri {
    int origId = -1;              /* the level's render id; -1: added (after `after`) */
    int pos[3] = {-1, -1, -1};
    int16_t st[3][2] = {};
    uint8_t rgba[3][4] = {};
    int flag = 0;                 /* the G_TRI1 flag byte */
    int after = -1;               /* added: the render id whose run it joins */
    bool removed = false;
    int state = -1;               /* the render id whose list and texture it draws with (itself, or `after`) */
};

struct FloorTri {
    int origId = -1;
    int pos[3] = {-1, -1, -1};
    int surface = 1, flag = 0;
    bool removed = false;
};

struct WallTri {
    int set = 0;                  /* 0 walls_a, 1 walls_b */
    int origId = -1;              /* the level's id in its set; -1: added */
    int pos[3] = {-1, -1, -1};
    int heading = -1, side = 0, fail = 0;   /* the record's values (-1: the normal's, for an added one) */
    bool removed = false;
};

/* what the last SUBDIVIDE / SEAL did */
struct SplitInfo {
    int selected = 0;             /* selected drawn triangles split */
    int neighbours = 0;           /* other drawn triangles split along a shared edge */
    int floors = 0, walls = 0;    /* floor and wall triangles split */
    int keptEdges = 0;            /* selected edges kept whole (a triangle there can't be split) */
    int seamRounds = 0, seamsClosed = 0;   /* corners on edges of triangles not sharing them, closed (rounds) */
    int snapped = 0;              /* edges split at a corner already on them near the middle (another mesh's) */
    std::string detail;           /* (the first seams closed, for the log) */
};

enum StepKind { ST_MOVE, ST_SUBDIVIDE, ST_PAINT, ST_RETEX, ST_SEAL };
struct Step {
    int kind = ST_MOVE;
    std::vector<std::pair<int, P3>> moves;   /* MOVE: corner -> its new place */
    std::vector<int> tris;                   /* SUBDIVIDE / PAINT: drawn triangles (model indexes) */
    int run = -1;                            /* PAINT: the render id whose run they join */
    bool planar = false;                     /* PAINT: texture coordinates projected from above (else kept) */
    double density = 0;                      /* PAINT planar: texture units (s, t) per map unit (0: the run's) */
    int piece = -1, k = -1;                  /* RETEX: the list and its G_SETTIMG */
    uint32_t tex = 0;
    std::string what;                        /* for the change list */
};

/* a brush: radius (map units), strength (map units per application at the centre; for smooth / flatten /
 * ramp the fraction 0..1 of the way to go), falloff (0: a hard edge .. 1: smooth from the centre) */
struct Brush {
    double radius = 200, strength = 8, falloff = 0.5;
};
enum BrushTool { B_RAISE, B_LOWER, B_SMOOTH, B_FLATTEN };

class Model {
public:
    /* the level as the ROM has it (big-endian, both members); false and *err */
    bool open(const uint8_t *file, size_t size, std::string &err);
    bool isOpen() const { return mesh_ != nullptr; }
    void close();
    ~Model();
    Model() = default;
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    /* back to the level as opened (no edit) */
    void reset();
    /* a geometry edit (a mod's) taken in as the starting point: moves, removes, added triangles (drawn, floor and
     * wall, bound to the corners they share), settimg; moves of corners the level doesn't have are kept as they
     * are and written back */
    bool import(const LgEdit &e, std::string &err);
    /* one step; false and *err (the model left as it was) */
    bool apply(const Step &s, std::string &err);
    /* the edit (arrays malloc'd: lg_edit_free); false: out of memory */
    bool exportEdit(LgEdit &out) const;
    bool edited() const;

    /* ---- what the tools read ---- */
    const std::vector<Pos> &positions() const { return pos_; }
    const std::vector<DrawTri> &drawn() const { return draw_; }
    const std::vector<FloorTri> &floor() const { return floor_; }
    const std::vector<WallTri> &walls() const { return wall_; }
    const LgTri *levelTri(int set, int id) const { return lg_mesh_tri(mesh_, set, id); }
    int levelCount(int set) const { return lg_mesh_count(mesh_, set); }
    /* the drawn triangle's list / texture state (its own or the run's it joined) */
    const LgTri *stateOf(int t) const;
    /* corners whose x, z lie within r of (x, z) (live triangles' corners only) */
    std::vector<int> nearby(double x, double z, double r) const;
    /* neighbours of a corner (sharing a live drawn or floor triangle's edge) */
    const std::vector<int> &neighbours(int p) const;
    /* the floor's height under (x, z): the highest live floor triangle there (else the drawn ones); false: none */
    bool heightAt(double x, double z, double &y) const;
    /* the same over corner values `vals` (a snapshot of positions(); records-follow) */
    bool heightAtWith(const std::vector<P3> &vals, double x, double z, double &y) const;
    /* the live drawn triangle a ray meets first (origin, unit direction); -1: none */
    int pick(const double o[3], const double d[3], double &t, double hit[3]) const;
    /* can corner p move (not protected, not a follower of an edge: a split midpoint's or another mesh's) */
    bool movable(int p) const {
        return p >= 0 && p < int(pos_.size()) && !pos_[p].prot && pos_[p].pa < 0 && pos_[p].host < 0;
    }
    /* a MOVE's corner -> place pairs with the welds: the groups their corners belong to and the corners following
     * edges that moved (sorted by corner; what apply() sets); info: what the welds added */
    std::vector<std::pair<int, P3>> resolve(const std::vector<std::pair<int, P3>> &moves, WeldInfo *info) const;
    const WeldInfo &lastWeld() const { return lastWeld_; }
    /* the weld group of a level corner (its lowest-numbered member; -1: not a level corner) and its members */
    int groupOf(int p) const { return p >= 0 && p < int(grp_.size()) ? grp_[p] : -1; }
    std::vector<int> groupMembers(int p) const;
    /* corners now following another mesh's edge (the level's own T-junctions whose edge is still there) */
    int followerCount() const;
    /* the last SUBDIVIDE's selected triangles' pieces (what the editor selects next) and what it split */
    const std::vector<int> &lastSplit() const { return lastSplit_; }
    const SplitInfo &lastInfo() const { return lastInfo_; }
    /* seams: corners lying on another live triangle's edge (drawn, floor or wall; within a map unit of it, more than
     * 2 from its ends), each (corner, the edge's corners); only corners the edits made (the level's own are Rare's) */
    struct Seam {
        int p, a, b;
    };
    std::vector<Seam> seams() const;

    /* ---- the brushes (a MOVE step's values; skipped: protected corners the brush reached) ----
     * cur: the stroke's running heights (corner -> y, kept between applications so small steps add up); the
     * step holds the rounded values */
    void brush(BrushTool tool, const Brush &b, double cx, double cz, double flattenY, std::unordered_map<int, double> &cur,
               int &skipped) const;
    /* a ramp from a to b (x, y, z): corners within the brush's radius of the segment go to its height there */
    Step ramp(const double a[3], const double b[3], const Brush &br, int &skipped) const;
    /* the MOVE step of a stroke's heights (only corners that changed) */
    Step strokeStep(const std::unordered_map<int, double> &cur, const std::string &what) const;

private:
    void rebuildIndex() const;
    void settle();                 /* followers to their edges' middles; which midpoints still follow; markFollowers */
    void buildWelds();             /* (open) the groups and the level's corners on other triangles' edges */
    void markFollowers();          /* which of those edges are still there: Pos::host */
    struct Rel {
        int p, a, b;               /* level corner p lies on edge a-b (a < b) */
        double t;                  /* where along it (0 at a) */
        bool wide;                 /* more than kWeldTol from it (a wider seam between two open edges) */
    };
    /* the displacement following rel r gives its corner under the moves n (resolve's formula) */
    P3 predict(const Rel &r, const std::map<int, P3> &n) const;
    /* MOVE: corners on two edges the moves n would take apart: the edges split at them (and each half at the other
     * corners following it); -> the moves to add (the crossing corners at the mean place, the corners taken in where
     * they were following to), the number of corners taken in */
    int splitCrossings(const std::map<int, P3> &n, std::map<int, P3> &extra);
    std::vector<int> grp_;                                /* level corner -> its group (lowest member); others -1 */
    std::unordered_map<int, std::vector<int>> grpMembers_;   /* groups of more than one corner: members, sorted */
    std::vector<Rel> rels_;                               /* sorted by (p, a, b) */
    std::vector<std::pair<int, int>> hosts_;              /* (group, rel) for the groups following an edge, by group */
    WeldInfo lastWeld_;
    int addPos(const P3 &v, bool level);
    int posAt(const P3 &v) const;  /* a corner now at v (-1) */
    /* triangle k of set `kind` (0 drawn, 1 floor, 2 wall): its corners, alive, can it be split */
    const int *corners(int kind, int i) const;
    bool alive(int kind, int i) const;
    bool splittable(int kind, int i) const;
    int count(int kind) const;
    /* every live triangle with an edge in mid_ split at those midpoints, until none is left (selected: drawn
     * triangles whose pieces stay selected) */
    void splitAll(std::unordered_map<int, char> &selected);
    void splitOne(int kind, int i, std::unordered_map<int, char> &selected);
    std::vector<Seam> seamsOf(const std::vector<char> *only) const;   /* only: those corners (else the edits') */
    /* the seams of `fresh` corners closed where the triangles there can split (rounds of splitAll); true: any */
    bool closeSeams(const std::vector<char> *fresh, std::unordered_map<int, char> &selected);
    std::vector<int> lastSplit_;
    SplitInfo lastInfo_;
    std::unordered_map<uint64_t, char> subEdges_;   /* this step's halves of split edges (a corner already on the
                                                       edge - the level's own T-junction - is on one of them) */
    std::unordered_map<int, char> stepSel_;         /* the step's selected triangles (lastInfo_.selected) */
    LgMesh *mesh_ = nullptr;
    std::vector<Pos> pos_;
    std::vector<DrawTri> draw_;
    std::vector<FloorTri> floor_;
    std::vector<WallTri> wall_;
    std::vector<LgSettimg> settimg_;
    std::unordered_map<uint64_t, int> levelPos_;   /* orig value -> corner */
    std::unordered_map<uint64_t, int> mid_;        /* (corner a, corner b) -> their midpoint */
    /* the level as opened (reset) */
    std::vector<Pos> pos0_;
    std::vector<DrawTri> draw0_;
    std::vector<FloorTri> floor0_;
    std::vector<WallTri> wall0_;
    std::unordered_map<uint64_t, int> levelPos0_;
    /* kept as imported */
    std::vector<LgMove> foreignMoves_;
    int nLevelWalls_[2] = {0, 0};
    /* lookups, rebuilt on demand */
    mutable bool indexOk_ = false;
    mutable std::vector<std::vector<int>> nb_;
    mutable std::vector<char> live_;               /* a corner of a live triangle */
};

/* weight of a brush at distance d (1 inside radius * (1 - falloff), smooth to 0 at the radius) */
double brushWeight(const Brush &b, double d);
uint64_t key3(const P3 &p);

}  // namespace terrain
}  // namespace live

#endif
