/* Boot, frame accounting, RDRAM dumps and the front-end overlay reload. */
#include "plat.h"
#include "../audio/port_audio.h"
#include "game/port.h"

PlatConfig plat_cfg;
PlatStats plat_stats;

/* game state shown in traces and crash reports (raw N64 addresses) */
#define G_MODE (*(u64 *) 0x80364A90)      /* game mode (master switch) */
#define G_NEXTMODE (*(u64 *) 0x80364A98)
#define G_LEVEL (*(s32 *) 0x802E8BDC)     /* levelno */
#define G_MODEFRAMES (*(u32 *) 0x80358060) /* frames in this mode */
#define G_VICOUNT (*(u32 *) 0x803156C4)   /* the game's retrace counter */

/* the boot entry in hd_code (00000.c); init.us.v11's job is done natively */
extern void hd_boot(void);

/* ---- front-end overlay ------------------------------------------------------ */
/* The front end (0x801E7000-0x8021ED00) is inflated from the ROM whenever the
 * game returns to the menus (46C20.c hd_load_front_end); its .data and .bss come
 * back to their initial state each time, because the heap overwrote them.
 * Natively its code is in the exe; its data lives in RDRAM.  The initial
 * data image (ROM bytes after the load-time swap, plus the native
 * initialisers, plus zeroed .bss) is snapshotted at boot and put back after
 * every reload (the game's inflate has just written big-endian bytes over
 * it). */
#define FE_DATA_START 0x80208040u
#define FE_END 0x8021ED00u
static u8 *g_fe_snap;
static void *g_fe_units;   /* their units for the stale-byte emulation (load/swap.c) */
void *port_unit_save(unsigned int addr, unsigned int n);
void port_unit_restore(unsigned int addr, const void *saved, unsigned int n);

void plat_fe_snapshot(void) {
    u32 n = FE_END - FE_DATA_START;
    g_fe_snap = host_realloc(NULL, n);
    PORT_MEMCPY(g_fe_snap, (void *) FE_DATA_START, n);
    g_fe_units = port_unit_save(FE_DATA_START, n);
}

void port_fe_loaded(void) {
    plat_stats.fe_reloads++;
    PORT_MEMCPY((void *) FE_DATA_START, g_fe_snap, FE_END - FE_DATA_START);
    port_unit_restore(FE_DATA_START, g_fe_units, FE_END - FE_DATA_START);
    if (!plat_cfg.quiet)
        host_log("fe: front end reloaded (#%u) at frame %u, vi %u\n", (unsigned) plat_stats.fe_reloads,
                 (unsigned) plat_stats.frames, (unsigned) plat_vi_count);
}

/* ---- frame interpolation tags (port.h PORT_ITAG) ----------------------------- */
/* bc.exe's tagged interpolation records them (live_interp.cpp); nothing else
 * does, and nothing goes back to the game. */
void (*host_itag_hook)(unsigned addr, int kind, int index);
void (*host_camera_hook)(int snap);
short *(*host_draw_cells_hook)(short *game);
void (*host_input_hook)(unsigned short *button, signed char *x, signed char *y);

/* bc.exe's level viewer records them too while it captures a frame (live_viewer.cpp) */
void (*host_viewer_itag_hook)(unsigned addr, int kind, int index);

void port_itag(const void *gdl, int kind, int index) {
    /* PORT_ICUT_IF: every frame, at the end of the game's camera (index: the snap flag) */
    if (kind == 30 && host_camera_hook != NULL) host_camera_hook(index);
    if (host_itag_hook != NULL && (kind != 30 || index != 0)) host_itag_hook((unsigned) (size_t) gdl, kind, index);
    if (host_viewer_itag_hook != NULL && kind < 30) host_viewer_itag_hook((unsigned) (size_t) gdl, kind, index);
}

/* ---- widescreen tags (port.h PORT_WTAG) ------------------------------------- */
/* bc.exe's widescreen records them (live_wide.cpp); nothing goes back to the game. */
void (*host_wtag_hook)(unsigned addr, int kind, int x0, int x1);

void port_wtag(const void *gdl, int kind, int x0, int x1) {
    if (host_wtag_hook != NULL) host_wtag_hook((unsigned) (size_t) gdl, kind, x0, x1);
}

/* PORT_THROTTLE (45BB0.c cont_update): bc.exe's analog throttle (NULL otherwise: nothing changes) */
void (*host_throttle_hook)(int rawY, unsigned short *button, signed char *y);
void (*host_throttle_input_hook)(unsigned short *button, signed char *x, signed char *y, int trigger);
int host_live_trigger = -1;

void port_throttle(int rawY, unsigned short *button, signed char *y) {
    if (host_throttle_hook != NULL) host_throttle_hook(rawY, button, y);
}

/* PORT_DRAW_CELLS: the visible-cell list the drawing paths use */
short *port_draw_cells(short *game) {
    return host_draw_cells_hook != NULL ? host_draw_cells_hook(game) : game;
}

/* ---- bc.exe's level viewer (port.h PORT_VIEWER_FRAME) -------------------------
 * The viewer (live_viewer.cpp) asks for a level; at the end of a frame of
 * hd_main_thread's frame loop, when the game is in a level or the attract demo
 * (no front end loaded: hd_setup_level would destroy the save thread), not
 * fading and not already switching mode, the request becomes the game's own:
 * levelno, gDiggerVehicle (bonus levels: the vehicle a type-1 spawn stands
 * for) and gNextGameMode = MODE_LEVEL_START, so the mode switch runs the
 * level's set-up as the globe's start would.  A finished mission would be set
 * up with the status block the front end loaded last (pakBuffer: another
 * level's), so its medal reads "not done" for that one switch (put back at the
 * next frame's end).  Nothing happens unless the viewer asked: with it never
 * used, the game runs exactly as without it. */
extern s32 levelno;
extern u64 gNextGameMode;
extern u8 frontEndPresent;
extern u8 gDiggerVehicle;
extern u8 playerNumber;
extern u8 players[][0x100];
extern u8 gLevelInfo[][0x44];
s32 areWeFading(void);
static int g_view_state, g_view_level, g_view_vehicle;
static int g_view_medal_player = -1, g_view_medal_level;
static u8 g_view_medal;

void host_viewer_request(int level, int vehicle) {
    g_view_level = level;
    g_view_vehicle = vehicle;
    g_view_state = level >= 0 && level < 60 ? 1 : 0;
}

int host_viewer_state(void) {
    return g_view_state;
}

void host_viewer_done(void) {
    g_view_state = 0;
}

void port_viewer_frame(void) {
    if (g_view_medal_player >= 0) {
        players[g_view_medal_player][0x18 + g_view_medal_level] = g_view_medal;
        g_view_medal_player = -1;
    }
    if (g_view_state != 1 || frontEndPresent || gNextGameMode != 0 || areWeFading()) return;
    levelno = g_view_level;
    if (gLevelInfo[levelno][0] != 1) gDiggerVehicle = (u8) g_view_vehicle;
    if (playerNumber < 4) {
        u8 *medal = &players[playerNumber][0x18 + levelno];
        if (*medal > 0 && *medal < 6) {
            g_view_medal_player = playerNumber;
            g_view_medal_level = levelno;
            g_view_medal = *medal;
            *medal = 0;
        }
    }
    gNextGameMode = 0x2000; /* MODE_LEVEL_START */
    g_view_state = 2;
    host_log("viewer: level %d asked for at frame %u (vehicle %d)\n", g_view_level, (unsigned) plat_stats.frames,
             g_view_vehicle);
}

/* bc.exe's orbit camera: the commands and vertex values 2D2D810.c
 * backdrop_draw would write for the backdrop with the camera heading and
 * pitch given (the game's gCamYaw / gCamPitch units), into host memory:
 * `out` (at most `max` Gfx), and `vtx[i]` = {ob[1], tc[0], tc[1]} of the 4
 * vertices of gBackdropVtx[0].  Nothing in RDRAM is written; the game's own
 * state (gBackdropScrollS ..., the vertex buffer index) isn't touched.  Returns the
 * number of commands, 0 when the backdrop would not be drawn. */
typedef struct {
    s16 id;
    u32 ptr;
    u8 sShift, tShift, cms, cmt;
} PortFadeEntry; /* 2D810.c FadeEntry */
int port_backdrop_gfx(void *outv, int max, unsigned dyn, int level, int heading, int pitch, short vtx[4][3]) {
    Gfx *gdl = (Gfx *) outv;
    Gfx *start = gdl;
    const u16 *fmts = (const u16 *) (size_t) 0x802FA8A0u;
    const PortFadeEntry *tab = (const PortFadeEntry *) (size_t) (0x802FA280u + (u32) level * 0x18u);
    const PortFadeEntry *entry;
    const f32 speed = *(const f32 *) (size_t) 0x8036BFC0u;
    f32 scroll, cc, d0, c8, s0, s1, t0, t1;
    s32 i;

    if (*(const u8 *) (size_t) 0x8036BFC4u == 0 || max < 32 || level < 0 || level > 0x3F) return 0;
    scroll = 120.0 - (f32) (s16) pitch * 0.22 + speed;
    cc = (0.0 > scroll) ? 0.0 : scroll;
    d0 = (0.0 > -scroll) ? 0.0 : -scroll;
    if ((s32) cc <= 0) return 0;
    c8 = ((s16) heading - 2048.0) * 0.5;
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPPipeSync(gdl++);
    gDPSetCombine(gdl++, 0xFC24A3FF, 0x1FFC9238);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_OPA_SURF2);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    for (i = 0; i < 2; i++) {
        entry = &tab[i];
        gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (void *) (size_t) entry->ptr);
        gDPTileSync(gdl++);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, i * 256, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPLoadSync(gdl++);
        gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 1023, 256);
    }
    gSPMatrix(gdl++, (Mtx *) (size_t) (u32) (dyn + 0x80000100u), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, (Mtx *) (size_t) (u32) (dyn + 0x800001C0u), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPTileSync(gdl++);
    gDPSetTextureLOD(gdl++, G_TL_TILE);
    for (i = 0; i < 2; i++) {
        entry = &tab[i];
        gDPSetTile(gdl++, fmts[i], G_IM_SIZ_16b, 8, i * 256, i, 0, entry->cmt & 3, 5, i, entry->cms & 3, 5, i);
        gDPSetTileSize(gdl++, i, 0, 0, 31 << G_TEXTURE_IMAGE_FRAC, 31 << G_TEXTURE_IMAGE_FRAC);
    }
    entry = &tab[1];
    s0 = (c8 + 16.0f) / (f32) (1 << entry->sShift);
    s1 = 319.0 / (f32) (1 << entry->sShift);
    t0 = (cc - speed - d0) / (f32) (1 << entry->tShift);
    t1 = (cc - 1.0) / (f32) (1 << entry->tShift);
    for (i = 0; i < 4; i++) {
        vtx[i][0] = (i >= 2) ? (s16) (cc * 4.0 - 1.0) : 0x7FFF;   /* 0x7FFF: keep the vertex's own */
        vtx[i][1] = (s16) (((i & 1) ? s0 + s1 : s0) * 32.0);
        vtx[i][2] = (s16) ((i < 2 ? t0 : t0 - t1) * 32.0);
    }
    gSPVertex(gdl++, (Vtx *) (size_t) 0x802FA820u, 4, 0);
    gDPPipeSync(gdl++);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 1, 2, 3, 0);
    return (int) (gdl - start);
}

/* ---- the player select's ticker beyond 4:3 (port.h PORT_TICKER) ------------- */
/* bc.exe's widescreen (live_wide.cpp) is told about the ticker's line and draws
 * the characters on either side of the 4:3 picture: those that left the line
 * and the text it shows next.  Nothing goes back to the game. */
void (*host_ticker_hook)(int what, int a, const void *t);

void port_ticker(int what, int a, const PortTicker *t) {
    if (host_ticker_hook != NULL) host_ticker_hook(what, a, t);
}

/* the decoders of 60F60.c (pure: they write dst only) */
u8 *tex_decode_type1(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type2(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type3(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type6(s16 *src, s32 len, u8 *dst);

/* Texture-table entry `id` (a font glyph: 32x32 I4, 0x200 bytes) decoded from
 * the ROM into `dst` (`max` bytes) as the game's tex_queue_load leaves it in
 * RDRAM (big-endian texel bytes); returns its size, 0 if it can't. */
int port_glyph_decode(unsigned id, unsigned char *dst, int max) {
    const u8 *rom = plat_rom;
    u32 e, rom0, rom1, size, type, i;
    unsigned int tab;
    static s16 tok[0x2000];
    static u8 out[0x4000];
    u8 *end;

    if (rom == NULL || id >= 0xFFF) return 0;
    rom_map_dma(0x4CE0u, 8, &tab);   /* (the table's place: a ROM from the shiftable build may have moved it) */
    e = tab + id * 8u;
    if (e + 16 > plat_rom_size) return 0;
#define RD32(a) ((u32) rom[a] << 24 | (u32) rom[(a) + 1] << 16 | (u32) rom[(a) + 2] << 8 | rom[(a) + 3])
    rom0 = tab + RD32(e);
    rom1 = tab + RD32(e + 8);
    size = (u32) rom[e + 4] << 8 | rom[e + 5];
    type = (u32) rom[e + 6] << 8 | rom[e + 7];
#undef RD32
    if (rom1 < rom0 || rom1 > plat_rom_size || size > rom1 - rom0 || size > sizeof tok || (size & 1)) return 0;
    if (type == 0) {
        size &= ~7u;
        if ((int) size > max) return 0;
        for (i = 0; i < size; i++) dst[i] = rom[rom0 + i];
        return (int) size;
    }
    for (i = 0; i < size / 2; i++) tok[i] = (s16) ((u16) rom[rom0 + 2 * i] << 8 | rom[rom0 + 2 * i + 1]);
    switch (type) {
        case 1: end = tex_decode_type1(tok, (s32) size, out); break;
        case 2: end = tex_decode_type2(tok, (s32) size, out); break;
        case 3: end = tex_decode_type3(tok, (s32) size, out); break;
        case 6: end = tex_decode_type6(tok, (s32) size, out); break;
        default: return 0;   /* (4 and 5 need a colour table: no glyph is one) */
    }
    if (end < out || end - out > max || end - out > (int) sizeof out) return 0;
    for (i = 0; i < (u32) (end - out); i++) {
        /* the texels as big-endian bytes (load/port_assets.c port_texture_output) */
        u32 j = type == 1 ? (i ^ 1) : type == 2 ? (i ^ 3) : i;
        dst[i] = out[j];
    }
    return (int) (end - out);
}

/* the glyph text_queue_string (14B30.c) draws for character ch of the narrow font */
static u16 tk_glyph(u32 ch) {
    u16 g;

    if (ch >= '0' && ch <= '9') g = ch - '0';
    else if (ch >= 'A' && ch <= 'D') g = ch - 'A' + 10;
    else if (ch >= 'E' && ch <= 'Z') g = ch - 'A' + 11;
    else switch (ch) {
        case 0x7F: g = 14; break;
        case '\'': g = 38; break;
        case ')': g = 39; break;
        case ':': g = 40; break;
        case ',': g = 41; break;
        case '$': g = 42; break;
        case '!': g = 43; break;
        case '.': g = 44; break;
        case '-': g = 45; break;
        case '(': g = 46; break;
        case '%': g = 47; break;
        case '?': g = 48; break;
        case '#': g = 49; break;
        case '/': g = 50; break;
        case 'a': case 'b': case 'd': case 'e': case 'k': case 'm': g = (u16) (ch - 0x22C); break;
        default: g = 0; break;   /* (' ', '&' and the rest) */
    }
    return (u16) (g + 0xF4C);
}

/* one character quad as text_queue_string writes it (forward, align 0) */
static Gfx *tk_char(Gfx *gdl, Vtx *v, u32 tex, f32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    s32 i;

    for (i = 0; i < 4; i++) {
        v[i].v.ob[0] = (i == 1 || i == 2) ? (s16) (x + w) : (s16) x;
        v[i].v.ob[1] = (i >= 2) ? y + h : y;
        v[i].v.ob[2] = -10;
        v[i].v.flag = 0;
        v[i].v.tc[0] = (i == 1 || i == 2) ? 0x3E0 : 0;
        v[i].v.tc[1] = (i <= 1) ? 0x3E0 : 0;
        v[i].v.cn[0] = r;
        v[i].v.cn[1] = g;
        v[i].v.cn[2] = b;
        v[i].v.cn[3] = a;
    }
    gDPLoadTextureBlock_4b(gdl++, (void *) (size_t) tex, G_IM_FMT_I, 32, 32, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK,
                           G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gdl++, v, 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    return gdl;
}

Gfx *spr_draw(Gfx *gdl, u8 slot, s16 x, s16 y, u8 alpha, u8 mode, f32 scale);   /* 2E490.c */
Gfx *spr_begin(Gfx *gfx);
Gfx *spr_end(Gfx *gfx);
void text_set_render_state(Gfx **gdlp, s32 arg1);                                  /* 14B30.c */
extern u8 gSpriteFlags[64];

/* spr_draw's commands for an icon at x (mode 1: its shadow, then itself; spr_draw
 * writes nothing but the commands), each texture rectangle turned into a quad
 * (vertices from v[*nv]) cut to clipLo <= x < clipHi: unlike a rectangle, a
 * quad can lie beyond the game's 4:3 scissor.  spr_draw draws at x + shift,
 * clear of the rectangle's 0 limit; the quads are moved back. */
static Gfx *tk_icon(Gfx *gdl, Vtx *v, s32 *nv, s32 maxVtx, s32 slot, s32 x, s32 alpha, s32 shift, f32 clipLo,
                    f32 clipHi) {
    Gfx tmp[96];
    Gfx *c, *e = spr_draw(tmp, (u8) slot, (s16) (x + shift), 0xC2, (u8) alpha, 1, 1.0f);
    s32 i;

    for (c = tmp; c < e; c++) {
        const u32 w0 = c->words.w0, w1 = c->words.w1;
        f32 xl, yl, xh, yh, a0, a1, s0, t0, dsdx, dtdy;
        Vtx *q;

        if ((w0 >> 24) != (u8) G_TEXRECT || c + 2 >= e) {
            *gdl++ = *c;
            continue;
        }
        xh = ((w0 >> 12) & 0xFFF) / 4.0f - shift;
        yh = (w0 & 0xFFF) / 4.0f;
        xl = ((w1 >> 12) & 0xFFF) / 4.0f - shift;
        yl = (w1 & 0xFFF) / 4.0f;
        s0 = (s16) (c[1].words.w1 >> 16);
        t0 = (s16) (c[1].words.w1 & 0xFFFF);
        dsdx = (s16) (c[2].words.w1 >> 16) / 32.0f;   /* S5.10 per pixel -> S10.5 */
        dtdy = (s16) (c[2].words.w1 & 0xFFFF) / 32.0f;
        c += 2;
        a0 = xl > clipLo ? xl : clipLo;
        a1 = xh < clipHi ? xh : clipHi;
        if (a1 <= a0 || *nv + 4 > maxVtx) continue;
        q = v + *nv;
        for (i = 0; i < 4; i++) {
            const f32 qx = (i == 1 || i == 2) ? a1 : a0, qy = i >= 2 ? yh : yl;
            q[i].v.ob[0] = (s16) qx;
            q[i].v.ob[1] = (s16) qy;
            q[i].v.ob[2] = -10;
            q[i].v.flag = 0;
            q[i].v.tc[0] = (s16) (s0 + (qx - xl) * dsdx);
            q[i].v.tc[1] = (s16) (t0 + (qy - yl) * dtdy);
            q[i].v.cn[0] = q[i].v.cn[1] = q[i].v.cn[2] = q[i].v.cn[3] = 0xFF;
        }
        gSPVertex(gdl++, q, 4, 0);
        gSP1Triangle(gdl++, 0, 1, 2, 0);
        gSP1Triangle(gdl++, 0, 2, 3, 0);
        *nv += 4;
    }
    return gdl;
}

/* The commands that draw the ticker beyond the 4:3 picture, in host memory
 * (`gfx`, at most `max`; vertices at `vtx`, at most `maxVtx`, which must lie
 * where RT64 reads them: its extended RDRAM addresses = these pointers):
 *  pass 0: the characters' shadows, 1: the characters; left of the line the
 *  `nhist` characters that left it (hist[0] the last), right of it the text it
 *  shows next; glyph(id) = the address of glyph id's texture, 0: skip it;
 *  pass 2: the rank / medal icons left of the picture (left of x = 0, where the
 *  game cuts them), 3: right of it (those that come next, and the game's own
 *  past x = 320), as quads (tk_icon; `shift`: spr_draw's x offset, clear of a
 *  rectangle's 0 limit), then the text's render state back.  `ext`: the
 *  picture's width beyond 4:3 on each side (4:3 pixels).
 * Returns the number of commands; *nvtx the vertices used. */
int port_ticker_gfx(const void *tv, int pass, double ext, int shift, const unsigned short *hist, int nhist,
                    void *gfxv, int max, void *vtxv, int maxVtx, unsigned (*glyph)(unsigned id), int *nvtx) {
    const PortTicker *t = (const PortTicker *) tv;
    Gfx *gdl = (Gfx *) gfxv, *start = gdl, *start2;
    Vtx *vtx = (Vtx *) vtxv;
    s32 nv = 0, n, i, w = t->charWidth, size = 0x14;
    f32 x, step;

    *nvtx = 0;
    if (t->glyphs || t->alpha <= 0 || w <= 0 || max < 64) return 0;
    if (pass <= 1) {
        s32 a = pass == 0 ? ((t->alpha / 2 - 0x1B < 0) ? 0 : t->alpha / 2 - 0x1B) : t->alpha;
        s32 y = pass == 0 ? 0xC9 : 0xC7;
        u8 c = pass == 0 ? 0 : 0xFF;
        f32 x0 = (f32) ((-t->pixel % w) - (pass == 0 ? 3 : 0));

        if (a == 0) return 0;
        step = size * t->advance;
        /* right: past the line's characters, the text that comes next */
        x = x0;
        for (n = 0; t->line[n] != t->terminator && n < 64; n++)
            if ((u16) (u8) t->line[n] != t->noAdvance) x += step;
        for (i = 0; t->len > 0 && i < 64 && x < 320.0 + ext + 4.0; i++) {
            u32 ch = t->text[(t->pos + 1 + i) % t->len];
            u32 tex;
            if (ch != t->skip[0] && ch != t->skip[1] && ch != t->skip[2] && nv + 4 <= maxVtx &&
                gdl - start + 12 < max && (tex = glyph(tk_glyph(ch))) != 0) {
                gdl = tk_char(gdl, vtx + nv, tex, x, y, size, size, c, c, c, (u8) a);
                nv += 4;
            }
            if (ch != t->noAdvance) x += step;
        }
        /* left: the characters that left the line, the last first */
        x = x0;
        for (i = 0; i < nhist && x + size > -ext - 4.0; i++) {
            u32 ch = (u8) hist[i];
            u32 tex;
            if (ch == t->noAdvance) continue;
            x -= step;
            if (ch != t->skip[0] && ch != t->skip[1] && ch != t->skip[2] && nv + 4 <= maxVtx &&
                gdl - start + 12 < max && (tex = glyph(tk_glyph(ch))) != 0) {
                gdl = tk_char(gdl, vtx + nv, tex, x, y, size, size, c, c, c, (u8) a);
                nv += 4;
            }
        }
        *nvtx = nv;
        return (s32) (gdl - start);
    }
    /* the icons: sprite slots t->iconSprite and t->medalSprite + i */
    gdl = spr_begin(gdl);
    /* (quads, not rectangles: texturing and perspective on, shaded as the text
     * is (RT64 drew unshaded ones only in part); the depth of a rectangle, the
     * primitive's, where the render mode compares depth) */
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetDepthSource(gdl++, G_ZS_PRIM);
    start2 = gdl;
    if (pass == 2) {
        /* left of the picture: those the game moved past x = 0 (it cuts them there) */
        for (i = 0; i < 5; i++) {
            s32 slot = i == 0 ? t->iconSprite : t->medalSprite + i;
            s32 xi = 0x136 - (i == 0 ? t->iconX : t->medalX[i]);
            if (xi < 0 && xi >= -ext - 40.0 && !(gSpriteFlags[slot] & 4) && gdl - start + 40 < max)
                gdl = tk_icon(gdl, vtx, &nv, maxVtx, slot, xi, t->alpha, shift, -10000.0f, 0.0f);
        }
    } else {
        /* the game's own icons reaching past x = 320: their part beyond it */
        for (i = 0; i < 5; i++) {
            s32 slot = i == 0 ? t->iconSprite : t->medalSprite + i;
            s32 xi = 0x136 - (i == 0 ? t->iconX : t->medalX[i]);
            if (xi >= 320 - 40 && xi < 320 && !(gSpriteFlags[slot] & 4) && gdl - start + 40 < max)
                gdl = tk_icon(gdl, vtx, &nv, maxVtx, slot, xi, t->alpha, shift, 320.0f, 10000.0f);
        }
        /* right of it: where the icons fed in next would be now (the game's
         * scroll, frame by frame, from this frame's state: entered at frame f,
         * an icon is at 0x136 - its speed there and moves by the speed each
         * frame, so it is now further right by the frames' moves before f) */
        s32 p = t->pixel, pos = t->pos, hold = t->hold, f, sum = 0, k;
        f32 s = t->speed, goal = t->speedGoal;
        s32 entered = 0;

        for (f = 1; f < 600 && t->len > 0 && entered < 8; f++) {
            s32 d;
            s32 fed = -1;
            p = (s32) ((f32) p + s);
            if (p >= w) {
                p -= w;
                pos = (pos + 1) % t->len;
                fed = pos;
                if (hold != 0) {
                    hold--;
                    if (hold == 0) goal = 3.0f;
                }
                s = (goal - s) * 0.2 + s;
            }
            d = (s32) (0.0f + s);
            if (fed >= 0) {
                for (k = 0; k < 5; k++) {
                    s32 slot = k == 0 ? t->iconSprite : t->medalSprite + k;
                    s32 xi;
                    if (k == 0 ? fed != t->iconPos : (k > t->maxRank || fed != t->medalPos[k])) continue;
                    entered++;
                    xi = 0x136 + sum;   /* (the frames before f moved it by sum) */
                    if (xi < 320.0 + ext + 40.0 && !(gSpriteFlags[slot] & 4) && gdl - start + 40 < max)
                        gdl = tk_icon(gdl, vtx, &nv, maxVtx, slot, xi, t->alpha, shift, -10000.0f, 10000.0f);
                }
            }
            sum += d;
            if (0x136 + sum > 320.0 + ext + 40.0) break;   /* (those fed later are further right) */
        }
    }
    if (gdl == start2) return 0;   /* no icon */
    gdl = spr_end(gdl);
    gDPSetDepthSource(gdl++, G_ZS_PIXEL);
    text_set_render_state(&gdl, 0);
    *nvtx = nv;
    return (s32) (gdl - start);
}

/* ---- frames ---------------------------------------------------------------- */

static void dump_rdram(u32 frame) {
    char path[512];
    const char *d = plat_cfg.dump_dir ? plat_cfg.dump_dir : ".";
    u32 i = 0, j;
    char num[16];
    while (d[i] && i < 400) {
        path[i] = d[i];
        i++;
    }
    for (j = 0; "/frame_"[j]; j++) path[i++] = "/frame_"[j];
    for (j = 0; j < 7; j++) num[6 - j] = '0' + (frame % 10), frame /= 10;
    for (j = 0; j < 7; j++) path[i++] = num[j];
    for (j = 0; ".bin"[j]; j++) path[i++] = ".bin"[j];
    path[i] = 0;
    if (host_write_file(path, (void *) 0x80000000, 0x800000) != 0) host_log("dump: can't write %s\n", path);
    else if (!plat_cfg.quiet) host_log("dump: %s\n", path);
}

static void print_stats(void) {
    host_log("stats: frames %u, retraces %u, time %u.%03u s, gfx tasks %u (cull tests %u), audio tasks %u,\n"
             "       PI DMAs %u (%u KB), pad reads %u, EEPROM r/w %u/%u, front-end reloads %u,\n"
             "       osGetTime calls %u, thread switches %u\n",
             (unsigned) plat_stats.frames, (unsigned) plat_vi_count, (unsigned) (plat_now / PLAT_COUNT_HZ),
             (unsigned) (plat_now % PLAT_COUNT_HZ / (PLAT_COUNT_HZ / 1000)), (unsigned) plat_stats.gfx_tasks,
             (unsigned) plat_stats.cull_tasks, (unsigned) plat_stats.aud_tasks, (unsigned) plat_stats.pi_dmas,
             (unsigned) (plat_stats.pi_bytes >> 10), (unsigned) plat_stats.cont_reads,
             (unsigned) plat_stats.eeprom_reads, (unsigned) plat_stats.eeprom_writes,
             (unsigned) plat_stats.fe_reloads, (unsigned) plat_stats.gettime_calls,
             (unsigned) plat_stats.thread_switches);
    host_log("game: mode 0x%08X%08X, level %d, frames in mode %u, game VI counter %u\n",
             (unsigned) (G_MODE >> 32), (unsigned) G_MODE, (int) G_LEVEL, (unsigned) G_MODEFRAMES,
             (unsigned) G_VICOUNT);
    host_log("cull: %u tests, %u not visible", (unsigned) plat_stats.cull_tasks, (unsigned) plat_stats.cull_hidden);
    if (plat_stats.cull_keyed)
        host_log("; %u answers from --clock, the model disagrees with %u", (unsigned) plat_stats.cull_keyed,
                 (unsigned) plat_stats.cull_disagree);
    host_log("\n");
    plat_clock_report();
    plat_sync_report();
}

static u64 g_last_mode = ~0ull;

/* A frame: the game handed the scheduler its frame-ending gfx task. */
void plat_on_frame(void) {
    u32 f = ++plat_stats.frames;
    u32 i;
    host_trace("%u vi=%u t=%u mode=%08X%08X lvl=%d mf=%u gvi=%u rd=%u\n", (unsigned) f, (unsigned) plat_vi_count,
               (unsigned) plat_now, (unsigned) (G_MODE >> 32), (unsigned) G_MODE, (int) G_LEVEL,
               (unsigned) G_MODEFRAMES, (unsigned) G_VICOUNT, (unsigned) plat_stats.cont_reads);
    if (G_MODE != g_last_mode) {
        g_last_mode = G_MODE;
        if (!plat_cfg.quiet)
            host_log("frame %u (vi %u): game mode 0x%08X%08X, level %d\n", (unsigned) f,
                     (unsigned) plat_vi_count, (unsigned) (G_MODE >> 32), (unsigned) G_MODE, (int) G_LEVEL);
    }
    for (i = 0; i < plat_cfg.n_dump_frames; i++)
        if (plat_cfg.dump_frames[i] == f) dump_rdram(f);
    if (plat_cfg.dump_every && f % plat_cfg.dump_every == 0) dump_rdram(f);
    host_watch_frame(f);   /* --watch (debugging, plat_host.c) */
    host_crash_test_frame(f);   /* --crash-test (crash.c) */
    for (i = 0; i < plat_cfg.n_pokes; i++) {
        const unsigned *p = plat_cfg.pokes[i];
        if (p[0] != f) continue;
        if (p[2] == 1) *(u8 *) p[1] = (u8) p[3];
        else if (p[2] == 2) *(u16 *) p[1] = (u16) p[3];
        else *(u32 *) p[1] = p[3];
        if (!plat_cfg.quiet) host_log("poke: frame %u: %08X = %X (%u bytes)\n", (unsigned) f, p[1], p[3], p[2]);
    }
    plat_cheats_frame(f);   /* --cheats-on, the debug cheats' saves (save.c) */
    if (plat_cfg.frames && f >= plat_cfg.frames) {
        host_log("stopping: %u frames reached\n", (unsigned) f);
        print_stats();
        plat_si_flush();
        host_exit(0);
    }
}

/* the game state in a crash report (crash.c; no stdio, no heap) */
static void describe(char *buf, unsigned size) {
    host_snprintf(buf, size,
                  "game: thread %s, frame %u, retrace %u, mode 0x%08X%08X (next 0x%08X%08X), level %d, "
                  "frames in mode %u, game VI counter %u",
                  plat_thread_name(plat_running()), (unsigned) plat_stats.frames, (unsigned) plat_vi_count,
                  (unsigned) (G_MODE >> 32), (unsigned) G_MODE, (unsigned) (G_NEXTMODE >> 32), (unsigned) G_NEXTMODE,
                  (int) G_LEVEL, (unsigned) G_MODEFRAMES, (unsigned) G_VICOUNT);
}

/* after the report: with --dump-dir, frame_9999999.bin = RDRAM at the crash */
static void after_crash(void) {
    if (plat_cfg.dump_dir) dump_rdram(9999999);
}

/* ---- boot ------------------------------------------------------------------- */

int port_layout_check(void);   /* layout_check.c */

void plat_start(const HostOpts *o) {
    int bad = port_layout_check();
    if (bad) host_fatal("layout_check.c line %d: a GBI/ABI bit-field is not where the game expects it", bad);
    plat_cfg = *o;
    plat_now = o->boot_count;
    plat_rom = o->rom;
    plat_rom_size = o->rom_size;
    host_crash_set_describe(describe, after_crash);
    if (o->trace_path && host_trace_open(o->trace_path) != 0) host_fatal("can't write %s", o->trace_path);

    /* what init.us.v11 leaves behind besides the inflated images: the front
     * end's compressed ROM range at the top of RDRAM (init_boot) */
    *(u32 *) 0x803FFFF8 = 0x7E3AD0;
    *(u32 *) 0x803FFFFC = 0x7F9BE0;

    if (o->live != NULL && o->live->boot != NULL) o->live->boot();
    port_audio_init(o);
    plat_fe_snapshot();
    plat_cheats_boot();   /* the debug cheats on at boot: their save files (save.c) */
    plat_si_init();
    plat_input_init(o->input_path);
    plat_clock_init(o->gettime_path, o->frame_done_path);
    if (o->syms_path) plat_syms_load(o->syms_path);
    if (o->clock_path) plat_clock_keyed_load(o->clock_path);
    if (o->calls) {
        if (!o->syms_path) host_fatal("--calls needs --syms");
        plat_calls_init(o->calls);
    }
    if (o->sync_path) {
        if (!o->syms_path) host_fatal("--sync needs --syms");
        plat_sync_load(o->sync_path);
    }

    hd_boot();   /* osInitialize, debug command line, idle thread */
    plat_run();
}
