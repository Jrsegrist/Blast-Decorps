/* bc.exe's level viewer (live_viewer.cpp): the level file's records and the
 * game's runtime records it shows, and the free-fly camera's math.  Plain
 * C++: no renderer, window or game state, so bc_cameratest.exe
 * (viewer_test.cpp) tests it on synthetic data (nothing from the ROM).
 *
 * The level file is read as it is in the port's memory (docs/systems/
 * levels.md; port/src/load/schemas.h swap_level): the header and the
 * sections the game reads natively are in host order (little-endian), the
 * byte streams it reads bytewise (vehicle spawns, the carrier record, object
 * placements but their u16 at 0xA and 0xC, the collision grids) stay
 * big-endian.  Positions are in drawing units (the file's map units; the
 * game's world units / 32), headings in degrees (the game's 12-bit
 * 0..0xFFF: 0 = +z, 90 = +x). */
#ifndef VIEWER_RECORDS_H
#define VIEWER_RECORDS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live {
namespace viewer {

/* record kinds (one per section the viewer reads, then the game's runtime records) */
enum RecKind {
    REC_VEHICLE,      /* 0x50: vehicle spawn (also the respawn point) */
    REC_CARRIER,      /* 0x54: the missile carrier's record (start, heading, speed, route length) */
    REC_OBJECT,       /* 0x5C: object (building) placement */
    REC_RDU,          /* 0x34 */
    REC_TNT,          /* 0x38 */
    REC_CRATE,        /* 0x20: ammo crate */
    REC_COMMPT,       /* 0x28: communication point */
    REC_BLOCK,        /* 0x3C: push block */
    REC_BLOCKSLOT,    /* 0x3C: a push block's slot (hole) */
    REC_PANEL,        /* 0x58: carrier-progress panel event */
    REC_CAMZONE,      /* 0x40: camera height zone */
    REC_CHOPPERZONE,  /* 0x44: chopper cruise-height zone */
    REC_GOBJECT,      /* runtime: a gObjects LevelObject (0xFC bytes) */
    REC_GVEHICLE,     /* runtime: a gVehicles record (0x74 bytes) */
    REC_KINDS
};
const char *recKindName(int kind);     /* "Vehicle spawn" ... */
const char *recKindKey(int kind);      /* "vehicle" ... (the test script's names) */

/* VEH_* (include/game/types.h): the vehicle's name and its model file (docs/systems/files.md) */
const char *vehicleName(int type);
const char *vehicleModelFile(int type);

struct Field {
    std::string name, value;
};

struct Record {
    int kind = REC_KINDS;
    int index = 0;                  /* within its section (or the runtime array) */
    uint32_t offset = 0;            /* in the level file (runtime records: the N64 address) */
    std::vector<uint8_t> raw;       /* the record's bytes as they are in memory */
    bool hasPos = false;
    double x = 0, y = 0, z = 0;     /* drawing units */
    bool yKnown = true;             /* (false: a 2-D record; y is a guess the viewer may snap to the ground) */
    bool hasHeading = false;
    double heading = 0;             /* degrees */
    double radius = 0;              /* drawing units, for markers (0: none) */
    bool hasBox = false;            /* zones: x/z rectangle (and y = height) */
    double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    int type = -1;                  /* vehicle type, object type, TNT/crate type ... */
    std::string label;              /* short: the marker's text */
    std::string title;              /* "Vehicle spawn 2: Ramdozer" */
    std::string model;              /* model file / packed object */
    std::vector<Field> fields;
};

/* the header's grid sizes and bounds (lvl_read_header) */
struct LevelHeader {
    unsigned cols = 0, rows = 0, cellW = 0, cellD = 0;          /* visibility grid (cell sizes in map units) */
    unsigned groundCols = 0, groundRows = 0, groundCellW = 0, groundCellD = 0;
    unsigned collCols = 0, collRows = 0, collCellW = 0, collCellD = 0;
    int gravity = 0, dlProfile = 0;
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;                  /* level box (0x4C) */
    uint32_t off[0x2A] = {};       /* the section offsets at 0x20 + 4k (k = 0..0x29: 0x20..0xC4) */
    uint32_t offAt(uint32_t hdrOffset) const { return off[(hdrOffset - 0x20) / 4]; }
};

/* the header of a level image of `size` bytes; false (and *why) if it doesn't look like one.
 * fileOrder: the image as the ROM holds it (every field big-endian: the level editor's copy,
 * src/load/level_mod.h), not as the port's loader left it in memory */
bool parseHeader(const uint8_t *lvl, size_t size, LevelHeader &h, std::string *why = nullptr, bool fileOrder = false);

/* every record of the sections above, in section order; problems (a section
 * that ends inside a record, an unknown vehicle type) go to *notes */
std::vector<Record> parseLevel(const uint8_t *lvl, size_t size, std::vector<std::string> *notes = nullptr,
                               bool fileOrder = false);

/* the game's runtime records (host-order structs, include/game/object.h, 00000.c VehRecord) */
Record gameObjectRecord(const uint8_t *obj, int index, uint32_t addr);   /* 0xFC bytes */
Record gameVehicleRecord(const uint8_t *veh, int index, uint32_t addr);  /* 0x74 bytes */

/* "00 11 22 ..." in lines of 16, with offsets */
std::string hexDump(const std::vector<uint8_t> &b);

/* ---- the free-fly camera ------------------------------------------------------ */
struct Vec {
    double x = 0, y = 0, z = 0;
};
/* A position and a yaw / pitch (degrees; yaw 0 looks along +z, 90 along +x;
 * pitch up positive), moved in its own frame. */
struct FlyCam {
    Vec pos;
    double yaw = 0, pitch = -20;
    Vec forward() const;           /* unit, with the pitch */
    Vec right() const;             /* unit, horizontal */
    /* fwd / right / up: -1..1 each; `dist`: drawing units for a full input */
    void move(double fwd, double right, double up, double dist);
    void turn(double dyawDeg, double dpitchDeg);   /* pitch held to -89..89 */
    /* the view matrix (row vectors, translation in row 3: guLookAt's layout) */
    void view(double m[4][4]) const;
    /* looking at `p` from `dist` away, at this yaw and pitch */
    void lookAt(const Vec &p, double dist);
};
/* the vehicle's heading (0..0xFFF) in degrees, and the direction it faces (x, z) */
double headingDegrees(int h12);
void headingDir(double deg, double &dx, double &dz);

/* a world point through view * proj (row vectors) to the picture rectangle
 * (x, y, w, h in window pixels); false behind the camera.  *depth: the
 * view-space distance */
bool project(const double view[4][4], const double proj[4][4], const Vec &p, double rx, double ry, double rw,
             double rh, double &sx, double &sy, double *depth = nullptr);
/* the picture rectangle of a picture of aspect `aspect` (w/h) fitted into a window w x h, centred */
void pictureRect(double winW, double winH, double aspect, double &rx, double &ry, double &rw, double &rh);
/* a perspective projection's far plane moved to `farZ` (the near plane kept); false if it isn't one */
bool setFar(double p[4][4], double farZ);

}  // namespace viewer
}  // namespace live

#endif
