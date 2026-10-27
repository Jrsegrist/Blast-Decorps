#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gSolarNodeMtx
#define LEGACY_gDepthBufferPhys
#define LEGACY_gHeapPtr
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gDynamicBufs ((Dynamic *) gDynamicBufs)
#define gFrameBufferPhys ((u16 * N64P *) gFrameBufferPhys)
#define players ((PlayerInfo *) players)
#ifdef NON_MATCHING
#define gSolarNodeMtx ((Mtx *) gSolarNodeMtx)
#define gDepthBufferPhys (*(u16 * N64P *) &gDepthBufferPhys)
#define gHeapPtr (*(s32 *) &gHeapPtr)
#endif
/* end of views */


#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) < (b) ? (b) : (a))
#define JTEXT(addr) ((u16 * N64P) HD_JTEXT_ADDR(addr))

GlobeLevel gGlobeLevels[60] = {
    { 0x64, 1, N64_DPTR("SIMIAN ACRES"), JTEXT(0x80303700), 0, 0.0f, 0.0f, { -1 }, { 0x1C, 0x13, 0x03, 0x37, 0x32, -1 } },
    { 0x64, 1, N64_DPTR("ANGEL CITY"), JTEXT(0x8030370C), 0, -20.0f, -120.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("OUTLAND FARM"), JTEXT(0x80303720), 0, 20.0f, 60.0f, { 0x25, 0x1B, -1 }, { 0x05, 0x0D, 0x09, -1 } },
    { 0x64, 1, N64_DPTR("BLACKRIDGE WORKS"), JTEXT(0x80303734), 0, -20.0f, -20.0f, { 0x14, 0x06, -1 }, { 0x1D, 0x35, 0x0F, 0x0A, -1 } },
    { 0x64, 1, N64_DPTR("GLORY CROSSING"), JTEXT(0x80303748), 0, 70.0f, 180.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("SHUTTLE GULLY"), JTEXT(0x8030375C), 0, 50.0f, 20.0f, { 0x29, -1 }, { 0x04, 0x21, 0x02, -1 } },
    { 0x64, 1, N64_DPTR("SALVAGE WHARF"), JTEXT(0x8030376C), 0, -30.0f, -10.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("SKYFALL"), JTEXT(0x8030377C), 0, 40.0f, 20.0f, { -1 }, { 0x05, -1 } },
    { 0x64, 1, N64_DPTR("TWILIGHT FOUNDRY"), JTEXT(0x80303794), 0, 10.0f, 30.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("CRYSTAL RIFT"), JTEXT(0x803037A8), 0, 20.0f, 120.0f, { 0x33, 0x1F, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("ARGENT TOWERS"), JTEXT(0x803037B8), 0, -20.0f, 20.0f, { 0x15, 0x22, -1 }, { 0x0D, 0x11, 0x10, 0x03, -1 } },
    { 0x64, 1, N64_DPTR("SKERRIES"), JTEXT(0x803037CC), 0, 70.0f, 0.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("DIAMOND SANDS"), JTEXT(0x803037D8), 0, -70.0f, 110.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("EBONY COAST"), JTEXT(0x803037EC), 0, -20.0f, 60.0f, { 0x20, -1 }, { 0x1A, 0x11, 0x02, -1 } },
    { 0x64, 1, N64_DPTR("OYSTER HARBOR"), JTEXT(0x803037FC), 0, -70.0f, -110.0f, { 0x17, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("CARRICK POINT"), JTEXT(0x8030380C), 0, 20.0f, -20.0f, { 0x19, 0x1E, -1 }, { 0x21, 0x12, 0x10, 0x03, -1 } },
    { 0x64, 1, N64_DPTR("HAVOC DISTRICT"), JTEXT(0x8030381C), 0, 20.0f, 20.0f, { 0x16, 0x08, -1 }, { 0x07, 0x02, 0x0F, 0x0A, -1 } },
    { 0x64, 1, N64_DPTR("IRONSTONE MINE"), JTEXT(0x8030382C), 0, -50.0f, 20.0f, { -1 }, { 0x0A, 0x0C, 0x0D, 0x3A, -1 } },
    { 0x64, 1, N64_DPTR("BEETON TRACKS"), JTEXT(0x80303844), 0, 20.0f, -60.0f, { 0x30, -1 }, { 0x1D, 0x0F, 0x21, 0x39, -1 } },
    { 0x00, 0, N64_DPTR("J-BOMB"), JTEXT(0x80303854), 0, 10.0f, 10.0f, { -1 }, { 0x10, -1 } },
    { 0x64, 1, N64_DPTR("JADE PLATEAU"), JTEXT(0x80303864), 0, -10.0f, -30.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MARINE QUARTER"), JTEXT(0x80303874), 0, -30.0f, 10.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("COOTER CREEK"), JTEXT(0x80303884), 0, 30.0f, 10.0f, { 0x0B, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("GIBBON'S GATE"), JTEXT(0x80303890), 0, -50.0f, 180.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("BABOON CATACOMB"), JTEXT(0x803038A0), 0, 30.0f, -150.0f, { -1 }, { 0x25, -1 } },
    { 0x64, 1, N64_DPTR("SLEEK STREETS"), JTEXT(0x803038B0), 0, 10.0f, -30.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("OBSIDIAN MILE"), JTEXT(0x803038C0), 0, -20.0f, 120.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("CORVINE BLUFF"), JTEXT(0x803038D0), 0, 10.0f, 50.0f, { 0x38, -1 }, { -1 } },
    { 0x28, 1, N64_DPTR("SIDESWIPE"), JTEXT(0x803038E0), 0, 10.0f, -10.0f, { -1 }, { 0x0F, -1 } },
    { 0x64, 1, N64_DPTR("ECHO MARCHES"), JTEXT(0x803038F8), 0, -20.0f, -60.0f, { 0x2A, 0x27, -1 }, { 0x01, 0x3A, 0x12, -1 } },
    { 0x64, 1, N64_DPTR("KIPLING PLANT"), JTEXT(0x80303904), 0, 30.0f, -10.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("FALCHION FIELD"), JTEXT(0x80303918), 0, 45.0f, 100.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MORGAN HALL"), JTEXT(0x80303930), 0, -10.0f, 50.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("TEMPEST CITY"), JTEXT(0x8030393C), 0, 50.0f, -20.0f, { 0x23, -1 }, { 0x12, 0x05, 0x04, -1 } },
    { 0x64, 1, N64_DPTR("ORION PLAZA"), JTEXT(0x80303950), 0, -10.0f, 30.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("GLANDER'S RANCH"), JTEXT(0x80303960), 0, 50.0f, -70.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("DAGGER PASS"), JTEXT(0x80303974), 0, -20.0f, 180.0f, { 0x34, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("GEODE SQUARE"), JTEXT(0x80303980), 0, 50.0f, 180.0f, { 0x3B, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("SHUTTLE ISLAND"), JTEXT(0x80303990), 0, 0.0f, -140.0f, { -1 }, { 0x28, -1 } },
    { 0x64, 1, N64_DPTR("MICA PARK"), JTEXT(0x803039A8), 0, -40.0f, -80.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MOON"), JTEXT(0x803039B4), 0, 10.0f, -140.0f, { -1 }, { 0x2B, -1 } },
    { 0x64, 1, N64_DPTR("COBALT QUARRY"), JTEXT(0x803039BC), 0, 50.0f, 70.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MORAINE CHASE"), JTEXT(0x803039CC), 0, -10.0f, -80.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MERCURY"), JTEXT(0x803039E0), 0, 20.0f, -140.0f, { -1 }, { 0x2C, -1 } },
    { 0x64, 1, N64_DPTR("VENUS"), JTEXT(0x803039F0), 0, 30.0f, -140.0f, { -1 }, { 0x2D, -1 } },
    { 0x64, 1, N64_DPTR("MARS"), JTEXT(0x803039FC), 0, 40.0f, -140.0f, { -1 }, { 0x2E, -1 } },
    { 0x64, 1, N64_DPTR("NEPTUNE"), JTEXT(0x80303A04), 0, 50.0f, -140.0f, { -1 }, { -1 } },
    { 0x00, 0, N64_DPTR("CMO INTRO"), JTEXT(0x80303A14), 0, 70.0f, 0.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("SILVER JUNCTION"), JTEXT(0x80303A24), 0, 10.0f, -80.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("END SEQUENCE"), JTEXT(0x80303A34), 0, 0.0f, -10.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("SHUTTLE CLEAR"), JTEXT(0x80303A48), 0, 0.0f, -15.0f, { -1 }, { 0x26, -1 } },
    { 0x64, 1, N64_DPTR("DARK HEARTLAND"), JTEXT(0x80303A58), 0, 0.0f, 90.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("MAGMA PEAK"), JTEXT(0x80303A68), 0, 0.0f, -150.0f, { 0x18, -1 }, { -1 } },
    { 0x0A, 0, N64_DPTR("THUNDERFIST"), JTEXT(0x80303A74), 0, -40.0f, -20.0f, { -1 }, { 0x3A, -1 } },
    { 0x64, 1, N64_DPTR("SALINE WATCH"), JTEXT(0x80303A8C), 0, 0.0f, 150.0f, { 0x24, -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("BACKLASH"), JTEXT(0x80303A98), 0, -10.0f, 10.0f, { -1 }, { 0x0A, -1 } },
    { 0x64, 1, N64_DPTR("BISON RIDGE"), JTEXT(0x80303AB0), 0, 0.0f, 40.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("EMBER HAMLET"), JTEXT(0x80303AC0), 0, 20.0f, -120.0f, { -1 }, { -1 } },
    { 0x64, 1, N64_DPTR("CROMLECH COURT"), JTEXT(0x80303AD4), 0, -50.0f, -20.0f, { -1 }, { 0x0E, 0x11, 0x1D, -1 } },
    { 0x64, 1, N64_DPTR("LIZARD ISLAND"), JTEXT(0x80303AE8), 0, 30.0f, 150.0f, { 0x36, -1 }, { -1 } },
};

u16 gVehicleIcons[38] = {
    0x0000, 0x0000, 0x0571, 0x0570, 0x0918, 0x0917, 0x090C, 0x090B, 0x070A, 0x0709, 0x0772, 0x0713, 0x0000, 0x0000, 0x0000, 0x0000, 0x0912, 0x0911, 0x0916, 0x0915, 0x090A, 0x0909, 0x0000, 0x0000, 0x0000, 0x0000, 0x090E, 0x090D, 0x0908, 0x0907, 0x091A, 0x0919, 0x0712, 0x0711, 0x0000, 0x0000, 0x0000, 0x0000
};

u16 gGlobeExtraSprites[4] = { 0x0A98, 0x0A99, 0x0A9A, 0x0A9B };

/* one per frame buffer (gBufferIndex) */
Lights1 gGlobeLights[2] = {
    gdSPDefLights1(0x10, 0x10, 0x10, 0xFF, 0xFF, 0xFF, 0, 0, 1),
    gdSPDefLights1(0x10, 0x10, 0x10, 0xFF, 0xFF, 0xFF, 0, 0, 1),
};

f32 gGlobeStickAngle = 0.0f;

/* (gLevelInfo[]: LevelInfo, game/types.h) */

/* (players[]: PlayerInfo, game/types.h) */

/* hd_code gDynamicBufs: per-frame-buffer dynamic data (0x21498 bytes each) */
typedef struct {
    /* 0x0000 */ u8 pad0[0x80];
    /* 0x0080 */ Mtx persp;
    /* 0x00C0 */ u8 padC0[0x80];
    /* 0x0140 */ Mtx view;
    /* 0x0180 */ u8 pad180[0x1280 - 0x180];
    /* 0x1280 */ Mtx translate;
    /* 0x12C0 */ u8 pad12C0[0x15C0 - 0x12C0];
    /* 0x15C0 */ Vtx stars[0x80 * 4];
    /* 0x35C0 */ u8 pad35C0[0x48B0 - 0x35C0];
    /* 0x48B0 */ Gfx gfx[(0x21498 - 0x48B0) / 8];
    /* 0x21498 */ u8 pad21498[0x21498 - 0x48B0 - ((0x21498 - 0x48B0) / 8) * 8];
} Dynamic;

#ifndef NON_MATCHING
extern u16 *gDepthBufferPhys;
#endif
f32 sqrtf(f32);

/* front end */
extern Gfx * N64P gGlobeStarsDl;
extern u8 gGlobeSelLevel;
extern u16 gGlobeState;
extern u8 gStarSpeeds[];
#ifndef NON_MATCHING
extern Mtx gSolarNodeMtx[];
#endif
extern u8 gGlobeFromLevel;
extern s8 gGlobeStartLevel;
extern s8 gGlobeStickLevel;
extern u8 gGlobeShownLevel;
extern u8 gRouteCount;
u64 gRouteDrawn[60];
extern f32 gGlobeLonStep;
extern f32 gGlobeLatStep;
extern u16 gFlightState;
extern s32 gRouteVtxBufs[];
extern u8 gGlobeStickCentred;
extern u8 gGlobeExtraSpriteBase;
extern f32 gRouteRevealMin;
extern s16 gGlobeNameAlpha;
extern u8 gGlobeSelectPending;
extern void * N64P gRouteRevealSound;
extern f32 angd;
extern f32 dmm;
extern f32 mmm;
#ifndef NON_MATCHING
extern s32 gHeapPtr;
#endif
void fe_globe_select_level(s32 arg0);
f32 fe_globe_angle_diff(f32 a, f32 b, f32 range);
Gfx *fe_globe_draw_stars(Gfx *arg0, Dynamic *dyn);
Gfx *fe_globe_draw_plane(Dynamic *dyn, Gfx *gdl, u8 from, u8 to);
void fe_globe_set_light(void);
extern Gfx * N64P gGlobeMeshDl;
extern Gfx * N64P gGlobeLevelIconsDl;
extern Gfx * N64P gGlobeAcademyIconsDl;
extern u8 gGlobeOverlayAlpha;
extern PlayerInfo * N64P gGlobePlayer;
extern LevelInfo * N64P gGlobeLevelInfo;
extern f32 gPlaneX;
extern f32 gPlaneY;
extern f32 gPlaneZ;
extern Vtx gPlaneVtx[2][4];
extern s32 gFlightSteps; /* flight time */
extern f32 gFlightFromX; /* flight start/end points */
extern f32 gFlightToX;
extern f32 gFlightFromY;
extern f32 gFlightToY;
extern f32 gFlightFromZ;
extern f32 gFlightToZ;
extern f32 gFlightDist; /* distance */
extern f32 gFlightCos; /* cos of the arc */
extern f32 gFlightAngle; /* arc angle */
extern f32 gFlightProgress; /* flight progress 0..1 */
extern f32 gFlightAltitude; /* altitude scale */
extern f32 gGlobeCamX;
extern f32 gGlobeCamY;
extern f32 gGlobeCamZ;
extern s32 gGlobeZoomDoneTime;
extern s32 gGlobeRoutesDlLen;
extern s32 gGlobePlaneDlLen;
extern f32 gGlobeTargetLon;
extern f32 gGlobeTargetLat;
extern Gfx * N64P gGlobeRoutesDl;
extern Gfx * N64P gGlobePlaneDl;
Gfx *fe_globe_build_scene_dl(Gfx *, Dynamic *, s32 *);
Gfx *fe_globe_build_routes_dl(Gfx *, Dynamic *, s32 *);
Gfx *fe_globe_build_plane_dl(Gfx *, Dynamic *, s32 *);
void fe_globe_scroll_stars(void);

void fe_globe_quad_at(Vtx *v, f32 x, f32 y, f32 z, u8 w, u8 h, f32 scale, u8 flip);
Gfx *fe_globe_draw_routes(Gfx *gdl, Dynamic *dyn, f32 arg2, s8 *arg3);
Gfx *fe_globe_draw_route(Dynamic *dyn, Gfx *arg1, u8 from, u8 to, s8 *out, f32 *lon, u8 curved, u8 r0, u8 g0, u8 b0,
                   u8 r1, u8 g1, u8 b1);

/* The level-select globe's setup (0x801F8530).
 * Level-select globe: initialise for the given level. */
void fe_globe_init(s32 level) {
    Dynamic *d;
    u32 i;
    GlobeLevel *e;
    f32 lat;
    f32 lon;

    gGlobePlayer = &players[playerNumber];
    gGlobeLevelInfo = &gLevelInfo[level];
    gGlobeCamLon = fe_globe_angle_diff(90.0f, gGlobeLevels[level].lon, 180.0f);
    gGlobeCamLat = fe_globe_angle_diff(90.0f, gGlobeLevels[level].lat, 180.0f);
    gGlobeMeshDl = fe_lvlsel_build_globe_dl();
    gGlobeLevelIconsDl = fe_lvlsel_build_level_icons_dl();
    gGlobeAcademyIconsDl = fe_lvlsel_build_academy_dl();
    for (i = 0; i < 2; i++) {
        gRouteVtxBufs[i] = gHeapPtr;
        gHeapPtr += 0x17000;
    }
    gGlobeStarsDl = fe_lvlsel_build_starfield_dl();
    for (i = 0; i < 2; i++) {
        d = &gDynamicBufs[i];
        guPerspective(&d->persp, &gPerspNorm, 45.0f, 4.0f / 3.0f, 100.0f, 20000.0f, 1.0f);
        guTranslate(&d->translate, 0.0f, -50.0f, 0.0f);
    }
    gGlobePlanet = 6;
    fe_globe_select_level(level);
    gGlobeFromLevel = level;
    gGlobeShownLevel = level;
    gFlightState = 1;
    gGlobeStickCentred = 0;
    gGlobeStickLevel = -1;
    gGlobeOverlayAlpha = 0;
    gGlobeStartLevel = level;
    gRouteRevealMin = 0.0f;
    gRouteRevealSound = 0;
    gGlobeNameAlpha = 0;
    angd = 0.915f;
    dmm = 0.009f;
    mmm = 26100.0f;
    fe_load_vehicle_icons();
    gGlobeExtraSpriteBase = spr_load(gGlobeExtraSprites, 0, 4, 1, 1, 1.0f);
    for (i = 0; i < 60; i++) {
        e = &gGlobeLevels[i];
        lat = e->lat;
        lon = e->lon;
        fe_globe_latlon_to_xyz(&lat, &lon, &e->x, &e->y, &e->z, 250.0f);
    }
    gGlobeSelectPending = 0;
}

/* Selects a level: its planet, the Earth music, a move sound (0x801F885C). */
void fe_globe_select_level(s32 arg0) {
    u8 tune;

    tune = lvl_planet(arg0);
    gGlobeSelLevel = arg0;
    if (tune != gGlobePlanet) {
        gGlobeState = 0;
        gGlobeCamDist = 6250.0f;
        if (gGlobePlanet != 6) {
            if (gGlobePlanet == 3) {
                music_start_tune(MUS_WORLD_SPACE);
            } else if (tune == 3) {
                music_start_tune(MUS_WORLD_EARTH);
            }
        }
    }
    if (gGlobePlanet != 6) {
        if (tune == 3 && gGlobePlanet == 3) {
            snd_play(gSfxBank, SFX_MENU_MOVE, 0);
        } else {
            snd_play(gSfxBank, SFX_GLOBE_PLANET_MOVE, 0);
        }
    }
    gGlobePlanet = tune;
    gGlobeLevelInfo = &gLevelInfo[gGlobeSelLevel];
}

/* The globe's frame: input, zoom, spin, three RSP tasks (0x801F8980).
 * Level-select: one frame (input, globe spin, fades, draw). */
void fe_globe_frame(void) {
    s16 ox;
    s16 oy;
    s32 i;
    u32 dist;
    Dynamic *dyn;

    nextdma = 0;
    no_palette_dmas = 0;
    tex_reset_decode_queue();
    cont_start_read();
    if (((s32) gFramesInMode)) {
        rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].gfx), gFrameDlLen, 2, 0, 1234, 0);
        rsp_send_task((u64 *) gGlobeRoutesDl, gGlobeRoutesDlLen, 0, 0, 1234, 0);
        rsp_send_task((u64 *) gGlobePlaneDl, gGlobePlaneDlLen, 1, 1, 1234, 0);
    } else {
        rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].gfx), gFrameDlLen, 1, 1, 1234, 0);
    }
    gBufferIndex ^= 1;
    dyn = &gDynamicBufs[gBufferIndex];
    cont_update();
    if (gGlobeSelectPending || (!areWeFading() && gGlobeState == 1 && (gContButtons & (A_BUTTON | START_BUTTON)) && !(gContPrevButtons & (A_BUTTON | START_BUTTON)))) {
        if (gFlightState == 0) {
            snd_stop_all_looping();
            snd_play(gSfxBank, SFX_SELECT, 0);
            gGlobeState = 2;
            debug_printf("selected level %d\n", gGlobeSelLevel);
            levelno = gGlobeSelLevel;
            fe_player_load_level_save();
            gGlobeSelectPending = 0;
        } else {
            gGlobeSelectPending = 1;
        }
    }
    if ((gContButtons & B_BUTTON) && !(gContPrevButtons & B_BUTTON) && !areWeFading() && gGlobeState == 1) {
        gRouteRevealFlags = 0;
        snd_play(gSfxBank, SFX_BACK, 0);
        fade_to_mode(MODE_QUIT_GAME);
        levelno = gGlobeSelLevel;
    }
    dist = gPadStickX * gPadStickX + gPadStickY * gPadStickY;
    if (dist < 1500) {
        gGlobeStickCentred = 1;
    }
    if (dist != 0) {
        gGlobeStickAngle = math_angle_2d_deg(0, 0, gPadStickX, -gPadStickY);
    }
    if (dist > 1500 && gGlobeStickCentred && gGlobeStickLevel >= 0 && gGlobeNameAlpha == 0xFF && gGlobeState == 1 && yoshiState == 1) {
        gGlobeStickCentred = 0;
        gGlobeFromLevel = gGlobeSelLevel;
        fe_globe_select_level(gGlobeStickLevel);
        gFlightState = 1;
        gRouteRevealMin = 1.0f;
        if (gRouteRevealSound) {
            snd_stop(gRouteRevealSound);
        }
    }
    switch (gGlobeState) {
        case 0:
            if (gGlobeOverlayAlpha + 10 >= 0x100) {
                gGlobeOverlayAlpha = 0xFF;
            } else {
                gGlobeOverlayAlpha += 10;
            }
            gGlobeCamDist -= 200.0f;
            if (gGlobeCamDist <= 925.0) {
                gGlobeState = 1;
                gGlobeCamDist = 925.0f;
                gGlobeZoomDoneTime = gScheduler.frameCount;
            }
            break;
        case 2:
            gGlobeCamDist -= 50.0f;
            if (gGlobeOverlayAlpha - 25 < 0) {
                gGlobeOverlayAlpha = 0;
            } else {
                gGlobeOverlayAlpha -= 25;
            }
            if (gGlobeNameAlpha - 64 < 0) {
                gGlobeNameAlpha = 0;
            } else {
                gGlobeNameAlpha -= 64;
            }
            if (gGlobeCamDist <= 300.0) {
                gNextGameMode = seq_get_loop_done(levelno);
            }
            break;
        default:
            gGlobeOverlayAlpha = 0xFF;
            if (gFlightState == 0) {
                if (gGlobeNameAlpha + 32 >= 0x100) {
                    gGlobeNameAlpha = 0xFF;
                } else {
                    gGlobeNameAlpha += 32;
                }
            }
            break;
    }
    if (gGlobePlanet != 3) {
        gGlobeOverlayAlpha = 0;
    }
    ox = gGlobeCamX;
    oy = gGlobeCamY;
    fe_globe_latlon_to_xyz(&gGlobeCamLat, &gGlobeCamLon, &gGlobeCamX, &gGlobeCamY, &gGlobeCamZ, 925.0f);
    gGlobeLonStep = gGlobeCamX - ox;
    gGlobeLatStep = gGlobeCamY - oy;
    gGlobeTargetLon = gGlobeLevels[gGlobeSelLevel].lon;
    gGlobeTargetLat = gGlobeLevels[gGlobeSelLevel].lat;
    gGlobeLonStep = fe_globe_angle_diff(gGlobeTargetLon, gGlobeCamLon, 180.0f) * MAX2(dist >> 2, 500) / 10000.0f;
    gGlobeLatStep = fe_globe_angle_diff(gGlobeTargetLat, gGlobeCamLat, 180.0f) * MAX2(dist >> 2, 500) / 10000.0f;
    gGlobeCamLon -= gGlobeLonStep;
    gGlobeCamLat -= gGlobeLatStep;
    gGlobeRoutesDl = fe_globe_build_scene_dl(dyn->gfx, dyn, &gFrameDlLen);
    gGlobePlaneDl = fe_globe_build_routes_dl(gGlobeRoutesDl, dyn, &gGlobeRoutesDlLen);
    fe_globe_build_plane_dl(gGlobePlaneDl, dyn, &gGlobePlaneDlLen);
    fe_globe_scroll_stars();
    for (i = 0; i < nextdma; i++) {
        osRecvMesg(&textureDmaMessageQ, NULL, OS_MESG_BLOCK);
    }
    for (i = 0; i < nextdma - no_palette_dmas; i++) {
        tex_decode_next();
    }
}

/* Stars, the solar system, the level name and vehicle icons (0x801F9258).
 * Level-select info screen: level name and the level's collectable icons. */
Gfx *fe_globe_build_scene_dl(Gfx *arg0, Dynamic *dyn, s32 *count) {
    Gfx *gdl = arg0;
    s32 padC0;
    s32 i;
    s32 alt;
    s32 y;
    s32 padA0[5];
    s32 size;

    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(dyn));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gSPDisplayList(gdl++, seg1RspInitDl);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetFillColor(gdl++, 0x10001);
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    gDPPipeSync(gdl++);
    gDPPipelineMode(gdl++, G_PM_1PRIMITIVE);
    gDPSetColorDither(gdl++, 0x80);
    gdl = fe_globe_draw_stars(gdl, dyn);
    gdl = fe_solar_draw(gdl, (u8 *) dyn);
    text_begin_frame();
    {
        u32 h;
        s32 n;
        LevelInfo *info;
        s32 x;
        s32 w;

        size = 0x18;
        h = gGlobeNameAlpha / 9;
        text_print_gradient((Gfx * N64P *) dyn, (u8 *) (gGlobeLevels[gGlobeShownLevel].name), gGlobeLevels[gGlobeShownLevel].jname, 0, 0xA0, 0, (0x1C - h) / 2 + 0x12,
                      size, h, 1, 0xFF, 0xFF, 0xFF, gGlobeNameAlpha, 0, 0, 0xFF, gGlobeNameAlpha);
        gdl = hd_debug_dl_stub(gdl, (DynamicBuf *) dyn, &n);
        text_flush_screen(&gdl, (Mtx *) dyn);
        info = &gLevelInfo[gGlobeShownLevel];
        gdl = spr_begin(gdl);
        y = 0xDA;
        for (i = 0, alt = 0; i < 0x13 && y > 0x28; i++) {
            if ((info->vehicles & (1 << i)) && (info->kind == LEVEL_KIND_MISSION || (gGlobePlayer->vehiclesUnlocked & (1 << i))) &&
                gVehicleIcons[i * 2] != 0) {
                if (alt) {
                    gdl = spr_draw(gdl, gVehicleIconTex + i, 0x16 - (0xFF - gGlobeNameAlpha) / 6, y, gGlobeNameAlpha, 0, 0.8125f);
                } else {
                    gdl = spr_draw(gdl, gVehicleIconTex + i, (0xFF - gGlobeNameAlpha) / 6 + 0xF6, y -= 0x2C, gGlobeNameAlpha, 0,
                                        0.8125f);
                }
                alt ^= 1;
            }
        }
        x = font_center_x(0xA0, size, (u8 *) (gGlobeLevels[gGlobeShownLevel].name), (s32) (gGlobeLevels[gGlobeShownLevel].jname));
        w = (s32) (size * gFontAdvance[0]) * font_strlen((u8 *) (gGlobeLevels[gGlobeShownLevel].name));
    }
    gdl = spr_end(gdl);
    gSPEndDisplayList(gdl++);
    *count = gdl - arg0;
    return gdl;
}

/* The globe camera and the route lines (0x801F9820).
 * Level-select globe: frame setup, camera matrices, then the globe itself. */
Gfx *fe_globe_build_routes_dl(Gfx *arg0, Dynamic *dyn, s32 *count) {
    Gfx *gdl = arg0;

    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(dyn));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RspInitDl);
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPSetDepthImage(gdl++, gDepthBufferPhys);
    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, 0x00507048, 0);
    gImmp1(gdl++, G_RDPHALF_1, gPerspNorm);
    gSPMatrix(gdl++, &dyn->persp, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->view, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gSolarNodeMtx[gBufferIndex + 12], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gSolarNodeMtx[gBufferIndex + 14], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->translate, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gdl = fe_globe_draw_routes(gdl, dyn, gGlobeStickAngle, &gGlobeStickLevel);
    gSPEndDisplayList(gdl++);
    *count = gdl - arg0;
    return gdl;
}

/* The flying marker (debug angd/dmm/mmm tweaks) (0x801F9B84).
 * Level-select globe in close-up (route view), with a debug camera tweak on the controller. */
Gfx *fe_globe_build_plane_dl(Gfx *arg0, Dynamic *dyn, s32 *count) {
    Gfx *gdl = arg0;

    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(dyn));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RspInitDl);
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPSetDepthImage(gdl++, gDepthBufferPhys);
    gDPSetRenderMode(gdl++, 0x00504240, 0);
    gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gGlobeOverlayAlpha);
    gDPSetCombine(gdl++, 0xFF97FF, 0xFF2CFE7F);
    gImmp1(gdl++, G_RDPHALF_1, gPerspNorm);
    gSPMatrix(gdl++, &dyn->persp, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->view, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gSolarNodeMtx[gBufferIndex + 12], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gSolarNodeMtx[gBufferIndex + 14], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->translate, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gdl = fe_globe_draw_plane(dyn, gdl, gGlobeFromLevel, gGlobeSelLevel);
    fe_globe_set_light();
    gdl = fade_draw((s32) &dyn[gBufferIndex], gdl);
    gDPFullSync(gdl++);
    gSPEndDisplayList(gdl++);
    if (gDebugRamEeprom) {
        if (gContButtons & U_JPAD) {
            angd += 0.01;
        }
        if (gContButtons & D_JPAD) {
            angd -= 0.01;
        }
        if (gContButtons & (U_JPAD | D_JPAD)) {
            debug_printf("angd %f\n", angd);
        }
        if (gContButtons & R_JPAD) {
            dmm += 0.001;
        }
        if (gContButtons & L_JPAD) {
            dmm -= 0.001;
        }
        if (gContButtons & (L_JPAD | R_JPAD)) {
            debug_printf("dmm %f\n", dmm);
        }
        if (gContButtons & R_TRIG) {
            mmm += 100.0f;
        }
        if (gContButtons & L_TRIG) {
            mmm -= 100.0f;
        }
        if (gContButtons & (L_TRIG | R_TRIG)) {
            debug_printf("mmm %f\n", mmm);
        }
    }
    *count = gdl - arg0;
    return gdl;
}

/* The routes between the levels (0x801FA180).
 * Draw the routes between open levels; pick the route marker nearest the globe's facing longitude. */
Gfx *fe_globe_draw_routes(Gfx *arg0, Dynamic *dyn, f32 lon0, s8 *selected) {
    GlobeLevel *e;
    Gfx *gdl = arg0;
    f32 lon;
    f32 best;
    s32 i;
    s32 j;
    s8 out;
#ifdef NON_MATCHING
    s8 sel = 0; /* only stored when best < 45, which some entry setting sel guarantees */
#else
    s8 sel;
#endif

    best = 180.0f;
    gRouteCount = 0;
    gDPPipeSync(gdl++);
    for (i = 0; i < 60; i++) {
        gRouteDrawn[i] = 0;
    }
    for (i = 0; i < 60; i++) {
        e = &gGlobeLevels[i];
        if ((players[playerNumber].medal[i] > 0 && players[playerNumber].medal[i] < 6) ? 1 : 0) {
            for (j = 0; j < 8 && e->routes[j] != -1; j++) {
                gdl = fe_globe_draw_route(dyn, gdl, i, e->routes[j], &out, &lon, 0, 0, 0, 0, 0, 0, 0);
            }
        }
    }
    for (i = 0; i < 60; i++) {
        gRouteDrawn[i] = 0;
    }
    for (i = 0; i < 60; i++) {
        e = &gGlobeLevels[i];
        if ((players[playerNumber].medal[i] > 0 && players[playerNumber].medal[i] < 6) ? 1 : 0) {
            for (j = 0; j < 4 && e->commRoutes[j] != -1; j++) {
                if (players[playerNumber].commPts[i] & (1 << j)) {
                    gdl = fe_globe_draw_route(dyn, gdl, i, e->commRoutes[j], &out, &lon, 1, 0, 0xFF, 0, 0xFF, 0xFF, 0);
                    if (out != -1) {
                        f32 d;

                        d = fe_globe_angle_diff(lon0, lon, 180.0f);
                        d = (d > 0.0f) ? d : -d;
                        if (d < best) {
                            best = d;
                            sel = out;
                        }
                    }
                }
            }
        }
    }
    for (i = 0; i < 60; i++) {
        e = &gGlobeLevels[i];
        if ((players[playerNumber].medal[i] > 0 && players[playerNumber].medal[i] < 6) ? 1 : 0) {
            for (j = 0; j < 8 && e->routes[j] != -1; j++) {
                gdl = fe_globe_draw_route(dyn, gdl, i, e->routes[j], &out, &lon, 1, 0xFF, 0, 0, 0xFF, 0x80, 0x80);
                if (out != -1) {
                    f32 d;

                    d = fe_globe_angle_diff(lon0, lon, 180.0f);
                    d = (d > 0.0f) ? d : -d;
                    if (d < best) {
                        best = d;
                        sel = out;
                    }
                }
            }
        }
    }
    osWritebackDCache((void *) N64_IPTR(gRouteVtxBufs[gBufferIndex]), 0x17000);
    if (best < 45.0f) {
        *selected = sel;
    } else {
        *selected = -1;
    }
    return gdl;
}

/*
 * fe_globe_draw_route (1947 insns) draws one route between two levels: an arc of line segments
 * over the globe (gSPVertex + gSPLineW3D), coloured from (r0,g0,b0) to (r1,g1,b1), brightened
 * towards the camera, with the route-open fade timed by gRouteRevealMin and a sound on completion.
 * Matched (Oct 2026). The levers, besides the earlier ones (the MAX with gRouteRevealMin is
 * "(AB28 > e) ? AB28 : e", the colour factors are (f32) u8, *lon gets an int ternary, the acos
 * sign goes first as in fe_globe_draw_plane):
 *   - `switch (gGlobePlanet) { case 3: ... break; default: ... }`: the case label stops ugen
 *     caching x/y in FP registers across the hud_project_point call (1953 -> 1947 insns);
 *   - MAX2R (0.0 / 0.001 tested first) for bright and for the curve's falloff;
 *   - the colour sums written (0.5 - |t - 0.5|) * c1 + |t - 0.5| * c0: IDO hoists the FABS
 *     ternaries left to right, so this decides which of them lands in $f22 and which in $f20;
 *   - `gdl = arg1;` before the other initialisers (the $t2/$t5 pair at the top).
 * IDO -O1 hands out FP temporaries from a queue that carries over from the end of the function to
 * its start, so one wrong spelling anywhere renumbers the FP registers everywhere. Two compiles
 * with the same allocation pattern differ only by a consistent renaming of $f4..$f18; the first
 * line where no single renaming fits marks the real divergence (scratchpad match100_sigma.py).
 * All of these are exact rewrites (commutative sums and products, and ternaries that pick the
 * same operand for NaN), so the port computes the same values.
 */
/* gSolarNodeMtx seen as a struct (the same address) */
typedef struct {
    Mtx pad[12];
    Mtx view[2];
    Mtx rot[2];
} GlobeMtx;
#define GLOBE_MTX (*(GlobeMtx *) gSolarNodeMtx)

#define FABS(x) ((x) > 0 ? (x) : -(x))
#define MAX2R(a, b) ((b) > (a) ? (b) : (a))

/* One great-circle arc between two levels (0x801FA74C).
 * One route between two levels: an arc of line segments over the globe, coloured r0..r1 along it. */
Gfx *fe_globe_draw_route(Dynamic *dyn, Gfx *arg1, u8 from, u8 to, s8 *out, f32 *lon, u8 curved, u8 r0, u8 g0, u8 b0,
                   u8 r1, u8 g1, u8 b1) {
    Vtx *v;
    Vtx *vbase;
    Gfx *gdl;
    s32 i;
    s32 n;
    s32 steps;
    u8 unk117;
    u8 found;
    f32 progress;
    GlobeLevel *e1;
    GlobeLevel *e2;
    f32 x;
    f32 y;
    f32 z;
    f32 dist;
    s16 sx1;
    s16 sy1;
    s16 sx0;
    s16 sy0;
    f32 x0;
    f32 x1;
    f32 y0;
    f32 y1;
    f32 z0;
    f32 z1;
    f32 a;
    f32 b;
    f32 cosang;
    f32 angle;
    f32 t;
    f32 scale;
    f32 bright;
    u8 loaded;
    u8 lit;
    u32 extra;

    gdl = arg1;
    unk117 = 1;
    lit = 0;
    scale = 1.0f;
    if (to == gGlobeSelLevel || players[playerNumber].medal[from] == 0) {
        u8 tmp;

        tmp = to;
        to = from;
        from = tmp;
    }
    if (gRouteDrawn[from] & ((u64) 1 << to)) {
        found = 1;
    } else {
        found = 0;
    }
    gRouteDrawn[from] |= (u64) 1 << to;
    gRouteDrawn[to] |= (u64) 1 << from;
    *out = -1;
    if (found || fe_globe_level_locked(to)) {
        return arg1;
    }
    if (lvl_planet(from) == 3 || lvl_planet(to) == 3) {
        lit = 1;
    }
    if (from == gGlobeSelLevel) {
        *out = to;
    }
    if (lvl_planet(from) == 3 && lvl_planet(to) == 3) {
        if (gRouteRevealFlags &&
            (players[playerNumber].medal[to] == 0 ||
             ((gRouteRevealFlags & 1) && from == gGlobeSelLevel &&
              !((players[playerNumber].medal[to] > 0 && players[playerNumber].medal[to] < 6) ? 1 : 0)))) {
            if (gGlobeState) {
                progress = MIN2(1.0, (gRouteRevealMin > (f32) (gScheduler.frameCount - gGlobeZoomDoneTime) * 60.0 / 60.0 / 90.0) ? gRouteRevealMin : (f32) (gScheduler.frameCount - gGlobeZoomDoneTime) * 60.0 / 60.0 / 90.0);
                if (progress == 1.0 && gRouteRevealSound) {
                    snd_stop(gRouteRevealSound);
                } else if (!gRouteRevealSound && progress != 1.0 && gGlobePlanet == 3) {
                    snd_play(gSfxBank, SFX_ROUTE_REVEAL_LOOP, &gRouteRevealSound);
                }
            } else {
                progress = 0.0f;
            }
        } else {
            progress = 1.0f;
        }
    } else {
        progress = 1.0f;
    }
    e1 = &gGlobeLevels[from];
    e2 = &gGlobeLevels[to];
    x0 = e1->x;
    y0 = e1->y;
    z0 = e1->z;
    x1 = e2->x;
    y1 = e2->y;
    z1 = e2->z;
    dist = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
    steps = MAX2(MIN2(dist / 32.0, 15.0), 3.0);
    n = steps + 1;
    v = vbase = (Vtx *) N64_IPTR(gRouteVtxBufs[gBufferIndex]) + gRouteCount * 16;
    cosang = (x0 * x1 + y0 * y1 + z0 * z1) / 250.0 / 250.0;
    angle = 90.0 - ((cosang >= 0.0f) ? 1 : -1) * math_asin_c(((cosang > 0.0f) ? cosang : -cosang) * 65535.0) /
                       16.0 / 11.377777;
    if (angle >= 180.0) {
        angle -= 180.0;
    }
    if (angle < -180.0) {
        angle += 180.0;
    }
    angle *= 0.017453292519943295;
    loaded = 0;
    for (i = 0; i < n; i++, v++) {
        t = MIN2(progress, 1.0 / (n - 1) * (f32) i);
        a = hd_sinf((1.0 - t) * angle) / hd_sinf(angle);
        b = hd_sinf(t * angle) / hd_sinf(angle);
        if (curved) {
            scale = hd_sinf(t * 3.141592653) * steps / 64.0 + 1.0;
        }
        x = (a * x0 + b * x1) * scale;
        y = (a * y0 + b * y1) * scale;
        z = (a * z0 + b * z1) * scale;
        if (*out != -1 && i < 2) {
            switch (gGlobePlanet) {
                case 3:
                    if (i != 0) {
                        hud_project_point(dyn, x, y, z, &sx1, &sy1, &GLOBE_MTX.view[gBufferIndex],
                                          &GLOBE_MTX.rot[gBufferIndex], &dyn->translate, 1.0f);
                        *lon = math_angle_2d_deg(sx0, sy0, sx1, sy1);
                    } else {
                        hud_project_point(dyn, x, y, z, &sx0, &sy0, &GLOBE_MTX.view[gBufferIndex],
                                          &GLOBE_MTX.rot[gBufferIndex], &dyn->translate, 1.0f);
                    }
                    break;
                default:
                    if (i != 0) {
                        *lon = (from < to) ? 0 : 180;
                    }
            }
        }
        v->v.ob[0] = x;
        v->v.ob[1] = y;
        v->v.ob[2] = z;
        bright = MAX2R((x * gGlobeCamX + y * gGlobeCamY + z * gGlobeCamZ) / scale / 250000.0, 0.0);
        if (curved) {
            extra = 1.0 / MAX2R(FABS(bright - angd), 0.001) * dmm * dmm * mmm;
        } else {
            extra = 0;
        }
        v->v.cn[0] = MIN2(255.0, ((0.5 - FABS(t - 0.5)) * (f32) r1 + FABS(t - 0.5) * (f32) r0) * 2.0 + extra);
        v->v.cn[1] = MIN2(255.0, ((0.5 - FABS(t - 0.5)) * (f32) g1 + FABS(t - 0.5) * (f32) g0) * 2.0 * bright + extra);
        v->v.cn[2] = MIN2(255.0, ((0.5 - FABS(t - 0.5)) * (f32) b1 + FABS(t - 0.5) * (f32) b0) * 2.0 * (1.0 - bright) + extra);
        v->v.cn[3] = gGlobeOverlayAlpha * bright / (2 - curved);
        if (i != 0 && !found && lit && bright > 0.0f) {
            if (!loaded) {
                gSPVertex(gdl++, vbase, n, 0);
                loaded = 1;
            }
            gSPLineW3D(gdl++, i - 1, i, 2.0 - MIN2(2.0, gGlobeCamDist / 1000.0f / (1.0 + bright / 2.0f)), 0);
        }
    }
    gRouteCount++;
    return gdl;
}

/* The marker flying between two levels (0x801FC5B8).
 * The plane icon flying along the great circle between two levels. */
Gfx *fe_globe_draw_plane(Dynamic *dyn, Gfx *arg1, u8 from, u8 to) {
    f32 a;
    f32 b;
    Gfx *gdl;
    Vtx *v;
    GlobeLevel *e1;
    GlobeLevel *e2;

    gdl = arg1;
    v = gPlaneVtx[gBufferIndex];
    switch (gFlightState) {
        case 0:
            break;
        case 1:
            e1 = &gGlobeLevels[from];
            e2 = &gGlobeLevels[to];
            gFlightFromX = e1->x;
            gFlightFromY = e1->y;
            gFlightFromZ = e1->z;
            gFlightToX = e2->x;
            gFlightToY = e2->y;
            gFlightToZ = e2->z;
            gFlightDist = sqrtf((gFlightToX - gFlightFromX) * (gFlightToX - gFlightFromX) +
                               (gFlightToY - gFlightFromY) * (gFlightToY - gFlightFromY) +
                               (gFlightToZ - gFlightFromZ) * (gFlightToZ - gFlightFromZ));
            gFlightSteps = MAX2(MIN2(gFlightDist / 32.0, 15.0), 3.0);
            if (from != to) {
                gFlightState = 2;
            } else {
                gFlightState = 0;
            }
            gFlightProgress = 0.0f;
            gFlightCos = (gFlightFromX * gFlightToX + gFlightFromY * gFlightToY + gFlightFromZ * gFlightToZ) / 250.0 / 250.0;
            gFlightAngle = 90.0 - ((gFlightCos >= 0.0f) ? 1 : -1) * math_asin_c(((gFlightCos > 0.0f) ? gFlightCos : -gFlightCos) * 65535.0) / 16.0 / 11.377777;
            if (gFlightAngle >= 180.0) {
                gFlightAngle -= 180.0;
            }
            if (gFlightAngle < -180.0) {
                gFlightAngle += 180.0;
            }
            gFlightAngle *= 0.017453292519943295;
            break;
        case 2:
            gFlightProgress += 0.4 / gFlightSteps;
            if (gFlightProgress > 1.0) {
                gFlightState = 0;
                gFlightProgress = 1.0f;
            }
            gGlobeNameAlpha = ((0.5 - gFlightProgress > 0.0) ? 0.5 - gFlightProgress : -(0.5 - gFlightProgress)) * 510.0;
            if (gFlightProgress >= 0.5) {
                gGlobeShownLevel = gGlobeSelLevel;
            }
            break;
    }
    a = hd_sinf((1.0 - gFlightProgress) * gFlightAngle) / hd_sinf(gFlightAngle);
    b = hd_sinf(gFlightProgress * gFlightAngle) / hd_sinf(gFlightAngle);
    gFlightAltitude = hd_sinf(gFlightProgress * 3.141592653) * gFlightSteps / 64.0 + 1.0;
    gPlaneX = (a * gFlightFromX + b * gFlightToX) * gFlightAltitude;
    gPlaneY = (a * gFlightFromY + b * gFlightToY) * gFlightAltitude;
    gPlaneZ = (a * gFlightFromZ + b * gFlightToZ) * gFlightAltitude;
    fe_globe_quad_at(v, gPlaneX, gPlaneY, gPlaneZ, 0x20, 0x20, 1.75f, 1);
    osWritebackDCache(v, sizeof(Vtx) * 4);
    gDPLoadTextureBlock(gdl++, gLvlselMarkerFrames[gScheduler.frameCount / 3 % 3], G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gdl++, v, 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 2, 3, 0, 0);
    return gdl;
}

/* A flat quad at a level's latitude/longitude (0x801FCE74). */
void fe_globe_quad_at_level(Vtx *v, u8 level, f32 dlat, f32 dlon, u8 w, u8 h, f32 scale, u8 flip) {
    GlobeLevel *info;
    f32 lat;
    f32 lon;
    f32 x;
    f32 y;
    f32 z;

    info = &gGlobeLevels[level];
    lat = info->lat + dlat;
    lon = info->lon + dlon;
    fe_globe_latlon_to_xyz(&lat, &lon, &x, &y, &z, 248.75f);
    fe_globe_quad_at(v, x, y, z, w, h, scale, flip);
}

/* A quad lying flat on the sphere at x, y, z (0x801FCF38).
 * A w x h textured quad lying flat on the globe at (x, y, z), rotated 45 degrees about the normal. */
void fe_globe_quad_at(Vtx *v, f32 x, f32 y, f32 z, u8 w, u8 h, f32 scale, u8 flip) {
    f32 m[4][4];
    f32 ux;
    f32 uy;
    f32 uz;
    f32 vx;
    f32 vy;
    f32 vz;
    f32 len1;
    f32 len2;
    f32 x0;
    f32 x1;
    f32 x2;
    f32 x3;
    f32 y0;
    f32 y1;
    f32 y2;
    f32 y3;
    f32 z0;
    f32 z1;
    f32 z2;
    f32 z3;

#ifdef PORT_HOST
    {
        /* the quads live in fresh heap memory (09570.c's icon buffer, the
         * cursor): their flag halfwords are never written, the N64 keeps the
         * heap's bytes; give the native records those (port/src/load/swap.c) */
        void port_vtx_stale(void *v, u32 n);
        port_vtx_stale(v, 4);
    }
#endif
    ux = -z;
    uy = 0.0f;
    uz = x;
    vx = x * y;
    vy = -(x * x + z * z);
    vz = y * z;
    len1 = sqrtf(ux * ux + uy * uy + uz * uz) / 50.0 * 2.0 / scale;
    if (len1 < 0.1) {
        len1 = 0.1f;
    }
    ux /= len1;
    uy /= len1;
    uz /= len1;
    len2 = sqrtf(vx * vx + vy * vy + vz * vz) / 50.0 * 2.0 / scale;
    if (len2 < 0.1) {
        len2 = 0.1f;
    }
    vx /= len2;
    vy /= len2;
    vz /= len2;
    guRotateF(m, 45.0f, x, y, z);
    x0 = x + ux;
    y0 = y + uy;
    z0 = z + uz;
    x1 = x + vx;
    y1 = y + vy;
    z1 = z + vz;
    x2 = x - ux;
    y2 = y - uy;
    z2 = z - uz;
    x3 = x - vx;
    y3 = y - vy;
    z3 = z - vz;
    guMtxXFMF(m, x0, y0, z0, &x0, &y0, &z0);
    guMtxXFMF(m, x1, y1, z1, &x1, &y1, &z1);
    guMtxXFMF(m, x2, y2, z2, &x2, &y2, &z2);
    guMtxXFMF(m, x3, y3, z3, &x3, &y3, &z3);
    v[0].v.tc[0] = 0;
    v[0].v.tc[1] = (flip ? 0 : h - 1) << 6;
    v[1].v.tc[0] = (w - 1) << 6;
    v[1].v.tc[1] = (flip ? 0 : h - 1) << 6;
    v[2].v.tc[0] = (w - 1) << 6;
    v[2].v.tc[1] = (flip ? h - 1 : 0) << 6;
    v[3].v.tc[0] = 0;
    v[3].v.tc[1] = (flip ? h - 1 : 0) << 6;
    v[0].v.ob[0] = x0;
    v[0].v.ob[1] = y0;
    v[0].v.ob[2] = z0;
    v[1].v.ob[0] = x1;
    v[1].v.ob[1] = y1;
    v[1].v.ob[2] = z1;
    v[2].v.ob[0] = x2;
    v[2].v.ob[1] = y2;
    v[2].v.ob[2] = z2;
    v[3].v.ob[0] = x3;
    v[3].v.ob[1] = y3;
    v[3].v.ob[2] = z3;
}

/* A point on the sphere from latitude/longitude (0x801FD484).
 * Latitude/longitude (degrees, normalised in place) to a point on a sphere of radius r. */
void fe_globe_latlon_to_xyz(f32 *lat, f32 *lon, f32 *x, f32 *y, f32 *z, f32 r) {
    if (*lat >= 90.0 || *lat < -90.0) {
        if (*lat >= 90.0) {
            *lat = 180.0 - *lat;
        } else {
            *lat = -180.0 - *lat;
        }
        *lon = *lon + 180.0;
    }
    if (*lon >= 180.0) {
        *lon = *lon - 360.0;
    } else if (*lon < -180.0) {
        *lon = *lon + 360.0;
    }
    *x = hd_cosf(-*lon * 0.017453292519943295) * hd_cosf(*lat * 0.017453292519943295) * r;
    *y = hd_sinf(*lat * 0.017453292519943295) * r;
    *z = hd_sinf(-*lon * 0.017453292519943295) * hd_cosf(*lat * 0.017453292519943295) * r;
}

/* B - a wrapped into [-range, range) (0x801FD6B8).
 * Wrap the difference b - a into [-range, range). */
f32 fe_globe_angle_diff(f32 a, f32 b, f32 range) {
    f32 d;

    d = b - a;
    if (d >= -range && d < range) {
        return d;
    }
    if (d >= range) {
        return -2.0f * range + d;
    }
    return 2.0f * range + d;
}

#define ABS(x) ((x) > 0 ? (x) : -(x))

/* Moves the starfield (0x801FD748).
 * Scroll the star field with the globe's spin; respawn stars that wrap or pass the camera. */
void fe_globe_scroll_stars(void) {
    s32 i;
    s32 k;
    s32 n;
    s32 nx;
    s32 ny;
    s32 nz;
    s32 ox;
    s32 oy;
    s32 bigx;
    s32 bigy;
    s32 minx;
    s32 miny;
    s32 respawn;
    s32 pad[2];
    Vtx *cur;
    Vtx *prev;

    cur = gDynamicBufs[gBufferIndex].stars;
    prev = gDynamicBufs[gBufferIndex ^ 1].stars;
    for (i = 0; i < 0x80; i++) {
        minx = miny = 0x40000000;
        for (k = 0, bigx = bigy = 0; k < 4; k++) {
            n = i * 4 + k;
            nx = cur[n].v.ob[0] = prev[n].v.ob[0] - (gGlobePlanet == 3 ? gGlobeLonStep * 40.0f : 0.0f);
            ny = cur[n].v.ob[1] = prev[n].v.ob[1] + (gGlobePlanet == 3 ? gGlobeLatStep * 40.0f : 0.0f);
            nz = cur[n].v.ob[2] = prev[n].v.ob[2] - gStarSpeeds[i] * gGlobeCamDist / 925.0;
            if (ABS(nx) > 4000) {
                bigx = nx;
            }
            if (ABS(ny) > 4000) {
                bigy = ny;
            }
            if (ABS(nx) < minx) {
                minx = ABS(nx);
            }
            if (ABS(ny) < miny) {
                miny = ABS(ny);
            }
        }
        respawn = 0;
        if (bigx != 0 || bigy != 0) {
            for (k = 0; k < 4; k++) {
                n = i * 4 + k;
                ox = cur[n].v.ob[0];
                oy = cur[n].v.ob[1];
                if (bigx != 0) {
                    nx = cur[n].v.ob[0] = fe_globe_angle_diff(0.0f, cur[n].v.ob[0], 3890.0f);
                }
                if (bigy != 0) {
                    ny = cur[n].v.ob[1] = fe_globe_angle_diff(0.0f, cur[n].v.ob[1], 3890.0f);
                }
                if (bigx != 0 && ox * nx > 0) {
                    respawn = 1;
                }
                if (bigy != 0 && oy * ny > 0) {
                    respawn = 1;
                }
            }
        }
        if (respawn || nz < 0 || (minx < 231250.0 / gGlobeCamDist && miny < 231250.0 / gGlobeCamDist)) {
            fe_globe_respawn_star(cur, i, 25000);
        }
    }
}

/* Respawns a star at a random place and speed (0x801FDCA4).
 * A randomly placed 0x6D-unit square (4 vertices) at depth z. */
void fe_globe_respawn_star(Vtx *v, s32 idx, s32 z) {
    s32 n;
    s32 x;
    s32 y;

    n = idx * 4;
    gStarSpeeds[idx] = util_rand_range(0x50, 0xC8);
    x = util_rand_range(-4000, 4000);
    y = util_rand_range(-4000, 4000);
    v[n].v.ob[0] = x - 0x37;
    v[n].v.ob[1] = y - 0x37;
    v[n].v.ob[2] = z;
    v[n + 1].v.ob[0] = x - 0x37;
    v[n + 1].v.ob[1] = y + 0x36;
    v[n + 1].v.ob[2] = z;
    v[n + 2].v.ob[0] = x + 0x36;
    v[n + 2].v.ob[1] = y + 0x36;
    v[n + 2].v.ob[2] = z;
    v[n + 3].v.ob[0] = x + 0x36;
    v[n + 3].v.ob[1] = y - 0x37;
    v[n + 3].v.ob[2] = z;
}

/* The vehicle icon textures (0x801FDE50). */
void fe_load_vehicle_icons(void) {
    gVehicleIconTex = spr_load(gVehicleIcons, 0, 0x13, 2, 1, 1.0f);
}

/* The light toward the marker (0x801FDE98). */
void fe_globe_set_light(void) {
    f32 len;
    f32 x;
    f32 y;
    f32 z;

    len = sqrtf(gPlaneX * gPlaneX + gPlaneY * gPlaneY + gPlaneZ * gPlaneZ);
    if (len < 1.0) {
        len = 1.0f;
    }
    x = gPlaneX / len * 120.0f;
    y = gPlaneY / len * 120.0f;
    z = gPlaneZ / len * 120.0f;
    gGlobeLights[gBufferIndex].l[0].l.dir[0] = x;
    gGlobeLights[gBufferIndex].l[0].l.dir[1] = y;
    gGlobeLights[gBufferIndex].l[0].l.dir[2] = z;
    osWritebackDCache(&gGlobeLights[gBufferIndex], sizeof(gGlobeLights));
}

/* Opens the levels reachable from the done ones (0x801FE018).
 * Open every level reachable from a completed one (no prototype in scope for fe_globe_level_locked). */
void fe_open_reachable_levels(u8 rank) {
    s32 i;
    s32 j;
    GlobeLevel *e;
    LevelInfo *info;

    gGlobePlayer = &players[playerNumber];
    for (i = 0; i < 60; i++) {
        if ((players[playerNumber].medal[i] > 0 && players[playerNumber].medal[i] < 6) ? 1 : 0) {
            e = &gGlobeLevels[i];
            info = &gLevelInfo[i];
            for (j = 0; j < 8 && e->routes[j] != -1; j++) {
                if (gGlobePlayer->medal[e->routes[j]] == 0 && fe_globe_level_locked(e->routes[j]) == 0) {
                    gGlobePlayer->medal[e->routes[j]] = rank;
                }
            }
            for (j = 0; j < 4 && e->commRoutes[j] != -1; j++) {
                if (gGlobePlayer->commPts[i] & (1 << j)) {
                    if (gGlobePlayer->medal[e->commRoutes[j]] == 0) {
                        gGlobePlayer->medal[e->commRoutes[j]] = rank;
                    }
                }
            }
        }
    }
}

/* The Earth node: the globe mesh and the icon lists (0x801FE238). */
Gfx *fe_globe_draw_earth(Gfx *arg0, s32 arg1) {
    Gfx *gdl = arg0;

    gDPSetTextureLOD(gdl++, G_TL_LOD);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetRenderMode(gdl++, 0x0C182048, 0);
    if (gGlobePlanet == 3) {
        gSPSetLights1(gdl++, gGlobeLights[gBufferIndex]);
        gSPSetGeometryMode(gdl++, G_LIGHTING | G_CULL_BACK | G_SHADING_SMOOTH | G_SHADE);
        gDPSetCombine(gdl++, 0x26A024, 0x1FFC93FC);
    } else {
        gSPSetGeometryMode(gdl++, G_LIGHTING | G_CULL_BACK | G_SHADING_SMOOTH | G_SHADE);
        gDPSetCombine(gdl++, 0x26A1FF, 0x1FFC927C);
    }
    gSPDisplayList(gdl++, gGlobeMeshDl);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_LIGHTING);
    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gGlobeOverlayAlpha);
    gDPSetEnvColor(gdl++, 0, 0, 0, gGlobeOverlayAlpha * 2 / 3);
    gSPDisplayList(gdl++, gGlobeLevelIconsDl);
    gSPDisplayList(gdl++, gGlobeAcademyIconsDl);
    gSPEndDisplayList(gdl++);
    return gdl;
}

/* The starfield display list (0x801FE5D0). */
Gfx *fe_globe_draw_stars(Gfx *arg0, Dynamic *dyn) {
    Gfx *gdl = arg0;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetRenderMode(gdl++, 0x0F0A7008, 0);
    gDPSetCombine(gdl++, 0xFFFFFF, 0xFFFCF279);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    PORT_WTAG(gdl, PORT_WTAG_STARS, 0, 0); /* bc.exe's widescreen: spread across the picture */
    gSPDisplayList(gdl++, gGlobeStarsDl);
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    gDPPipeSync(gdl++);
    return gdl;
}

/* Whether a level stays locked (needs more progress, or a prerequisite level unfinished).
 * K&R in the matching build, where the unused second parameter is the
 * scratch the checks below reuse (no caller passes it). */
#ifdef NON_MATCHING
/* Progress needed, or a prerequisite level not done (0x801FE760). */
s32 fe_globe_level_locked(u8 level) {
    s32 arg1;
    u8 locked = 0;
#else
s32 fe_globe_level_locked(level, arg1)
    u8 level;
    s32 arg1;
{
    u8 locked = 0;
#endif

    if (gLevelInfo[level].gameState > players[playerNumber].gameState &&
        ((gLevelInfo[level].kind & LEVEL_KIND_ANY_MISSION) || (level >= 0x2B && level < 0x2F))) {
        locked = 1;
    }
    if (level == 0xA) {
        arg1 = (players[playerNumber].medal[0x37] > 0 && players[playerNumber].medal[0x37] < 6) ? 1 : 0;
        if (!arg1) {
            locked = 1;
        }
    }
    if (level == 0xF) {
        arg1 = (players[playerNumber].medal[0x1C] > 0 && players[playerNumber].medal[0x1C] < 6) ? 1 : 0;
        if (!arg1) {
            locked = 1;
        }
    }
    if (level == 0x3A) {
        arg1 = (players[playerNumber].medal[0x35] > 0 && players[playerNumber].medal[0x35] < 6) ? 1 : 0;
        if (!arg1) {
            locked = 1;
        }
    }
    if (level == 5) {
        arg1 = (players[playerNumber].medal[0x07] > 0 && players[playerNumber].medal[0x07] < 6) ? 1 : 0;
        if (!arg1) {
            locked = 1;
        }
    }
    if (level == 0x10) {
        arg1 = (players[playerNumber].medal[0x13] > 0 && players[playerNumber].medal[0x13] < 6) ? 1 : 0;
        if (!arg1) {
            locked = 1;
        }
    }
    if (level == 0) {
        locked = 0;
    }
    return locked;
}
