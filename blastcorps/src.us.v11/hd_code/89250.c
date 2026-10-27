#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gObjects ((LevelObject *) gObjects)
#ifdef NON_MATCHING
#define gObjectsEnd (*(LevelObject * N64P *) &gObjectsEnd)
#endif
/* end of views */

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The blast effect at (x, y, z) (0x802CDA10).
 * Sets up a gEffects record (fx_spawn) for the definition gFxDefBlast
 * with data 0x591C8, type 0, position (x, y, z) << 11 and every other input
 * (tag, words, b35) 0. The asm saves s0-s7 and fp; 48D00.c calls it. */
void coll_spawn_blast_fx(s32 x, s32 y, s32 z) {
    Io802A6274 io;

    io.a3 = 0;
    io.t6 = 0;
    io.s1 = 0;
    fx_spawn(&io, gFxDefBlast, 0x591C8, 0, x << 11, y << 11, z << 11, 0, 0, 0, 0, 0, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_spawn_blast_fx.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Blast damage at the probe point (0x802CDAE8).
 * Blast damage (obj_blast_damage) at the position gCollX/Y/Z << 11 with
 * the given radius and amount. The asm uses the whole registers (48D00.c
 * declares both s16) and saves s0-s7, fp. */
void coll_blast_at_probe(s32 amount, s32 radius) {
    obj_blast_damage(gCollX << 11, gCollY << 11, gCollZ << 11, radius, amount);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_blast_at_probe.s")
#endif

#ifdef NON_MATCHING
/* (the level objects and their collision triangles: game/object.h) */

extern u16 gImpactDamage; /* impact damage */

s32 coll_probe_object_tris(s32 x, s32 y, s32 z, s32 r, LevelObject *e, s32 hit);
void coll_damage_object_part(LevelObject *e, CollisionTri *tri, s32 amount);
s32 coll_probe_vehicle_spheres(s32 x, s32 y, s32 z, s32 r, s32 id);

/* The sphere-vs-triangle test the asm runs as a call chain (see 56040.c,
 * math_sphere_hits_tri_edges): plane distance, projected point-in-triangle, edge, vertex. */
static s32 sphere_hits_tri_89250(s32 x, s32 y, s32 z, s32 r, CollisionTri *tri) {
    s32 hit[3];
    s32 uv[8];

    if (math_sphere_tri_plane(x, y, z, r, (u8 *) tri, hit) == 0) {
        return 0;
    }
    math_tri_project_2d((u8 *) tri, hit[0], hit[1], hit[2], uv);
    if (math_point_in_tri_2d(uv[2], uv[3], uv[4], uv[5], uv[0], uv[1], uv[6], uv[7]) != 0) {
        return 1;
    }
    if (math_sphere_hits_tri_edges(x, y, z, r, (u8 *) tri) != 0) {
        return 1;
    }
    return math_sphere_hits_tri_vertex0(x, y, z, r, (u8 *) tri) != 0;
}
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The probe sphere against the objects' triangles (0x802CDB70).
 * Impact of the point gCollX/Y/Z with radius r against the objects:
 * stores amount (low 16 bits) to gImpactDamage, then every object (gObjects
 * up to gObjectsEnd, read once, walked with `!=`) within range (math_spheres_overlap)
 * and not a 0x38 object while obj_end_object_hidden is set gets the triangle pass
 * coll_probe_object_tris, which carries a "hit" flag from object to object (the first
 * hit applies the damage). Clears the ring span gFreeArcStart/7412 when
 * gFreeArcCollapsed is set. Returns the flag (0/1).
 * ABI apart from f20-f28, which the triangle tests leave changed (the asm
 * doesn't save them). The asm uses whole registers; the C callers declare
 * (s16, s16) (48D00.c, 4DA80.c) or (s16, s32) returning void (4B5E0.c). */
s32 coll_probe_objects(s32 r, s32 amount) {
    s32 x = gCollX;
    s32 y = gCollY;
    s32 z = gCollZ;
    LevelObject *end = gObjectsEnd;
    LevelObject *o;
    s32 hit = 0;

    gImpactDamage = amount;
    for (o = gObjects; o != end; o++) {
        if (math_spheres_overlap(z, r, o->pos[0], o->pos[1], x, y, o->pos[2], o->radius) == 0) {
            continue;
        }
        if (o->kind == OBJ_KIND_END && obj_end_object_hidden() != 0) {
            continue;
        }
        hit = coll_probe_object_tris(x, y, z, r, o, hit);
    }
    if (gFreeArcCollapsed != 0) {
        gFreeArcStart = 0;
        gFreeArcEnd = 0;
    }
    return hit;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_probe_objects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One object's triangles against the probe sphere (0x802CDC7C).
 * Every collidable triangle of e (start up to end, read once, walked with
 * `!=`) that the sphere (x, y, z, r) >> 2 (arithmetic shifts) hits: on the
 * first hit overall (hit == 0) with a nonzero gImpactDamage, applies that damage
 * (coll_damage_object_part); then the plane-side update obj_tri_clip_free_arc(triangle), and
 * hit becomes 1. Returns hit.
 * Register convention: x, y, z, r in v0, v1, a0, a1, e in t3, hit in fp (in
 * and out). The asm saves v0-a2, t3 and t4 and leaves t5/t6 = z/r >> 2, t9 =
 * e, s0/s1 = the triangle range, s2-s4 and f20-f28 changed by the tests;
 * asm caller coll_probe_objects keeps a0, a1, t3 and t4 live across the call (the
 * s4/f24/f26 the survey lists as read there are dead: s4 is restored). */
s32 coll_probe_object_tris(s32 x, s32 y, s32 z, s32 r, LevelObject *e, s32 hit) {
    CollisionTri *t = e->tris;
    CollisionTri *end = e->trisEnd;

    x >>= 2;
    y >>= 2;
    z >>= 2;
    r >>= 2;
    for (; t != end; t++) {
        if (t->isSolid == 0) {
            continue;
        }
        if (!sphere_hits_tri_89250(x, y, z, r, t)) {
            continue;
        }
        if (hit == 0) {
            hit = gImpactDamage;
            if (hit != 0) {
                coll_damage_object_part(e, t, hit);
            }
        }
        obj_tri_clip_free_arc(t);
        hit = 1;
    }
    return hit;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_probe_object_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Impact damage to a part; collapses it at 100 (0x802CDD74).
 * Impact damage to the part of triangle tri of object e: screen shake 10/200;
 * unless e is a 0x38 object, the part's percentage += amount / e->info->type
 * (unsigned; the asm traps (break 7) on type 0), capped at 100, and
 * obj_spawn_break_effects updates the part. A part reaching 100 collapses
 * (obj_mark_part_gone, obj_start_topple, obj_start_part_fade, obj_start_sinking_part,
 * obj_spawn_people; its triangles off and the triangles whose unk57 is the part
 * on unless their own part is done; obj_drop_unsupported_parts, obj_check_destroyed).
 * Register convention: e in t9, tri in s0, amount in fp; the asm saves every
 * register (asm caller coll_probe_object_tris keeps a1, t3-t6 and t9 live). */
void coll_damage_object_part(LevelObject *e, CollisionTri *tri, s32 amount) {
    u8 *pcts = e->partPct;
    s32 part;
    s32 level;

    gShakeTime = 10;
    gShakeSize = 200;
    if (e->kind == OBJ_KIND_END) {
        return;
    }
    part = tri->partNo;
    level = pcts[part - 1] + (u32) amount / e->info->type;
    if (level >= 100) {
        level = 100;
    }
    pcts[part - 1] = level;
    obj_spawn_break_effects(e, part, level);
    if (level == 100) {
        CollisionTri *t;
        CollisionTri *end;

        obj_mark_part_gone(e, part);
        obj_start_topple(e, part);
        obj_start_part_fade(part, e);
        obj_start_sinking_part(part, e);
        obj_spawn_people(e);
        end = e->trisEnd;
        for (t = e->tris; t != end; t++) {
            if (t->partNo == part) {
                t->isSolid = 0;
            }
            if (t->linkedPart == part && pcts[t->partNo - 1] != 100) {
                t->isSolid = 1;
            }
        }
        obj_drop_unsupported_parts(e);
        obj_check_destroyed(e);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_damage_object_part.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The probe sphere against every vehicle's spheres (0x802CDF94).
 * Impact of the point gCollX/Y/Z with radius r against the vehicles:
 * clears gProbeHitCarrier/932E, then for every gVehBoundSpheres sphere (until flag -1;
 * vehicle 0 is skipped while gDriverHidden is set) within range (math_spheres_overlap)
 * adds coll_probe_vehicle_spheres's 0/1 (that vehicle's hit spheres) to the count. Sets
 * gCollBlocked and gCollHit unless the ring span is 0/0xFFF, clears it when
 * gFreeArcCollapsed is set, and returns the count.
 * ABI (the asm saves s0-s7 and fp). The asm uses the whole a0; the C
 * callers declare (s16) returning u8 (48D00.c, 4B5E0.c) or void (4DA80.c). */
s32 coll_probe_vehicles(s32 r) {
    s32 x = gCollX;
    s32 y = gCollY;
    s32 z = gCollZ;
    u8 *p = gVehBoundSpheres;
    s32 count = 0;

    gProbeHitCarrier = 0;
    gProbeHitPlayer = 0;
    while ((s8) p[0x11] != -1) {
        s32 id = p[0x10];

        if (id == 0 && gDriverHidden != 0) {
            p += 0x14;
            continue;
        }
        p += 0x14;
        if (math_spheres_overlap(z, r, ((s32 *) p)[-5], ((s32 *) p)[-4], x, y, ((s32 *) p)[-3], ((s32 *) p)[-2]) != 0) {
            count += coll_probe_vehicle_spheres(x, y, z, r, id);
        }
    }
    if (((u16) gFreeArcStart) != 0 || ((u16) gFreeArcEnd) != 0xFFF) {
        gCollBlocked = 1;
        gCollHit = 1;
    }
    if (gFreeArcCollapsed != 0) {
        gFreeArcStart = 0;
        gFreeArcEnd = 0;
    }
    return count;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_probe_vehicles.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One vehicle's part spheres against the probe (0x802CE0E4).
 * Vehicle id's hit spheres vs the point (x, y, z) radius r: finds id's first
 * gVehPartSpheres sphere (until flag -1), then walks that run of consecutive
 * spheres of id (vehicle 6 skips spheres of radius 0x3BD); for each within
 * range (math_spheres_overlap): gProbeHitPlayer = 1 if id is the mode gCurVehicle,
 * gProbeHitCarrier = 1 for vehicle 0xFF, and the plane-side update
 * coll_block_toward(x, z, sphere x, sphere z). Returns 1 if any sphere was in
 * range, else 0.
 * Register convention: x, y, z, r in v0, v1, a0, a1, id in t4, result in s4
 * (clobbered); the asm saves v0-t0, t6, t7, s0, s1 and leaves t5 = -1, the
 * constant asm caller coll_probe_vehicles keeps there. Asm caller coll_probe_vehicles
 * keeps a0, a1 and t3 live across the call. */
s32 coll_probe_vehicle_spheres(s32 x, s32 y, s32 z, s32 r, s32 id) {
    u8 *p = gVehPartSpheres;
    s32 hit = 0;

    for (;; p += 0x14) {
        if ((s8) p[0x13] == -1) {
            return 0;
        }
        if (p[0x12] == id) {
            break;
        }
    }
    while ((s8) p[0x13] != -1 && p[0x12] == id) {
        s32 ex = ((s32 *) p)[0];
        s32 ey = ((s32 *) p)[1];
        s32 ez = ((s32 *) p)[2];
        s32 er = ((s32 *) p)[3];

        p += 0x14;
        if (id == 6 && er == 0x3BD) {
            continue;
        }
        if (math_spheres_overlap(z, r, ex, ey, x, y, ez, er) == 0) {
            continue;
        }
        if (gCurVehicle == id) {
            gProbeHitPlayer = 1;
        }
        if (id == 0xFF) {
            gProbeHitCarrier = 1;
        }
        hit = 1;
        coll_block_toward(x, z, ex, ez);
    }
    return hit;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_probe_vehicle_spheres.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
f32 sqrtf(f32);
#pragma intrinsic(sqrtf)

/* cvt.w.s under the game's FCSR: round to nearest, ties to even. */
static s32 port_cvt_w_s(f32 x) {
    s32 t = (s32) x;
    f32 frac = x - (f32) t;

    if (frac > 0.5f || (frac == 0.5f && (t & 1))) {
        t++;
    } else if (frac < -0.5f || (frac == -0.5f && (t & 1))) {
        t--;
    }
    return t;
}

/* Heading from one point to another, +-0x400, into the free arc (0x802CE204).
 * Heading from (x1, z1) to (x2, z2), as in veh_block_toward_part (56040.c): 0 when
 * both differences are 0, else (arcsine(round(65536 * leg / dist)) >> 4) +
 * quadrant * 0x400 with dist = sqrtf of the float squares and the quadrant
 * picked by signed compares (x2 >= x1: z2 >= z1 -> leg x2 - x1, else z1 - z2
 * + 0x400; x2 < x1: z2 < z1 -> x1 - x2 + 0x800, else z2 - z1 + 0xC00).
 * Then widens the ring span with veh_clip_free_arc(angle + 0x400, angle - 0x400).
 * Register convention: x1, z1, x2, z2 in v0, a0, a2, t0 (conventions.txt);
 * the asm saves every register it uses (v0-t0, t7, s0, fp). Asm callers
 * rely on preserved: veh_parts_touch_sphere keeps a2, a3, t0, t1, t5, t7, t8;
 * coll_probe_vehicle_spheres keeps a0, a1, t4, t5, t7. */
void coll_block_toward(s32 x1, s32 z1, s32 x2, s32 z2) {
    s32 dx = x2 - x1;
    s32 dz = z2 - z1;
    u32 angle = 0;

    if (dx != 0 || dz != 0) {
        f32 fx = dx;
        f32 fz = dz;
        f32 dist = sqrtf(fx * fx + fz * fz);

        if (!(x2 < x1)) {
            if (!(z2 < z1)) {
                angle = (u32) math_asin(port_cvt_w_s(65536.0f * ((f32) (x2 - x1) / dist))) >> 4;
            } else {
                angle = ((u32) math_asin(port_cvt_w_s(65536.0f * ((f32) (z1 - z2) / dist))) >> 4) + 0x400;
            }
        } else if (z2 < z1) {
            angle = ((u32) math_asin(port_cvt_w_s(65536.0f * ((f32) (x1 - x2) / dist))) >> 4) + 0x800;
        } else {
            angle = ((u32) math_asin(port_cvt_w_s(65536.0f * ((f32) (z2 - z1) / dist))) >> 4) + 0xC00;
        }
    }
    veh_clip_free_arc(angle + 0x400, angle - 0x400);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_block_toward.s")
#endif

#ifdef NON_MATCHING
/* Shared declarations for the NON_MATCHING (port) rewrites below. */
extern u8 gProbeNonBlockingParts[];
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A heading inside the free arc (or its middle) (0x802CE3B8).
 * Given a ring index (0..0xFFF), finds which end of the span
 * [gFreeArcStart, gFreeArcEnd] is nearer in ring distance (|d|, folded as
 * 0xFFF - |d| above 0x800). Nearer to B: step 0x78 back from B (+0xFFF if
 * negative); otherwise step 0x78 on from A (-0xFFF if >= 0x1000). If that
 * candidate lies outside the span (for a wrapped span, B < A: strictly
 * between B and A; otherwise below A or above B), veh_free_arc_mid's value is
 * returned instead. The asm returns the full 32-bit value; the C callers
 * declare s16. */
s32 coll_heading_in_ring(s32 idx) {
    s32 a = ((u16) gFreeArcStart);
    s32 b = ((u16) gFreeArcEnd);
    s32 da = a - idx;
    s32 db = b - idx;
    s32 v;

    if (da < 0) {
        da = -da;
    }
    if (da > 0x800) {
        da = 0xFFF - da;
    }
    if (db < 0) {
        db = -db;
    }
    if (db > 0x800) {
        db = 0xFFF - db;
    }
    if (db < da) {
        v = b - 0x78;
        if (v < 0) {
            v += 0xFFF;
        }
    } else {
        v = a + 0x78;
        if (v >= 0x1000) {
            v -= 0xFFF;
        }
    }
    if (b < a) {
        if (v < a && b < v) {
            v = veh_free_arc_mid();
        }
    } else {
        if (v < a || b < v) {
            v = veh_free_arc_mid();
        }
    }
    return v;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_heading_in_ring.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The probe point; the free arc and flags reset (0x802CE4F0).
 * Starts a new span at position (x, y, z): the ring span becomes the empty
 * 0/0xFFF pair, the position goes to gCollX/Y/Z, the flags
 * gFreeArcCollapsed/7427/7424/7425 are cleared, gNonBlockingParts points at gProbeNonBlockingParts,
 * then veh_contacts_clear is called. */
void coll_begin_probe(s32 x, s32 y, s32 z) {
    gFreeArcStart = 0;
    gFreeArcEnd = 0xFFF;
    gCollX = x;
    gCollY = y;
    gCollZ = z;
    gFreeArcCollapsed = 0;
    gImpactEffectOn = 0;
    gNonBlockingParts = (s8 *) gProbeNonBlockingParts;
    gCollHit = 0;
    gCollBlocked = 0;
    veh_contacts_clear();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_begin_probe.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The probe sphere against the level triangles (0x802CE5BC).
 * Reports every triangle the sphere (x, y, z) radius r touches for vehicle
 * `kind` (veh_report_world_hits with everything >> 2 and part id 0), after storing
 * the low byte of `tag` to gProbeSkipPart (the part veh_report_world_hits skips for kind
 * 0xC8). If that set gFreeArcCollapsed, the ring span gFreeArcStart/End is reset to
 * 0/0. The C callers declare r s16; the asm shifts the whole register. The
 * asm saves s0-s7 and fp. */
void coll_probe_tris(s32 x, s32 y, s32 z, s32 r, s32 kind, s32 tag) {
    gProbeSkipPart = tag;
    veh_report_world_hits(x >> 2, y >> 2, z >> 2, r >> 2, kind, 0);
    if (gFreeArcCollapsed != 0) {
        gFreeArcStart = 0;
        gFreeArcEnd = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_probe_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gCollStepLen;

/* (x, z) moved along an angle (0x802CE65C).
 * Stores (x, z) to gCollStepX/9324 and len to gCollStepLen, then moves the
 * point by len along heading angle (veh_move_along_heading with scale 0.0) and stores
 * the result back. ABI (the asm saves s0-s7 and fp); the C callers declare
 * (s32, s32, s16, s16), the asm uses the whole angle register. */
void coll_step_xz(s32 x, s32 z, s32 len, s32 angle) {
    Out802A860C out;

    gCollStepX = x;
    gCollStepZ = z;
    gCollStepLen = len;
    gCollStepX = veh_move_along_heading(0.0f, angle, &gCollStepLen, &gCollStepX, &gCollStepZ, &out);
    gCollStepZ = out.t1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_step_xz.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

#define PORT_ABS(d) ((d) < 0 ? -(d) : (d))

/* The nearest ground height under (x, z) near y (0x802CE6F8).
 * Ground height under (x, z) for an object at height y: with y' = y + 0x78,
 * takes the nearest-to-y' heights of the gGroupTris triangles (h1,
 * ground_scan_part_tris), of the transformed triangles (h2, ground_scan_moveable, no id
 * skipped) and of the level grid (h3, ground_scan_grid, which leaves h3 = h2
 * when it finds nothing). With a grid hit, h3 wins unless h2 or h1 is
 * strictly nearer to y'; then (or without a grid hit) h2 wins if strictly
 * nearer than h1, else h1; with no hit at all (h1 and h2 both 0x5F5E0FF) the
 * result is y. gGroundTriId = ground_scan_moveable's id when h2 won, else 0. Returns
 * the height, clamped at 0 from below.
 * The asm's sub/addi trap on overflow (game coordinates don't reach it); it
 * saves s0-s7 and fp, so the fp passed through to ground_scan_part_tris /
 * ground_scan_grid (*fpOut, only written) and the FP results they leave in
 * f12-f26 are dead here, but it does change f20-f28 (conventions.txt
 * `clobbers`). Called from C (4DA80, 48D00, 4B5E0) and asm lvl_init_object_shadow. */
s32 coll_ground_height(s32 x, s32 z, s32 y) {
    TriSideOut f;
    s32 fp;
    s32 id;
    s32 a2;
    s32 h1;
    s32 h2;
    s32 h3;
    s32 d1;
    s32 d2;
    s32 d3;
    s32 res;

    y += 0x78;
    h1 = ground_scan_part_tris(x, z, y, &f, &fp);
    h2 = ground_scan_moveable(x, z, y, 0, &f, &id, &a2);
    h3 = h2;
    if (ground_scan_grid(x, z, y, &f, &h3, &fp) != 0) {
        d2 = PORT_ABS(h2 - y);
        d3 = PORT_ABS(h3 - y);
        d1 = PORT_ABS(h1 - y);
        if (d2 >= d3 && d1 >= d3) {
            res = h3;
            id = 0;
            goto done;
        }
    } else {
        if (h1 == 0x5F5E0FF && h2 == 0x5F5E0FF) {
            res = y - 0x78;
            id = 0;
            goto done;
        }
        d2 = PORT_ABS(h2 - y);
        d1 = PORT_ABS(h1 - y);
    }
    if (d2 < d1) {
        res = h2;
    } else {
        res = h1;
        id = 0;
    }
done:
    gGroundTriId = id;
    if (res < 0) {
        res = 0;
    }
    return res;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/89250/coll_ground_height.s")
#endif
