#ifndef GAME_REGS_H
#define GAME_REGS_H

/*
 * Register structs for the NON_MATCHING C rewrites of hand-written asm.
 *
 * Much of the hand-written vehicle/ground code passes values between
 * functions in registers outside the o32 ABI (t-, s- and FP registers that
 * a callee reads or leaves for its caller). The C rewrites pass those as
 * pointers to these structs; tools_port/conventions.txt maps each field to
 * its asm register for eqcheck. Field names are the registers.
 */

/* fx_spawn's three in/out registers (60F60.c). */
typedef struct {
    /* 0x0 */ s32 a3; /* in: tag byte for gFxSpawnTag; out: last gFxTileSlots byte scanned */
    /* 0x4 */ s32 t6; /* in: word for rec+0x20; out: (s16) def[0] (when a record was set up) */
    /* 0x8 */ s32 s1; /* in: word for rec+0x14; out: the record's 0x100-byte buffer (when def[0] != -1) */
} Io802A6274;

/* Results of ground_tri_contains that its asm callers read from FP registers; the
 * triangle scans and the ground functions hand them on (62740.c). */
typedef struct {
    f32 pz;    /* f12: (f32) z */
    f32 cross; /* f14: last edge cross product of the point */
    f32 cz;    /* f20: centroid-ish z */
    f32 side;  /* f22: last edge cross product of the centre (2.0 if none computed) */
    f32 sideZ; /* f24: its z term (in/out: unchanged if never computed) */
    f32 dz;    /* f26: last edge's z extent */
} TriSideOut;

/* veh_move_along_heading's results besides t0, its return value (62740.c). */
typedef struct {
    s32 t1; /* new z */
    s32 s3; /* *px as read */
    s32 fp; /* the cosine (math_cos's result) */
} Out802A860C;

/* veh_ground_contact's register results besides the FP state (62740.c). */
typedef struct {
    s32 s3;  /* in/out: the slot functions' result */
    s16 *s4; /* out: angle, or veh + 0x4C when veh_update_stand_on ran */
    s32 fp;  /* out: the second tilt (also stored to gModelRot) */
} Regs802A8768;

/* veh_reground's pointer results, the asm's s1 and s3 (62740.c). */
typedef struct {
    s32 *s1; /* dst + 9 (past the three records) */
    s32 *s3; /* dst */
} Out802A9A60;

/* Registers mtx_chain_transform reads and writes besides its arguments (62740.c);
 * veh_place_parts and veh_points_transform pass them through. */
typedef struct {
    s32 v1; /* out: y' >> 11 */
    s32 a0; /* out: z' >> 11 */
    s32 a3; /* in/out: the last matrix used */
    s32 s1; /* in/out: y' */
    s32 s2; /* in/out: z' */
    s32 s0; /* in: only read when count == 0 */
} MtxChainRegs;

/* Registers ground_tri_interp (and veh_deck_to_local / veh_deck_to_world / veh_follow_heading)
 * read and write besides their arguments (62740.c). */
typedef struct {
    s32 t3;  /* in: value u0 at corner 0; out: interpolated u */
    s32 t4;  /* in: value w0 at corner 0; out: interpolated w */
    f32 f12; /* out: 1 - k */
    s32 f14; /* out: raw bits; the asm leaves cvt.w.s's integer (= t4) there */
    f32 f20; /* out: t (position along the far edge) */
    f32 f22; /* out: x + dx0 * k */
    f32 f24; /* out: z + dz0 * k (also an input, kept on some paths) */
    f32 f26; /* out: (f32) z */
} InterpRegs;

/* Registers veh_zone_alpha leaves for its asm callers: the last zone looked
 * at (62740.c). The per-frame vehicle updates take them in and out. */
typedef struct {
    s32 t6; /* zone x */
    s32 t7; /* zone y */
    s32 s0; /* zone z */
    s32 s1; /* distance, or the scaled term when a level was computed */
    s32 s2; /* zone radius */
    s32 s3; /* scan counter, then zone byte +0x14 (or +0x10 when that is 0) */
    s32 s4; /* scan pointer, then zone byte +0x10 (when +0x14 != 0) */
} ZoneScanRegs;

/* Registers ground_scan_guard_tris leaves for its asm callers (62740.c). */
typedef struct {
    s32 a1; /* found flag */
    s32 a3; /* ground_tri_height's a3 (in/out) */
    s32 t6; /* ground_tri_height's t6 (in/out) */
    s32 t7; /* = fp: the list end */
    s32 fp;
    s32 s1; /* x0, y0, z0, x1 of the last triangle loaded (in/out) */
    s32 s2;
    s32 s3;
    s32 s4;
} TriScanRegs;

/* Register outputs of crane_find_test_part (56040.c); s0-s3 are in/out: left alone
 * unless the asm loads them. */
typedef struct {
    /* 0x00 */ s32 s0; /* entry word 0 */
    /* 0x04 */ s32 s1; /* entry word 4 */
    /* 0x08 */ s32 s2; /* entry word 8 */
    /* 0x0C */ s32 s3; /* entry word 0xC of the last kind-6 entry looked at */
    /* 0x10 */ s32 s4; /* 1 = found */
    /* 0x14 */ u8 *t6; /* entry where the scan stopped */
    /* 0x18 */ s32 t7; /* last byte compared */
} Unk8029C6E4Out;

/* Register results of tex_resolve_dl_impl its asm callers read (5BF40.c). */
typedef struct {
    /* 0x0 */ u8 * N64P *s2; /* &gTexCacheEnd */
    /* 0x4 */ u8 *s3;  /* the new gTexCacheEnd */
    /* 0x8 */ u8 *s4;  /* in/out: last cache pair looked at (only set when a G_SETTIMG was seen) */
} Unk802A08E4Regs;

/* Register results of lvl_load_model its asm callers read (5CB60.c). */
typedef struct {
    /* 0x0 */ u8 *s2; /* the loaded data (old heap pointer) */
    /* 0x4 */ u8 *s4; /* tex_resolve_dl_impl's s4 (in: the ROM start) */
    /* 0x8 */ u8 *a1; /* the new heap end */
} Out802A396C;

/* thunderfist_build_model's (6C5E0.c) and starski_build_model / cyclone_build_model's (8AEE0.c)
 * results besides t0, the return value. */
typedef struct {
    s32 t2; /* end of the second point list */
    s32 s1; /* mtx_chain_transform's y' (MtxChainRegs.s1) */
    s32 s4; /* the matrix base */
} VehMtxOut;

/* Register results of lvl_load_model_plain its asm caller reads (5CB60.c). */
typedef struct {
    /* 0x0 */ u8 * N64P s2; /* the loaded data (old heap pointer) */
    /* 0x4 */ u8 * N64P s3; /* tex_resolve_dl_impl's s3 */
    /* 0x8 */ u8 * N64P s4; /* tex_resolve_dl_impl's s4 (in: the ROM start) */
} Out802A32CC;

/* The FP registers the vehicle spawns hand to veh_init_ground (f12, f14, f20-f26;
 * FP side results only), as their TriSideOut (5CB60.c). */
typedef struct {
    f32 f12;
    f32 f14;
    f32 f20;
    f32 f22;
    f32 f24;
    f32 f26;
} SpawnFP;

/* veh_respawn_if_out's t0 / t1: position in, reset position out (unchanged if
 * none) (62740.c). */
typedef struct {
    s32 t0; /* x */
    s32 t1; /* z */
} Pos802A8CCC;

/* Register results of veh_gate_area_test its asm caller reads (all in/out: left
 * alone on paths that don't write them) (56040.c). */
typedef struct {
    /* 0x00 */ s32 s1;
    /* 0x04 */ s32 s3;
    /* 0x08 */ TriSideOut fp; /* f12, f14, f20, f22, f24 (in/out), f26 */
} Unk8029DA90Regs;

/* Register outputs of anim_set_spline_basis (f12/f14 are in/out: only written
 * when mode == 1) (56040.c). */
typedef struct {
    f32 f12;
    f32 f14;
    f32 f20;
} Unk8029F110Out;

/* Register outputs of anim_advance_channel (56040.c). */
typedef struct {
    s32 frame; /* t2 (in/out) */
    s32 dir;   /* t7 */
    f32 frac;  /* f30 */
} Unk8029F1BCOut;

/* A vehicle's position, model and its two 0x800-byte matrix buffers taken
 * from the heap gHeapPtr (swapped every frame by gBufferIndex); the shared
 * spawn helpers of 6B4A0.c and 6E200.c.  (N64 memory: a view of the vehicle
 * blocks gSkyfallPos / gRamdozerPos.) */
typedef struct {
    s32 x;
    s32 y;
    s32 z;
    u8 * N64P model;
    u64 * N64P bufA;
    u64 * N64P bufB;
} PortVehPos;

#endif
