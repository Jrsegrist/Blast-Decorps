/* bc_cameratest.exe: tests of the orbit camera's math (orbit_math.cpp):
 * limits, recentring, zoom, collision pull-in, the view matrix.  No window,
 * ROM or renderer; exit status 0 when all pass. */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "orbit_math.h"

using namespace live::orbit;

static int g_fail, g_pass;
void viewerTests(void (*check)(bool, const char *));   /* viewer_test.cpp */
void editTests(void (*check)(bool, const char *));     /* edit_test.cpp */
void terrainTestsAll(void (*check)(bool, const char *));   /* terrain_test.cpp */
int texcodecCross(const char *dir);                     /* terrain_test.cpp: --texcodec DIR */
int budgetsCross(const char *dir);                      /* terrain_test.cpp: --budgets DIR */
int subdivCross(const char *dir);                       /* terrain_test.cpp: --subdiv DIR */

static void check(bool ok, const char *what) {
    if (ok) g_pass++;
    else {
        g_fail++;
        printf("FAIL: %s\n", what);
    }
}

static bool near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

static bool nearV(const Vec3 &a, const Vec3 &b, double eps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

/* a game camera behind the target: yaw 30, pitch 20, distance 100 */
static Orbit::Game game(const Vec3 &target = {1000, 50, -400}) {
    Orbit::Game g;
    g.target = target;
    g.eye = target + direction(30, 20) * 100.0;
    return g;
}

static void run(Orbit &o, const Orbit::Game &g, const Input &in, double seconds, const Limits &lim,
                const HeightFn &h = nullptr, const HeightFn &terrain = nullptr) {
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < seconds - 1e-9; t += dt) o.step(g, in, dt, lim, h, terrain);
}

int main(int argc, char **argv) {
    if (argc >= 3 && std::string(argv[1]) == "--texcodec") return texcodecCross(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--budgets") return budgetsCross(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--subdiv") return subdivCross(argv[2]);
    Limits lim;
    check(lim.follow && !lim.recentre && lim.collision == COLLISION_OFF,
          "defaults: the view kept relative to the game's camera, no recentring, collision off");
    /* (most tests below: the world-fixed view, recentring and smooth collision, settings version 1's
     * behaviour; the new defaults have their own tests further down) */
    lim.follow = false;
    lim.recentre = true;
    lim.collision = COLLISION_SMOOTH;
    /* angles and directions */
    {
        double y, p;
        angles(direction(123, -17), y, p);
        check(near(y, 123, 1e-9) && near(p, -17, 1e-9), "angles(direction) round trip");
        check(near(wrap180(190), -170, 1e-12) && near(wrap360(-10), 350, 1e-12) && near(wrap180(-180), 180, 1e-12),
              "wrap180 / wrap360");
    }
    /* the view matrix: a look-at gives back its eye and looks at the target */
    {
        const Vec3 eye = {10, 20, 30}, at = {-5, 2, 7};
        double m[4][4];
        lookAt(eye, at, m);
        Vec3 e, f;
        check(viewEye(m, e, f), "viewEye accepts a look-at matrix");
        check(nearV(e, eye, 1e-9), "viewEye gives the eye back");
        check(nearV(normalize(f), normalize(at - eye), 1e-9), "the view looks at the target");
        /* the target in camera space: straight ahead (-z), at its distance */
        const double cz = at.x * m[0][2] + at.y * m[1][2] + at.z * m[2][2] + m[3][2];
        const double cx = at.x * m[0][0] + at.y * m[1][0] + at.z * m[2][0] + m[3][0];
        check(near(cz, -length(at - eye), 1e-9) && near(cx, 0, 1e-9), "the target is straight ahead");
        /* straight down is still a valid view */
        lookAt({0, 100, 0}, {0, 0, 0}, m);
        check(viewEye(m, e, f) && nearV(e, {0, 100, 0}, 1e-9), "a view straight down");
    }
    /* untouched: exactly the game's camera */
    {
        Orbit o;
        const Orbit::Game g = game();
        run(o, g, Input(), 1.0, lim);
        Vec3 eye;
        o.eyeFor(g, 0, 0, lim, eye);
        check(!o.engaged() && nearV(eye, g.eye, 1e-9), "not engaged, zoom 1: the game's own eye");
    }
    /* turning: yaw moves, the distance stays the game's */
    {
        Orbit o;
        const Orbit::Game g = game();
        Input in;
        in.yaw = 1.5;   /* 90 degrees a second */
        run(o, g, in, 1.0, lim);
        check(o.engaged() && near(wrap180(o.yaw() - 120), 0, 1e-6), "turning 90 degrees in a second");
        Vec3 eye;
        o.eyeFor(g, 0, 0, lim, eye);
        check(near(length(eye - g.target), 100, 1e-6), "turning keeps the distance");
        double y, p;
        angles(eye - g.target, y, p);
        check(near(y, 120, 1e-6) && near(p, 20, 1e-6), "the eye is at the orbit's yaw and the game's pitch");
        /* the extra yaw of pictures between steps */
        Vec3 e2;
        o.eyeFor(g, 5, 0, lim, e2);
        angles(e2 - g.target, y, p);
        check(near(y, 125, 1e-6), "extra yaw for a picture between steps");
    }
    /* pictures between steps (Orbit::between): a constant turn moves the view evenly picture by picture,
     * across steps too; the shorter arc over 0/360; engaging eases through the blend from the game's view */
    {
        Orbit o;
        const Orbit::Game g = game();
        Input in;
        in.yaw = 2.0;   /* 120 degrees a second at 60 steps */
        std::vector<Orbit> steps;
        for (int i = 0; i < 6; i++) {
            o.step(g, in, 1.0 / 60, lim, nullptr);
            steps.push_back(o);
        }
        std::vector<double> yaws;   /* 4 pictures a step (240 a second) */
        for (int i = 0; i + 1 < int(steps.size()); i++)
            for (int k = 0; k < 4; k++) {
                Vec3 e;
                Orbit::between(steps[i], steps[i + 1], k / 4.0).eyeFor(g, 0, 0, lim, e);
                double y, p;
                angles(e - g.target, y, p);
                yaws.push_back(y);
            }
        bool even = true;
        for (size_t i = 1; i < yaws.size(); i++) even = even && near(wrap180(yaws[i] - yaws[i - 1]), 0.5, 1e-6);
        check(even, "between steps: an even 0.5 degree turn per picture");
        Orbit a = steps[0], b = steps[0];
        Input t;
        t.yaw = 350 - a.yaw();
        a.step(g, t, 1.0 / 60, lim, nullptr);   /* yaw 350 */
        b = a;
        t.yaw = 20;
        b.step(g, t, 1.0 / 60, lim, nullptr);   /* yaw 10 */
        check(near(Orbit::between(a, b, 0.5).yaw(), 0.0, 1e-6) || near(Orbit::between(a, b, 0.5).yaw(), 360.0, 1e-6),
              "between: the shorter arc over 0 / 360");
        Orbit idle;
        idle.step(g, Input(), 1.0 / 60, lim, nullptr);
        Orbit engaged = idle;
        Input go;
        go.yaw = 10;
        engaged.step(g, go, 1.0 / 60, lim, nullptr);
        Vec3 e0, eh, e1;
        Orbit::between(idle, engaged, 0.0).eyeFor(g, 0, 0, lim, e0);
        Orbit::between(idle, engaged, 0.5).eyeFor(g, 0, 0, lim, eh);
        Orbit::between(idle, engaged, 1.0).eyeFor(g, 0, 0, lim, e1);
        double y0, yh, y1, p;
        angles(e0 - g.target, y0, p);
        angles(eh - g.target, yh, p);
        angles(e1 - g.target, y1, p);
        check(near(y0, 30, 1e-6) && near(y1, 40, 1e-6) && yh > 30 + 1e-3 && yh < 40 - 1e-3,
              "between: engaging eases from the game's view to the orbit's");
        check(nearV(e0, g.eye, 1e-9), "between at 0: the first state exactly");
    }
    /* the pitch limits */
    {
        Orbit o;
        const Orbit::Game g = game();
        Input in;
        in.pitch = 2.0;
        run(o, g, in, 2.0, lim);
        check(near(o.pitch(), lim.pitchMax, 1e-9), "tilting up stops at the highest tilt");
        in.pitch = -2.0;
        run(o, g, in, 3.0, lim);
        check(near(o.pitch(), lim.pitchMin, 1e-9), "tilting down stops at the lowest tilt");
        Vec3 e;
        o.eyeFor(g, 0, -50, lim, e);
        double y, p;
        angles(e - g.target, y, p);
        check(p >= lim.pitchMin - 1e-9, "extra pitch between steps stays inside the limits");
    }
    /* zoom: log steps, clamped */
    {
        Orbit o;
        const Orbit::Game g = game();
        Input in;
        in.zoom = 1.0;
        o.step(g, in, 1.0 / 60, lim, nullptr);
        check(near(o.zoom(), 2.0, 1e-9), "zoom out one step (log2 1 = twice as far)");
        o.step(g, in, 1.0 / 60, lim, nullptr);
        check(near(o.zoom(), lim.zoomMax, 1e-9), "zoom stops at the farthest");
        in.zoom = -10;
        o.step(g, in, 1.0 / 60, lim, nullptr);
        check(near(o.zoom(), lim.zoomMin, 1e-9), "zoom stops at the closest");
        Vec3 eye;
        o.eyeFor(g, 0, 0, lim, eye);
        check(near(length(eye - g.target), 100 * lim.zoomMin, 1e-6), "zoom scales the game's distance");
        Input r;
        r.reset = true;
        o.step(g, r, 1.0 / 60, lim, nullptr);
        check(near(o.zoom(), 1.0, 1e-12) && !o.engaged(), "reset: zoom 1, the game's camera");
    }
    /* recentring: idle for the delay, then back to the game's view */
    {
        Orbit o;
        Orbit::Game g = game();
        Input in;
        in.yaw = 3.0;
        run(o, g, in, 0.5, lim);   /* 90 degrees */
        const double turned = o.yaw();
        run(o, g, Input(), lim.recentreDelay - 0.1, lim);
        check(o.engaged() && near(o.yaw(), turned, 1e-9), "recentre waits for the delay");
        run(o, g, Input(), 4.0, lim);
        check(!o.engaged(), "recentred after the delay (back to the game's camera)");
        Vec3 eye;
        o.eyeFor(g, 0, 0, lim, eye);
        check(nearV(eye, g.eye, 1e-6), "recentred: exactly the game's eye");
        /* recentring follows a game camera that turns meanwhile */
        run(o, g, in, 0.5, lim);
        Orbit::Game g2 = g;
        g2.eye = g.target + direction(200, 25) * 100.0;
        run(o, g2, Input(), lim.recentreDelay + 4.0, lim);
        o.eyeFor(g2, 0, 0, lim, eye);
        check(!o.engaged() && nearV(eye, g2.eye, 1e-6), "recentring to a game camera that moved");
    }
    /* recentring off: stays where it was left (world direction) */
    {
        Limits l2 = lim;
        l2.recentre = false;
        Orbit o;
        Orbit::Game g = game();
        Input in;
        in.yaw = 3.0;
        run(o, g, in, 0.5, l2);
        const double turned = o.yaw();
        g.eye = g.target + direction(300, 20) * 100.0;   /* the game's camera swings round */
        run(o, g, Input(), 10.0, l2);
        check(o.engaged() && near(o.yaw(), turned, 1e-9), "recentre off: the camera stays where it was left");
    }
    /* ---- the kept view (the defaults: follow, no recentring) ---- */
    {
        const Limits d;   /* the defaults */
        const double dt = 1.0 / 60;
        /* the game's chase camera behind a vehicle driving a full circle in 12 s (yaw 30 -> 390) */
        auto chase = [](double t, double pitch = 20.0, double dist = 100.0) {
            Orbit::Game g;
            const double a = t / 12.0 * 2.0 * 3.14159265358979;
            g.target = {1000 + 300 * std::sin(a), 50, -400 + 300 * std::cos(a)};
            g.eye = g.target + direction(30 + 360.0 * t / 12.0, pitch) * dist;
            return g;
        };
        auto viewYaw = [&](const Orbit &o, const Orbit::Game &g, const Limits &l, double *pitch = nullptr) {
            Vec3 e;
            o.eyeFor(g, 0, 0, l, e);
            double y, p;
            angles(e - g.target, y, p);
            if (pitch) *pitch = p;
            return y;
        };
        Orbit o;
        Input in;
        in.yaw = 1.5;   /* 90 degrees in a second (the game's camera still) */
        for (int i = 0; i < 60; i++) o.step(chase(0), in, dt, d, nullptr);
        check(o.engaged() && near(o.yaw(), 90.0, 1e-6), "follow: the kept yaw is an offset from the game's (90)");
        bool kept = true, engaged = true;
        double t = 0;
        for (int i = 0; i < 12 * 60; i++) {
            t = (i + 1) * dt;
            const Orbit::Game g = chase(t);
            o.step(g, Input(), dt, d, nullptr);
            double gy, gp;
            angles(g.eye - g.target, gy, gp);
            kept = kept && near(wrap180(viewYaw(o, g, d) - gy), 90.0, 1e-6);
            engaged = engaged && o.engaged();
        }
        check(kept && engaged, "follow: driving a full circle the view stays 90 degrees from the chase camera");
        run(o, chase(t), Input(), 20.0, d);
        check(o.engaged() && near(o.yaw(), 90.0, 1e-9), "no recentring by default: still there 20 s later");
        /* the world-fixed frame: the same circle, the view's compass direction kept */
        Limits w = d;
        w.follow = false;
        Orbit ow;
        for (int i = 0; i < 60; i++) ow.step(chase(0), in, dt, w, nullptr);
        bool fixed = true;
        for (int i = 0; i < 12 * 60; i++) {
            const Orbit::Game g = chase((i + 1) * dt);
            ow.step(g, Input(), dt, w, nullptr);
            fixed = fixed && near(wrap180(viewYaw(ow, g, w) - 120.0), 0.0, 1e-6);
        }
        check(fixed, "follow off: driving a full circle the view keeps its world direction");
        /* the tilt: an offset too, the view limited where it is made (the offset kept) */
        Orbit op;
        Input up;
        up.pitch = 0.25;   /* 15 degrees up */
        for (int i = 0; i < 60; i++) op.step(chase(0), up, dt, d, nullptr);
        check(near(op.pitch(), 15.0, 1e-6), "follow: the tilt kept as an offset (15)");
        double p;
        op.step(chase(0, 35), Input(), dt, d, nullptr);
        viewYaw(op, chase(0, 35), d, &p);
        check(near(p, 50.0, 1e-6), "... the game's camera tilting with it (35 + 15)");
        op.step(chase(0, 75), Input(), dt, d, nullptr);
        viewYaw(op, chase(0, 75), d, &p);
        check(near(p, d.pitchMax, 1e-6) && near(op.pitch(), 15.0, 1e-9), "... at most the highest tilt, the offset kept");
        op.step(chase(0, 20), Input(), dt, d, nullptr);
        viewYaw(op, chase(0, 20), d, &p);
        check(near(p, 35.0, 1e-6), "... and back with the game's camera");
        /* the zoom and the offset through getting out (a closer camera, another place and direction) and a
         * pause (no steps meanwhile) */
        Orbit oz;
        Input zi;
        zi.zoom = std::log2(1.6);
        zi.yaw = 40;
        oz.step(chase(0), zi, dt, d, nullptr);
        Orbit::Game foot = chase(0, 30, 40);
        foot.target = foot.target + Vec3{200, 0, -150};
        foot.eye = foot.target + direction(250, 30) * 40.0;
        run(oz, foot, Input(), 3.0, d);
        Vec3 e;
        oz.eyeFor(foot, 0, 0, d, e);
        check(near(oz.zoom(), 1.6, 1e-9) && near(length(e - foot.target), 64.0, 1e-6) &&
                  near(wrap180(viewYaw(oz, foot, d) - 290.0), 0.0, 1e-6),
              "getting out: the zoom (1.6 x the new distance) and the offset (40) kept");
        const Orbit before = oz;
        run(oz, chase(5), Input(), 1.0, d);   /* back in, after a pause */
        check(near(oz.zoom(), before.zoom(), 1e-12) && near(oz.yaw(), before.yaw(), 1e-12) && oz.engaged(),
              "getting back in after a pause: the same view");
        /* a new level (bc.exe: reset(false)): behind the player again, the zoom kept; camera_reset: all */
        oz.reset(false);
        check(!oz.engaged() && near(oz.zoom(), 1.6, 1e-12), "a new level: the game's direction, the zoom kept");
        Input r;
        r.reset = true;
        oz.step(chase(0), r, dt, d, nullptr);
        check(!oz.engaged() && near(oz.zoom(), 1.0, 1e-12), "camera_reset: the game's camera, zoom 1");
        /* recentring on (an option): the offset eases back to 0 after the delay */
        Limits rc = d;
        rc.recentre = true;
        Orbit orc;
        for (int i = 0; i < 60; i++) orc.step(chase(0), in, dt, rc, nullptr);
        for (int i = 0; i < 8 * 60; i++) orc.step(chase((i + 1) * dt), Input(), dt, rc, nullptr);
        Vec3 ge;
        orc.eyeFor(chase(8), 0, 0, rc, ge);
        check(!orc.engaged() && nearV(ge, chase(8).eye, 1e-6), "recentre on: back to the game's camera, following it");
        /* pictures between steps with a turning game camera: the offset between the two states' */
        Orbit a = o, b = o;
        Input more;
        more.yaw = 10;
        b.step(chase(t), more, dt, d, nullptr);
        const Orbit h = Orbit::between(a, b, 0.5);
        double gy, gp;
        angles(chase(t).eye - chase(t).target, gy, gp);
        check(near(wrap180(viewYaw(h, chase(t), d) - gy), 95.0, 1e-6), "between: the offsets blend (90 .. 100: 95)");
    }
    /* ---- collision off (the default): buildings ignored, the terrain under the eye, no snaps ---- */
    {
        const Limits d;
        const double dt = 1.0 / 60;
        Input in;
        in.yaw = 1e-6;
        /* the terrain: a cliff 30 high at x 150 (the eye 100 ahead of the target on +x, the target driving
         * towards it at 30 units a second) */
        const HeightFn cliff = [](double x, double, double) { return x > 150.0 ? 30.0 : 0.0; };
        const HeightFn wall = [](double x, double, double z) { return x > 60 && x < 80 && std::fabs(z) < 50 ? 60.0 : 0.0; };
        Orbit o;
        double maxStep = 0, lastY = 0, minAbove = 1e9;
        bool have = false;
        for (int i = 0; i < 6 * 60; i++) {
            Orbit::Game g;
            g.target = {i * 0.5, 5, 0};
            g.eye = g.target + direction(90, 3) * 100.0;
            o.step(g, in, dt, d, wall, cliff);
            Vec3 e;
            o.eyeFor(g, 0, 0, d, e);
            if (have) maxStep = std::max(maxStep, e.y - lastY);
            lastY = e.y, have = true;
            minAbove = std::min(minAbove, e.y - cliff(e.x, e.y, e.z));
        }
        check(minAbove >= d.floorClear - 1e-6, "off: the eye never under the terrain");
        check(maxStep < 10.0, "off: up a 30-unit cliff on a spring (no step over 10 units)");
        check(near(o.distanceRatio(), 1.0, 1e-9) && near(o.rise(), 0.0, 1e-9), "off: never pulled in or risen");
        /* the eye in a building: counted, not moved */
        Orbit ob;
        Orbit::Game g;
        g.target = {0, 5, 0};
        g.eye = g.target + direction(90, 10) * 70.0;   /* the eye at x ~69: inside the wall */
        run(ob, g, in, 1.0, d, wall, [](double, double, double) { return 0.0; });
        check(ob.eyeInside() && near(ob.distanceRatio(), 1.0, 1e-9), "off: the eye inside a building is counted only");
    }
    /* collision (strict: as before the smooth default): flat ground at y = 0, the target 5 above it */
    {
        const HeightFn flat = [](double, double, double) { return 0.0; };
        Limits l = lim;
        l.pitchMin = -30;
        l.collision = COLLISION_STRICT;
        Orbit o;
        Orbit::Game g;
        g.target = {0, 5, 0};
        g.eye = g.target + direction(0, 20) * 100.0;
        /* clearShare on its own: a ray 30 degrees down from y = 5 reaches y = 1 after 8 units */
        const double share = clearShare(g.target, direction(0, -30), 100.0, 1.0, flat, 0.0);
        check(near(share * 100.0, 8.0, 0.05), "clearShare: the free part of a ray into the ground");
        check(near(clearShare(g.target, direction(0, 30), 100.0, 1.0, flat), 1.0, 1e-12), "clearShare: a clear ray");
        check(near(clearShare(g.target, direction(0, -30), 100.0, 1.0, nullptr), 1.0, 1e-12), "clearShare: no ground known");
        Input in;
        in.pitch = -3.0;   /* tilt down below the horizontal */
        run(o, g, in, 1.0, l, flat);
        Vec3 eye;
        o.eyeFor(g, 0, 0, l, eye);
        check(near(o.pitch(), -30, 1e-9), "tilted to the lowest tilt");
        check(eye.y >= l.clearance - 0.05, "the eye stays above the ground");
        check(o.rise() > 25.0, "tilted into the ground: the view rises over it");
        check(o.distanceRatio() > 0.5, "... at once, not pulled in to the target meanwhile");
        run(o, g, Input(), 1.0, l, flat);
        run(o, g, Input(), 5.0, l, flat);   /* idle: recentres too, all the way out */
        o.eyeFor(g, 0, 0, l, eye);
        check(near(length(eye - g.target), 100.0, 0.5), "out to the full distance again");
        /* the game's own camera is never pulled in (zoom 1, not engaged) */
        Orbit o2;
        Orbit::Game low = g;
        low.eye = g.target + direction(0, -30) * 100.0;
        run(o2, low, Input(), 1.0, l, flat);
        o2.eyeFor(low, 0, 0, l, eye);
        check(nearV(eye, low.eye, 1e-9), "the game's own camera is left as it is");
        /* a wall behind the target 30 units away: pulled in to just before it */
        const HeightFn wall = [](double x, double, double) { return x > 30.0 ? 1e6 : 0.0; };
        Orbit o3;
        Orbit::Game gw;
        gw.target = {0, 5, 0};
        gw.eye = gw.target + direction(90, 10) * 100.0;   /* +x side */
        in = Input();
        in.yaw = 1e-6;
        run(o3, gw, in, 2.0, l, wall);
        o3.eyeFor(gw, 0, 0, l, eye);
        check(eye.x <= 30.0 + 1e-6 && eye.x > 25.0, "kept just before a wall too high to see over");
        check(near(o3.lift(), 0.0, 1e-9) && o3.rise() > 40.0 && length(eye - gw.target) > 50.0,
              "... looking down past it from higher up rather than pulled in to the target (no lift)");
        /* the rise that sees farthest only if clearly farther: a wall right behind the target
         * (no rise helps much) still pulls in */
        const HeightFn nearWall = [](double x, double, double) { return x > 3.0 ? 1e6 : 0.0; };
        Orbit o5;
        run(o5, gw, in, 0.5, l, nearWall);
        check(o5.rise() < 1e-6 && o5.distanceRatio() < 0.2, "a wall right behind the target: pulled in, no rise "
                                                             "(none sees clearly farther)");
        /* the wall gone: lets out slowly, not at once */
        o5.step(gw, in, 1.0 / 60, l, flat);
        o5.step(gw, in, 1.0 / 60, l, flat);
        check(o5.distanceRatio() < 0.5, "lets out slowly");
        /* a low wall 20 high at x 20..30: rises over it (clear 4 above it from x = 20:
         * tan p >= 19 / 20, p >= 43.5: to 45 degrees, 10 + 35) */
        const HeightFn lowWall = [](double x, double, double) { return x > 20.0 && x < 30.0 ? 20.0 : 0.0; };
        Orbit o4;
        run(o4, gw, in, 4.0, l, lowWall);
        o4.eyeFor(gw, 0, 0, l, eye);
        check(near(o4.rise(), 35.0, 0.5), "rises over a low wall");
        check(near(o4.distanceRatio(), 1.0, 0.01), "... at the full distance");
        /* turned to face a low wall close behind: rises at once, never pulled in to the target meanwhile */
        Orbit o6;
        Orbit::Game gf = gw;
        gf.eye = gf.target + direction(270, 10) * 100.0;   /* -x side: open */
        in = Input();
        in.yaw = 1e-6;
        run(o6, gf, in, 1.0, l, lowWall);
        in.yaw = 180.0;   /* one step round to the wall's side */
        o6.step(gf, in, 1.0 / 60, l, lowWall);
        check(o6.rise() > 30.0 && o6.distanceRatio() > 0.5, "turned to a low wall: rises at once, not pulled in");
    }
    /* the critically damped spring */
    {
        double x = 0, v = 0, maxX = 0;
        bool rising = true;
        std::vector<double> xs;
        for (int i = 0; i < 120; i++) {
            const double before = x;
            Spring::step(x, v, 1.0, 0.5, 1.0 / 60);
            rising = rising && x >= before;
            maxX = std::max(maxX, x);
            xs.push_back(x);
        }
        check(rising && maxX <= 1.0, "spring: rises monotonically, never past the target");
        check(xs[29] > 0.4 && xs[29] < 0.8 && xs[119] > 0.99, "spring: about the smoothing time to get there");
        double y = 0, w = 0;
        for (int i = 0; i < 120; i++) Spring::step(y, w, 1.0, 0.5, 1.0 / 60);
        check(y == x, "spring: deterministic");
        double a = 1, av = 0;
        Spring::step(a, av, 0.0, 0.05, 1.0 / 60);
        check(a > 0.0 && a < 1.0, "spring: a fast one still moves over a few steps, not at once");
        check(std::string(collisionName(COLLISION_SMOOTH)) == "smooth" && std::string(collisionName(COLLISION_OFF)) == "off" &&
                  std::string(collisionName(COLLISION_STRICT)) == "strict",
              "collision mode names");
    }
    /* smooth collision (the default): calm, ignores thin and brief obstructions, never stays inside */
    {
        Limits l = lim;
        check(l.collision == COLLISION_SMOOTH, "collision: smooth (these tests)");
        Orbit::Game gw;
        gw.target = {0, 5, 0};
        gw.eye = gw.target + direction(90, 10) * 100.0;   /* the eye on the +x side */
        Input in;
        in.yaw = 1e-6;   /* engaged, not turning */
        const double dt = 1.0 / 60;
        auto eyeOf = [&](const Orbit &o) {
            Vec3 e;
            o.eyeFor(gw, 0, 0, l, e);
            return e;
        };
        /* a thin pole on the line of sight (x 45, 1 unit wide, very tall): ignored (it just occludes) */
        const HeightFn pole = [](double x, double, double z) {
            return std::fabs(x - 45.0) < 0.5 && std::fabs(z) < 0.5 ? 1e6 : 0.0;
        };
        Orbit o;
        run(o, gw, in, 2.0, l, pole);
        check(near(o.distanceRatio(), 1.0, 1e-9) && near(o.rise(), 0.0, 1e-9), "smooth: a thin pole doesn't move it");
        Limits ls = l;
        ls.collision = COLLISION_STRICT;
        Orbit os;
        run(os, gw, in, 2.0, ls, pole);
        check(os.distanceRatio() < 0.99 || os.rise() > 1.0, "... (strict moves for it)");
        /* a wide wall too high to see over (x > 30) that appears for a moment (0.1 s): nothing moves */
        double now = 0;
        bool wallOn = false;
        /* (between the target and the eye at x ~98) */
        const HeightFn farWall = [&](double x, double, double) { return wallOn && x > 70.0 && x < 90 ? 1e6 : 0.0; };
        Orbit ob;
        run(ob, gw, in, 0.5, l, farWall);
        wallOn = true;
        for (int i = 0; i < 6; i++) ob.step(gw, in, dt, l, farWall);
        wallOn = false;
        run(ob, gw, in, 1.0, l, farWall);
        check(near(ob.distanceRatio(), 1.0, 1e-9) && near(ob.rise(), 0.0, 1e-9),
              "smooth: a brief obstruction (0.1 s, under holdIn) just occludes");
        /* the same far wall for good: after holdIn it rises over it or pulls in, smoothly (no step over 20 % of
         * the distance or 15 degrees) */
        wallOn = true;
        Orbit oc;
        run(oc, gw, in, 0.5, l, nullptr);
        double maxJump = 0, maxRiseJump = 0, lastR = oc.distanceRatio(), lastU = oc.rise();
        int moveStep = -1;
        for (int i = 0; i < 180; i++) {
            oc.step(gw, in, dt, l, farWall);
            maxJump = std::max(maxJump, std::fabs(oc.distanceRatio() - lastR));
            maxRiseJump = std::max(maxRiseJump, std::fabs(oc.rise() - lastU));
            if (moveStep < 0 && (oc.distanceRatio() < 0.999 || oc.rise() > 0.01)) moveStep = i;
            lastR = oc.distanceRatio(), lastU = oc.rise();
        }
        const Vec3 ec = eyeOf(oc);
        check(moveStep >= int(l.holdIn * 60) - 1 && moveStep <= int(l.holdIn * 60) + 2,
              "smooth: a lasting obstruction moves it after holdIn");
        check(!farWall(ec.x, ec.y, ec.z) || ec.y > 1e5, "... out of the wall's way");
        check(clearShare(gw.target, normalize(ec - gw.target), length(ec - gw.target), l.clearance, farWall, l.nearShare) >
                  0.99,
              "... seeing the target past it");
        check(maxJump < 0.2 && maxRiseJump < 15.0, "... without jumps (springs)");
        /* the wall gone: holds for holdOut, then back out over about the slow spring's time, never past */
        wallOn = false;
        const double r0 = oc.distanceRatio(), u0 = oc.rise();
        run(oc, gw, in, l.holdOut - 0.1, l, farWall);
        check(near(oc.distanceRatio(), r0, 1e-9) && near(oc.rise(), u0, 1e-9), "smooth: holds a while after it clears");
        run(oc, gw, in, 0.6, l, farWall);
        const double mid = oc.rise() + (1.0 - oc.distanceRatio()) * 100;
        check(mid > 0.01, "... then goes back slowly");
        run(oc, gw, in, 5.0, l, farWall);
        check(near(oc.distanceRatio(), 1.0, 1e-3) && near(oc.rise(), 0.0, 0.05), "... all the way");
        /* buildings passing by (the wall on for 0.3 s, off for 0.3 s, for 12 s): smooth moves once and stays;
         * strict goes in and out every time */
        auto passing = [&](const Limits &lm) {
            Orbit p;
            CollisionStats st;
            run(p, gw, in, 0.5, lm, nullptr);
            for (int i = 0; i < 12 * 60; i++) {
                now = i * dt;
                wallOn = std::fmod(now, 0.6) < 0.3;
                p.step(gw, in, dt, lm, farWall);
                st.add(p.distanceRatio(), p.rise(), p.eyeInside(), dt);
            }
            return st;
        };
        const CollisionStats calm = passing(l), jumpy = passing(ls);
        check(calm.ratioTurns + calm.riseTurns <= 2, "smooth: passing buildings: at most one move in and one out");
        check(jumpy.ratioTurns + jumpy.riseTurns >= 10, "... (strict: in and out every time)");
        check(calm.maxIn < jumpy.maxIn || jumpy.maxIn == 0, "... and its fastest move is slower than strict's");
        /* the eye inside an obstacle (a wall appearing around it): out within a few steps */
        const HeightFn around = [&](double x, double, double) { return wallOn && x > 60.0 ? 1e6 : 0.0; };
        Orbit oi;
        wallOn = false;
        run(oi, gw, in, 0.5, l, around);
        wallOn = true;
        int out = -1;
        for (int i = 0; i < 60 && out < 0; i++) {
            oi.step(gw, in, dt, l, around);
            const Vec3 e = eyeOf(oi);
            if (!(e.x > 60.0 && e.y < 1e5)) out = i;
        }
        check(out >= 0 && out <= 12, "smooth: the eye inside an obstacle gets out within 0.2 s");
        /* the target itself under a roof (pushing into a building): nothing sees it, pulling in would only put
         * the eye into the vehicle: the view stays (occluded) */
        const HeightFn roof = [](double x, double, double z) {
            return std::fabs(x) < 20.0 && std::fabs(z) < 20.0 ? 1e6 : 0.0;
        };
        Orbit orf;
        run(orf, gw, in, 2.0, l, roof);
        check(near(orf.distanceRatio(), 1.0, 1e-9) && near(orf.rise(), 0.0, 1e-9) && !orf.eyeInside(),
              "smooth: the target under a roof: no pull-in into the vehicle");
        /* off: never moves for obstacles; kept above the terrain */
        Limits lo = l;
        lo.collision = COLLISION_OFF;
        Orbit oo;
        wallOn = true;
        const HeightFn terrain = [](double x, double, double) { return x > 50 ? 40.0 : 0.0; };   /* a hill */
        run(oo, gw, in, 2.0, lo, farWall, terrain);
        const Vec3 eo = eyeOf(oo);
        check(near(oo.distanceRatio(), 1.0, 1e-9) && near(oo.rise(), 0.0, 1e-9), "off: never pulled in or risen");
        check(eo.y >= 40.0 + lo.clearance - 1e-6, "off: kept above the terrain");
        Orbit oo2;
        run(oo2, gw, in, 2.0, lo, farWall);
        check(near(oo2.lift(), 0.0, 1e-12), "off: no terrain given: not even lifted");
    }
    /* the collision statistics: turning points with a minimum move */
    {
        CollisionStats st;
        const double v[] = {1.0, 0.9, 0.5, 0.52, 0.51, 0.9, 1.0, 0.99, 0.6};
        for (double x : v) st.add(x, 0.0, false, 1.0 / 60);
        check(st.ratioTurns == 2 && st.riseTurns == 0, "stats: two turning points (small wobbles don't count)");
        check(near(st.maxIn, 0.4 * 60, 1e-9) && near(st.maxOut, 0.39 * 60, 1e-9), "stats: the fastest moves");
        check(st.steps == 9 && st.pulled == 3, "stats: steps and steps pulled in under 0.6");
    }
    /* camera-relative steering: the stick turned, its length kept */
    {
        int x, y;
        rotateStick(0, 80, 90, x, y);
        check(x == 80 && y == 0, "stick: up turned 90 clockwise is right");
        rotateStick(0, 80, 180, x, y);
        check(x == 0 && y == -80, "stick: 180 is down");
        rotateStick(80, 80, 45, x, y);
        check(x == 113 && y == 0, "stick: a keyboard diagonal keeps its length (113)");
        rotateStick(37, -61, 0, x, y);
        check(x == 37 && y == -61, "stick: no turn, the same values");
        rotateStick(-80, 0, -90, x, y);
        check(x == 0 && y == -80, "stick: left turned 90 counterclockwise is down");
        /* the model of a camera-relative game: stick (sx, sy) moves the character
         * sy * forward + sx * right, forward away from the eye (yaw Y: eye - target) */
        auto move = [](double sx, double sy, double Y) {
            const double r = Y * 3.14159265358979 / 180.0;
            const Vec3 fwd = {-std::sin(r), 0, -std::cos(r)}, right = {std::cos(r), 0, -std::sin(r)};
            return fwd * sy + right * sx;
        };
        unsigned seed = 4242;
        auto rnd = [&](double lo, double hi) {
            seed = seed * 1103515245u + 12345u;
            return lo + (hi - lo) * double((seed >> 8) & 0xFFFF) / 65535.0;
        };
        int bad = 0;
        for (int i = 0; i < 2000; i++) {
            const double gy = rnd(0, 360), vy = rnd(0, 360);
            const int sx = int(rnd(-80, 80)), sy = int(rnd(-80, 80));
            if (sx * sx + sy * sy < 400) continue;
            int rx, ry;
            rotateStick(sx, sy, steerTurn(gy, vy), rx, ry);
            const Vec3 got = normalize(move(rx, ry, gy)), want = normalize(move(sx, sy, vy));
            bad += dot(got, want) < std::cos(3.0 * 3.14159265358979 / 180.0);
        }
        check(bad == 0, "steering: the turned stick moves the character as the stick points in the orbit's view");
        const Vec3 away = normalize(move(0, 80, 200.0));
        int rx, ry;
        rotateStick(0, 80, steerTurn(30.0, 200.0), rx, ry);
        check(dot(normalize(move(rx, ry, 30.0)), away) > 0.999 && near(steerTurn(30, 30), 0, 1e-12),
              "steering: up goes away from the orbit's eye; the same view: no turn");
    }
    /* the collision's triangle grid: the same heights as a test of every triangle */
    {
        unsigned seed = 12345;
        auto rnd = [&](double lo, double hi) {
            seed = seed * 1103515245u + 12345u;
            return lo + (hi - lo) * double((seed >> 8) & 0xFFFF) / 65535.0;
        };
        std::vector<std::array<Vec3, 3>> tris;
        for (int i = 0; i < 1500; i++) {   /* small walls and roofs, a few big slabs, some vertical */
            const double cx = rnd(-2000, 2000), cz = rnd(-2000, 2000), r = i % 97 == 0 ? 3000 : rnd(2, 60);
            std::array<Vec3, 3> t = {{{cx + rnd(-r, r), rnd(0, 300), cz + rnd(-r, r)},
                                      {cx + rnd(-r, r), rnd(0, 300), cz + rnd(-r, r)},
                                      {cx + rnd(-r, r), rnd(0, 300), cz + rnd(-r, r)}}};
            if (i % 50 == 0) t[2] = {t[0].x, t[0].y + 40, t[0].z};
            tris.push_back(t);
        }
        TriGrid grid;
        for (const auto &t : tris) grid.add(t.data());
        grid.build();
        check(grid.size() == tris.size(), "the grid keeps every triangle");
        int same = 0, total = 0, hits = 0;
        for (int q = 0; q < 20000; q++) {
            /* points anywhere, at triangle corners and outside the triangles' area */
            double x = rnd(-2600, 2600), z = rnd(-2600, 2600);
            if (q % 4 == 0) {
                const auto &t = tris[q % tris.size()];
                x = t[q % 3].x, z = t[q % 3].z;
            }
            double all = -1e31, h;
            for (const auto &t : tris)
                if (triHeight(t.data(), x, z, h)) all = std::max(all, h);
            double binned = -1e31;
            grid.forEachAt(x, z, [&](const Vec3 *p) {
                if (triHeight(p, x, z, h)) binned = std::max(binned, h);
            });
            same += all == binned;
            hits += all > -1e30;
            total++;
        }
        check(same == total, "grid height queries equal a test of every triangle");
        check(hits > 1000, "... with many queries over triangles");
        TriGrid empty;
        empty.build();
        int calls = 0;
        empty.forEachAt(0, 0, [&](const Vec3 *) { calls++; });
        check(calls == 0, "an empty grid has no triangles anywhere");
    }
    /* the drawing list's view volume */
    {
        /* guPerspective(fovy 60, aspect 4:3, near 10, far 5000, scale 1), row vectors */
        const double n = 10, f = 5000, cot = 1.0 / std::tan(30.0 * 3.14159265358979 / 180.0);
        double p[4][4] = {{cot / (4.0 / 3.0), 0, 0, 0}, {0, cot, 0, 0}, {0, 0, (n + f) / (n - f), -1},
                          {0, 0, 2 * n * f / (n - f), 0}};
        double nz, fz;
        check(perspectiveRange(p, nz, fz) && near(nz, 10, 1e-9) && near(fz, 5000, 1e-6), "perspective near/far");
        for (auto &r : p)
            for (double &v : r) v *= 2.0;   /* guPerspective's scale */
        check(perspectiveRange(p, nz, fz) && near(nz, 10, 1e-9) && near(fz, 5000, 1e-6), "... with a scale");
        double id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        check(!perspectiveRange(id, nz, fz), "not a perspective");
        const double tx = 1.0 / cot * (4.0 / 3.0), ty = 1.0 / cot;
        const Vec3 eye = {0, 100, 0}, at = {0, 100, -100};   /* looking along -z */
        const ViewVolume v(eye, at, tx, ty, 0.0, 0.0, 5000);
        auto box = [](const Vec3 &c, double r) { return std::make_pair(c - Vec3{r, r, r}, c + Vec3{r, r, r}); };
        auto vis = [&](const ViewVolume &vv, const Vec3 &c, double r) {
            const auto b = box(c, r);
            return vv.boxVisible(b.first, b.second);
        };
        check(vis(v, {0, 100, -500}, 5), "a box straight ahead is seen");
        check(!vis(v, {0, 100, 500}, 5), "a box behind is not");
        check(!vis(v, {2000, 100, -500}, 5), "a box far to the right is not");
        check(vis(v, {500 * tx - 1, 100, -500}, 5), "a box just inside the right edge is");
        check(!vis(v, {500 * tx + 10, 100, -500}, 5), "... just outside it is not");
        check(!vis(v, {0, 100 + 500 * ty + 10, -500}, 5), "... nor just above the top edge");
        check(!vis(v, {0, 100, -6000}, 5), "a box beyond the far plane is not");
        check(vis(v, {0, 100, 2}, 5), "a box around the eye is");
        const ViewVolume wide(eye, at, tx, ty, 15.0, 0.0, 5000);
        check(vis(wide, {500 * tx + 10, 100, -500}, 5), "the margin widens the edges");
        const ViewVolume back(eye, at, tx, ty, 0.0, 50.0, 5000);
        check(vis(back, {0, 100, 30}, 5), "the apex moved back sees a little behind the eye");
        const ViewVolume all(eye, at, tx, ty, 80.0, 0.0, 0);
        check(vis(all, {5000, 100, -1}, 5) && !vis(all, {0, 100, 500}, 5), "a side over 89 degrees stops limiting");
        /* the margins only ever add: every box the exact volume sees, a wider one sees */
        unsigned seed = 777;
        auto rnd = [&](double lo, double hi) {
            seed = seed * 1103515245u + 12345u;
            return lo + (hi - lo) * double((seed >> 8) & 0xFFFF) / 65535.0;
        };
        int lost = 0, seen = 0;
        for (int i = 0; i < 20000; i++) {
            const Vec3 e = {rnd(-500, 500), rnd(0, 300), rnd(-500, 500)};
            const Vec3 t = {rnd(-500, 500), rnd(0, 100), rnd(-500, 500)};
            if (length(t - e) < 1) continue;
            const ViewVolume ex(e, t, tx * 1.467, ty, 0.0, 0.0, 3000);
            const ViewVolume wd(e, t, tx * 1.467, ty, rnd(0, 40), rnd(0, 80), 3000);
            const Vec3 c = {rnd(-3000, 3000), rnd(-100, 400), rnd(-3000, 3000)};
            const double r = rnd(1, 200);
            /* (boxes in front of the eye: one reaching behind it may pass the
             * plane test without being seen at all) */
            const Vec3 fw = normalize(t - e);
            bool front = true;
            for (int k = 0; k < 8; k++) {
                const Vec3 q = {c.x + ((k & 1) ? r : -r), c.y + ((k & 2) ? r : -r), c.z + ((k & 4) ? r : -r)};
                front = front && dot(q - e, fw) > 0;
            }
            if (front && vis(ex, c, r)) {
                seen++;
                lost += !vis(wd, c, r);
            }
        }
        check(seen > 1000 && lost == 0, "margins never drop a box the exact view sees");
        /* the exact test: a big flat box beside and behind the eye passes the plane test but isn't seen */
        {
            /* the game's projection: fovy 45, 4:3 (proj [0][0] 1.81065, [1][1] 2.4142) */
            const Vec3 e = {2480.7, 252.8, 1524.2}, t = {2178.7, 0.7, 1328.2};
            const ViewVolume ex(e, t, 1.0 / 1.81065, 1.0 / 2.4142, 0.0, 0.0, 10200);
            check(ex.boxVisible({3000, 0, 0}, {4500, 1, 2500}) && !ex.boxVisibleExact({3000, 0, 0}, {4500, 1, 2500}),
                  "exact test: a big box beside the eye (level 3) is out, the plane test keeps it");
            check(ex.boxVisibleExact({2000, 0, 1000}, {2400, 1, 1600}), "exact test: the ground under the target is in");
            check(ex.boxVisibleExact({2400, 200, 1500}, {2500, 300, 1550}), "exact test: a box around the eye is in");
            check(ex.boxVisibleExact({-50000, -50000, -50000}, {50000, 50000, 50000}), "exact test: inside a huge box");
        }
        /* exact implies the plane test; and the margins keep every box the exact view sees */
        int bad = 0, lost2 = 0, seen2 = 0;
        for (int i = 0; i < 20000; i++) {
            const Vec3 e = {rnd(-500, 500), rnd(0, 300), rnd(-500, 500)};
            const Vec3 t = {rnd(-500, 500), rnd(0, 100), rnd(-500, 500)};
            if (length(t - e) < 1) continue;
            const ViewVolume ex(e, t, tx * 1.467, ty, 0.0, 0.0, 3000);
            const ViewVolume wd(e, t, tx * 1.467, ty, rnd(0, 40), rnd(0, 80), 3000);
            const Vec3 c = {rnd(-3000, 3000), rnd(-100, 400), rnd(-3000, 3000)};
            const Vec3 r = {rnd(1, 1500), rnd(1, 200), rnd(1, 1500)};
            if (ex.boxVisibleExact(c - r, c + r)) {
                seen2++;
                bad += !ex.boxVisible(c - r, c + r);
                lost2 += !wd.boxVisible(c - r, c + r);
            }
        }
        check(seen2 > 1000 && bad == 0, "the exact test only passes boxes the plane test passes");
        check(lost2 == 0, "margins keep every box the exact view sees (big boxes too)");
    }
    /* the stress test's motion (--orbit-test sweep) */
    {
        double yMin = 1e9, yMax = -1e9, pMin = 1e9, pMax = -1e9, zMin = 1e9, zMax = -1e9;
        bool inLimits = true, same = true;
        for (int i = 0; i < 60 * 60; i++) {
            const double t = i / 60.0;
            const Sweep s = sweepAt(t, lim), s2 = sweepAt(t, lim);
            same = same && s.yawRate == s2.yawRate && s.pitch == s2.pitch && s.zoom == s2.zoom;
            yMin = std::min(yMin, s.yawRate), yMax = std::max(yMax, s.yawRate);
            pMin = std::min(pMin, s.pitch), pMax = std::max(pMax, s.pitch);
            zMin = std::min(zMin, s.zoom), zMax = std::max(zMax, s.zoom);
            inLimits = inLimits && s.pitch >= lim.pitchMin && s.pitch <= lim.pitchMax && s.zoom >= lim.zoomMin - 1e-9 &&
                       s.zoom <= lim.zoomMax + 1e-9 && std::isfinite(s.yawRate);
        }
        check(same && inLimits, "sweep: the same at every call, pitch and zoom within the limits");
        check(yMin < -180 && yMax >= 320 && pMin < lim.pitchMin + 4 && pMax > lim.pitchMax - 7 &&
                  zMin < lim.zoomMin * 1.05 && zMax > lim.zoomMax * 0.95,
              "sweep: turns both ways with flicks, over the pitch and zoom ranges in a minute");
        check(sweepAt(12.5, lim).yawRate == 320 && sweepAt(25.5, lim).yawRate == 320 && sweepAt(12.0, lim).yawRate != 320,
              "sweep: a flick every 13 s");
    }
    /* the spike attribution's rules */
    {
        SpikeWindow w;
        w.targetMs = 8.33;
        w.frameMs = 33.3;
        check(spikeCause(w) == SPIKE_PRESENT, "spike: nothing found -> present / other");
        w.textures = 3;
        check(spikeCause(w) == SPIKE_PRESENT, "spike: a few new textures are usual");
        w.textures = 12;
        check(spikeCause(w) == SPIKE_TEXTURES, "spike: a burst of new textures");
        w.gpuMs = 6;
        check(spikeCause(w) == SPIKE_TEXTURES, "spike: a picture under 3/4 of the target is no GPU spike");
        w.gpuMs = 0;
        w.shaders = 1;
        check(spikeCause(w) == SPIKE_SHADERS, "spike: new shaders before new textures");
        w.renderMs = 40;
        check(spikeCause(w) == SPIKE_RENDER, "spike: the render thread over the game frame");
        w.gpuMs = 9;
        check(spikeCause(w) == SPIKE_GPU, "spike: a picture over the target on the GPU");
        w.retraceMs = 24;   /* under 1.5 retraces: not missed */
        w.orbitMs = 20;
        check(spikeCause(w) == SPIKE_GPU, "spike: a retrace under 1.5 periods is not missed");
        w.retraceMs = 30;
        w.gameMs = 6, w.listsMs = 3, w.submitMs = 1;
        check(spikeCause(w) == SPIKE_MAIN_ORBIT, "spike: a missed retrace, mostly the orbit camera");
        w.gameMs = 25;
        check(spikeCause(w) == SPIKE_MAIN_GAME, "spike: a missed retrace, mostly the game");
        w.listsMs = 26;
        check(spikeCause(w) == SPIKE_MAIN_LISTS, "spike: a missed retrace, mostly the display lists");
        w.queueFull = true;
        check(spikeCause(w) == SPIKE_GPU, "spike: ... with RT64's queue full and a slow picture: the GPU");
        w.gpuMs = 5;
        check(spikeCause(w) == SPIKE_RENDER, "spike: ... with RT64's queue full: the render thread");
        w.queueFull = false;
        w.gpuMs = 9;
        w.submitMs = 27;
        check(spikeCause(w) == SPIKE_MAIN_SUBMIT, "spike: a missed retrace, mostly RT64's submit");
        w.otherMs = 28;
        check(spikeCause(w) == SPIKE_MAIN_OTHER, "spike: a missed retrace, mostly the host's other work");
        bool named = true;
        for (int c = 0; c < SPIKE_CAUSES; c++) named = named && spikeCauseName(c)[0] != '?';
        check(named && spikeCauseName(SPIKE_CAUSES)[0] == '?', "spike: every cause has a name");
    }
    viewerTests(check);   /* the level viewer's parsing and camera (viewer_test.cpp) */
    editTests(check);     /* the level editor: records, change list, mod files (edit_test.cpp) */
    terrainTestsAll(check);   /* the terrain tools, mods with geometry and textures, the texture codec */
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
