/* The free orbit camera's math (see orbit_math.h). */
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "orbit_math.h"

namespace live {
namespace orbit {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kShort = 0.97;   /* smooth collision: pulled in to this share of the free part */
}  // namespace

Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3 &a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3 &a, const Vec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(const Vec3 &a) { return std::sqrt(dot(a, a)); }
Vec3 normalize(const Vec3 &a) {
    const double l = length(a);
    return l > 1e-12 ? a * (1.0 / l) : Vec3{0, 0, 1};
}
Vec3 lerp(const Vec3 &a, const Vec3 &b, double t) { return a + (b - a) * t; }

double wrap180(double d) {
    d = std::fmod(d, 360.0);
    if (d <= -180.0) d += 360.0;
    if (d > 180.0) d -= 360.0;
    return d;
}

double wrap360(double d) {
    d = std::fmod(d, 360.0);
    if (d < 0) d += 360.0;
    if (d >= 360.0) d -= 360.0;
    return d;
}

Vec3 direction(double yawDeg, double pitchDeg) {
    const double cp = std::cos(pitchDeg * kDeg);
    return {std::sin(yawDeg * kDeg) * cp, std::sin(pitchDeg * kDeg), std::cos(yawDeg * kDeg) * cp};
}

void angles(const Vec3 &d, double &yawDeg, double &pitchDeg) {
    const double h = std::sqrt(d.x * d.x + d.z * d.z);
    yawDeg = h > 1e-12 ? wrap360(std::atan2(d.x, d.z) / kDeg) : 0.0;
    pitchDeg = std::atan2(d.y, h) / kDeg;
}

double clearShare(const Vec3 &target, const Vec3 &dir, double dist, double clearance, const HeightFn &height,
                  double from) {
    if (!height || dist <= 0) return 1.0;
    /* march out from the target; the first blocked sample ends the free part,
     * refined by bisection between it and the last free one */
    const int n = std::max(8, std::min(64, int(dist / 4.0)));
    auto blocked = [&](double s) {
        const Vec3 p = target + dir * (dist * s);
        const double h = height(p.x, p.y, p.z);
        return h > -1e30 && p.y < h + clearance;
    };
    double lastFree = from;
    for (int i = 1; i <= n; i++) {
        const double s = from + (1.0 - from) * double(i) / n;
        if (blocked(s)) {
            double lo = lastFree, hi = s;
            for (int k = 0; k < 10; k++) {
                const double mid = 0.5 * (lo + hi);
                if (blocked(mid)) hi = mid;
                else lo = mid;
            }
            return lo;
        }
        lastFree = s;
    }
    return 1.0;
}

const char *collisionName(int mode) {
    return mode == COLLISION_OFF ? "off" : mode == COLLISION_STRICT ? "strict" : "smooth";
}

void Spring::step(double &x, double &v, double target, double smoothTime, double dt) {
    /* the exact-ish critically damped step (a Pade approximation of exp(-w dt);
     * Game Programming Gems 4, 1.10), clamped so it never passes the target
     * coming from rest */
    if (!(dt > 0)) return;
    const double w = 2.0 / std::max(1e-4, smoothTime);
    const double k = w * dt;
    const double e = 1.0 / (1.0 + k + 0.48 * k * k + 0.235 * k * k * k);
    const double change = x - target;
    const double temp = (v + w * change) * dt;
    v = (v - w * temp) * e;
    double nx = target + (change + temp) * e;
    if ((change > 0) == (nx < target) && change != 0.0) {   /* passed it: stop there */
        nx = target;
        v = 0;
    }
    x = nx;
}

void Orbit::reset(bool zoomToo) {
    blend_ = 0.0;
    recentring_ = false;
    idle_ = 1e9;
    ratio_ = 1.0;
    lift_ = 0.0;
    rise_ = 0.0;
    ratioV_ = riseV_ = liftV_ = 0.0;
    ratioT_ = 1.0;
    riseT_ = 0.0;
    pullIn_ = letOut_ = riseUp_ = riseDown_ = 0.0;
    inside_ = false;
    haveEye_ = false;
    if (zoomToo) zoom_ = 1.0;
}

void Orbit::step(const Game &g, const Input &in, double dt, const Limits &lim, const HeightFn &height,
                 const HeightFn &terrain) {
    dt = std::max(0.0, std::min(dt, 0.25));
    const Vec3 off = g.eye - g.target;
    const double gameDist = length(off);
    double gy = 0, gp = 0;
    angles(off, gy, gp);
    if (in.reset) reset(true);

    /* turning: from the game's direction if not engaged yet (follow: offsets from it, 0 = the game's) */
    const bool moved = in.yaw != 0.0 || in.pitch != 0.0;
    const double baseYaw = lim.follow ? gy : 0.0, basePitch = lim.follow ? gp : 0.0;
    if (moved) {
        if (blend_ <= 0.0) {
            yaw_ = gy - baseYaw;
            pitch_ = gp - basePitch;
        }
        blend_ = 1.0;
        recentring_ = false;
        yaw_ = lim.follow ? wrap180(yaw_ + in.yaw) : wrap360(yaw_ + in.yaw);
        pitch_ += in.pitch;
        idle_ = 0.0;
    } else {
        idle_ += dt;
    }
    if (blend_ > 0.0) {
        /* the pitch limits (on the view's absolute pitch); widened to include the game's own pitch (so easing
         * back to it is never stopped by them).  Following, the kept offset is only limited where the player
         * moves it: the game camera's pitch changing later doesn't change it (the view is limited as it is
         * made: viewAngles) */
        const double lo = std::min(lim.pitchMin, gp), hi = std::max(lim.pitchMax, gp);
        if (moved) pitch_ = std::max(lim.pitchMin, std::min(lim.pitchMax, pitch_ + basePitch)) - basePitch;
        if (!lim.follow) pitch_ = std::max(lo, std::min(hi, pitch_));
        if (lim.recentre && idle_ >= lim.recentreDelay) recentring_ = true;
        if (recentring_) {
            const double k = 1.0 - std::exp(-dt / std::max(0.01, lim.recentreTime));
            const double toYaw = gy - baseYaw, toPitch = gp - basePitch;
            yaw_ = yaw_ + wrap180(toYaw - yaw_) * k;
            yaw_ = lim.follow ? wrap180(yaw_) : wrap360(yaw_);
            pitch_ += (toPitch - pitch_) * k;
            /* close enough: hand over to the game's camera itself (blend to 0) */
            if (std::fabs(wrap180(toYaw - yaw_)) < 2.0 && std::fabs(toPitch - pitch_) < 2.0) {
                blend_ -= dt / 0.25;
                if (blend_ <= 0.0) {
                    blend_ = 0.0;
                    recentring_ = false;
                }
            }
        }
    }

    /* zoom (log scale) */
    if (in.zoom != 0.0) zoom_ *= std::pow(2.0, in.zoom);
    zoom_ = std::max(lim.zoomMin, std::min(lim.zoomMax, zoom_));

    /* collision: only where the camera differs from the game's (the game's own
     * camera is never moved: zoom 1, not engaged) */
    const bool collide = gameDist > 1e-6 && (blend_ > 0.0 || zoom_ > 1.0 + 1e-9);
    Vec3 dir0 = normalize(off);
    if (blend_ > 0.0) {
        double vy, vp;
        viewAngles(gy, gp, lim, vy, vp);
        dir0 = normalize(lerp(dir0, direction(vy, vp), blend_));
    }
    const double dist = gameDist * zoom_;
    if (lim.collision == COLLISION_STRICT) stepStrict(g, dir0, dist, collide, dt, lim, height);
    else if (lim.collision == COLLISION_OFF) stepOff(g, dir0, dist, collide, dt, lim, height, terrain);
    else stepSmooth(g, dir0, dist, collide, dt, lim, height);
}

/* the eye at the share `ratio` of `dist` along `dir`, lifted by `lift`, inside an obstacle (below its top
 * plus the clearance) */
static bool insideAt(const Vec3 &target, const Vec3 &dir, double dist, double ratio, double lift, double clearance,
                     const HeightFn &height) {
    if (!height) return false;
    Vec3 eye = target + dir * (dist * ratio);
    eye.y += lift;
    const double h = height(eye.x, eye.y, eye.z);
    return h > -1e30 && eye.y < h + clearance - 1e-6;
}

void Orbit::stepOff(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                    const HeightFn &height, const HeightFn &terrain) {
    /* nothing moves it for obstacles: back out (if it was pulled in under another setting), kept above the
     * terrain only */
    ratioT_ = 1.0, riseT_ = 0.0;
    pullIn_ = letOut_ = riseUp_ = riseDown_ = 0.0;
    Spring::step(ratio_, ratioV_, 1.0, lim.springOut, dt);
    Spring::step(rise_, riseV_, 0.0, lim.springOut, dt);
    double wantLift = 0.0, floorLift = 0.0;
    Vec3 dir = dir0;
    if (rise_ > 1e-6) {
        double y0, p0;
        angles(dir0, y0, p0);
        dir = direction(y0, std::min(89.0, p0 + rise_));
    }
    const Vec3 eye = g.target + dir * (dist * ratio_);
    if (collide && terrain) {
        /* the clearance above the highest terrain around the eye (a ring of offRing units: rising ground is
         * seen coming, so the eye goes up on the spring before it gets there, not in one step), and never
         * below the terrain right under it (floorClear) */
        const double qy = eye.y;
        const double h = terrain(eye.x, qy, eye.z);
        double around = h;
        const double r = std::max(2.0, std::min(12.0, lim.offRing * dist));
        for (int k = 0; k < 8; k++) {
            const double a = k * (kPi / 4.0);
            around = std::max(around, terrain(eye.x + r * std::cos(a), qy, eye.z + r * std::sin(a)));
        }
        /* and where the eye is going (its last step's move, 3 .. 18 steps on: a fast turn or drive sees the
         * ground ahead in time for the spring) */
        if (haveEye_) {
            const Vec3 v = eye - lastEye_;
            if (length(v) < dist)   /* (not a cut) */
                for (int k : {3, 6, 12, 18}) {
                    const Vec3 p = eye + v * double(k);
                    around = std::max(around, terrain(p.x, std::max(p.y, eye.y), p.z));
                }
        }
        if (around > -1e30) wantLift = std::max(0.0, around + lim.clearance - eye.y);
        if (h > -1e30) floorLift = std::max(0.0, h + lim.floorClear - eye.y);
    }
    if (wantLift > lift_) Spring::step(lift_, liftV_, wantLift, lim.springIn, dt);
    else Spring::step(lift_, liftV_, wantLift, lim.springOut, dt);
    if (lift_ < floorLift) {   /* (the spring too slow for it: at the terrain itself at once) */
        lift_ = floorLift;
        liftV_ = std::max(0.0, liftV_);
        floorHits_++;
    }
    lastEye_ = eye, haveEye_ = true;
    /* (only counted: the eye inside a building, nothing moves it) */
    Vec3 e = eye;
    e.y += lift_;
    const double hb = collide && height ? height(e.x, e.y, e.z) : -1e31;
    inside_ = hb > -1e30 && e.y < hb - 1e-6;
}

void Orbit::stepSmooth(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                       const HeightFn &height) {
    if (!collide || !height) {
        ratioT_ = 1.0, riseT_ = 0.0;
        pullIn_ = letOut_ = riseUp_ = riseDown_ = 0.0;
        Spring::step(ratio_, ratioV_, 1.0, lim.springOut, dt);
        Spring::step(rise_, riseV_, 0.0, lim.springOut, dt);
        Spring::step(lift_, liftV_, 0.0, lim.springOut, dt);
        inside_ = false;
        return;
    }
    double y0, p0;
    angles(dir0, y0, p0);
    const double top = std::max(lim.pitchMax, p0);
    auto dirAt = [&](double r) { return direction(y0, std::min(89.0, p0 + r)); };
    const double minShare = std::max(lim.minShare, std::min(1.0, lim.minDistance / dist));
    /* the view along d: the median of the line of sight and a ray each side (to points beside the eye) */
    auto robust = [&](const Vec3 &d, double *centreOut) {
        const double c = clearShare(g.target, d, dist, lim.clearance, height, lim.nearShare);
        if (centreOut) *centreOut = c;
        Vec3 side = cross(d, Vec3{0, 1, 0});
        if (length(side) < 1e-6) side = {1, 0, 0};
        side = normalize(side);
        const double offs = std::max(1.0, std::min(6.0, lim.rayOffset * dist));
        double s[3] = {c, 0, 0};
        for (int k = 0; k < 2; k++) {
            const Vec3 end = g.target + d * dist + side * (k ? offs : -offs);
            const double len = length(end - g.target);
            s[k + 1] = clearShare(g.target, normalize(end - g.target), len, lim.clearance, height, lim.nearShare);
        }
        std::sort(s, s + 3);
        /* (the median ignores a thin obstruction on the line of sight; a clear line of sight between two
         * obstacles beside it is clear) */
        return std::max(c, s[1]);
    };
    /* the eye now (last step's spring positions) */
    inside_ = insideAt(g.target, dirAt(rise_), dist, ratio_, lift_, lim.clearance, height);

    /* the rise: up to the first steeper view that sees past the obstruction (else the one that sees farthest,
     * if clearly farther) once it has lasted holdIn; down to the lowest that sees past it with the margin once
     * that has lasted holdOut */
    const double atTarget = robust(dirAt(riseT_), nullptr);
    double up = riseT_;
    if (atTarget < lim.riseBelow) {
        double best = atTarget + lim.riseGain;
        for (double r = riseT_ + lim.riseStep; p0 + r <= top + 1e-9 && r <= lim.riseMax + 1e-9; r += lim.riseStep) {
            const double s = robust(dirAt(r), nullptr);
            if (s >= lim.riseBelow) {
                up = r;
                break;
            }
            if (s > best) best = s, up = r;
        }
        /* nothing clearly better and the camera would end up pulled in close to the target (into the
         * vehicle): as strict, the rise along the line of sight alone that sees farthest */
        if (up <= riseT_ + 1e-9 && atTarget < lim.riseSnapBelow) {
            double bestC = clearShare(g.target, dirAt(riseT_), dist, lim.clearance, height, lim.nearShare) + lim.riseGain;
            for (double r = riseT_ + lim.riseStep; p0 + r <= top + 1e-9 && r <= lim.riseMax + 1e-9; r += lim.riseStep) {
                const double s = clearShare(g.target, dirAt(r), dist, lim.clearance, height, lim.nearShare);
                if (s >= lim.riseBelow) {
                    up = r;
                    break;
                }
                if (s > bestC) bestC = s, up = r;
            }
        }
    }
    if (up > riseT_ + 1e-9) {
        riseUp_ += dt;
        if (riseUp_ >= lim.holdIn - 1e-9 || inside_) riseT_ = up, riseUp_ = 0.0;
    } else {
        riseUp_ = 0.0;
    }
    double down = riseT_;
    if (riseT_ > 1e-9 && up <= riseT_ + 1e-9) {
        for (double r = 0.0; r < riseT_ - 1e-9; r += lim.riseStep) {
            const double s = robust(dirAt(r), nullptr);
            if (s >= std::min(1.0, lim.riseBelow + lim.hysteresis)) {
                down = r;
                break;
            }
        }
    }
    if (down < riseT_ - 1e-9) {
        riseDown_ += dt;
        if (riseDown_ >= lim.holdOut - 1e-9) riseT_ = down, riseDown_ = 0.0;
    } else {
        riseDown_ = 0.0;
    }
    Spring::step(rise_, riseV_, riseT_, riseT_ > rise_ ? (inside_ ? lim.springEye : lim.springIn) : lim.springOut, dt);

    /* the distance along the risen view: pulled in to what the rays see once an obstruction has lasted holdIn,
     * let out (with the margin) once the view has been clear for holdOut; the eye inside an obstacle: to the
     * line of sight's free part at once (on the fast spring) */
    /* (a little short of the obstacle: the springs then get the eye past its edge in a few steps, and the near
     * plane doesn't cut into it) */
    /* (measured along the view the rise is going to: a rise that sees past the obstruction needs no pull-in) */
    const Vec3 dir = dirAt(rise_);
    double centre = 1.0;
    double s = robust(dirAt(riseT_), &centre);
    /* blocked right at the target (the player pushing into a building, under a roof): pulling in wouldn't see
     * it either, only put the eye into the vehicle: the view stays as it is (occluded) */
    const bool buried = s <= lim.nearShare + 0.02;
    if (s < 1.0) s *= kShort;
    centre *= kShort;
    s = std::max(minShare, s);
    centre = std::max(minShare, centre);
    if (s < ratioT_ - 1e-6 && !buried) {
        pullIn_ += dt;
        if (pullIn_ >= lim.holdIn - 1e-9) ratioT_ = s, pullIn_ = 0.0;
    } else {
        pullIn_ = 0.0;
    }
    if (s > ratioT_ + 1e-6 && (s >= ratioT_ + lim.hysteresis || s >= 1.0 - 1e-9)) {
        letOut_ += dt;
        if (letOut_ >= lim.holdOut - 1e-9) ratioT_ = s, letOut_ = 0.0;
    } else {
        letOut_ = 0.0;
    }
    const bool insideNow = insideAt(g.target, dir, dist, ratio_, 0.0, lim.clearance, height);
    if (insideNow) {
        /* out of it the shortest way along the line: to the obstacle's near side (from the eye towards the
         * target, the first point outside), not to the first obstruction from the target */
        double c = minShare;
        const int n = 48;
        for (int i = 1; i <= n; i++) {
            const double r = ratio_ - (ratio_ - minShare) * double(i) / n;
            if (!insideAt(g.target, dir, dist, r, 0.0, lim.clearance, height)) {
                c = std::max(minShare, r - 0.02);
                break;
            }
        }
        ratioT_ = std::min(ratioT_, c);
    }
    Spring::step(ratio_, ratioV_, ratioT_, ratioT_ < ratio_ ? (insideNow ? lim.springEye : lim.springIn) : lim.springOut,
                 dt);
    ratio_ = std::max(0.0, std::min(1.0, ratio_));
    /* still below the ground where it is (pulled in as far as allowed): lifted (fast), settling slowly */
    double wantLift = 0.0;
    {
        const Vec3 eye = g.target + dir * (dist * ratio_);
        const double h = height(eye.x, eye.y, eye.z);
        if (h > -1e30) wantLift = std::max(0.0, h + lim.clearance - eye.y);
    }
    /* (under the ground: up at once, as strict - a few centimetres up is no zoom - and back down slowly) */
    if (wantLift > lift_) lift_ = wantLift, liftV_ = 0.0;
    else Spring::step(lift_, liftV_, wantLift, lim.springOut, dt);
    inside_ = insideAt(g.target, dir, dist, ratio_, lift_, lim.clearance, height);
}

void Orbit::stepStrict(const Game &g, const Vec3 &dir0, double dist, bool collide, double dt, const Limits &lim,
                       const HeightFn &height) {
    double want = 1.0, wantLift = 0.0, wantRise = 0.0;
    if (collide) {
        want = clearShare(g.target, dir0, dist, lim.clearance, height, lim.nearShare);
        /* blocked close to the target: rise over the obstacle (a steeper view
         * that sees past it) rather than pull in to it */
        if (want < lim.riseBelow && rise_ < 1e-9 + lim.riseMax) {
            double y0, p0;
            angles(dir0, y0, p0);
            /* the first rise that sees past it; else the one that sees farthest, if that is
             * clearly farther (rather than pull in to the target: into the vehicle) */
            double best = want + lim.riseGain, bestRise = 0;
            for (double r = lim.riseStep; p0 + r <= std::max(lim.pitchMax, p0) + 1e-9 && r <= lim.riseMax + 1e-9;
                 r += lim.riseStep) {
                const double s = clearShare(g.target, direction(y0, p0 + r), dist, lim.clearance, height, lim.nearShare);
                if (s >= lim.riseBelow) {
                    bestRise = r;
                    break;
                }
                if (s > best) best = s, bestRise = r;
            }
            wantRise = bestRise;
        }
    }
    if (wantRise > rise_) {
        /* easing up would meanwhile pull the eye in close to the target (into the vehicle): at once */
        double y0, p0;
        angles(dir0, y0, p0);
        if (clearShare(g.target, direction(y0, std::min(89.0, p0 + rise_)), dist, lim.clearance, height,
                       lim.nearShare) < lim.riseSnapBelow)
            rise_ = wantRise;
        else
            rise_ += (wantRise - rise_) * (1.0 - std::exp(-dt / std::max(0.001, lim.riseTime)));
    } else {
        rise_ += (wantRise - rise_) * (1.0 - std::exp(-dt / std::max(0.001, lim.letOutTime)));
    }
    Vec3 dir = dir0;
    if (rise_ > 1e-6) {
        double y0, p0;
        angles(dir0, y0, p0);
        dir = direction(y0, std::min(89.0, p0 + rise_));
    }
    if (collide) {
        want = clearShare(g.target, dir, dist, lim.clearance, height, lim.nearShare);
        const double minShare = std::max(lim.minShare, std::min(1.0, lim.minDistance / dist));
        want = std::max(want, minShare);
    }
    if (want < ratio_) {
        /* in at once (the eye must never be inside), unless it's only a little:
         * then smoothly, so small bumps don't jerk the view */
        if (ratio_ - want > 0.05) ratio_ = want;
        else ratio_ += (want - ratio_) * (1.0 - std::exp(-dt / std::max(0.001, lim.pullInTime)));
        ratio_ = std::max(want - 0.05, ratio_);
    } else {
        ratio_ += (want - ratio_) * (1.0 - std::exp(-dt / std::max(0.001, lim.letOutTime)));
    }
    ratio_ = std::max(0.0, std::min(1.0, ratio_));
    /* still below the ground where it is (pulled in as far as allowed): lift it */
    if (collide && height) {
        const Vec3 eye = g.target + dir * (dist * ratio_);
        const double h = height(eye.x, eye.y, eye.z);
        if (h > -1e30) wantLift = std::max(0.0, h + lim.clearance - eye.y);
    }
    if (wantLift > lift_) lift_ = wantLift;
    else lift_ += (wantLift - lift_) * (1.0 - std::exp(-dt / std::max(0.001, lim.liftTime)));
    inside_ = collide && insideAt(g.target, dir, dist, ratio_, lift_, lim.clearance, height);
}

bool CollisionStats::Turns::add(double v, double step) {
    if (dir == 0) {
        if (std::fabs(v - extreme) >= step) dir = v > extreme ? 1 : -1, extreme = v;
        return false;
    }
    if ((dir > 0 && v > extreme) || (dir < 0 && v < extreme)) {
        extreme = v;
        return false;
    }
    if (std::fabs(v - extreme) >= step) {
        dir = -dir;
        extreme = v;
        return true;
    }
    return false;
}

void CollisionStats::add(double ratio, double rise, bool eyeInside, double dt, double lift, double eyeRel) {
    steps++;
    seconds += dt;
    inside += eyeInside;
    pulled += ratio < 0.6;
    if (!have_) {
        have_ = true;
        ratio_ = Turns(), rise_ = Turns();
        ratio_.extreme = ratio, rise_.extreme = rise;
        lastRatio_ = ratio, lastRise_ = rise, lastLift_ = lift, lastEye_ = eyeRel;
        return;
    }
    {
        /* (what the lift moved the picture's eye up: a lift growing while the eye stays put - the view tilted
         * or zoomed into the ground, held at it - shows no jump) */
        const double up = std::min(lift - lastLift_, eyeRel - lastEye_);
        maxEyeStep = std::max(maxEyeStep, up);
        /* (a snap: a rise over jumpStep out of a much slower step - a spring's rise grows over a few steps) */
        eyeJumps += up > jumpStep && up > 2.5 * std::max(0.0, lastUp_) + 1.0;
        lastUp_ = up;
    }
    lastEye_ = eyeRel;
    if (dt > 0) {
        const double dr = (ratio - lastRatio_) / dt, du = (rise - lastRise_) / dt;
        maxIn = std::max(maxIn, -dr), maxOut = std::max(maxOut, dr);
        maxUp = std::max(maxUp, du), maxDown = std::max(maxDown, -du);
        maxLiftUp = std::max(maxLiftUp, (lift - lastLift_) / dt);
        maxLiftStep = std::max(maxLiftStep, lift - lastLift_);
    }
    ratioTurns += ratio_.add(ratio, ratioStep);
    riseTurns += rise_.add(rise, riseStep);
    lastRatio_ = ratio, lastRise_ = rise, lastLift_ = lift;
}

void rotateStick(int x, int y, double deg, int &ox, int &oy) {
    /* clockwise by deg: (x, y) -> (x cos + y sin, -x sin + y cos) */
    const double a = deg * kDeg, c = std::cos(a), s = std::sin(a);
    const double rx = x * c + y * s, ry = -x * s + y * c;
    ox = std::max(-127, std::min(127, int(std::lround(rx))));
    oy = std::max(-127, std::min(127, int(std::lround(ry))));
}

double steerTurn(double gameYaw, double viewYaw) {
    /* a stick s read relative to a camera at yaw Y moves (x, z) = flipZ(rotate s counterclockwise by Y): the view
     * at Y + d is the game's camera with s turned counterclockwise by d, i.e. clockwise by -d */
    return wrap180(gameYaw - viewYaw);
}

void Orbit::eyeFor(const Game &g, double extraYaw, double extraPitch, const Limits &lim, Vec3 &eye) const {
    const Vec3 off = g.eye - g.target;
    const double gameDist = length(off);
    if (gameDist < 1e-6) {
        eye = g.eye;
        return;
    }
    Vec3 dir = off * (1.0 / gameDist);
    if (blend_ > 0.0) {
        double gy, gp, vy, vp;
        angles(off, gy, gp);
        viewAngles(gy, gp, lim, vy, vp);
        const double p = std::max(std::min(lim.pitchMin, vp), std::min(std::max(lim.pitchMax, vp), vp + extraPitch));
        dir = normalize(lerp(dir, direction(vy + extraYaw, p), blend_));
    }
    if (rise_ > 1e-6) {
        double y0, p0;
        angles(dir, y0, p0);
        dir = direction(y0, std::min(89.0, p0 + rise_));
    }
    eye = g.target + dir * (gameDist * zoom_ * ratio_);
    eye.y += lift_;
}

void Orbit::viewAngles(double gameYaw, double gamePitch, const Limits &lim, double &yaw, double &pitch) const {
    if (blend_ <= 0.0) {
        yaw = gameYaw, pitch = gamePitch;
        return;
    }
    if (!lim.follow) {
        yaw = yaw_, pitch = pitch_;
        return;
    }
    yaw = wrap360(gameYaw + yaw_);
    /* (the limits widened to include the game's own pitch, as step) */
    pitch = std::max(std::min(lim.pitchMin, gamePitch), std::min(std::max(lim.pitchMax, gamePitch), gamePitch + pitch_));
}

Orbit Orbit::between(const Orbit &a, const Orbit &b, double t) {
    if (!(t > 0.0)) return a;
    if (t >= 1.0) return b;
    Orbit r = b;
    double ya = a.yaw_, pa = a.pitch_, yb = b.yaw_, pb = b.pitch_;
    if (a.blend_ <= 0.0) ya = yb, pa = pb;
    if (b.blend_ <= 0.0) yb = ya, pb = pa;
    r.yaw_ = wrap360(ya + wrap180(yb - ya) * t);
    r.pitch_ = pa + (pb - pa) * t;
    r.blend_ = a.blend_ + (b.blend_ - a.blend_) * t;
    r.zoom_ = a.zoom_ > 0.0 && b.zoom_ > 0.0 ? a.zoom_ * std::pow(b.zoom_ / a.zoom_, t) : b.zoom_;
    r.ratio_ = a.ratio_ + (b.ratio_ - a.ratio_) * t;
    r.lift_ = a.lift_ + (b.lift_ - a.lift_) * t;
    r.rise_ = a.rise_ + (b.rise_ - a.rise_) * t;
    return r;
}

void lookAt(const Vec3 &eye, const Vec3 &target, double m[4][4]) {
    /* camera space: x right, y up, z backward (the eye looks along -z) */
    Vec3 back = normalize(eye - target);
    Vec3 up = {0, 1, 0};
    if (std::fabs(dot(back, up)) > 0.9999) up = {0, 0, 1};   /* straight down/up: any right vector */
    const Vec3 right = normalize(cross(up, back));
    const Vec3 up2 = cross(back, right);
    const Vec3 ax[3] = {right, up2, back};
    for (int c = 0; c < 3; c++) {
        m[0][c] = ax[c].x;
        m[1][c] = ax[c].y;
        m[2][c] = ax[c].z;
        m[3][c] = -dot(eye, ax[c]);
    }
    m[0][3] = m[1][3] = m[2][3] = 0.0;
    m[3][3] = 1.0;
}

bool viewEye(const double m[4][4], Vec3 &eye, Vec3 &forward) {
    for (int r = 0; r < 3; r++) {
        const double len = m[r][0] * m[r][0] + m[r][1] * m[r][1] + m[r][2] * m[r][2];
        if (std::fabs(len - 1.0) > 0.02) return false;
    }
    /* eye = -R^T t (R's columns are the axes) */
    eye.x = -(m[3][0] * m[0][0] + m[3][1] * m[0][1] + m[3][2] * m[0][2]);
    eye.y = -(m[3][0] * m[1][0] + m[3][1] * m[1][1] + m[3][2] * m[1][2]);
    eye.z = -(m[3][0] * m[2][0] + m[3][1] * m[2][1] + m[3][2] * m[2][2]);
    forward = {-m[0][2], -m[1][2], -m[2][2]};
    return true;
}

void mtxFromN64(const uint32_t w[16], double m[4][4]) {
    for (int n = 0; n < 16; n++) {
        const uint32_t iw = w[n / 2], fw = w[8 + n / 2];
        const int16_t hi = int16_t((n & 1) ? (iw & 0xFFFF) : (iw >> 16));
        const uint16_t lo = uint16_t((n & 1) ? (fw & 0xFFFF) : (fw >> 16));
        m[n / 4][n % 4] = double((int32_t(hi) * 65536) | lo) / 65536.0;
    }
}

bool triHeight(const Vec3 p[3], double x, double z, double &h) {
    const double minX = std::min({p[0].x, p[1].x, p[2].x}), maxX = std::max({p[0].x, p[1].x, p[2].x});
    const double minZ = std::min({p[0].z, p[1].z, p[2].z}), maxZ = std::max({p[0].z, p[1].z, p[2].z});
    if (x < minX || x > maxX || z < minZ || z > maxZ) return false;
    const Vec3 n = cross(p[1] - p[0], p[2] - p[0]);
    if (std::fabs(n.y) < 1e-9 * (std::fabs(n.x) + std::fabs(n.z) + 1.0)) return false;
    /* inside in x/z: the three edge signs agree */
    auto side = [&](const Vec3 &a, const Vec3 &b) { return (b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x); };
    const double s0 = side(p[0], p[1]), s1 = side(p[1], p[2]), s2 = side(p[2], p[0]);
    const bool neg = s0 < -1e-9 || s1 < -1e-9 || s2 < -1e-9, pos = s0 > 1e-9 || s1 > 1e-9 || s2 > 1e-9;
    if (neg && pos) return false;
    h = p[0].y - (n.x * (x - p[0].x) + n.z * (z - p[0].z)) / n.y;
    return true;
}

/* ---- TriGrid ---------------------------------------------------------------- */
namespace {
constexpr int kGridMax = 64;          /* bins a side at most */
constexpr int kBigBins = 256;         /* a triangle over more bins than this is tested by every query */
}  // namespace

void TriGrid::clear() {
    tris_.clear();
    start_.clear();
    index_.clear();
    big_.clear();
}

void TriGrid::add(const Vec3 p[3]) {
    Tri t;
    for (int i = 0; i < 3; i++) t.p[i] = p[i];
    t.minX = std::min({p[0].x, p[1].x, p[2].x});
    t.maxX = std::max({p[0].x, p[1].x, p[2].x});
    t.minZ = std::min({p[0].z, p[1].z, p[2].z});
    t.maxZ = std::max({p[0].z, p[1].z, p[2].z});
    tris_.push_back(t);
}

int TriGrid::col(double x) const {
    const double c = std::floor((x - x0_) / cw_);
    return c < 0 ? 0 : c >= nx_ ? nx_ - 1 : int(c);
}

int TriGrid::row(double z) const {
    const double r = std::floor((z - z0_) / cd_);
    return r < 0 ? 0 : r >= nz_ ? nz_ - 1 : int(r);
}

void TriGrid::build() {
    start_.clear();
    index_.clear();
    big_.clear();
    if (tris_.empty()) return;
    double x1 = tris_[0].maxX, z1 = tris_[0].maxZ;
    x0_ = tris_[0].minX;
    z0_ = tris_[0].minZ;
    for (const Tri &t : tris_) {
        x0_ = std::min(x0_, t.minX);
        z0_ = std::min(z0_, t.minZ);
        x1 = std::max(x1, t.maxX);
        z1 = std::max(z1, t.maxZ);
    }
    /* about as many bins as triangles, square-ish */
    const double w = std::max(1e-6, x1 - x0_), d = std::max(1e-6, z1 - z0_);
    const double side = std::sqrt(w * d / double(tris_.size()));
    nx_ = std::max(1, std::min(kGridMax, int(std::ceil(w / std::max(side, 1e-6)))));
    nz_ = std::max(1, std::min(kGridMax, int(std::ceil(d / std::max(side, 1e-6)))));
    cw_ = w / nx_;
    cd_ = d / nz_;
    /* counting pass, then the bins */
    std::vector<uint32_t> count(size_t(nx_) * nz_ + 1, 0);
    for (uint32_t i = 0; i < tris_.size(); i++) {
        const Tri &t = tris_[i];
        const int a = col(t.minX), b = col(t.maxX), c = row(t.minZ), e = row(t.maxZ);
        if ((b - a + 1) * (e - c + 1) > kBigBins) continue;
        for (int r = c; r <= e; r++)
            for (int q = a; q <= b; q++) count[size_t(r) * nx_ + q]++;
    }
    start_.assign(count.size(), 0);
    for (size_t b = 0; b + 1 < count.size(); b++) start_[b + 1] = start_[b] + count[b];
    index_.resize(start_.back());
    std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
    for (uint32_t i = 0; i < tris_.size(); i++) {
        const Tri &t = tris_[i];
        const int a = col(t.minX), b = col(t.maxX), c = row(t.minZ), e = row(t.maxZ);
        if ((b - a + 1) * (e - c + 1) > kBigBins) {
            big_.push_back(i);
            continue;
        }
        for (int r = c; r <= e; r++)
            for (int q = a; q <= b; q++) index_[fill[size_t(r) * nx_ + q]++] = i;
    }
}

/* ---- ViewVolume ---------------------------------------------------------------- */
ViewVolume::ViewVolume(const Vec3 &eye, const Vec3 &target, double tanX, double tanY, double marginDeg, double back,
                       double farDist) {
    const Vec3 fwd = normalize(target - eye);
    const Vec3 apex = eye - fwd * back;
    lookAt(apex, apex + fwd, m_);
    const double ax = std::atan(std::max(0.0, tanX)) / kDeg + marginDeg, ay = std::atan(std::max(0.0, tanY)) / kDeg + marginDeg;
    limX_ = ax < 89.0;
    limY_ = ay < 89.0;
    sx_ = limX_ ? std::tan(ax * kDeg) : 0.0;
    sy_ = limY_ ? std::tan(ay * kDeg) : 0.0;
    far_ = farDist > 0 ? farDist + back : 0.0;
}

bool ViewVolume::boxVisible(const Vec3 &lo, const Vec3 &hi) const {
    /* camera space: x right, y up, the view along -z (depth d = -z) */
    unsigned outR = 0, outL = 0, outT = 0, outB = 0, outBehind = 0, outFar = 0;
    for (int i = 0; i < 8; i++) {
        const double x = (i & 1) ? hi.x : lo.x, y = (i & 2) ? hi.y : lo.y, z = (i & 4) ? hi.z : lo.z;
        const double cx = x * m_[0][0] + y * m_[1][0] + z * m_[2][0] + m_[3][0];
        const double cy = x * m_[0][1] + y * m_[1][1] + z * m_[2][1] + m_[3][1];
        const double d = -(x * m_[0][2] + y * m_[1][2] + z * m_[2][2] + m_[3][2]);
        if (limX_) {
            outR += cx > sx_ * d;
            outL += -cx > sx_ * d;
        }
        if (limY_) {
            outT += cy > sy_ * d;
            outB += -cy > sy_ * d;
        }
        outBehind += d < 0.0;
        if (far_ > 0) outFar += d > far_;
    }
    return outR < 8 && outL < 8 && outT < 8 && outB < 8 && outBehind < 8 && outFar < 8;
}

bool ViewVolume::boxVisibleExact(const Vec3 &lo, const Vec3 &hi) const {
    /* the corners in camera space (x, y, depth) */
    double c[8][3];
    for (int i = 0; i < 8; i++) {
        const double x = (i & 1) ? hi.x : lo.x, y = (i & 2) ? hi.y : lo.y, z = (i & 4) ? hi.z : lo.z;
        c[i][0] = x * m_[0][0] + y * m_[1][0] + z * m_[2][0] + m_[3][0];
        c[i][1] = x * m_[0][1] + y * m_[1][1] + z * m_[2][1] + m_[3][1];
        c[i][2] = -(x * m_[0][2] + y * m_[1][2] + z * m_[2][2] + m_[3][2]);
    }
    /* the planes as "inside >= 0" functions of (x, y, d) */
    auto inside = [&](int plane, const double *p) {
        switch (plane) {
            case 0: return limX_ ? sx_ * p[2] - p[0] : 1.0;
            case 1: return limX_ ? sx_ * p[2] + p[0] : 1.0;
            case 2: return limY_ ? sy_ * p[2] - p[1] : 1.0;
            case 3: return limY_ ? sy_ * p[2] + p[1] : 1.0;
            case 4: return p[2] - 1e-6;   /* in front of the apex */
            default: return far_ > 0 ? far_ - p[2] : 1.0;
        }
    };
    static const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto &f : faces) {
        double poly[2][16][3];
        int n = 4, cur = 0;
        for (int k = 0; k < 4; k++)
            for (int j = 0; j < 3; j++) poly[0][k][j] = c[f[k]][j];
        for (int plane = 0; plane < 6 && n > 0; plane++, cur ^= 1) {
            int m = 0;
            for (int k = 0; k < n && m < 15; k++) {
                const double *a = poly[cur][k], *b = poly[cur][(k + 1) % n];
                const double da = inside(plane, a), db = inside(plane, b);
                if (da >= 0) {
                    for (int j = 0; j < 3; j++) poly[cur ^ 1][m][j] = a[j];
                    m++;
                }
                if ((da >= 0) != (db >= 0)) {
                    const double t = da / (da - db);
                    for (int j = 0; j < 3; j++) poly[cur ^ 1][m][j] = a[j] + (b[j] - a[j]) * t;
                    m++;
                }
            }
            n = m;
        }
        if (n > 0) return true;
    }
    /* no face reaches into the view: it is inside the box, or the box is out of it */
    const double ax = -m_[3][0] * m_[0][0] - m_[3][1] * m_[0][1] - m_[3][2] * m_[0][2];   /* the apex (world) */
    const double ay = -m_[3][0] * m_[1][0] - m_[3][1] * m_[1][1] - m_[3][2] * m_[1][2];
    const double az = -m_[3][0] * m_[2][0] - m_[3][1] * m_[2][1] - m_[3][2] * m_[2][2];
    const Vec3 p = {ax - m_[0][2], ay - m_[1][2], az - m_[2][2]};   /* one unit ahead on the axis */
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y && p.z >= lo.z && p.z <= hi.z;
}

bool perspectiveRange(const double p[4][4], double &nearZ, double &farZ) {
    /* guPerspective (times its scale): [2][2] = (f + n) / (n - f), [2][3] = -1,
     * [3][2] = 2 n f / (n - f), [3][3] = 0 */
    const double s = -p[2][3];
    if (!(s > 1e-9) || std::fabs(p[3][3]) > 1e-6 * s) return false;
    const double a = p[2][2] / s, b = p[3][2] / s;
    if (std::fabs(a - 1.0) < 1e-12 || std::fabs(a + 1.0) < 1e-12) return false;
    nearZ = b / (a - 1.0);
    farZ = b / (a + 1.0);
    return nearZ > 0 && farZ > nearZ;
}

Sweep sweepAt(double t, const Limits &lim) {
    const double pi2 = 2.0 * kPi;
    Sweep s;
    s.yawRate = 150.0 * std::sin(pi2 * t / 9.0) + 60.0 * std::sin(pi2 * t / 2.3);
    if (std::fmod(t, 13.0) >= 12.25) s.yawRate = 320.0;   /* a flick */
    const double lo = lim.pitchMin + 2.0, hi = lim.pitchMax - 5.0;
    s.pitch = hi > lo ? (lo + hi) * 0.5 + (hi - lo) * 0.5 * std::sin(pi2 * t / 7.0) : lim.pitchMin;
    const double zl = std::max(1e-3, lim.zoomMin), zh = std::max(zl, lim.zoomMax);
    s.zoom = zl * std::pow(zh / zl, 0.5 + 0.5 * std::sin(pi2 * t / 11.0));
    return s;
}

const char *spikeCauseName(int cause) {
    static const char *const kNames[SPIKE_CAUSES] = {
        "main thread: game", "main thread: display lists", "main thread: orbit camera", "main thread: RT64 submit",
        "main thread: other", "GPU", "RT64 render thread", "new shaders", "new textures", "present / other"};
    return cause >= 0 && cause < SPIKE_CAUSES ? kNames[cause] : "?";
}

int spikeCause(const SpikeWindow &w) {
    if (w.retraceMs > 1.5 * w.periodMs) {
        const double parts[5] = {w.gameMs, w.listsMs, w.orbitMs, w.submitMs, w.otherMs};
        int best = 0;
        for (int i = 1; i < 5; i++)
            if (parts[i] > parts[best]) best = i;
        if (best == 1 && w.queueFull)
            return w.targetMs > 0 && w.gpuMs > kSpikeGpuShare * w.targetMs ? SPIKE_GPU : SPIKE_RENDER;
        return SPIKE_MAIN_GAME + best;
    }
    if (w.targetMs > 0 && w.gpuMs > kSpikeGpuShare * w.targetMs) return SPIKE_GPU;
    if (w.frameMs > 0 && w.renderMs > w.frameMs) return SPIKE_RENDER;
    if (w.shaders > 0) return SPIKE_SHADERS;
    if (w.textures >= kSpikeTextures) return SPIKE_TEXTURES;
    return SPIKE_PRESENT;
}

}  // namespace orbit
}  // namespace live
