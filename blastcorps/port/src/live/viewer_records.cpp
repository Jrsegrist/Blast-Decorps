/* bc.exe's level viewer: the records it reads and the free-fly camera's
 * math (see viewer_records.h).  The layouts are docs/systems/levels.md's;
 * the byte order port/src/load/schemas.h's swap_level. */
#include "viewer_records.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace live {
namespace viewer {

namespace {

constexpr double kPi = 3.14159265358979323846;

/* host order (the port's swapped fields) */
uint16_t le16(const uint8_t *p) {
    return uint16_t(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
/* the byte streams the game reads bytewise (BE16U / BE16S of 5CB60.c) */
uint16_t be16(const uint8_t *p) {
    return uint16_t((p[0] << 8) | p[1]);
}
/* the fields the game reads natively: host order in the port's memory, big-endian in the file as the ROM holds
 * it (parseHeader / parseLevel's fileOrder) */
bool g_fileOrder = false;
uint16_t n16(const uint8_t *p) {
    return g_fileOrder ? be16(p) : le16(p);
}
uint32_t n32(const uint8_t *p) {
    return g_fileOrder ? uint32_t(be16(p)) << 16 | be16(p + 2) : le32(p);
}
struct OrderScope {
    bool old;
    explicit OrderScope(bool f) : old(g_fileOrder) { g_fileOrder = f; }
    ~OrderScope() { g_fileOrder = old; }
};

std::string fmt(const char *f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

void add(Record &r, const char *name, const std::string &value) {
    r.fields.push_back({name, value});
}

Record base(int kind, int index, const uint8_t *lvl, uint32_t off, uint32_t len) {
    Record r;
    r.kind = kind;
    r.index = index;
    r.offset = off;
    r.raw.assign(lvl + off, lvl + off + len);
    return r;
}

void posFields(Record &r) {
    add(r, "position", fmt("%.0f, %.0f, %.0f (drawing units = map units; world units %d, %d, %d)", r.x, r.y, r.z,
                           int(r.x) * 32, int(r.y) * 32, int(r.z) * 32));
}

/* a section [lo, hi) of the header's chain, checked */
bool section(const LevelHeader &h, size_t size, uint32_t slot, uint32_t endSlot, uint32_t &lo, uint32_t &hi,
             std::vector<std::string> *notes) {
    lo = h.offAt(slot);
    hi = h.offAt(endSlot);
    if (lo <= hi && hi <= size) return true;
    if (notes) notes->push_back(fmt("section 0x%X: bad range 0x%X..0x%X (file 0x%zX bytes)", slot, lo, hi, size));
    return false;
}

void endsInside(std::vector<std::string> *notes, uint32_t slot, uint32_t left, uint32_t recSize) {
    if (notes && left != 0)
        notes->push_back(fmt("section 0x%X: %u bytes left after the last %u-byte record", slot, left, recSize));
}

}  // namespace

const char *recKindName(int kind) {
    static const char *const n[REC_KINDS] = {"Vehicle spawn", "Carrier record", "Object placement", "RDU", "TNT crate",
                                             "Ammo crate", "Communication point", "Push block", "Block slot",
                                             "Panel event", "Camera height zone", "Chopper height zone",
                                             "Level object (runtime)", "Vehicle record (runtime)"};
    return kind >= 0 && kind < REC_KINDS ? n[kind] : "?";
}

const char *recKindKey(int kind) {
    static const char *const n[REC_KINDS] = {"vehicle", "carrier", "object", "rdu", "tnt", "crate", "commpt",
                                             "block", "slot", "panel", "camzone", "chopperzone", "gobject",
                                             "gvehicle"};
    return kind >= 0 && kind < REC_KINDS ? n[kind] : "?";
}

const char *vehicleName(int type) {
    switch (type) {
    case 0: return "Driver (on foot)";
    case 1: return "Sideswipe";
    case 2: return "Thunderfist";
    case 3: return "Skyfall";
    case 4: return "Ramdozer";
    case 5: return "Backlash";
    case 6: return "Crane";
    case 7: return "Train";
    case 8: return "American Dream";
    case 9: return "J-Bomb";
    case 10: return "Ballista";
    case 11: return "Barge (0x0B)";
    case 12: return "type 12 (no code)";
    case 13: return "Police car";
    case 14: return "A-Team van";
    case 15: return "Starski";
    case 16: return "Cyclone Suit";
    case 17: return "Barge (0x11)";
    case 18: return "Barge (0x12)";
    case 0xFD: return "Shuttle";
    case 0xFE: return "BCT chopper";
    case 0xFF: return "Missile carrier";
    default: return "unknown vehicle";
    }
}

const char *vehicleModelFile(int type) {
    switch (type) {
    case 0: return "driver";
    case 1: return "sideswipe";
    case 2: return "magoo";
    case 3: return "buggy";
    case 4: return "bulldozer";
    case 5: return "truck";
    case 6: return "crane";
    case 7: return "train";
    case 8: return "hotrod";
    case 9: return "jetpack";
    case 10: return "bike";
    case 11: case 17: case 18: return "barge";
    case 13: return "police";
    case 14: return "ateam";
    case 15: return "starski";
    case 16: return "minimagoo";
    case 0xFD: return "shuttle";
    case 0xFE: return "chopper";
    case 0xFF: return "cmo";
    case 0x96: return "commpoint";
    case 0x98: return "scientist";
    default: return "driver (the default)";
    }
}

bool parseHeader(const uint8_t *lvl, size_t size, LevelHeader &h, std::string *why, bool fileOrder) {
    OrderScope order(fileOrder);
    h = LevelHeader();
    if (lvl == nullptr || size < 0xC8) {
        if (why) *why = "no level file (or under 0xC8 bytes)";
        return false;
    }
    h.cols = n16(lvl + 0), h.rows = n16(lvl + 2), h.cellW = n16(lvl + 4), h.cellD = n16(lvl + 6);
    h.groundCols = n16(lvl + 8), h.groundRows = n16(lvl + 0xA), h.groundCellW = n16(lvl + 0xC),
    h.groundCellD = n16(lvl + 0xE);
    h.collCols = n16(lvl + 0x10), h.collRows = n16(lvl + 0x12), h.collCellW = n16(lvl + 0x14),
    h.collCellD = n16(lvl + 0x16);
    h.gravity = int32_t(n32(lvl + 0x18));
    h.dlProfile = int(n32(lvl + 0x1C) & 0xFF);
    for (int k = 0; k < 0x2A; k++) h.off[k] = n32(lvl + 0x20 + 4 * k);
    /* the data sections' offsets (0x20 .. 0x74) must lie in the file (each section's own range is checked
     * where it is read) */
    for (uint32_t s = 0x20; s <= 0x74; s += 4) {
        if (h.offAt(s) < 0xC8 || h.offAt(s) > size) {
            if (why) *why = fmt("header offset 0x%X = 0x%X is not a section start (file 0x%zX bytes)", s, h.offAt(s), size);
            return false;
        }
    }
    const uint32_t box = h.offAt(0x4C);
    if (box + 8 > size) {
        if (why) *why = "the level box (0x4C) is outside the file";
        return false;
    }
    h.minX = int16_t(n16(lvl + box)), h.minZ = int16_t(n16(lvl + box + 2));
    h.maxX = int16_t(n16(lvl + box + 4)), h.maxZ = int16_t(n16(lvl + box + 6));
    if (h.cols == 0 || h.rows == 0 || h.cols * h.rows > 0x8000) {
        if (why) *why = fmt("visibility grid %ux%u", h.cols, h.rows);
        return false;
    }
    return true;
}

double headingDegrees(int h12) {
    return double(h12 & 0xFFF) * 360.0 / 4096.0;
}

void headingDir(double deg, double &dx, double &dz) {
    dx = std::sin(deg * kPi / 180.0);
    dz = std::cos(deg * kPi / 180.0);
}

std::vector<Record> parseLevel(const uint8_t *lvl, size_t size, std::vector<std::string> *notes, bool fileOrder) {
    OrderScope order(fileOrder);
    std::vector<Record> out;
    LevelHeader h;
    std::string why;
    if (!parseHeader(lvl, size, h, &why, fileOrder)) {
        if (notes) notes->push_back(why);
        return out;
    }
    uint32_t lo, hi;
    /* 0x50 vehicle spawns: 9-byte BE records {u8 type; s16 x, y, z, heading} */
    if (section(h, size, 0x50, 0x54, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 9 <= hi; p += 9, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_VEHICLE, i, lvl, p, 9);
            r.type = b[0];
            if (r.type > 18) {
                if (notes) notes->push_back(fmt("vehicle spawn %d: type %d: the game stops the list here", i, r.type));
                break;
            }
            r.hasPos = true;
            r.x = int16_t(be16(b + 1)), r.y = int16_t(be16(b + 3)), r.z = int16_t(be16(b + 5));
            r.hasHeading = true;
            r.heading = headingDegrees(be16(b + 7));
            r.radius = 40;
            r.label = vehicleName(r.type);
            r.title = fmt("Vehicle spawn %d: %s", i, vehicleName(r.type));
            r.model = vehicleModelFile(r.type);
            add(r, "type", fmt("%d (%s)", r.type, vehicleName(r.type)));
            posFields(r);
            add(r, "heading", fmt("0x%03X (%.1f degrees)", be16(b + 7) & 0xFFF, r.heading));
            add(r, "model file", r.model);
            if (r.type == 0) add(r, "note", "the driver: the player's start (a vehicle on the same spot is the start vehicle)");
            if (r.type == 1) add(r, "note", "on bonus levels this record is the vehicle the player picked (gDiggerVehicle)");
            add(r, "respawn", "a vehicle of this type respawns here (veh_respawn_if_out)");
            out.push_back(r);
        }
        if (p != hi && p + 9 > hi) endsInside(notes, 0x50, hi - p, 9);
    }
    /* 0x54 the carrier record: 9 bytes BE {u8 speed; s16 x, z, heading, route length} */
    {
        const uint32_t p = h.offAt(0x54);
        if (p + 9 <= size) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_CARRIER, 0, lvl, p, 9);
            const int speed = b[0];
            r.hasPos = true;
            r.x = int16_t(be16(b + 1)), r.z = int16_t(be16(b + 3));
            r.y = 0, r.yKnown = false;
            r.hasHeading = true;
            r.heading = headingDegrees(be16(b + 5));
            const int len = int16_t(be16(b + 7));
            r.radius = 60;
            r.type = speed;
            r.label = speed ? "Carrier start" : "(no carrier)";
            r.title = speed ? "The missile carrier's record" : "Carrier record (speed 0: no carrier)";
            r.model = vehicleModelFile(0xFF);
            add(r, "speed", fmt("%d (target speed; 0 = no carrier, and no intro chopper)", speed));
            add(r, "start", fmt("x %.0f, z %.0f (map units)", r.x, r.z));
            add(r, "heading", fmt("0x%03X (%.1f degrees; the route is a straight line along it)", be16(b + 5) & 0xFFF,
                                  r.heading));
            add(r, "route length", fmt("%d map units (gCarrierRouteLen %d)", len, len * 32));
            double dx, dz;
            headingDir(r.heading, dx, dz);
            add(r, "route end", fmt("x %.0f, z %.0f", r.x + dx * len, r.z + dz * len));
            out.push_back(r);
        }
    }
    /* 0x5C object placements: 14 bytes {BE u16 x, y, z, type; u8 path, people; u16 motion, tag (host order)} */
    if (section(h, size, 0x5C, 0x60, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 14 <= hi; p += 14, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_OBJECT, i, lvl, p, 14);
            r.hasPos = true;
            r.x = be16(b + 0), r.y = be16(b + 2), r.z = be16(b + 4);
            r.type = be16(b + 6);
            const int motion = n16(b + 0xA), tag = n16(b + 0xC);
            r.radius = 30;
            r.label = fmt("obj 0x%02X", r.type);
            r.title = fmt("Object placement %d: type 0x%02X", i, r.type);
            r.model = fmt("packed object 0x%02X (ROM table 0x6EC4C0, entry %d)", r.type, r.type);
            add(r, "object type", fmt("0x%02X (%d)%s", r.type, r.type,
                                      r.type == 0x38 ? ": the end object" : r.type >= 0xBA && r.type <= 0xBC
                                          ? ": casts a blob shadow" : ""));
            posFields(r);
            add(r, "path building", b[8] ? fmt("yes (%u): must fall for the carrier", b[8]) : "no");
            add(r, "people inside", fmt("%u", b[9]));
            static const char *const motions[] = {"static", "bob", "orbit", "spline path", "maze walker", "swing"};
            add(r, "motion", fmt("%d (%s)", motion, motion < 6 ? motions[motion] : "?"));
            add(r, "tag", fmt("%d (the motion's parameter: path index, speed or amplitude)", tag));
            out.push_back(r);
        }
        endsInside(notes, 0x5C, hi - p, 14);
    }
    /* 0x34 RDUs: {s16 x, y, z} */
    if (section(h, size, 0x34, 0x38, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 6 <= hi; p += 6, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_RDU, i, lvl, p, 6);
            r.hasPos = true;
            r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
            r.radius = 12;
            r.label = fmt("RDU %d", i);
            r.title = fmt("RDU %d", i);
            r.model = "RDU (code-drawn beacon)";
            posFields(r);
            if (h.cellW && h.cellD)
                add(r, "visibility cell", fmt("%d", int(r.x) / int(h.cellW) + int(r.z) / int(h.cellD) * int(h.cols)));
            out.push_back(r);
        }
        endsInside(notes, 0x34, hi - p, 6);
    }
    /* 0x38 TNT: 12 bytes {s16 x, y, z; u8 type, timer; s16 @8, @A} */
    if (section(h, size, 0x38, 0x3C, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 12 <= hi; p += 12, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_TNT, i, lvl, p, 12);
            r.hasPos = true;
            r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
            r.type = b[6];
            r.radius = 16;
            r.label = fmt("TNT %d", i);
            r.title = fmt("TNT crate %d", i);
            r.model = fmt("box definition %d (gTntTypeList)", b[6]);
            posFields(r);
            add(r, "y", "snapped to the ground at spawn (coll_ground_height)");
            add(r, "type", fmt("%u (box definition: extents, face textures, radius)", b[6]));
            add(r, "timer", fmt("%u (fuse %u frames)", b[7], b[7] * 60u));
            add(r, "@8 (unk1A)", fmt("%d", int16_t(n16(b + 8))));
            add(r, "@A (unk1C)", fmt("%d", int16_t(n16(b + 0xA))));
            out.push_back(r);
        }
        endsInside(notes, 0x38, hi - p, 12);
    }
    /* 0x20 ammo crates: {s16 x, y, z, type} */
    if (section(h, size, 0x20, 0x24, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 8 <= hi; p += 8, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_CRATE, i, lvl, p, 8);
            r.hasPos = true;
            r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
            r.type = int16_t(n16(b + 6));
            r.radius = 14;
            r.label = fmt("crate %d", i);
            r.title = fmt("Ammo crate %d", i);
            r.model = fmt("gCrateTypes[%d]", r.type);
            posFields(r);
            add(r, "type", fmt("%d (gCrateTypes: size, textures, radar size, pickup radius, vehicle trigger)", r.type));
            out.push_back(r);
        }
        endsInside(notes, 0x20, hi - p, 8);
    }
    /* 0x28 communication points: {s16 x, y, z; u16 w} */
    if (section(h, size, 0x28, 0x2C, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 8 <= hi; p += 8, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_COMMPT, i, lvl, p, 8);
            r.hasPos = true;
            r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
            r.radius = 16;
            r.label = fmt("comm %d", i);
            r.title = fmt("Communication point %d", i);
            r.model = "commpoint (model 0x96)";
            posFields(r);
            add(r, "w", fmt("%u (-> the entry's unk12)", n16(b + 6)));
            out.push_back(r);
        }
        endsInside(notes, 0x28, hi - p, 8);
    }
    /* 0x3C push blocks: s16 n; n x {s16 x, y, z, model}; s16 m; m x {s16 x, y, z; u8 model, n; s16 target;
     * n x 0x16-byte triangles} */
    if (section(h, size, 0x3C, 0x40, lo, hi, notes) && lo + 2 <= hi) {
        uint32_t p = lo;
        const int n = int16_t(n16(lvl + p));
        p += 2;
        for (int i = 0; i < n && p + 8 <= hi; i++, p += 8) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_BLOCK, i, lvl, p, 8);
            r.hasPos = true;
            r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
            r.type = int16_t(n16(b + 6));
            r.radius = 20;
            r.label = fmt("block %d", i);
            r.title = fmt("Push block %d", i);
            r.model = fmt("gBlockModels[%d]", r.type);
            posFields(r);
            add(r, "y", "snapped to the ground at spawn");
            add(r, "model", fmt("%d (gBlockModels)", r.type));
            out.push_back(r);
        }
        if (p + 2 <= hi) {
            const int m = int16_t(n16(lvl + p));
            p += 2;
            for (int i = 0; i < m && p + 10 <= hi; i++) {
                const uint8_t *b = lvl + p;
                const unsigned tris = b[7];
                const uint32_t len = 10 + tris * 0x16;
                if (p + len > hi) break;
                Record r = base(REC_BLOCKSLOT, i, lvl, p, len);
                r.hasPos = true;
                r.x = int16_t(n16(b)), r.y = int16_t(n16(b + 2)), r.z = int16_t(n16(b + 4));
                r.type = b[6];
                r.radius = 20;
                r.label = fmt("slot %d", i);
                r.title = fmt("Block slot %d", i);
                r.model = fmt("gBlockModels[%u]", b[6]);
                posFields(r);
                add(r, "model", fmt("%u", b[6]));
                add(r, "triangles", fmt("%u (0x16 bytes each, -> gDynTris)", tris));
                const int target = int16_t(n16(b + 8));
                add(r, "target", target ? fmt("%d: a +-40 gBoxTargets box (\"fill the holes\")", target) : "0");
                out.push_back(r);
                p += len;
            }
        }
    }
    /* 0x58 panel events: 8 bytes {s16 carrier z; u8 a, b, c, d; u8 done; pad}; dig_coach_update reads the
     * s16 natively, so the loader swaps it (schemas.h; it used to leave the section big-endian) */
    if (section(h, size, 0x58, 0x5C, lo, hi, notes)) {
        int i = 0;
        uint32_t p = lo;
        for (; p + 8 <= hi; p += 8, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(REC_PANEL, i, lvl, p, 8);
            r.label = fmt("panel %d", i);
            r.title = fmt("Panel event %d", i);
            r.type = b[2];
            add(r, "carrier z", fmt("%d map units: fires once gCarrierZ >> 5 passes it", int16_t(n16(b))));
            add(r, "hud_panel_start", fmt("(%u, %u, %u, %u)", b[2], b[3], b[4], b[5]));
            add(r, "done", fmt("%u", b[6]));
            out.push_back(r);
        }
        endsInside(notes, 0x58, hi - p, 8);
    }
    /* 0x40 / 0x44 height zones: {s16 minX, minZ, maxX, maxZ, height} */
    for (int z = 0; z < 2; z++) {
        const uint32_t s = z == 0 ? 0x40 : 0x44;
        if (!section(h, size, s, s + 4, lo, hi, notes)) continue;
        int i = 0;
        uint32_t p = lo;
        for (; p + 10 <= hi; p += 10, i++) {
            const uint8_t *b = lvl + p;
            Record r = base(z == 0 ? REC_CAMZONE : REC_CHOPPERZONE, i, lvl, p, 10);
            r.hasBox = true;
            r.x0 = int16_t(n16(b)), r.z0 = int16_t(n16(b + 2)), r.x1 = int16_t(n16(b + 4)),
            r.z1 = int16_t(n16(b + 6));
            const int height = int16_t(n16(b + 8));
            r.hasPos = true;
            r.x = (r.x0 + r.x1) / 2, r.z = (r.z0 + r.z1) / 2, r.y = height == 3000 ? 0 : height;
            r.yKnown = height != 3000;
            r.type = height;
            r.label = fmt("%s %d", z == 0 ? "camzone" : "chopzone", i);
            r.title = fmt("%s %d", z == 0 ? "Camera height zone" : "Chopper height zone", i);
            add(r, "box", fmt("x %.0f..%.0f, z %.0f..%.0f", r.x0, r.x1, r.z0, r.z1));
            add(r, "height", height == 3000 ? std::string("3000 (HEIGHT_ZONE_NONE)") : fmt("%d", height));
            out.push_back(r);
        }
        endsInside(notes, s, hi - p, 10);
    }
    return out;
}

Record gameObjectRecord(const uint8_t *o, int index, uint32_t addr) {
    Record r;
    r.kind = REC_GOBJECT;
    r.index = index;
    r.offset = addr;
    r.raw.assign(o, o + 0xFC);
    int32_t v[0x3F];   /* the words, host order */
    memcpy(v, o, sizeof v);
    r.hasPos = true;
    r.x = v[4] / 32.0, r.y = v[5] / 32.0, r.z = v[6] / 32.0;   /* pos: the bounding sphere's centre */
    r.radius = std::max(8.0, v[3] / 32.0);
    r.type = v[0x30 / 4];
    r.label = fmt("obj %d", index);
    r.title = fmt("gObjects[%d]: kind 0x%02X", index, r.type);
    r.model = fmt("packed object 0x%02X", r.type);
    add(r, "info (model header)", fmt("0x%08X", uint32_t(v[0])));
    add(r, "collision triangles", fmt("0x%08X..0x%08X (%d)", uint32_t(v[1]), uint32_t(v[2]),
                                      int((uint32_t(v[2]) - uint32_t(v[1])) / 0x60)));
    add(r, "radius", fmt("%d (%.1f drawing units)", v[3], v[3] / 32.0));
    add(r, "pos (sphere centre)", fmt("%d, %d, %d (/32: %.1f, %.1f, %.1f)", v[4], v[5], v[6], r.x, r.y, r.z));
    add(r, "spawnPos", fmt("%d, %d, %d", v[7], v[8], v[9]));
    add(r, "centre x, z", fmt("%d, %d", v[10], v[11]));
    add(r, "kind", fmt("0x%02X%s", r.type, r.type == 0x38 ? " (the end object)" : ""));
    add(r, "tag", fmt("%d", v[0x34 / 4]));
    add(r, "groundY", fmt("%d", v[0x44 / 4]));
    add(r, "cell", fmt("%u", o[0xE8]));
    add(r, "parts", fmt("%u", o[0xE9]));
    add(r, "destroyed", fmt("%u", o[0xEA]));
    add(r, "path building", fmt("%u", o[0xEB]));
    std::string pct;
    for (int i = 0; i < 16 && i < o[0xE9]; i++) pct += fmt("%s%u", i ? " " : "", o[0xEC + i]);
    add(r, "part percent (100 = down)", pct.empty() ? "-" : pct);
    return r;
}

Record gameVehicleRecord(const uint8_t *b, int index, uint32_t addr) {
    Record r;
    r.kind = REC_GVEHICLE;
    r.index = index;
    r.offset = addr;
    r.raw.assign(b, b + 0x74);
    int32_t v[0x1D];
    memcpy(v, b, sizeof v);
    r.type = v[0x5C / 4];
    r.label = vehicleName(r.type);
    r.title = fmt("gVehicles[%d]: %s", index, vehicleName(r.type));
    r.model = vehicleModelFile(r.type);
    add(r, "type", fmt("%d (%s)", r.type, vehicleName(r.type)));
    add(r, "model file", r.model);
    add(r, "segment 6 (model)", fmt("0x%08X", uint32_t(v[0])));
    add(r, "segment 7 (matrices)", fmt("0x%08X / 0x%08X", uint32_t(v[1]), uint32_t(v[2])));
    add(r, "layer lists (odd buffer)", fmt("0x%08X 0x%08X 0x%08X", uint32_t(v[3]), uint32_t(v[4]), uint32_t(v[5])));
    add(r, "layer lists (even buffer)", fmt("0x%08X 0x%08X 0x%08X", uint32_t(v[0x30 / 4]), uint32_t(v[0x34 / 4]),
                                            uint32_t(v[0x38 / 4])));
    add(r, "shadow / blur lists", fmt("0x%08X / 0x%08X", uint32_t(v[0x54 / 4]), uint32_t(v[0x58 / 4])));
    add(r, "env alpha", fmt("%u", uint32_t(v[0x60 / 4])));
    add(r, "x, y, z (veh_set_record)", fmt("%d, %d, %d (world units; /32: %.0f, %.0f, %.0f)", v[0x64 / 4], v[0x68 / 4],
                                           v[0x6C / 4], v[0x64 / 4] / 32.0, v[0x68 / 4] / 32.0, v[0x6C / 4] / 32.0));
    add(r, "on a moving part", fmt("%d", v[0x70 / 4]));
    return r;
}

std::string hexDump(const std::vector<uint8_t> &b) {
    std::string s;
    for (size_t i = 0; i < b.size(); i += 16) {
        s += fmt("%03zX:", i);
        for (size_t k = i; k < i + 16 && k < b.size(); k++) s += fmt(" %02X", b[k]);
        s += "\n";
    }
    return s;
}

/* ---- the free-fly camera ------------------------------------------------------ */
Vec FlyCam::forward() const {
    const double y = yaw * kPi / 180.0, p = pitch * kPi / 180.0;
    return {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
}

Vec FlyCam::right() const {
    /* guLookAt's right: look x up (y up, right-handed: looking along +z, +x is on the left) */
    const double y = yaw * kPi / 180.0;
    return {-std::cos(y), 0.0, std::sin(y)};
}

void FlyCam::move(double fwd, double rt, double up, double dist) {
    const Vec f = forward(), r = right();
    pos.x += (f.x * fwd + r.x * rt) * dist;
    pos.y += (f.y * fwd + up) * dist;
    pos.z += (f.z * fwd + r.z * rt) * dist;
}

void FlyCam::turn(double dyaw, double dpitch) {
    yaw = std::fmod(yaw + dyaw, 360.0);
    if (yaw < 0) yaw += 360.0;
    pitch = std::max(-89.0, std::min(89.0, pitch + dpitch));
}

void FlyCam::view(double m[4][4]) const {
    const Vec l = forward();
    /* right = look x up, up' = right x look (guLookAt) */
    Vec r = {-l.z, 0.0, l.x};
    double n = std::sqrt(r.x * r.x + r.z * r.z);
    if (n < 1e-9) r = right(), n = 1.0;
    r.x /= n, r.z /= n;
    const Vec u = {r.y * l.z - r.z * l.y, r.z * l.x - r.x * l.z, r.x * l.y - r.y * l.x};
    m[0][0] = r.x, m[1][0] = r.y, m[2][0] = r.z;
    m[0][1] = u.x, m[1][1] = u.y, m[2][1] = u.z;
    m[0][2] = -l.x, m[1][2] = -l.y, m[2][2] = -l.z;
    m[3][0] = -(pos.x * r.x + pos.y * r.y + pos.z * r.z);
    m[3][1] = -(pos.x * u.x + pos.y * u.y + pos.z * u.z);
    m[3][2] = pos.x * l.x + pos.y * l.y + pos.z * l.z;
    m[0][3] = m[1][3] = m[2][3] = 0.0;
    m[3][3] = 1.0;
}

void FlyCam::lookAt(const Vec &p, double dist) {
    const Vec f = forward();
    pos = {p.x - f.x * dist, p.y - f.y * dist, p.z - f.z * dist};
}

bool project(const double v[4][4], const double pr[4][4], const Vec &p, double rx, double ry, double rw, double rh,
             double &sx, double &sy, double *depth) {
    double e[4], c[4];
    const double in[4] = {p.x, p.y, p.z, 1.0};
    for (int j = 0; j < 4; j++) e[j] = in[0] * v[0][j] + in[1] * v[1][j] + in[2] * v[2][j] + in[3] * v[3][j];
    for (int j = 0; j < 4; j++) c[j] = e[0] * pr[0][j] + e[1] * pr[1][j] + e[2] * pr[2][j] + e[3] * pr[3][j];
    if (depth) *depth = -e[2];
    if (!(c[3] > 1e-6)) return false;
    const double nx = c[0] / c[3], ny = c[1] / c[3];
    sx = rx + (nx * 0.5 + 0.5) * rw;
    sy = ry + (0.5 - ny * 0.5) * rh;
    return std::isfinite(sx) && std::isfinite(sy);
}

void pictureRect(double winW, double winH, double aspect, double &rx, double &ry, double &rw, double &rh) {
    if (!(aspect > 0) || winW <= 0 || winH <= 0) {
        rx = ry = 0, rw = winW, rh = winH;
        return;
    }
    if (winW / winH > aspect) {
        rh = winH, rw = winH * aspect;
    } else {
        rw = winW, rh = winW / aspect;
    }
    rx = (winW - rw) / 2, ry = (winH - rh) / 2;
}

bool setFar(double p[4][4], double farZ) {
    const double s = -p[2][3];
    if (!(std::fabs(s) > 1e-9) || std::fabs(p[3][3]) > 1e-6) return false;
    const double A = p[2][2] / s, B = p[3][2] / s;
    if (std::fabs(A - 1.0) < 1e-12 || std::fabs(A + 1.0) < 1e-12) return false;
    const double n = B / (A - 1.0), f = B / (A + 1.0);
    if (!(n > 0) || !(f > n) || !(farZ > n)) return false;
    p[2][2] = (n + farZ) / (n - farZ) * s;
    p[3][2] = 2.0 * n * farZ / (n - farZ) * s;
    return true;
}

}  // namespace viewer
}  // namespace live
