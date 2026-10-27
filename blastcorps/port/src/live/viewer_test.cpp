/* bc_cameratest.exe's tests of the level viewer's parsing and camera math
 * (viewer_records.cpp), on a synthetic level image built here (nothing from
 * the ROM): the header, every section the viewer reads, in the byte order the
 * port's loader leaves them in. */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "viewer_records.h"

using namespace live::viewer;

namespace {

void (*g_check)(bool, const char *);

struct Img {
    std::vector<uint8_t> b;
    void le16(size_t at, int v) {
        grow(at + 2);
        b[at] = uint8_t(v), b[at + 1] = uint8_t(v >> 8);
    }
    void be16(size_t at, int v) {
        grow(at + 2);
        b[at] = uint8_t(v >> 8), b[at + 1] = uint8_t(v);
    }
    void le32(size_t at, uint32_t v) {
        grow(at + 4);
        for (int i = 0; i < 4; i++) b[at + i] = uint8_t(v >> (8 * i));
    }
    void u8(size_t at, int v) {
        grow(at + 1);
        b[at] = uint8_t(v);
    }
    void grow(size_t n) {
        if (b.size() < n) b.resize(n, 0);
    }
};

const Field *field(const Record &r, const char *name) {
    for (const Field &f : r.fields)
        if (f.name == name) return &f;
    return nullptr;
}

bool has(const Record &r, const char *name, const char *text) {
    const Field *f = field(r, name);
    return f != nullptr && f->value.find(text) != std::string::npos;
}

int countKind(const std::vector<Record> &v, int kind) {
    int n = 0;
    for (const Record &r : v) n += r.kind == kind;
    return n;
}

const Record *nth(const std::vector<Record> &v, int kind, int i) {
    for (const Record &r : v)
        if (r.kind == kind && r.index == i) return &r;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

/* a level image: header, then the sections in header order */
Img makeLevel() {
    Img m;
    m.grow(0xC8);
    /* grid sizes (u16, host order) */
    const int hdr[12] = {8, 4, 1000, 1000, 16, 8, 500, 500, 10, 6, 800, 800};
    for (int i = 0; i < 12; i++) m.le16(2 * i, hdr[i]);
    m.le32(0x18, uint32_t(-4));
    m.le32(0x1C, 1);
    size_t p = 0xC8 + 4 * 16;   /* (some segment-8 vertices) */
    auto at = [&](uint32_t slot) { m.le32(slot, uint32_t(p)); };
    /* 0x20 crates: 2 x {s16 x, y, z, type} */
    at(0x20);
    for (int i = 0; i < 2; i++) {
        m.le16(p, 100 + i), m.le16(p + 2, -5), m.le16(p + 4, 300), m.le16(p + 6, i + 1);
        p += 8;
    }
    at(0x24);   /* guard triangles: none */
    /* 0x28 comm points: 1 */
    at(0x28);
    m.le16(p, -1200), m.le16(p + 2, 40), m.le16(p + 4, 2500), m.le16(p + 6, 7);
    p += 8;
    at(0x2C);   /* texture records: none */
    at(0x30);   /* ground grid: 4 bytes */
    p += 4;
    /* 0x34 RDUs: 3 */
    at(0x34);
    for (int i = 0; i < 3; i++) {
        m.le16(p, 1500 + i * 10), m.le16(p + 2, 20), m.le16(p + 4, 2500);
        p += 6;
    }
    /* 0x38 TNT: 1 */
    at(0x38);
    m.le16(p, 10), m.le16(p + 2, 11), m.le16(p + 4, 12), m.u8(p + 6, 3), m.u8(p + 7, 5), m.le16(p + 8, -2),
        m.le16(p + 10, 9);
    p += 12;
    /* 0x3C blocks: 1 block, 1 slot with 2 triangles */
    at(0x3C);
    m.le16(p, 1);
    p += 2;
    m.le16(p, 200), m.le16(p + 2, 0), m.le16(p + 4, -300), m.le16(p + 6, 2);
    p += 8;
    m.le16(p, 1);
    p += 2;
    m.le16(p, 210), m.le16(p + 2, 1), m.le16(p + 4, -310), m.u8(p + 6, 4), m.u8(p + 7, 2), m.le16(p + 8, 1);
    p += 10 + 2 * 0x16;
    /* 0x40 camera zones: 1; 0x44 chopper zones: 1 (HEIGHT_ZONE_NONE) */
    at(0x40);
    m.le16(p, -100), m.le16(p + 2, -200), m.le16(p + 4, 300), m.le16(p + 6, 400), m.le16(p + 8, 55);
    p += 10;
    at(0x44);
    m.le16(p, 0), m.le16(p + 2, 0), m.le16(p + 4, 10), m.le16(p + 6, 10), m.le16(p + 8, 3000);
    p += 10;
    at(0x48);   /* visibility grid: 8 x 4 pairs */
    p += 8 * 4 * 4;
    at(0x4C);   /* the level box */
    m.le16(p, -4000), m.le16(p + 2, -3000), m.le16(p + 4, 4000), m.le16(p + 6, 3000);
    p += 8;
    /* 0x50 vehicle spawns (BE): driver, Ramdozer, then type 40 (stops the list) */
    at(0x50);
    const int veh[3][5] = {{0, 100, 5, -200, 0x400}, {4, 100, 5, -200, 0x800}, {40, 1, 2, 3, 4}};
    for (const auto &v : veh) {
        m.u8(p, v[0]);
        m.be16(p + 1, v[1]), m.be16(p + 3, v[2]), m.be16(p + 5, v[3]), m.be16(p + 7, v[4]);
        p += 9;
    }
    /* 0x54 carrier (BE): speed 28, x 1980, z 2018, heading 0, length 3000 */
    at(0x54);
    m.u8(p, 28), m.be16(p + 1, 1980), m.be16(p + 3, 2018), m.be16(p + 5, 0), m.be16(p + 7, 3000);
    p += 9;
    /* 0x58 panel events (host order: the loader swaps the s16): 1 */
    at(0x58);
    m.le16(p, 2500), m.u8(p + 2, 1), m.u8(p + 3, 2), m.u8(p + 4, 3), m.u8(p + 5, 4);
    p += 8;
    /* 0x5C placements: 2 (BE but the u16 at 0xA, 0xC) */
    at(0x5C);
    m.be16(p, 1234), m.be16(p + 2, 56), m.be16(p + 4, 4321), m.be16(p + 6, 0x38), m.u8(p + 8, 1), m.u8(p + 9, 3),
        m.le16(p + 0xA, 3), m.le16(p + 0xC, 2);
    p += 14;
    m.be16(p, 10), m.be16(p + 2, 20), m.be16(p + 4, 30), m.be16(p + 6, 0x12), m.le16(p + 0xA, 0), m.le16(p + 0xC, 0);
    p += 14;
    for (uint32_t s = 0x60; s <= 0xC4; s += 4) at(s);
    m.grow(p + 16);
    return m;
}

}  // namespace

void viewerTests(void (*check)(bool, const char *)) {
    g_check = check;
    const Img m = makeLevel();
    LevelHeader h;
    std::string why;
    check(parseHeader(m.b.data(), m.b.size(), h, &why), "viewer: a synthetic level's header parses");
    check(h.cols == 8 && h.rows == 4 && h.cellW == 1000 && h.groundCols == 16 && h.collRows == 6 && h.gravity == -4 &&
              h.dlProfile == 1,
          "viewer: header grid sizes, gravity and DL profile in host order");
    check(h.minX == -4000 && h.minZ == -3000 && h.maxX == 4000 && h.maxZ == 3000, "viewer: the level box (0x4C)");
    check(!parseHeader(m.b.data(), 0x40, h, &why) && !why.empty(), "viewer: a short image is no level (with a reason)");
    {
        Img bad = m;
        bad.le32(0x50, 0x7FFFFFF0);
        check(!parseHeader(bad.b.data(), bad.b.size(), h, &why), "viewer: a section offset outside the file is refused");
    }
    std::vector<std::string> notes;
    const std::vector<Record> r = parseLevel(m.b.data(), m.b.size(), &notes);
    check(countKind(r, REC_CRATE) == 2 && countKind(r, REC_COMMPT) == 1 && countKind(r, REC_RDU) == 3 &&
              countKind(r, REC_TNT) == 1 && countKind(r, REC_BLOCK) == 1 && countKind(r, REC_BLOCKSLOT) == 1 &&
              countKind(r, REC_CAMZONE) == 1 && countKind(r, REC_CHOPPERZONE) == 1 && countKind(r, REC_PANEL) == 1 &&
              countKind(r, REC_OBJECT) == 2 && countKind(r, REC_CARRIER) == 1,
          "viewer: every section's records are found");
    check(countKind(r, REC_VEHICLE) == 2, "viewer: the vehicle list stops at an unknown type (as the game's)");
    bool noted = false;
    for (const std::string &s : notes) noted |= s.find("type 40") != std::string::npos;
    check(noted, "viewer: ... and says so");
    if (const Record *v = nth(r, REC_VEHICLE, 1)) {
        check(v->type == 4 && near(v->x, 100) && near(v->y, 5) && near(v->z, -200) && near(v->heading, 180.0),
              "viewer: vehicle spawn: BE type, position and heading (0x800 = 180 degrees)");
        check(std::string(v->model) == "bulldozer" && v->title.find("Ramdozer") != std::string::npos,
              "viewer: vehicle spawn: the type's name and model file (files.md)");
        check(v->raw.size() == 9 && v->raw[0] == 4 && v->offset > 0xC8, "viewer: vehicle spawn: its raw bytes");
    } else {
        check(false, "viewer: vehicle spawn 1 exists");
    }
    if (const Record *v = nth(r, REC_VEHICLE, 0)) check(has(*v, "note", "driver"), "viewer: the driver record is named");
    if (const Record *c = nth(r, REC_CARRIER, 0)) {
        check(c->type == 28 && near(c->x, 1980) && near(c->z, 2018) && !c->yKnown, "viewer: the carrier record (BE)");
        check(has(*c, "route length", "3000 map units") && has(*c, "route end", "z 5018"),
              "viewer: the carrier's route: its length along the heading (0 = +z)");
    } else {
        check(false, "viewer: the carrier record exists");
    }
    if (const Record *o = nth(r, REC_OBJECT, 0)) {
        check(o->type == 0x38 && near(o->x, 1234) && near(o->y, 56) && near(o->z, 4321),
              "viewer: placement: BE position and type");
        check(has(*o, "object type", "end object") && has(*o, "path building", "yes") &&
                  has(*o, "people inside", "3") && has(*o, "motion", "spline path") && has(*o, "tag", "2"),
              "viewer: placement: path flag, people, motion and tag (host-order u16 at 0xA, 0xC)");
    } else {
        check(false, "viewer: placement 0 exists");
    }
    if (const Record *t = nth(r, REC_TNT, 0))
        check(t->type == 3 && has(*t, "timer", "300 frames") && has(*t, "@8 (unk1A)", "-2") && near(t->z, 12),
              "viewer: TNT: type, fuse and the two unknown s16");
    if (const Record *x = nth(r, REC_RDU, 2))
        check(near(x->x, 1520) && near(x->y, 20) && has(*x, "visibility cell", "17"),
              "viewer: RDU position and visibility cell (x / cellW + z / cellD * cols)");
    if (const Record *x = nth(r, REC_CRATE, 1)) check(x->type == 2 && near(x->x, 101), "viewer: crate type");
    if (const Record *x = nth(r, REC_COMMPT, 0))
        check(near(x->x, -1200) && has(*x, "w", "7"), "viewer: communication point (signed x)");
    if (const Record *x = nth(r, REC_BLOCKSLOT, 0))
        check(x->raw.size() == 10 + 2 * 0x16 && has(*x, "triangles", "2") && has(*x, "target", "gBoxTargets"),
              "viewer: block slot with its triangles and target");
    if (const Record *x = nth(r, REC_PANEL, 0))
        check(has(*x, "carrier z", "2500 map units") && has(*x, "hud_panel_start", "(1, 2, 3, 4)"),
              "viewer: panel event: the carrier z in host order (as dig_coach_update reads it)");
    if (const Record *x = nth(r, REC_CAMZONE, 0))
        check(x->hasBox && near(x->x0, -100) && near(x->z1, 400) && near(x->y, 55), "viewer: camera height zone box");
    if (const Record *x = nth(r, REC_CHOPPERZONE, 0))
        check(!x->yKnown && has(*x, "height", "HEIGHT_ZONE_NONE"), "viewer: HEIGHT_ZONE_NONE");
    {
        Img cut = m;
        cut.le32(0x60, uint32_t(cut.b.size() + 100));   /* the placements' end past the file */
        std::vector<std::string> n2;
        const std::vector<Record> r2 = parseLevel(cut.b.data(), cut.b.size(), &n2);
        check(countKind(r2, REC_OBJECT) == 0 && !n2.empty(), "viewer: a section past the file is skipped, with a note");
    }

    /* runtime records */
    {
        uint8_t o[0xFC] = {};
        const int32_t w[] = {int32_t(0x80123450), int32_t(0x80200000), int32_t(0x80200000 + 0x60 * 3), 32 * 40,
                             3200, -640, 6400};
        memcpy(o, w, sizeof w);
        const int32_t kind = 0x38;
        memcpy(o + 0x30, &kind, 4);
        o[0xE8] = 9, o[0xE9] = 2, o[0xEC] = 100, o[0xED] = 50;
        const Record g = gameObjectRecord(o, 4, 0x803F4030u + 4 * 0xFC);
        check(g.kind == REC_GOBJECT && near(g.x, 100) && near(g.y, -20) && near(g.z, 200) && near(g.radius, 40) &&
                  g.type == 0x38,
              "viewer: gObjects record: position / 32, radius, kind");
        check(has(g, "collision triangles", "(3)") && has(g, "cell", "9") && has(g, "part percent (100 = down)", "100 50"),
              "viewer: gObjects record: triangles, cell, part percentages");
        check(g.raw.size() == 0xFC, "viewer: gObjects record: raw bytes");
    }
    {
        uint8_t v[0x74] = {};
        const int32_t type = 10;
        memcpy(v + 0x5C, &type, 4);
        const Record g = gameVehicleRecord(v, 2, 0x80364460u + 2 * 0x74);
        check(g.type == 10 && g.model == "bike" && g.title.find("Ballista") != std::string::npos,
              "viewer: gVehicles record: type, name, model file");
    }
    check(std::string(vehicleName(0xFF)) == "Missile carrier" && std::string(vehicleModelFile(0xFF)) == "cmo" &&
              std::string(vehicleModelFile(16)) == "minimagoo",
          "viewer: special vehicles' names and files");
    {
        const std::string d = hexDump({0, 1, 2, 0xAB, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
        check(d.find("000: 00 01 02 AB") == 0 && d.find("010: 10") != std::string::npos, "viewer: hex dump lines");
    }

    /* the free-fly camera */
    {
        FlyCam c;
        c.pos = {10, 20, 30};
        c.yaw = 0, c.pitch = 0;
        double m4[4][4];
        c.view(m4);
        /* the eye maps to the origin, a point ahead to -z */
        auto xf = [&](const Vec &p, double out[3]) {
            for (int j = 0; j < 3; j++) out[j] = p.x * m4[0][j] + p.y * m4[1][j] + p.z * m4[2][j] + m4[3][j];
        };
        double e[3], a[3];
        xf(c.pos, e);
        xf({10, 20, 130}, a);
        check(near(e[0], 0) && near(e[1], 0) && near(e[2], 0), "viewer camera: the eye is the view's origin");
        check(near(a[0], 0) && near(a[1], 0) && near(a[2], -100), "viewer camera: yaw 0 looks along +z (view -z)");
        c.yaw = 90;
        const Vec f = c.forward();
        check(near(f.x, 1) && near(f.z, 0, 1e-9), "viewer camera: yaw 90 looks along +x");
        c.move(1, 0, 0, 5);
        check(near(c.pos.x, 15) && near(c.pos.z, 30), "viewer camera: moving forward");
        c.yaw = 0;
        c.move(0, 1, 0, 2);
        double r3[3];
        c.view(m4);
        xf({c.pos.x + c.right().x * 7, c.pos.y, c.pos.z + c.right().z * 7}, r3);
        check(near(r3[0], 7) && near(r3[2], 0), "viewer camera: right() is the view's +x");
        c.move(0, 0, 1, 3);
        check(near(c.pos.y, 23), "viewer camera: up");
        c.turn(-450, 200);
        check(near(c.yaw, 270) && near(c.pitch, 89), "viewer camera: yaw wraps, pitch held at 89");
        c.yaw = 45, c.pitch = -30;
        c.lookAt({0, 0, 0}, 100);
        const Vec ff = c.forward();
        check(near(c.pos.x + ff.x * 100, 0, 1e-9) && near(c.pos.y + ff.y * 100, 0, 1e-9),
              "viewer camera: lookAt puts the point ahead at the distance");
    }
    {
        double dx, dz;
        headingDir(90, dx, dz);
        check(near(dx, 1) && near(dz, 0, 1e-12) && near(headingDegrees(0x400), 90) && near(headingDegrees(0x1C00), 270),
              "viewer: headings (0x400 = 90 degrees = +x)");
    }
    {
        /* guPerspective(fovy 45, aspect 4/3, near 10, far 10000), row vectors */
        const double fov = 45.0 * 3.14159265358979 / 180.0, f = 1.0 / std::tan(fov / 2), n = 10, fa = 10000;
        double p[4][4] = {{f / (4.0 / 3.0), 0, 0, 0}, {0, f, 0, 0}, {0, 0, (n + fa) / (n - fa), -1},
                          {0, 0, 2 * n * fa / (n - fa), 0}};
        double v[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        double sx, sy, depth;
        check(project(v, p, {0, 0, -100}, 0, 0, 400, 300, sx, sy, &depth) && near(sx, 200) && near(sy, 150) &&
                  near(depth, 100),
              "viewer: a point straight ahead projects to the picture's centre");
        check(!project(v, p, {0, 0, 100}, 0, 0, 400, 300, sx, sy), "viewer: a point behind the camera doesn't project");
        project(v, p, {0, 100 * std::tan(fov / 2), -100}, 0, 0, 400, 300, sx, sy);
        check(near(sy, 0, 1e-6), "viewer: the top of the view is the picture's top");
        check(setFar(p, 200000), "viewer: setFar on a perspective");
        const double A = p[2][2], B = p[3][2];
        check(near(B / (A - 1), 10, 1e-6) && near(B / (A + 1), 200000, 1e-3), "viewer: setFar keeps near, moves far");
        double o[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        check(!setFar(o, 1000), "viewer: setFar leaves an orthographic matrix");
        double rx, ry, rw, rh;
        pictureRect(1920, 1080, 4.0 / 3.0, rx, ry, rw, rh);
        check(near(rw, 1440) && near(rh, 1080) && near(rx, 240) && near(ry, 0), "viewer: 4:3 pillarboxed in 16:9");
        pictureRect(800, 900, 4.0 / 3.0, rx, ry, rw, rh);
        check(near(rw, 800) && near(rh, 600) && near(ry, 150), "viewer: 4:3 letterboxed in a tall window");
    }
}
