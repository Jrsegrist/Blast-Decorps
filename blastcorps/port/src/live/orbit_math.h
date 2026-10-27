/* The free orbit camera's math (bc.exe's optional camera, live_orbit.cpp):
 * plain C++, no renderer, window or game state, so bc_cameratest.exe
 * (orbit_test.cpp) can test it on its own.
 *
 * Units are the game's drawing units (its view matrix: game units / 32), y
 * up.  The game's own camera each frame is an eye E and the point it looks
 * at T (the player's vehicle, or the driver on foot).  The orbit camera
 * looks at the same T from a direction and distance of its own:
 *  - not engaged (nothing moved it, or it eased back): the game's direction,
 *    at the game's distance times the zoom: with zoom 1 exactly the game's
 *    camera;
 *  - engaged (the player turned it): a yaw and pitch of its own, at the
 *    game's distance times the zoom: with Limits::follow (the default) an
 *    offset from the game camera's yaw and pitch (the view turns with the
 *    game's chase camera, the player's offset kept), else absolute (world)
 *    angles.  With recentring on it eases back to the game's direction after
 *    the input has been idle for the delay; with it off (the default) it
 *    stays where it was left until reset (camera_reset, a new level).
 *  - collision: the eye is pulled in along the line to T where the ground
 *    (a height query the caller supplies, read-only) would cover it, quickly,
 *    and lets out again slowly.
 * The yaw is atan2(dx, dz) of eye - target in degrees, the pitch the angle
 * above the horizontal. */
#ifndef ORBIT_MATH_H
#define ORBIT_MATH_H

#include <cstdint>
#include <functional>
#include <vector>

namespace live {
namespace orbit {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};
Vec3 operator+(const Vec3 &a, const Vec3 &b);
Vec3 operator-(const Vec3 &a, const Vec3 &b);
Vec3 operator*(const Vec3 &a, double k);
double dot(const Vec3 &a, const Vec3 &b);
Vec3 cross(const Vec3 &a, const Vec3 &b);
double length(const Vec3 &a);
Vec3 normalize(const Vec3 &a);
Vec3 lerp(const Vec3 &a, const Vec3 &b, double t);

double wrap180(double deg);   /* to (-180, 180] */
double wrap360(double deg);   /* to [0, 360) */

/* unit direction from the target to the eye for a yaw and pitch (degrees) */
Vec3 direction(double yawDeg, double pitchDeg);
/* yaw and pitch of a direction (any length > 0) */
void angles(const Vec3 &dir, double &yawDeg, double &pitchDeg);

/* How obstacles move the camera (bc.ini [camera] collision):
 *  - smooth: an obstacle between the camera and the target
 *    has to block the median of three rays (the line of sight and one each
 *    side: a pole, a lamp post or a building corner passing by doesn't) for
 *    holdIn before the camera rises over it or pulls in, and the view has to
 *    stay clear (by `hysteresis` more of the distance than now) for holdOut
 *    before it goes back; it moves on critically damped springs (no
 *    overshoot, no jerk), quickly in and up, slowly out and down.  Only the
 *    eye itself inside an obstacle moves it at once (still on a spring, in
 *    springEye).  Small or brief obstructions just occlude for a moment;
 *  - strict: as before (pulled in or risen at once, every step);
 *  - off (the default): never moved for obstacles, only kept above the
 *    terrain (on a spring that sees rising ground coming, never below the
 *    ground right under the eye). */
enum Collision { COLLISION_SMOOTH = 0, COLLISION_OFF = 1, COLLISION_STRICT = 2 };
const char *collisionName(int mode);

/* A critically damped spring towards a target (smoothTime: about the time
 * to get there; no overshoot from rest).  Deterministic: the same steps give
 * the same values. */
struct Spring {
    static void step(double &x, double &v, double target, double smoothTime, double dt);
};

/* the limits and behaviour (bc.ini [camera]; live_setup.cpp) */
struct Limits {
    int collision = COLLISION_OFF;
    double holdIn = 0.15;         /* smooth: seconds an obstruction must last before the camera moves for it ... */
    double holdOut = 0.8;         /* ... and the view must stay clear before it moves back */
    double springIn = 0.15;       /* smooth: the springs' times in and up ... */
    double springOut = 0.9;       /* ... out and down ... */
    double springEye = 0.05;      /* ... and with the eye inside an obstacle */
    double hysteresis = 0.08;     /* smooth: clear by this share of the distance more before letting out */
    double rayOffset = 0.05;      /* smooth: the side rays this share of the distance beside the eye (1..6 units) */
    double pitchMin = -10.0;      /* degrees above the horizontal (negative: from below) */
    double pitchMax = 80.0;
    double zoomMin = 0.5;         /* times the game's own distance */
    double zoomMax = 2.5;
    bool follow = true;           /* the view the player set is kept relative to the game's camera (it turns
                                     with the game's camera as that follows the vehicle: a chase camera with
                                     the player's offset); false: kept fixed in the world (a compass
                                     direction: driving a circle, the view swings round the vehicle) */
    bool recentre = false;        /* ease back behind the player after the input is idle ... */
    double recentreDelay = 1.5;   /* ... this many seconds */
    double recentreTime = 0.35;   /* the easing's time constant (s) */
    double minDistance = 1.5;     /* drawing units: never closer to the target than this */
    double minShare = 0.15;       /* the collision pulls in to no less than this share of the distance ... */
    double nearShare = 0.15;      /* ... and doesn't test the part this close to the target (the
                                     target sits on the ground: the player's vehicle) */
    double clearance = 4.0;       /* drawing units above the ground */
    double offRing = 0.1;         /* off: the terrain looked at around the eye, this share of the distance
                                     away (2..12 units), so it rises on its spring before rising ground */
    double floorClear = 1.0;      /* off: never closer than this above the terrain right under the eye */
    double liftTime = 0.3;        /* where pulled in as far as allowed and still too low: the eye is
                                     lifted above the ground (rises at once, settles in this time) */
    double riseBelow = 0.6;       /* blocked before this share of the distance: look for a steeper view
                                     (up to riseMax degrees more, in riseStep steps, at most pitchMax)
                                     that sees past it; if none does, the one that sees farthest when
                                     that is at least riseGain more of the distance (a camera pulled in
                                     to the target ends up inside the vehicle: ugly, and pictures of
                                     big close-up polygons cost the GPU up to 3x) */
    double riseMax = 75.0;
    double riseStep = 5.0;
    double riseGain = 0.15;
    double riseTime = 0.2;        /* ... reached in about this time (and back with letOutTime), or at once
                                     where the view as it is would be pulled in under riseSnapBelow */
    double riseSnapBelow = 0.3;
    double pullInTime = 0.04;     /* time constants of the collision distance (s): in fast ... */
    double letOutTime = 0.5;      /* ... out slowly */
};

/* one step's input, already scaled by sensitivity and inverted as set */
struct Input {
    double yaw = 0;               /* degrees to turn (positive: the eye moves to larger yaw) */
    double pitch = 0;             /* degrees to tilt (positive: the eye moves up) */
    double zoom = 0;              /* log2 change of the zoom (positive: farther away) */
    bool reset = false;           /* back to the game's camera, zoom 1 */
};

/* the ground's height (drawing units) under x, z for a point at height y:
 * the highest surface there that isn't far above y (a bridge high above is
 * not the ground), or a value <= -1e30 if none / unknown */
using HeightFn = std::function<double(double x, double y, double z)>;

class Orbit {
public:
    /* the game's camera this step */
    struct Game {
        Vec3 eye, target;
    };

    /* forget everything (a cut, a new level, orbit switched off): the game's camera, zoom kept unless `zoomToo` */
    void reset(bool zoomToo);
    /* one step of dt seconds; `terrain` (optional): the ground without the
     * buildings, for collision = off (kept above it, nothing else) */
    void step(const Game &g, const Input &in, double dt, const Limits &lim, const HeightFn &height,
              const HeightFn &terrain = HeightFn());

    /* the camera for a picture: the target and the game's eye of that picture
     * (interpolated by the caller), `extraYaw`/`extraPitch` added (the input
     * since the last step, for pictures between steps) */
    void eyeFor(const Game &g, double extraYaw, double extraPitch, const Limits &lim, Vec3 &eye) const;

    /* the camera t (0..1) of the way from one step's state to the next's (a
     * picture shown between the two steps): yaw along the shorter arc, pitch,
     * the blend and the collision's pull-in, lift and rise linearly, the zoom
     * geometrically; a state not engaged (its own yaw and pitch unused) takes
     * the other's direction, so engaging and recentring ease through the blend
     * alone.  The rest (idle time, recentring) is b's. */
    static Orbit between(const Orbit &a, const Orbit &b, double t);

    bool engaged() const { return blend_ > 0.0; }
    /* the kept yaw and pitch: offsets from the game camera's (Limits::follow) or absolute */
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    /* the view's absolute yaw and pitch for a game camera direction (gameYaw, gamePitch; engaged or not) */
    void viewAngles(double gameYaw, double gamePitch, const Limits &lim, double &yaw, double &pitch) const;
    double zoom() const { return zoom_; }
    double blend() const { return blend_; }
    double distanceRatio() const { return ratio_; }   /* the collision's pull-in (1: none) */
    double lift() const { return lift_; }             /* ... and lift (drawing units) */
    double rise() const { return rise_; }             /* ... and rise over obstacles (degrees) */
    double idle() const { return idle_; }

    /* the eye of the last step inside an obstacle (point test; smooth/strict: below its top plus the
     * clearance; off: below its top, counted only) */
    bool eyeInside() const { return inside_; }
    double ratioTarget() const { return ratioT_; }   /* smooth: where the springs are going */
    double riseTarget() const { return riseT_; }
    unsigned floorHits() const { return floorHits_; }   /* off: steps the terrain right under the eye lifted it at once */
private:
    void stepStrict(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                    const HeightFn &height);
    void stepSmooth(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                    const HeightFn &height);
    void stepOff(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                 const HeightFn &height, const HeightFn &terrain);
    double yaw_ = 0, pitch_ = 0;  /* while engaged: offsets from the game's (Limits::follow), else absolute */
    double zoom_ = 1.0;
    double blend_ = 0.0;          /* 1: the orbit's direction, 0: the game's (eases to 0 after recentring) */
    bool recentring_ = false;
    double idle_ = 1e9;           /* seconds since the last input */
    double ratio_ = 1.0;          /* collision: share of the wanted distance used */
    double lift_ = 0.0;           /* collision: the eye raised by this much */
    double rise_ = 0.0;           /* collision: the view steeper by this many degrees */
    /* smooth: the springs' speeds, their targets, the hold timers */
    double ratioV_ = 0, riseV_ = 0, liftV_ = 0;
    double ratioT_ = 1.0, riseT_ = 0.0;
    double pullIn_ = 0, letOut_ = 0, riseUp_ = 0, riseDown_ = 0;
    bool inside_ = false;
    unsigned floorHits_ = 0;      /* (statistics; kept by reset) */
    Vec3 lastEye_;                /* off: the last step's eye before the lift (where it is going) */
    bool haveEye_ = false;
};

/* How calm the collision is (the [camera] collision comparison, bc.exe's
 * orbit summary): turning points of the pull-in (share of the distance) and
 * of the rise (degrees) - a move of at least `ratioStep` / `riseStep` one
 * way after the other - and the fastest moves (per second). */
struct CollisionStats {
    double ratioStep = 0.03, riseStep = 1.5;
    double seconds = 0;
    unsigned ratioTurns = 0, riseTurns = 0;
    double maxIn = 0, maxOut = 0, maxUp = 0, maxDown = 0;   /* share / s, share / s, deg / s, deg / s */
    unsigned steps = 0, inside = 0, pulled = 0;              /* steps; eye inside an obstacle; pulled under 0.6 */
    double maxLiftUp = 0, maxLiftStep = 0;                   /* the lift over the ground: units / s; one step's */
    /* the eye's height over the target (eyeRel): the largest rise in one step that the lift made (the smaller
     * of the lift's and the eye's rise), and the snaps: steps it rose over jumpStep that way out of a much
     * slower step before (not a spring's steady rise) */
    double maxEyeStep = 0, jumpStep = 3.0;
    unsigned eyeJumps = 0;
    void add(double ratio, double rise, bool eyeInside, double dt, double lift = 0.0, double eyeRel = 0.0);
    void restart() { have_ = false; }                        /* a cut: no move from the last value */

private:
    struct Turns {
        int dir = 0;
        double extreme = 0;
        bool add(double v, double step);   /* true at a turning point */
    };
    bool have_ = false;
    double lastRatio_ = 1, lastRise_ = 0, lastLift_ = 0, lastEye_ = 0, lastUp_ = 0;
    Turns ratio_, rise_;
};

/* Camera-relative steering (bc.exe's [camera] relative_steering): the N64
 * stick (x right, y up) turned by `deg` degrees clockwise (positive: up turns
 * towards right), its length kept, rounded to whole values in -127..127. */
void rotateStick(int x, int y, double deg, int &ox, int &oy);
/* The turn (for rotateStick) that makes a game that reads the stick relative
 * to its camera at yaw `gameYaw` (up: away from that eye, right: the view's
 * right) move the way the stick points relative to a view at `viewYaw`. */
double steerTurn(double gameYaw, double viewYaw);

/* the largest share (0..1] of the segment target -> target + dir * dist whose
 * points from share `from` on all stay `clearance` above the ground; 1 if
 * the height is unknown */
double clearShare(const Vec3 &target, const Vec3 &dir, double dist, double clearance, const HeightFn &height,
                  double from = 0.0);

/* a look-at view matrix (row vectors, translation in row 3: the N64's and
 * RT64's layout), up = +y */
void lookAt(const Vec3 &eye, const Vec3 &target, double m[4][4]);
/* eye and the point at `dist` ahead of a look-at view matrix */
bool viewEye(const double m[4][4], Vec3 &eye, Vec3 &forward);
/* an N64 Mtx (s15.16: the integer halves in words 0-7, the fractions in words 8-15, two elements a word, as
 * libultra's guMtxF2L writes them; the words in host order) as doubles */
void mtxFromN64(const uint32_t w[16], double m[4][4]);

/* the height of triangle p at (x, z): false outside it (in x/z) or for a
 * vertical one */
bool triHeight(const Vec3 p[3], double x, double z, double &h);

/* Triangles binned by their x/z bounds, for the collision's many height
 * queries (one step asks up to ~800): forEachAt gives every triangle whose
 * x/z bounds hold the point (and some others), so a query over them finds
 * exactly what a test of all of them would.  Rebuilt each step (the
 * buildings' triangles move and fall apart). */
class TriGrid {
public:
    void clear();
    void add(const Vec3 p[3]);
    void build();   /* after the adds, before the queries */
    size_t size() const { return tris_.size(); }
    template <typename F> void forEachAt(double x, double z, F f) const {
        if (tris_.empty() || !(x == x) || !(z == z)) return;
        for (uint32_t i : big_) f(tris_[i].p);
        const uint32_t b = uint32_t(row(z) * nx_ + col(x));
        for (uint32_t i = start_[b]; i < start_[b + 1]; i++) f(tris_[index_[i]].p);
    }

private:
    struct Tri {
        double minX, maxX, minZ, maxZ;
        Vec3 p[3];
    };
    int col(double x) const;
    int row(double z) const;
    std::vector<Tri> tris_;
    std::vector<uint32_t> start_, index_, big_;   /* bins (row-major), their triangles; ones in too many bins */
    double x0_ = 0, z0_ = 0, cw_ = 1, cd_ = 1;
    int nx_ = 1, nz_ = 1;
};

/* The drawing list's visibility test (render only): what a camera can see,
 * from the eye looking at the target, as a pyramid with the projection's
 * half angles (tan: tanX, tanY) widened by marginDeg (pictures between list
 * updates may look a little elsewhere), its apex moved back by `back` along
 * the view (the eye may move a little meanwhile), and the far plane at depth
 * farDist (> 0; plus `back`).  A side widened to 89 degrees or more stops
 * limiting.  (No `near`/`far` names here: windows.h defines them.) */
class ViewVolume {
public:
    ViewVolume(const Vec3 &eye, const Vec3 &target, double tanX, double tanY, double marginDeg, double back,
               double farDist);
    /* false if an axis-aligned box is surely outside (all its corners
     * beyond one of the planes) */
    bool boxVisible(const Vec3 &lo, const Vec3 &hi) const;
    /* exact: some part of the box is inside (its faces clipped against the
     * planes; or the view's axis starts inside it) - for checks; the plane
     * test above keeps some boxes that are outside (big ones beside the eye) */
    bool boxVisibleExact(const Vec3 &lo, const Vec3 &hi) const;

private:
    double m_[4][4];
    double sx_, sy_, far_;
    bool limX_, limY_;
};

/* the near and far distances of an N64 perspective projection (guPerspective,
 * row vectors); false if it isn't one */
bool perspectiveRange(const double p[4][4], double &nearZ, double &farZ);

/* The orbit stress test (--orbit-test sweep): the camera turned, tilted and
 * zoomed by itself, the same at every run, `t` seconds of game time after the
 * orbit became active: the yaw rate (degrees a second: both ways, up to about
 * 210, and a 320 flick of 0.75 s every 13 s), an absolute pitch over most of
 * the limits' range and a zoom over the whole range. */
struct Sweep {
    double yawRate = 0, pitch = 0, zoom = 1;
};
Sweep sweepAt(double t, const Limits &lim);

/* The frame-time report's spike attribution (live_frametimes.cpp): a present
 * interval over 1.5 times the target, against what happened in the retraces
 * before it.  The causes, the first that applies (a missed retrace mostly
 * spent in the display lists with RT64's queue full is the render thread's
 * or, with a picture over the target, the GPU's): */
enum SpikeCause {
    SPIKE_MAIN_GAME,      /* the main thread missed a retrace: mostly the game (and other host work) */
    SPIKE_MAIN_LISTS,     /* ... mostly reading the display lists (processDisplayLists) */
    SPIKE_MAIN_ORBIT,     /* ... mostly the orbit camera's host work */
    SPIKE_MAIN_SUBMIT,    /* ... mostly RT64's updateScreen (waiting for the render thread) */
    SPIKE_MAIN_OTHER,     /* ... mostly the rest: the host's work after the retrace (events, the controllers'
                             rescan, screenshots) or time the thread didn't run (the OS) */
    SPIKE_GPU,            /* a picture took the GPU over kSpikeGpuShare of the target (the present's own
                             GPU work, not timed, comes on top) */
    SPIKE_RENDER,         /* RT64's render thread took longer than the game frame (CPU) */
    SPIKE_SHADERS,        /* new materials (shaders submitted: the ubershader draws them meanwhile) */
    SPIKE_TEXTURES,       /* a burst of new textures (kSpikeTextures or more: a few each frame are usual) */
    SPIKE_PRESENT,        /* none of these: the present / pacing side (or the OS) */
    SPIKE_CAUSES
};
constexpr double kSpikeGpuShare = 0.75;
constexpr unsigned kSpikeTextures = 8;
const char *spikeCauseName(int cause);
/* what the retraces around a spike had (their largest values / sums) */
struct SpikeWindow {
    double retraceMs = 0;            /* the longest interval between two retraces (the main thread's loop) */
    double periodMs = 1000.0 / 60;   /* a retrace's period */
    double gameMs = 0, listsMs = 0, orbitMs = 0, submitMs = 0;   /* that retrace's main-thread work */
    double otherMs = 0;              /* ... and the rest of it (SPIKE_MAIN_OTHER) */
    double gpuMs = 0;                /* the longest picture on the GPU */
    double renderMs = 0, frameMs = 0;   /* the longest RT64 workload (render thread); the game frame's period */
    double targetMs = 0;             /* the present target */
    unsigned shaders = 0, textures = 0;   /* new shaders submitted, textures added */
    bool queueFull = false;          /* that retrace's frame task found RT64's workload queue full: reading
                                        its display lists ended waiting for the render thread */
};
int spikeCause(const SpikeWindow &w);

}  // namespace orbit
}  // namespace live

#endif
