#ifndef GAME_OBJECT_H
#define GAME_OBJECT_H

/*
 * Level objects (gObjects: buildings and other destructible objects, 0xFC
 * bytes each, built by lvl_add_object in 5CB60.c), their model headers and
 * collision triangles.  77E20.c (objects), 89250.c (collision probes) and
 * 13A70.c (ground shadows) use these; 7FB50.c keeps a view of its own
 * (Rec7FB50: `info` as bytes) with the same field names.
 */

/* A level object's model header (the `info` of a LevelObject): mostly byte offsets
 * (from the header) of its tables. */
typedef struct ObjectModel {
    /* 0x00 */ u16 numToppleKeys; /* number of s16 entries at toppleKeys (obj_part_outranked) */
    /* 0x02 */ u8 pad2[2];
    /* 0x04 */ u8 type;   /* toughness: the damage divisor; 0xFF = not tallied, 1 = flimsy (not counted) */
    /* 0x05 */ u8 debrisScale; /* scale of the debris records (obj_spawn_random_effects, obj_spawn_random_landing_effect) */
    /* 0x06 */ u8 numPeople; /* passed to people_spawn (obj_spawn_people) */
    /* 0x07 */ u8 partsTopple; /* parts topple (obj_start_topple); clear: they fade (obj_start_part_fade) */
    /* 0x08 */ s32 value; /* summed */
    /* 0x0C */ u8 padC;
    /* 0x0D */ u8 immunePart; /* 1-based index of the part obj_check_destroyed ignores */
    /* 0x0E */ u8 padE[0xE];
    /* 0x1C */ s32 markerPts; /* offset of 4 s16 x, y, z points the target marker frames (obj_find_nearest_target) */
    /* 0x20 */ s32 footprintBox; /* offset of an s16[4] box x0, z0, x1, z1 (obj_set_sink_quad) */
    /* 0x24 */ s32 floorTris; /* offsets of a table of 0x14-byte records (obj_mark_part_gone; also the
                           * floor triangles ground_scan_object walks) */
    /* 0x28 */ s32 texAnimTbl; /*   and its end = the texture-animation table */
    /* 0x2C */ s32 partPoints; /* offset of 8-byte s16 x, y, z points (obj_spawn_random_effects) */
    /* 0x30 */ s32 breakEffects; /* offsets of 0x38-byte records (obj_spawn_break_effects) */
    /* 0x34 */ s32 landEffects; /*   their end = start of more (obj_spawn_landing_effects) */
    /* 0x38 */ s32 partDlRecs; /*   their end = start of the conditional DL records (obj_start_part_fade) */
    /* 0x3C */ s32 toppleKeys; /* offset of an s16 per-part key table (obj_part_outranked: only the
                                * lowest-keyed part topples); end of the DL records */
    /* 0x40 */ s32 partHeights; /* offset of an s16 table indexed 1-based (obj_part_exposed) */
    /* 0x44 */ s32 supportRecs; /* offsets of variable-length records (obj_part_exposed) */
    /* 0x48 */ s32 supportRecsEnd; /*   and their end */
} ObjectModel;

/* A collision triangle (obj_move_model keeps its plane up to date). */
typedef struct CollisionTri {
    /* 0x00 */ s64 normal[3]; /* unnormalised: cross product of two edges */
    /* 0x18 */ s64 d;         /* -(normal . vertex 1) */
    /* 0x20 */ f32 len;       /* |normal| */
    /* 0x24 */ f32 lenSq;
    /* 0x28 */ s32 v[3][3];   /* vertices x, y, z */
    /* 0x4C */ u16 heading;   /* a wall's heading, 0..0xFFF: the centre of obj_tri_clip_free_arc's clip */
    /* 0x4E */ u8 axis;       /* dominant normal axis: 0 = z, 1 = y, 2 = x */
    /* 0x4F */ u8 owner;        /* vehicle triangles: the owning vehicle type (7 = the train) */
    /* 0x50 */ u8 id;
    /* 0x51 */ u8 isSolid;      /* collidable (coll_probe_object_tris) */
    /* 0x52 */ u16 partNo;     /* the part, 1-based */
    /* 0x54 */ u8 switchSide;   /* switch planes: the side last seen (TRI_SIDE_*) */
    /* 0x55 */ u8 wallTwoSided; /* ==1: clips the free arc on both sides */
    /* 0x56 */ u8 wallSide;     /* which side clips (obj_tri_clip_free_arc) */
    /* 0x57 */ u8 linkedPart;   /* becomes solid when this part falls; off when its own part lands */
    /* 0x58 */ u8 isWall;       /* a wall (obj_tri_clip_free_arc) */
    /* 0x59 */ u8 failTrigger;  /* grid walls: nonzero makes the wall lethal (gFailTrigger) */
    /* 0x5A */ u8 pad5A[6];
} CollisionTri; /* size 0x60 */

/* What obj_disable_landed_part_tris/obj_move_model get in v0: a model header and a range of
 * its triangles. */
typedef struct LevelObjectHead {
    /* 0x00 */ u8 * N64P data;
    /* 0x04 */ CollisionTri * N64P start;
    /* 0x08 */ CollisionTri * N64P end;
} LevelObjectHead;

/* A level object (gObjects): a building or another destructible object. */
typedef struct LevelObject {
    /* 0x00 */ ObjectModel * N64P info;
    /* 0x04 */ CollisionTri * N64P tris;  /* its collision triangles (on the heap) */
    /* 0x08 */ CollisionTri * N64P trisEnd;
    /* 0x0C */ s32 radius;                /* bounding sphere (the model's u16 at +2 << 5) */
    /* 0x10 */ s32 pos[3];                /* centre of the bounding sphere */
    /* 0x1C */ s32 spawnPos[3];           /* placed position (spline_object_matrix's base) */
    /* 0x28 */ s32 centreX;               /* footprint box centre (lvl_object_centre; the carrier's target) */
    /* 0x2C */ s32 centreZ;
    /* 0x30 */ s32 kind;                  /* object type (0x38 = the end object; 0xBA..0xBC cast ground shadows) */
    /* 0x34 */ s32 tag;
    /* 0x38 */ s32 poseState[3];          /* spline_object_matrix's state (zeroed at spawn) */
    /* 0x44 */ s32 groundY;               /* kinds 0xBA..0xBC: the ground height (13A70.c's shadow quads) */
    /* 0x48 */ u16 partFalling[0x10];     /* per part: falling (obj_drop_unsupported_parts) */
    /* 0x68 */ u16 partFallTime[0x10];    /* frames falling; also the spin scale */
    /* 0x88 */ u16 partSpinX[0x10];       /* the falling part's spin (obj_falling_part_mtx) */
    /* 0xA8 */ u16 partSpinY[0x10];
    /* 0xC8 */ u16 partSpinZ[0x10];
    /* 0xE8 */ u8 cell;                   /* the level grid cell (matched against gVisibleCells) */
    /* 0xE9 */ u8 numParts;               /* number of parts */
    /* 0xEA */ u8 destroyed;              /* every part down (obj_check_destroyed) */
    /* 0xEB */ u8 pathBuilding;           /* one of the level's path buildings (obj_path_buildings_destroyed) */
    /* 0xEC */ u8 partPct[0x10];          /* per part; 100 = destroyed. Indexed by gPathParts's byte lists */
} LevelObject; /* size 0xFC */

STATIC_ASSERT_SIZE(CollisionTri, 0x60);
STATIC_ASSERT_SIZE(ObjectModel, 0x4C);
STATIC_ASSERT_SIZE(LevelObject, 0xFC);

/* LevelObject.kind (word 0x30 of a gObjects record; lvl_add_object's type, gObjSolidKinds entries). */
#define OBJ_KIND_END             0x38 /* the end object: gEndObjectX/Y/Z, gEndObjectTouched, takes no damage */
#define OBJ_KIND_SHADOW_A        0xBA /* 0xBA..0xBC cast a blob shadow: groundY (lvl_init_object_shadow, */
#define OBJ_KIND_SHADOW_B        0xBB /*   obj_draw_ground_shadows); what the objects are is unknown */
#define OBJ_KIND_SHADOW_C        0xBC
#define OBJ_KIND_MARS_FAIL_FIRST 0xE7 /* level 0x2D: kinds 0xE7..0xE9 fail the mission when hit */
#define OBJ_KIND_MARS_FAIL_END   0xEA /*   (obj_check_mars_fail; one past the last) */

/* ObjectModel.type: the toughness (damage divisor) and its two special values (obj_tally). */
#define OBJ_TOUGH_FLIMSY    1    /* not counted as a building; no emitter, no "new target" mark */
#define OBJ_TOUGH_UNCOUNTED 0xFF /* left out of the level's tally */

/* Damage-rule record kinds: byte 2 of the gObjDamageRules records obj_calc_damage walks
 * (any other value is an animation-channel kind). */
#define DMG_RULE_EXPOSED    0xFB /* n * value if obj_part_exposed */
#define DMG_RULE_ATTACK     0xFC /* n * value if gVehAttacking */
#define DMG_RULE_SPEED      0xFD /* |gHitSpeed| * value * n >> 4, always (ends each key's list) */
#define DMG_RULE_SPEED_REV  0xFE /*   the same, skipped when gHitSpeed > 0 */
#define DMG_RULE_SPEED_FWD  0xFF /*   the same, skipped when gHitSpeed < 0 */

/* CollisionTri.axis: the dominant normal axis, dropped for the 2-D tests (math_tri_project_2d). */
#define TRI_AXIS_Z 0
#define TRI_AXIS_Y 1
#define TRI_AXIS_X 2

/* Byte 0x54 of a switch triangle (CollisionTri.switchSide): the side the vehicle was last on
 * (veh_hit_switch_triangle; crossing flips gDriverCanExit). */
#define TRI_SIDE_NONE  0 /* not yet touched (lvl_build_coll_tri) */
#define TRI_SIDE_FRONT 1 /* on d's side of the plane */
#define TRI_SIDE_BACK  2

#endif
