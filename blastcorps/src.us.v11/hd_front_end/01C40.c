#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gYoshiFeItems
#define LEGACY_gLevelInfo
#define LEGACY_gYoshiColors
#define LEGACY_gYoshiWindows
#define LEGACY_gDynamicBufs
#define LEGACY_players
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gLevelInfo ((LevelTimesView *) gLevelInfo)
#define gYoshiColors ((u8 *) gYoshiColors)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define gDynamicBufs ((PlayerSelDyn *) gDynamicBufs)
#define players ((PlayerInfo *) players)
#endif
/* end of views */

/* player.c (per its assert strings): save slots, player ranks and the
 * player select / Controller Pak screens */

/* This file's view of a LevelInfo (game/types.h): fe_player_draw_medals
 * indexes the medal times from 0x2E (times[1..4] = LevelInfo.medalTimes). */
typedef struct {
    /* 0x00 */ u8 kind;
    /* 0x01 */ u8 pad1[0x2D];
    /* 0x2E */ u16 times[5]; /* [1..4]: platinum, gold, silver, bronze; [4] is the time limit */
    /* 0x38 */ u8 pad38[0xC];
} LevelTimesView;

/* (players[]: PlayerInfo, game/types.h) */

/* (The windows and their items, gYoshiWindows / gYoshiFeItems: YoshiWindow,
 * YoshiItem, game/types.h.) */

/* Per-frame dynamic buffer (gDynamicBufs, two of 0x21498 bytes); only the
 * player select matrices are named here */
typedef struct PlayerSelDyn {
    /* 0x000 */ u8 pad0[0x80];
    /* 0x080 */ Mtx persp;
    /* 0x0C0 */ u8 padC0[0xC0];
    /* 0x180 */ Mtx lookAt;
    /* 0x1C0 */ u8 pad1C0[0x400];
    /* 0x5C0 */ Mtx trans;
    /* 0x600 */ u8 pad600[0x1E00 - 0x600];
    /* 0x1E00 */ Vtx vtx[16]; /* name entry boxes */
    /* 0x1F00 */ u8 pad1F00[0x21498 - 0x1F00];
} PlayerSelDyn;

#ifndef NON_MATCHING
extern PlayerInfo players[];
#endif
#ifndef NON_MATCHING
extern LevelTimesView gLevelInfo[];
#endif
#ifndef NON_MATCHING
extern YoshiItem gYoshiFeItems[];
#ifndef NON_MATCHING
extern YoshiWindow gYoshiWindows[];
#ifndef NON_MATCHING
extern PlayerSelDyn gDynamicBufs[];
#endif
#endif
#endif
extern char gSlotLabels[][25];
extern u16 gEraseGameJText[];
extern u16 gIgnorePakJText[];
extern u16 gGoBackJText[];
extern s16 gScrollHoldTime;
extern f32 gScrollSpeedGoal;
extern s32 gScrollIconPos;
extern s32 gScrollMedalPos[];
extern u8 gScrollText[]; /* ticker text */
#define SCROLL_TEXT ((char *) gScrollText)
extern char gMedalCountText[][16];
extern u16 * N64P gScrollGlyphText;

extern Vtx gPanelVtx[];
extern Gfx gPanelDl[];
extern Lights2 gPanelLights;
extern u16 gPanelPerspNorm;
extern s16 gPanelAlpha;
extern s32 gScrollCharWidth;
extern u8 gMedalTimeText[];
extern u8 gScrollIconSprite;
extern u8 gScrollMedalSpriteBase;
extern s16 gNameWheelAngleGoal;
extern s16 gNameWheelAngle;
extern s16 gNameWheelSel;
extern s16 gNameWheelPrevSel;
extern s16 gNameFlyChar;
extern u8 gNameLen;
extern s16 gNameEntryState;
extern s16 gNameBoxState;
extern s32 gNameBoxX;
extern s32 gNameBoxW;
extern s32 gNameEntryWidth;
extern u8 gNameEntryFirst;
extern s16 gNameWheelStartSel;
extern s16 gNameStick;
extern s32 gNameArrowStep;
extern u8 gNameMaxLen;
extern char * N64P gNameBuf;
#ifndef NON_MATCHING
extern u8 gYoshiColors[];
#endif
extern f32 gFlyCharW;
extern f32 gFlyCharH;
extern f32 gFlyCharWGoal;
extern f32 gFlyCharHGoal;
extern f32 gNameTextX;
extern f32 gNameTextXGoal;
extern f32 gNameBoxXAnim;
extern f32 gNameBoxXGoal;
extern f32 gNameFlyBoxW;
extern f32 gNameFlyCharSize;
extern s16 gNamePrevStick;
extern s32 gNameArrowPulse;
extern f32 gFlyCharX;
extern f32 gFlyCharY;
extern f32 gFlyCharXGoal;
extern f32 gFlyCharYGoal;
extern s16 gNameArrowAlpha;

extern s16 sslen;
extern s32 gScrollPos;
extern u8 gMedalState[];
extern u8 gMedalRank[];
extern s32 gMedalDelay[];
extern s16 gMedalAlpha[];
extern u8 gMedalSpriteBase;
extern u16 gMedalCounts[];
extern u16 gScrollLine[];
extern u8 gScrollUsesGlyphs;
extern s32 gScrollPixel;
extern f32 gScrollSpeed;
extern s32 gScrollIconX;
extern s32 gScrollMedalX[];
extern u16 gMedalPalettes[];
extern u16 gMedalSprites[];
extern u16 gMedalExtraSprites[];

void fe_player_count_medals(u8 arg0);
void fe_player_copy_times(u8 *src, u8 *dst);
u16 fe_player_ticker_next_glyph(void);
void fe_player_init_vehicles(PlayerInfo *p);
Gfx *fe_player_draw_medal(Gfx *arg0, s32 x, s32 y, u8 slot);

/* Highest rank shown: 4 once unk91 reaches 12, else 3 */
#define MAX_RANK() ((players[playerNumber].gameState >= 12) ? 4 : 3)


/* the rank names' Japanese text, in hd_code's .data */
#define JTEXT(addr) ((u16 * N64P) HD_JTEXT_ADDR(addr))

RankTitle gRankTitles[31] = {
    { N64_DPTR("ROOKIE WRECKER"), JTEXT(0x803041DC) },
    { N64_DPTR("TRAINED CRUSHER"), JTEXT(0x803041EC) },
    { N64_DPTR("EXPERIENCED RAVAGER"), JTEXT(0x803041FC) },
    { N64_DPTR("DECORATED DAMAGER"), JTEXT(0x8030420C) },
    { N64_DPTR("PROFESSIONAL RAZER"), JTEXT(0x8030421C) },
    { N64_DPTR("EXPERT DESTROYER"), JTEXT(0x8030422C) },
    { N64_DPTR("GIFTED RUINER"), JTEXT(0x8030423C) },
    { N64_DPTR("ACCOMPLISHED CONQUEROR"), JTEXT(0x8030424C) },
    { N64_DPTR("MASTER DESPOILER"), JTEXT(0x8030425C) },
    { N64_DPTR("DEMOLITION FANATIC"), JTEXT(0x8030426C) },
    { N64_DPTR("GRAND ERADICATOR"), JTEXT(0x8030427C) },
    { N64_DPTR("HEAVY DUTY WASTER"), JTEXT(0x80304288) },
    { N64_DPTR("TOTAL PULVERISER"), JTEXT(0x8030429C) },
    { N64_DPTR("CHAMPION RANSACKER"), JTEXT(0x803042AC) },
    { N64_DPTR("MECHANICAL MAESTRO"), JTEXT(0x803042B8) },
    { N64_DPTR("CHIEF OBLITERATOR"), JTEXT(0x803042CC) },
    { N64_DPTR("COMMANDING DESOLATOR"), JTEXT(0x803042E0) },
    { N64_DPTR("SUPREME DEVASTATOR"), JTEXT(0x803042F0) },
    { N64_DPTR("ULTIMATE ANNIHILATOR"), JTEXT(0x80304304) },
    { N64_DPTR("LEVELING LEGEND"), JTEXT(0x8030430C) },
    { N64_DPTR("DESTRUCTIVE PSYCHOPATH"), JTEXT(0x80304318) },
    { N64_DPTR("MINDLESS DESECRATOR"), JTEXT(0x80304328) },
    { N64_DPTR("HYSTERICAL CLAUSTROPHOBE"), JTEXT(0x80304334) },
    { N64_DPTR("UNCONTROLLABLE MADMAN"), JTEXT(0x80304344) },
    { N64_DPTR("WORLD CLASS MEGALOMANIAC"), JTEXT(0x80304358) },
    { N64_DPTR("CAPTAIN OF CARNAGE"), JTEXT(0x80304364) },
    { N64_DPTR("SINGLE MINDED CHAOSMONGER"), JTEXT(0x80304370) },
    { N64_DPTR("GRAND HIGH SLAUGHTERMASTER"), JTEXT(0x8030437C) },
    { N64_DPTR("LUNATIC LORD OF HAVOC"), JTEXT(0x80304388) },
    { N64_DPTR("ARMAGEDDON ADEPT"), JTEXT(0x80304394) },
    { N64_DPTR("YOU CAN STOP NOW."), JTEXT(0x8030439C) },
};

u8 gRankTitleLine1Words[0x20] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 2, 1, 1, 0,
};

/* sprite ids for spr_load */
u16 gMedalPalettes[6] = { 0x777, 0x777, 0x776, 0x773, 0, 0 };
u16 gMedalSprites[2] = { 0x774, 0x775 };
u16 gMedalExtraSprites[2] = { 0x576, 0x575 };
u16 gTickerMedalSprites[6] = { 0x91B, 0x91B, 0x91C, 0x91D, 0x572, 0 };
u16 gTickerIconSprite[2] = { 0x91F, 0 };

/* name entry character sets and their lengths */
u8 gNameEntrySymbols[2][12] = { "1234/.\x7F", "0123456789/\x7F" };
u8 gNameEntryLetters[2][26] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "BCDFGHJKLMNPQRSTVWXYZ" };
s32 gNameEntrySymbolCount[2] = { 7, 12 };
u32 gNameEntryLetterCount[2] = { 26, 21 };

/* Sets playerNumber, the panel's sprites and matrices, the ticker (0x801E8C40).
 * Set up the player select screen for a player: sprites and both frames' matrices */
void fe_player_panel_init(u8 arg0) {
    PlayerSelDyn *dyn;
    s32 i;

    playerNumber = arg0;
    gScrollIconSprite = spr_load(gTickerIconSprite, NULL, 1, 1, 1, 1.0f);
    gScrollMedalSpriteBase = spr_load(gTickerMedalSprites, NULL, 5, 1, 0, 1.0f);
    for (i = 0; i < 2; i++) {
        dyn = &gDynamicBufs[i];
        guPerspective(&dyn->persp, &gPanelPerspNorm, 45.0f, 4.0f / 3.0f, 10.0f, 10000.0f, 1.0f);
        guLookAt(&dyn->lookAt, 0.0f, 277.0f, 480.0f, 0.0f, 189.0f, 200.0f, 0.0f, 0.0f, 1.0f);
        guTranslate(&dyn->trans, 0.0f, 88.0f, 0.0f);
    }
    gPanelAlpha = 0;
    fe_player_ticker_reset(playerNumber);
}

/* The ticker's text, a blank line, scroll state reset (0x801E8DCC). */
void fe_player_ticker_reset(u8 arg0) {
    s32 i;

    fe_player_ticker_set_text(arg0, 0);
    for (i = 0; i < 0x1B; i++) {
        gScrollLine[i] = gTextSpace[gScrollUsesGlyphs];
    }
    gScrollLine[i] = gTextTerminator[gScrollUsesGlyphs];
#ifdef PORT_HOST
    PORT_TICKER(PORT_TICKER_RESET, 0, NULL); /* bc.exe's widescreen: the line was blanked */
#endif
    gScrollPixel = 0;
    gScrollIconX = 9999;
    for (i = 1; i < 5; i++) {
        gScrollMedalX[i] = 9999;
    }
    gScrollSpeed = 3.0f;
}

#define PLAYER_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "player.c", line)
#define TOTAL_SCROLL_LENGTH 256

/* The ticker: bio or name, title, medals, money (0x801E8EB8).
 * Build the player select ticker text for a slot (4 = guest/new): the
 * biography in mode 0x1000000000000, else name, title, rank counts and money.
 * The local arrays' templates are .data 0x80208358/0x80208368 and sPrefix is
 * 0x80208378 (IDO puts function statics after the templates). */
void fe_player_ticker_set_text(u8 slot, u8 arg1) {
    s32 i;
    PlayerInfo *p = &players[slot];
    u8 guest = 0;
    static char *sPrefix[2] = { "", "GUEST: " };
    char *bios[4] = {
        ".................... LEADER OF THE ARMY BASE WALKOUT YEARS AGO. AMBER'S SHARP MIND AND BRIGHT, SELFLESS "
        "OUTLOOK MAKE HER THE NEAREST THING BLAST CORPS HAS TO A LEADER ....................",
        ".................... A GENIUS IN HEAVY VEHICLE DESIGN. WHILE SOMETIMES OVERLY POSSESSIVE OF HIS CREATIONS, "
        "CLARK HAS TALENTS VITAL TO BLAST CORPS' SURVIVAL AND SUCCESS ....................",
        ".................... HEAD MECHANIC OF THE BLAST CORPS TEAM. WITH YEARS OF EXPERIENCE AND A GRUFF PRIDE IN "
        "HIS WORK, SPIKE ENSURES THAT THE DOZERS ARE BUILT TO PERFECTION ..................",
        ".................... A FEARLESS ARMY DAREDEVIL UNTIL HIS DISABLING ACCIDENT. WESLEY'S REJECTION BY HIS "
        "SUPERIORS TRIGGERED THE REBELLION THAT LED TO THE RISE OF BLAST CORPS ...............",
    };
    u16 *pics[4] = { (u16 *) HD_JTEXT_ADDR(0x803043B8), (u16 *) HD_JTEXT_ADDR(0x80304474),
                     (u16 *) HD_JTEXT_ADDR(0x80304544), (u16 *) HD_JTEXT_ADDR(0x80304614) };

    if (!(gGameMode & (MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_PAK_CHECK)) && slot != playerNumberAtStart) {
        guest = 1;
    }
    gScrollIconPos = -1;
    for (i = 1; i < 5; i++) {
        gScrollMedalPos[i] = -1;
    }
    if (slot < 4) {
        fe_player_count_medals(slot);
    }
    if (gNextGameMode == MODE_ATTRACT_PICTURE) {
        gScrollGlyphText = NULL;
        sprintf(SCROLL_TEXT, "%s", bios[slot]);
    } else {
        gScrollGlyphText = NULL;
        if (slot < 4) {
            if (gSlotState[slot] == 1) {
                /* (char *): a `char * N64P` (4 bytes) through varargs would leave the upper half of
                 * the native %s argument undefined (the port crashed here) */
                sprintf(SCROLL_TEXT, " ..... %s%s (%s) ... ", sPrefix[guest], p->name, (char *) gRankTitles[p->title].title);
                for (i = (players[slot].gameState >= 12) ? 4 : 3; i > 0; i--) {
                    gScrollMedalPos[i] = font_strlen((u8 *) SCROLL_TEXT);
                    if (i != 1) {
                        sprintf(SCROLL_TEXT, "%s  %d .. ", SCROLL_TEXT, gMedalCounts[i]);
                    }
                }
                sprintf(SCROLL_TEXT, "%s  %d ... ", SCROLL_TEXT, gMedalCounts[1]);
                if (gUseEeprom == 0) {
                    sprintf(SCROLL_TEXT, "%s$%d ... ", SCROLL_TEXT, p->money);
                }
                gScrollIconPos = font_strlen((u8 *) SCROLL_TEXT);
                sprintf(SCROLL_TEXT, "%s  %d", SCROLL_TEXT, p->title);
                if ((gNextGameMode & (MODE_BEST_TIMES | MODE_BEST_TIMES_RETURN)) || (gGameMode & MODE_GUEST_PLAYER)) {
                    sprintf(SCROLL_TEXT, "%s ..... %s", SCROLL_TEXT, "USE Z/R TO CHANGE PLAYER, THEN A TO SELECT!");
                }
            } else {
                sprintf(SCROLL_TEXT, " ... NEW GAME");
            }
        } else {
            sprintf(SCROLL_TEXT, " ");
        }
    }
    if (gScrollGlyphText != NULL) {
        sslen = font_wstrlen(gScrollGlyphText);
        gScrollUsesGlyphs = 1;
        gScrollCharWidth = 0x13;
        if (slot == 4) {
            gScrollHoldTime = 0x16;
        } else {
            gScrollHoldTime = 0xE;
        }
    } else {
        sslen = font_strlen((u8 *) SCROLL_TEXT);
        gScrollUsesGlyphs = 0;
        gScrollCharWidth = 0xC;
        if (slot == 4) {
            gScrollHoldTime = 0x24;
        } else {
            gScrollHoldTime = 0x16;
        }
    }
    PLAYER_ASSERT(sslen<TOTAL_SCROLL_LENGTH, 400);
    gScrollPos = -1;
    if (arg1 || slot == 4) {
        gScrollSpeedGoal = 12.0f;
    } else {
        gScrollSpeedGoal = 8.0f;
    }
}

/* player select backdrop: two lit quads (.data after fe_player_ticker_set_text's) */
Vtx gPanelVtx[8] = {
    { { { -160, 174, 180 }, 0, { 0, 0 }, { 0x00, 0x81, 0x00, 0x28 } } },
    { { { 160, 174, 180 }, 0, { 0, 0 }, { 0x00, 0x81, 0x00, 0x28 } } },
    { { { 160, 204, 180 }, 0, { 0, 0 }, { 0x00, 0x7F, 0x1E, 0x28 } } },
    { { { -160, 204, 180 }, 0, { 0, 0 }, { 0x00, 0x7F, 0x1E, 0x28 } } },
    { { { -160, 198, 200 }, 0, { 0, 0 }, { 0x5A, 0x5A, 0x00, 0xB4 } } },
    { { { -160, 180, 200 }, 0, { 0, 0 }, { 0x5A, 0xA6, 0x00, 0xB4 } } },
    { { { 160, 180, 200 }, 0, { 0, 0 }, { 0x5A, 0xA6, 0x00, 0xB4 } } },
    { { { 160, 198, 200 }, 0, { 0, 0 }, { 0x5A, 0x5A, 0x00, 0xB4 } } },
};

Gfx gPanelDl[] = {
    gsSPVertex(N64_DPTR(gPanelVtx), 8, 0),
    gsDPPipeSync(),
    gsSP1Triangle(0, 5, 1, 0),
    gsSP1Triangle(5, 1, 6, 0),
    gsSP1Triangle(4, 5, 6, 0),
    gsSP1Triangle(4, 6, 7, 0),
    gsSP1Triangle(4, 3, 7, 0),
    gsSP1Triangle(3, 7, 2, 0),
    gsSPEndDisplayList(),
};

Lights2 gPanelLights = gdSPDefLights2(0x28, 0x0A, 0x0A, 0xF0, 0xC8, 0x14, 69, -69, 69, 0xF0, 0x6E, 0x14, -69, 69, 69);
Lights2 gPanelLightsPurple = gdSPDefLights2(0x28, 0x02, 0x21, 0x5A, 0x02, 0xDC, 69, -69, 69, 0x5A, 0x02, 0xDC, -69, 69, 69);

u8 gNameEntryGlyph[4] = " "; /* one-character string for the name entry wheel */

/* A slot's medals by grade (0x801E93DC).
 * Count the player's ranks per grade (gMedalCounts[1..4]) and the finished
 * levels of the 0x81 types, other than levels 0x26, 0x2F and 0x31 ([3]) */
void fe_player_count_medals(u8 arg0) {
    s32 i;
    PlayerInfo *p;

    p = &players[arg0];
    for (i = 0; i < 6; i++) {
        gMedalCounts[i] = 0;
    }
    for (i = 0; i < 0x3C; i++) {
        if (p->medal[i] > 0 && p->medal[i] < 5) {
            gMedalCounts[p->medal[i]]++;
        }
        if (((players[arg0].medal[i] > 0 && players[arg0].medal[i] < 6) ? 1 : 0) && (gLevelInfo[i].kind & LEVEL_KIND_ANY_MISSION) &&
            i != 0x31 && i != 0x2F && i != 0x26) {
            gMedalCounts[3]++;
        }
    }
}

/* The next character fed into the ticker (0x801E9528).
 * Advance the name ticker by one character and return the next glyph */
u16 fe_player_ticker_next_glyph(void) {
    s32 i;

    gScrollPos = (gScrollPos + 1) % sslen;
    if (gScrollPos == gScrollIconPos) {
        gScrollIconX = 0;
    }
    for (i = 1; i <= MAX_RANK(); i++) {
        if (gScrollMedalPos[i] == gScrollPos) {
            gScrollMedalX[i] = 0;
        }
    }
    if (gScrollHoldTime != 0) {
        gScrollHoldTime--;
        if (gScrollHoldTime == 0) {
            gScrollSpeedGoal = 3.0f;
        }
    }
    gScrollSpeed = (gScrollSpeedGoal - gScrollSpeed) * 0.2 + gScrollSpeed;
    if (gScrollGlyphText != NULL) {
        return gScrollGlyphText[gScrollPos];
    }
    return gScrollText[gScrollPos];
}

/* 1 when the ticker text has scrolled to its end (0x801E96F8). */
s32 fe_player_ticker_at_end(void) {
    return sslen == gScrollPos + 8;
}

/* The lit band, the scrolling ticker and its medal icons (0x801E9718).
 * Player select scene: lit backdrop fading with L/R (gPanelAlpha), the
 * scrolling vehicle icons and the scrolling name ticker */
Gfx *fe_player_draw_panel(Gfx *arg0, PlayerSelDyn *dyn, s32 arg2) {
    Gfx *gdl = arg0;

    gSPMatrix(gdl++, &dyn->persp, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->lookAt, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->trans, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gImmp1(gdl++, G_RDPHALF_1, gPanelPerspNorm); /* old gSPPerspNormalize */
    gDPPipeSync(gdl++);
    gSPTexture(gdl++, 0x7C0, 0x7C0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_LIGHTING | G_SHADING_SMOOTH | G_SHADE);
    {
        s32 pad[4]; /* four unused stack slots sit here */

        gSPSetLights2(gdl++, gPanelLights);
    }
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    {
        s32 x;
        s32 i;

        if (gGameMode & (MODE_LEVEL_STATUS | MODE_LOAD_GAME | MODE_RESULTS | MODE_BEST_TIMES | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE)) {
            gPanelAlpha += 0x10;
            if (gPanelAlpha >= 0x100) {
                gPanelAlpha = 0xFF;
            }
        } else if (gGameMode & (MODE_NEW_PLAYER | MODE_PLAYER_CHOSEN | MODE_UNUSED_33 | MODE_REGRADE_NOTICE_END)) {
            gPanelAlpha -= 0x10;
            if (gPanelAlpha <= 0) {
                gPanelAlpha = 0;
            }
        }
        for (i = 0; i < 4; i++) {
            gPanelVtx[i].v.cn[3] = (gPanelAlpha * 40) / 255;
        }
        for (i = 4; i < 7; i++) {
            gPanelVtx[i].v.cn[3] = (gPanelAlpha * 180) / 255;
        }
        osWritebackDCache(gPanelVtx, 0x80);
        if (gPanelAlpha != 0) {
            PORT_WTAG(gdl, PORT_WTAG_TICKER, 0, 0); /* bc.exe's widescreen: as wide as the picture */
            gSPDisplayList(gdl++, gPanelDl);
        }
        PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
        gDPPipeSync(gdl++);
        gSPClearGeometryMode(gdl++, G_LIGHTING);
        gScrollPixel += gScrollSpeed;
        if (gScrollPixel >= gScrollCharWidth) {
            gScrollPixel -= gScrollCharWidth;
#ifdef PORT_HOST
            PORT_TICKER(PORT_TICKER_SHIFT, gScrollLine[0], NULL); /* bc.exe's widescreen: it leaves the line */
#endif
            for (i = 1; gScrollLine[i] != gTextTerminator[gScrollUsesGlyphs]; i++) {
                gScrollLine[i - 1] = gScrollLine[i];
            }
            i--;
            gScrollLine[i] = fe_player_ticker_next_glyph();
            gScrollLine[i + 1] = gTextTerminator[gScrollUsesGlyphs];
        }
        gdl = spr_begin(gdl);
        gScrollIconX += gScrollSpeed;
        x = 0x136 - gScrollIconX;
        if (x >= -0x1F && x < 0x140) {
            gdl = spr_draw(gdl, gScrollIconSprite, x, 0xC2, gPanelAlpha, 1, 1.0f);
        }
        for (i = 1; i < 5; i++) {
            gScrollMedalX[i] += gScrollSpeed;
            x = 0x136 - gScrollMedalX[i];
            if (x >= -0x1F && x < 0x140) {
                gdl = spr_draw(gdl, gScrollMedalSpriteBase + i, x, 0xC2, gPanelAlpha, 1, 1.0f);
            }
        }
        gdl = spr_end(gdl);
        text_print((Gfx * N64P *) dyn, (gScrollUsesGlyphs == 1) ? NULL : font_narrow(gScrollLine), (gScrollUsesGlyphs == 1) ? gScrollLine : NULL,
                      0, 0, (-gScrollPixel % gScrollCharWidth) - 3, 0xC9, 0x14, 0x14, 1, 0, 0, 0,
                      (gPanelAlpha / 2 - 0x1B < 0) ? 0 : gPanelAlpha / 2 - 0x1B);
        text_print_gradient((Gfx * N64P *) dyn, (gScrollUsesGlyphs == 1) ? NULL : font_narrow(gScrollLine), (gScrollUsesGlyphs == 1) ? gScrollLine : NULL,
                      0, 0, -gScrollPixel % gScrollCharWidth, 0xC7, 0x14, 0x14, 1, 0xFF, 0xFF, 0xFF, gPanelAlpha, 0xFF, 0xFF,
                      0xFF, gPanelAlpha);
#ifdef PORT_HOST
        {
            /* bc.exe's widescreen: the line as drawn, for the characters beyond 4:3 (port.h) */
            PortTicker t;
            s32 k;

            t.line = gScrollLine;
            t.text = gScrollText;
            t.len = sslen;
            t.pos = gScrollPos;
            t.pixel = gScrollPixel;
            t.charWidth = gScrollCharWidth;
            t.speed = gScrollSpeed;
            t.speedGoal = gScrollSpeedGoal;
            t.hold = gScrollHoldTime;
            t.alpha = gPanelAlpha;
            t.iconX = gScrollIconX;
            t.iconPos = gScrollIconPos;
            t.iconSprite = gScrollIconSprite;
            for (k = 0; k < 5; k++) {
                t.medalX[k] = k != 0 ? gScrollMedalX[k] : 9999;
                t.medalPos[k] = k != 0 ? gScrollMedalPos[k] : -1;
            }
            t.medalSprite = gScrollMedalSpriteBase;
            t.maxRank = MAX_RANK();
            t.skip[0] = gTextSpace[0];
            t.skip[1] = gFontAltBlankChar[0];
            t.skip[2] = gFontNoAdvanceChar[0];
            t.noAdvance = gFontNoAdvanceChar[0];
            t.terminator = gTextTerminator[0];
            t.advance = gFontAdvance[0];
            t.glyphs = gScrollUsesGlyphs;
            PORT_TICKER(PORT_TICKER_FRAME, 0, &t);
        }
#endif
        gDPPipeSync(gdl++);
    }
    return gdl;
}

/* Resets a slot to "NEW GAME" and saves it (0x801EA108).
 * Reset a save slot to a fresh "NEW GAME" player and tell the save thread */
void fe_player_new_game(u8 slot, u8 send, u8 newGame) {
    PlayerInfo *p;
    u32 i;

    p = &players[slot];
    for (i = 0; i < 0x100; i++) {
        ((u8 *) p)[i] = 0;
    }
    fe_player_init_vehicles(p);
    sprintf(p->name, "%s", "NEW GAME");
    gSlotState[slot] = 2;
    osSendMesg(&gPakRequestQueue, (OSMesg) ((slot << 16) | 7), OS_MESG_BLOCK);
    if (send) {
        osSendMesg(&gPakRequestQueue, (OSMesg) ((newGame ? 0x14 : 0x15) | (slot << 16) | 0x1000000), OS_MESG_BLOCK);
        osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
    } else {
        osSendMesg(&gPakRequestQueue, (OSMesg) ((newGame ? 0x14 : 0x15) | (slot << 16)), OS_MESG_BLOCK);
    }
}

/* A new player's vehicle mask (0x1063E) (0x801EA268). */
void fe_player_init_vehicles(PlayerInfo *p) {
    p->vehiclesUnlocked = 0x1063E;
}

/* Reads the four save slots and their labels ("PAK FULL") (0x801EA278).
 * Load the four save slots through the save thread and build their menu labels */
void fe_player_load_slots(void) {
    s32 i;
    s32 msg;

    for (i = 0; i < 4; i++) {
        osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 16) | 6 | 0x1000000), OS_MESG_BLOCK);
        osRecvMesg(&pakToGameMessageQ, (OSMesg *) &msg, OS_MESG_BLOCK);
        if (msg == 0) {
            if (font_strcmp((u8 *) (players[i].name), (u8 *) ("NEW GAME"))) {
                gSlotState[i] = 1;
            } else {
                gSlotState[i] = 2;
            }
        } else if (msg != 0x6E382) {
            if (i < gPakFullSlot) {
                osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 16) | 3 | 0x1000000), OS_MESG_BLOCK);
                osRecvMesg(&pakToGameMessageQ, (OSMesg *) &msg, OS_MESG_BLOCK);
            }
            if (msg != 0 || i >= gPakFullSlot) {
                gSlotState[i] = 0;
                sprintf(gSlotLabels[i], "%d : %s", i + 1, "PAK FULL");
            } else {
                fe_player_new_game(i, 1, 0);
            }
        } else {
            fe_player_new_game(i, 1, 0);
        }
        if (gSlotState[i] == 1 || gSlotState[i] == 2) {
            sprintf(gSlotLabels[i], "%d : %s", i + 1, players[i].name);
        }
    }
}

/* The load-game menu (ERASE GAME / IGNORE PAK) (0x801EA4B8).
 * Build the "load game" slot menu (entries 2-5, then ERASE GAME / IGNORE PAK) */
void fe_player_build_load_menu(void) {
    s32 i;

    fe_player_load_slots();
    gLoadMenuFullSlot = 0;
    for (i = 0; i < 4; i++) {
        gYoshiFeItems[i + 2].text = gSlotLabels[i];
        gYoshiFeItems[i + 2].flags |= 0x81;
        gYoshiFeItems[i + 2].flags &= ~0x20;
        gYoshiFeItems[i + 2].color = 7;
        gYoshiFeItems[i + 2].selColor = 4;
        gYoshiFeItems[i + 2].x = 0x50;
        /* an empty then-block: needed for i to be reloaded in the else */
        if (gSlotState[i] != 0) {
        } else {
            gYoshiFeItems[i + 2].flags &= ~0x81;
            gYoshiFeItems[i + 2].color = 8;
            gLoadMenuFullSlot++;
        }
    }
    if (gLoadMenuFullSlot != 4) {
        gYoshiFeItems[6].text = "ERASE GAME";
        gYoshiFeItems[6].jtext = gEraseGameJText;
    } else {
        gYoshiFeItems[6].text = "IGNORE PAK";
        gYoshiFeItems[6].jtext = gIgnorePakJText;
    }
    gYoshiFeItems[6].selColor = 2;
    gYoshiWindows[YW_PLAYER_MENU].flags &= ~0x400;
}

/* The erase-game menu (0x801EA6E8).
 * Build the "erase game" slot menu */
void fe_player_build_erase_menu(void) {
    s32 i;

    fe_player_load_slots();
    for (i = 0; i < 4; i++) {
        gYoshiFeItems[i + 2].x = 0x40;
        switch (gSlotState[i]) {
            case 1:
                sprintf(gSlotLabels[i], "ERASE %d : %s", i + 1, players[i].name);
                gYoshiFeItems[i + 2].flags |= 0x81;
                gYoshiFeItems[i + 2].flags &= ~0x20;
                gYoshiFeItems[i + 2].color = 7;
                gYoshiFeItems[i + 2].selColor = 4;
                break;
            case 0:
            case 2:
                gYoshiFeItems[i + 2].color = 8;
                gYoshiFeItems[i + 2].flags |= 0x20;
                gYoshiFeItems[i + 2].flags &= ~0x81;
                gYoshiFeItems[i + 2].color = 8;
                break;
        }
    }
    gYoshiFeItems[6].text = "GO BACK";
    gYoshiFeItems[6].jtext = gGoBackJText;
    gYoshiFeItems[6].selColor = 2;
    gYoshiWindows[YW_PLAYER_MENU].flags |= 0x400;
    gYoshiWindows[YW_PLAYER_MENU].sel = 6;
}

/* Opens the name entry ("ENTER NAME!") (0x801EA93C).
 * Open the name entry screen (menu entries 7 and 8) on buf */
void fe_player_name_entry_open(char *title, u16 *glyphs, u8 arg2, u8 width, char *buf) {
    gNameWheelAngleGoal = 0x7FFF;
    gNameWheelAngle = 0x7FFF;
    gNameWheelStartSel = 0;
    gNameWheelPrevSel = 0;
    gNameWheelSel = 0;
    gNameFlyChar = -1;
    gNameLen = 0;
    gNameEntryState = 0;
    gNameBoxState = 1;
    gYoshiFeItems[7].w = width;
    gYoshiFeItems[7].h = width;
    gNameEntryWidth = width;
    gNameBoxW = (width * 3) / 5;
    gNameBuf = buf;
    *buf = 0;
    gYoshiFeItems[7].text = gNameBuf;
    gNameBoxX = 0xA0 - gNameBoxW / 2;
    gYoshiFeItems[7].x = gNameBoxX;
    gYoshiFeItems[8].text = title;
    gYoshiFeItems[8].jtext = glyphs;
    gNameMaxLen = arg2;
    gNameEntryFirst = 1;
    gNameStick = 0;
    gNameArrowStep = 1;
    gNameArrowAlpha = 0;
}

#define ABS(x) ((x) > 0 ? (x) : -(x))
#define FABS(x) ((x) > 0.0f ? (x) : -(x))

/* The 33-character wheel, B delete, START accept (0x801EAA7C).
 * Name entry screen: the wheel of 33 characters turned with the stick,
 * the picked character flying into the name, B to delete, START to accept */
Gfx *fe_player_name_entry_draw(Gfx *arg0, PlayerSelDyn *dyn, s32 *count) {
    Gfx *gdl = arg0;
    YoshiItem *e = &gYoshiFeItems[7];
#ifdef NON_MATCHING
    u8 ch = 0; /* indeterminate in the original when no slot is selected */
#else
    u8 ch;
#endif
    s32 i;
    s32 pass;
    s16 diff;
    s32 x;
    s32 y;
#ifdef NON_MATCHING
    s32 selX = 0; /* indeterminate in the original when no slot is selected */
#else
    s32 selX;
#endif
#ifdef NON_MATCHING
    s32 selY = 0; /* indeterminate in the original when no slot is selected */
#else
    s32 selY;
#endif
    s32 selW;
    s32 selH;
    s32 nv = 0;
    s32 len = font_strlen((u8 *) gNameBuf);
    u8 *col = &gYoshiColors[0x80];
    u8 *col2;
    s16 ang;
    s16 ang2;
    u8 alpha;

    if ((gContButtons & (L_JPAD | R_JPAD)) && !(gContPrevButtons & (L_JPAD | R_JPAD))) {
        snd_play(gSfxBank, SFX_THUD, 0);
    }
    gNamePrevStick = gNameStick;
    gNameStick = gPadStickX;
    if (ABS(gNameStick) < 10) {
        gNameStick = 0;
    } else {
        gNameStick *= ABS(gNameStick);
    }
    if (gNameStick != 0) {
        if (gNamePrevStick == 0) {
            gNameWheelStartSel = gNameWheelSel;
        }
        gNameWheelAngle -= gNameStick >> 3;
    } else {
        if (gNamePrevStick != 0 && gNameWheelSel == gNameWheelStartSel) {
            gNameWheelSel += (gNamePrevStick >= 0) ? 1 : -1;
            if (gNameWheelSel < 0) {
                gNameWheelSel += 0x21;
            }
            if (gNameWheelSel >= 0x21) {
                gNameWheelSel -= 0x21;
            }
            gNameWheelAngleGoal = 0x7FFF - gNameWheelSel * 0x7C2;
        }
        diff = gNameWheelAngle - gNameWheelAngleGoal;
        gNameWheelAngle -= diff >> 4;
    }
    gNameWheelSel = (0x83E0 - gNameWheelAngle) / 0x7C2;
    if (gNameWheelSel == 0x21) {
        gNameWheelSel = 0;
    }
    if (gNameStick != 0) {
        gNameWheelAngleGoal = 0x7FFF - gNameWheelSel * 0x7C2;
    }
    if (gNameEntryFirst != 0) {
        gNameWheelPrevSel = gNameWheelSel;
        gNameEntryFirst = 0;
    }
    if (gNameWheelSel != gNameWheelPrevSel) {
        snd_play(gSfxBank, SFX_NAME_WHEEL_TICK, 0);
    }
    gNameWheelPrevSel = gNameWheelSel;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 0x21; i++) {
            ang = gNameWheelAngle + i * 0x7C2 - 0x6000;
            ang2 = ang - 0x2000;
            ang2 = ang2 + 0x7FFF;
            ang2 = ang2 + 0x2000;
            ang2 = ang2 + 0x7FFF;
            x = sins(ang2) * 120.0 / 32768.0 - 7.0;
            y = coss(ang2) * 96.0 / 32768.0 - 10.0;
            if (i < gNameEntryLetterCount[0]) {
                gNameEntryGlyph[0] = gNameEntryLetters[0][i];
            } else {
                gNameEntryGlyph[0] = gNameEntrySymbols[0][i - gNameEntryLetterCount[0]];
            }
            if (gNameEntryGlyph[0] != gNameFlyChar) {
                if (pass != 0) {
                    if (gNameWheelSel == i) {
                        ch = gNameEntryGlyph[0], selX = x, selY = y, selW = 0x18, selH = 0x14;
                    } else {
                        text_print_gradient((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 1, 0, x, y, 0x18, 0x14, 1, 0xC8, 0xC8, 0xC8, gYoshiAlpha, 0xFF,
                                      0xFF, 0xFF, gYoshiAlpha);
                    }
                } else if (gNameWheelSel == i) {
                    text_print((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 1, 0, x - 0xB, y, 0x30, 0x28, 1, 0, 0, 0, gYoshiAlpha / 2);
                } else {
                    text_print((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 1, 0, x - 4, y + 3, 0x18, 0x14, 1, 0, 0, 0, gYoshiAlpha / 2);
                }
            }
        }
    }
    if (ch != gNameFlyChar && gNameFlyChar == -1) {
        if (gScheduler.frameCount % 20 < 15) {
            alpha = gYoshiAlpha;
        } else {
            alpha = gYoshiAlpha / 3;
        }
        gNameEntryGlyph[0] = ch;
        text_flush(&gdl, (s32) dyn);
        text_print_gradient((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 1, 0, selX - 5, selY - 5, selW * 2, selH * 2, 1, col[0], col[1], col[2],
                      alpha, col[4], col[5], col[6], alpha);
    }
    if (yoshiState == 2 && (gContButtons & START_BUTTON) && !(gContPrevButtons & START_BUTTON) && areWeFading() == 0) {
        if (gNameLen != 0) {
            gNextGameMode = MODE_NAME_ENTERED;
            gSlotState[playerNumber] = 1;
            yoshi_demand(YOSHI_DEMAND_OFF);
        } else {
            snd_play(gSfxBank, SFX_ERROR, 0);
        }
    }
    switch (gNameEntryState) {
        case 0:
            if (yoshiState == 2 && (gContButtons & (A_BUTTON | B_BUTTON)) && !(gContPrevButtons & (A_BUTTON | B_BUTTON)) && areWeFading() == 0) {
                if (gContButtons & B_BUTTON) {
                    ch = 0x7F;
                }
                if (ch == 0x7F) {
                    if (gNameLen == 0) {
                        if (gContButtons & B_BUTTON) {
                            if (gUseEeprom != 0) {
                                fade_to_mode_speed(MODE_ATTRACT_START, 0.5f);
                            } else {
                                gNextGameMode = MODE_LOAD_GAME_RETURN;
                            }
                            snd_play(gSfxBank, SFX_BACK, 0);
                            yoshi_demand(YOSHI_DEMAND_OFF);
                        } else {
                            snd_play(gSfxBank, SFX_ERROR, 0);
                        }
                    } else {
                        gNameLen--;
                        gNameBuf[gNameLen] = 0;
                        snd_play(gSfxBank, SFX_NAME_DELETE, 0);
                        gNameBoxState = 0;
                        gNameFlyBoxW = e->w;
                        i = gNameFlyCharSize = gNameEntryWidth;
                        i = i * 3 / 5;
                        gNameTextX = e->x;
                        gNameTextXGoal = -((len - 1) * i / 2) - i / 2 + 0xA0;
                        gNameBoxXAnim = gNameBoxX;
                        gNameBoxXGoal = (len - 1) * i / 2 - i / 2 + 0xA0;
                    }
                } else if (gNameLen < gNameMaxLen) {
                    snd_play(gSfxBank, SFX_TYPE_ALT, 0);
                    gNameEntryState = 1;
                    gNameBoxState = 0;
                    gNameFlyChar = ch;
                    gNameFlyBoxW = e->w;
                    i = gNameFlyCharSize = gNameEntryWidth;
                    i = i * 3 / 5;
                    gNameTextX = e->x;
                    gNameTextXGoal = -((len + 1) * i / 2) - i / 2 + 0xA0;
                    gNameBoxXAnim = gNameBoxX;
                    gNameBoxXGoal = (len + 1) * i / 2 - i / 2 + 0xA0;
                    gFlyCharX = selX - 5;
                    gFlyCharY = selY - 5;
                    gFlyCharXGoal = gNameBoxXGoal - 160.0f - i;
                    gFlyCharYGoal = e->y - 0x78;
                    gFlyCharW = 48.0f;
                    gFlyCharH = 40.0f;
                    gFlyCharWGoal = gNameFlyCharSize;
                    gFlyCharHGoal = gNameFlyCharSize;
                } else {
                    snd_play(gSfxBank, SFX_ERROR, 0);
                }
            }
            break;
        case 1:
            gFlyCharX += (gFlyCharXGoal - gFlyCharX) * 0.2;
            gFlyCharY += (gFlyCharYGoal - gFlyCharY) * 0.2;
            gFlyCharW += (gFlyCharWGoal - gFlyCharW) * 0.2;
            gFlyCharH += (gFlyCharHGoal - gFlyCharH) * 0.2;
            gNameEntryGlyph[0] = gNameFlyChar;
            text_print((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 0, 0, gFlyCharX - 4.0f, gFlyCharY + 4.0f, gFlyCharW, gFlyCharH, 1, 0,
                          0, 0, gYoshiAlpha / 2);
            text_print_gradient((Gfx * N64P *) dyn, gNameEntryGlyph, 0, 0, 0, gFlyCharX, gFlyCharY, gFlyCharW, gFlyCharH, 1, col[0], col[1],
                          col[2], gYoshiAlpha, col[4], col[5], col[6], gYoshiAlpha);
            if (FABS(gFlyCharX - gFlyCharXGoal) < 0.15 && FABS(gFlyCharY - gFlyCharYGoal) < 0.15) {
                snd_play(gSfxBank, SFX_CLUNK, 0);
                gNameEntryState = 0;
                gNameBoxState = 0;
                gNameBuf[gNameLen] = gNameEntryGlyph[0];
                gNameLen++;
                gNameBuf[gNameLen] = 0;
                gNameFlyChar = -1;
            }
            break;
    }
    text_flush(&gdl, (s32) dyn);
    gNameArrowPulse += gNameArrowStep;
    if (gNameArrowStep < 0) {
        gNameArrowPulse += gNameArrowStep * 2;
    }
    if (gNameArrowPulse < 0 || gNameArrowPulse >= 0x10) {
        gNameArrowPulse -= gNameArrowStep * 2;
        gNameArrowStep = -gNameArrowStep;
    }
    if (yoshiState == 2) {
        gNameArrowAlpha = (s32) ((gNameArrowAlpha + 0x10 < 0xFF) ? gNameArrowAlpha + 0x10 : 0xFF);
    } else {
        gNameArrowAlpha = 0;
    }
    col2 = &gYoshiColors[0x98];
    nv = hud_sprite_quad_colors((struct SpriteVtxBuf *) dyn, 3, nv, gNameBoxX + gNameBoxW + gNameArrowPulse, e->y + e->h / 2,
                       e->w / 3 + gNameArrowPulse / 2, e->h / 2 + 3, gYoshiColors[0x98], gYoshiColors[0x99],
                       gYoshiColors[0x9A], gNameArrowAlpha, gYoshiColors[0x9C], gYoshiColors[0x9D], gYoshiColors[0x9E], gNameArrowAlpha,
                       gYoshiColors[0x98], gYoshiColors[0x99], gYoshiColors[0x9A], gNameArrowAlpha, gYoshiColors[0x9C],
                       gYoshiColors[0x9D], gYoshiColors[0x9E], gNameArrowAlpha);
    nv = hud_sprite_quad((struct SpriteVtxBuf *) dyn, 3, nv, gNameBoxX + gNameBoxW + gNameArrowPulse + 4, e->y + e->h / 2 + 3,
                       e->w / 3 + gNameArrowPulse / 2, e->h / 2 + 3, 0, 0, 0, gNameArrowAlpha / 2);
    gdl = hud_sprite_setup(gdl, 0);
    gSPVertex(gdl++, &dyn->vtx[0], 8, 0);
    gSP1Triangle(gdl++, 4, 5, 6, 0);
    gSP1Triangle(gdl++, 4, 6, 7, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    nv = hud_sprite_quad_colors((struct SpriteVtxBuf *) dyn, 2, nv, e->x - gNameArrowPulse - 4, e->y + e->h / 2, e->w / 3 + gNameArrowPulse / 2,
                       e->h / 2 + 3, col2[0], col2[1], col2[2], gNameArrowAlpha, col2[4], col2[5], col2[6], gNameArrowAlpha,
                       col2[0], col2[1], col2[2], gNameArrowAlpha, col2[4], col2[5], col2[6], gNameArrowAlpha);
    nv = hud_sprite_quad((struct SpriteVtxBuf *) dyn, 2, nv, e->x - gNameArrowPulse - 8, e->y + e->h / 2 + 3, e->w / 3 + gNameArrowPulse / 2,
                       e->h / 2 + 3, 0, 0, 0, gNameArrowAlpha / 2);
    gdl = hud_sprite_setup(gdl, 0);
    gSPVertex(gdl++, &dyn->vtx[nv - 8], 8, 0);
    gSP1Triangle(gdl++, 4, 5, 6, 0);
    gSP1Triangle(gdl++, 4, 6, 7, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    switch (gNameBoxState) {
        case 0:
            gNameTextX += (gNameTextXGoal - gNameTextX) * 0.2;
            e->x = gNameTextX;
            gNameBoxXAnim += (gNameBoxXGoal - gNameBoxXAnim) * 0.2;
            gNameBoxX = gNameBoxXAnim;
            break;
        case 1:
            break;
    }
    *count += gdl - arg0;
    return gdl;
}

/* Both medals after a 50-frame wait, no sound (0x801EC288). */
void fe_player_medals_show_quiet(u8 arg0) {
    s32 i;

    gMedalRank[0] = 3;
    gMedalRank[1] = arg0;
    for (i = 0; i < 2; i++) {
        gMedalState[i] = 4;
        gMedalDelay[i] = 0x32;
        gMedalAlpha[i] = 0;
    }
}

/* The medal sprites; a staggered reveal with sound (0x801EC30C). */
void fe_player_medals_init(u8 arg0) {
    s32 j;
    s32 i;

    gMedalSpriteBase = spr_load(gMedalSprites, gMedalPalettes, 4, 2, 0, 1.0f);
    spr_load(gMedalExtraSprites, 0, 1, 2, 1, 1.0f);
    gMedalRank[0] = 3;
    gMedalRank[1] = arg0;
    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
        j = 0;
    } else {
        j = 1;
    }
    for (i = 0; j < 2; j++, i++) {
        gMedalState[j] = 0;
        gMedalDelay[j] = i * 60 + 0x28;
        gMedalAlpha[j] = 0;
    }
}

/* Both medals fade out (0x801EC464). */
void fe_player_medals_hide(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        gMedalState[i] = 3;
    }
}

/* One medal: its state step and drawing (0x801EC49C).
 * Draw a save slot's rank badge, stepping its state: 0/4 count down, 1 fade
 * in, 2 shown, 3 fade out (gMedalState/Delay/Alpha[slot]) */
Gfx *fe_player_draw_medal(Gfx *arg0, s32 x, s32 y, u8 slot) {
    Gfx *gdl = arg0;
    s32 pad;
    s32 mode;

    if (yoshiState == 8) {
        gMedalState[slot] = 3;
    }
    switch (gMedalState[slot]) {
        case 0:
            gMedalDelay[slot]--;
            if (gMedalDelay[slot] == 0) {
                gMedalState[slot] = 1;
                if (gMedalRank[slot] == 5) {
                    snd_play(gSfxBank, SFX_MEDAL_NONE, 0);
                } else {
                    snd_play(gSfxBank, SFX_MEDAL, 0);
                }
            }
            break;
        case 4:
            gMedalDelay[slot]--;
            if (gMedalDelay[slot] == 0) {
                gMedalState[slot] = 1;
            }
            break;
        case 1:
            gMedalAlpha[slot] += 0x10;
            if (gMedalAlpha[slot] >= 0x100) {
                gMedalAlpha[slot] = 0xFF;
                gMedalState[slot] = 2;
            }
            break;
        case 3:
            gMedalAlpha[slot] -= 0x20;
            if (gMedalAlpha[slot] < 0) {
                gMedalAlpha[slot] = 0;
            }
            break;
        case 2:
            break;
    }
    if (gMedalState[slot] == 0) {
        return gdl;
    }
    if (gMedalRank[slot] == 5) {
        mode = 2;
    } else {
        mode = 1;
    }
    gdl = spr_draw(gdl, gMedalRank[slot] % 5 + gMedalSpriteBase, x, y, gMedalAlpha[slot], mode, 1.0f);
    return gdl;
}

/* The results medals and the next medal's target time (0x801EC770).
 * Draw the save-slot panel: the two slot boxes, or the rank badge and the
 * level's target time for it */
Gfx *fe_player_draw_medals(Gfx *start, void *gfxp, s32 *count) {
    Gfx *gdl;
#ifdef NON_MATCHING
    u8 n = 0; /* the first branch leaves it indeterminate in the original */
#else
    u8 n;
#endif

    gdl = start;
    gdl = spr_begin(gdl);
    if (gLevelKind == LEVEL_KIND_MISSION) {
        gdl = fe_player_draw_medal(gdl, 0x5C, 0x68, 0);
        if (gMedalRank[1] != 0) {
            gdl = fe_player_draw_medal(gdl, 0xA8, 0x68, 1);
        }
    } else {
        if (gMedalRank[1] == 5) {
            n = 1;
        } else {
            n = (MAX_RANK() < gMedalRank[1] + 1) ? MAX_RANK() : gMedalRank[1] + 1;
        }
        gdl = fe_player_draw_medal(gdl, 0x82, 0x68, 1);
        gdl = spr_draw(gdl, n + gMedalSpriteBase, 0x2E, 0x6C, (gMedalAlpha[1] * 3) / 4, 0, 0.75f);
    }
    gdl = spr_end(gdl);
    if (gLevelKind != LEVEL_KIND_MISSION) {
        hud_format_time((char *) gMedalTimeText, gLevelInfo[levelno].times[5 - n], 0);
        gMedalTimeText[5] = 0;
        text_print_gradient(gfxp, gMedalTimeText, 0, 0, 0, 0x29, 0x7D, 0x10, 0x10, 1, 0xFF, 0xB4, 0, gMedalAlpha[1], 0xFF, 0x78, 0,
                      gMedalAlpha[1]);
    }
    *count += gdl - start;
    return gdl;
}

/* The game mode after a level (0x801ECA50).
 * Level-select mask bit for a level: done, of a 0x81 type, or other */
u64 fe_player_loop_done_mode(u8 level) {
    u64 mask;

    if ((players[playerNumber].medal[level] > 0 && players[playerNumber].medal[level] < 6) ? 1 : 0) {
        mask = 0x80;
    } else if (gLevelInfo[level].kind & LEVEL_KIND_ANY_MISSION) {
        mask = 0x800;
    } else {
        mask = 0x20000000;
    }
    return mask;
}

/* A level picked on the globe: reads its saved records (0x801ECB18).
 * A level was picked on the globe: ask the save thread for its saved records */
void fe_player_load_level_save(void) {
    fe_open_reachable_levels(8);
    gRouteRevealFlags = 0;
    gLevelFirstClear = 0;
    if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) {
        if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
            osSendMesg(&gPakRequestQueue, (OSMesg) ((levelno << 8) | 0xC | (playerNumber << 16)), OS_MESG_BLOCK);
        }
        osSendMesg(&gPakRequestQueue, (OSMesg) ((levelno << 8) | 8 | (playerNumber << 16) | 0x1000000), OS_MESG_BLOCK);
    } else {
        osSendMesg(&gPakRequestQueue, (OSMesg) ((levelno << 8) | 0x16 | (playerNumber << 16) | 0x1000000), OS_MESG_BLOCK);
        fe_best_clear_times(playerNumber);
    }
}

/* Writes every pending save (saveIt; "saving player %d") (0x801ECC8C).
 * Send the pending saves (saveIt) to the save thread */
void fe_player_save_pending(void) {
    s32 i;
    s32 shown;

    shown = 0;
    for (i = 0; i < 4; i++) {
        if (saveIt[i] != 0) {
            if (shown == 0) {
                music_fade_to(0.0f);
                shown = 1;
            }
            osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 16) | 0x14), OS_MESG_BLOCK);
            debug_printf("saving player %d on level %d\n", i, saveIt[i] - 1);
            players[i].levelno = levelno;
            osSendMesg(&gPakRequestQueue, (OSMesg) (((saveIt[i] - 1) << 8) | 7 | (i << 16)), OS_MESG_BLOCK);
            osSendMesg(&gPakRequestQueue, (OSMesg) (((saveIt[i] - 1) << 8) | 9 | (i << 16)), OS_MESG_BLOCK);
            osSendMesg(&gPakRequestQueue, (OSMesg) (((saveIt[i] - 1) << 8) | 0xB | (i << 16)), OS_MESG_BLOCK);
            if (saveLevel != 0 && playerNumber == i) {
                osSendMesg(&gPakRequestQueue, (OSMesg) (((saveLevel - 1) << 8) | 0xD | (playerNumber << 16)),
                           OS_MESG_BLOCK);
                saveLevel = 0;
            }
            osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 16) | 0x15 | 0x1000000), OS_MESG_BLOCK);
            osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
            saveIt[i] = 0;
        }
    }
}

/* 0x81-kind levels become 0x80 or 1 by the game state (0x801ECE9C).
 * Retype the 0x81-type levels: 0x80 once unk91 >= 11 in mode 0x4000, else 1 */
void fe_player_update_level_kinds(void) {
    s32 i;

    for (i = 0; i < 0x3C; i++) {
        if (gLevelInfo[i].kind & LEVEL_KIND_ANY_MISSION) {
            if (players[playerNumber].gameState >= 11 && gNextGameMode == MODE_WORLD) {
                gLevelInfo[i].kind = LEVEL_KIND_MISSION_TIMED;
            } else {
                gLevelInfo[i].kind = LEVEL_KIND_MISSION;
            }
        }
    }
}


/* Game state 9: missions re-ranked by time (0x801ECF5C).
 * After a level: re-rank the player's medalled missions (LEVEL_KIND_MISSION,
 * except the non-graded levels 0x31, 0x2F and 0x26) from their saved times,
 * add up the rank points and award new titles (menu entries 185-194) */
void fe_player_regrade_missions(void) {
    PlayerInfo *p = &players[playerNumber];
    u8 buf[0x20];
    LevelTimesView *info;
    u8 stars[5] = { 0, 0, 0, 0, 0 };
    u8 gained;
    u16 oldStars = p->units;
    s32 i;
    s32 x;
    u16 time;
    u8 rank;
    u8 level;

    fe_player_copy_times((u8 *) gBestTimes[playerNumber], buf);
    PLAYER_ASSERT(saveIt[playerNumber], 1384);
    for (level = 0; level < 0x3C; level++) {
        if (((players[playerNumber].medal[level] > 0 && players[playerNumber].medal[level] < 6) ? 1 : 0) &&
            gLevelInfo[level].kind == LEVEL_KIND_MISSION && level != 0x31 && level != 0x2F && level != 0x26) {
            info = &gLevelInfo[level];
            if (saveIt[playerNumber] != level + 1) {
                osSendMesg(&gPakRequestQueue, (OSMesg) ((level << 8) | 8 | (playerNumber << 16) | 0x1000000), OS_MESG_BLOCK);
                osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
            } else {
                fe_player_copy_times(buf, (u8 *) gBestTimes[playerNumber]);
            }
            time = gBestTimes[playerNumber][gVehicleTimeSlot[0]];
            debug_printf("level %d time is %d\n", level, time);
            rank = fe_stats_grade_time(time, level, players[playerNumber].gameState);
            p->medal[level] = rank;
            stars[rank - 1]++;
        }
    }
    fe_player_copy_times(buf, (u8 *) gBestTimes[playerNumber]);
    levelno = 0;
    debug_printf(" %d %d %d %d %d\n", stars[2] * 3, stars[1] * 2, stars[0], p->units, oldStars);
    p->units += stars[2] * 3 + stars[1] * 2 + stars[0];
    gained = p->units / 12 - oldStars / 12;
    p->title += gained;
    debug_printf("%d stars\n", gained);
    i = 0;
    x = (0x140 - gained * 32) / 2 + 0x28;
    for (; i < gained; i++, x += 0x20) {
        gYoshiFeItems[i + 189].flags |= 0x100;
        gYoshiFeItems[i + 189].x = x;
        gYoshiFeItems[i + 189].sprite = 0x33;
    }
    for (; i < 6; i++) {
        gYoshiFeItems[i + 189].flags &= ~0x100;
        gYoshiFeItems[i + 189].sprite = 0;
    }
    for (i = 0; i < 3; i++) {
        sprintf(gMedalCountText[i], "*******%-2d**", stars[2 - i]);
        gYoshiFeItems[i + 185].text = gMedalCountText[i];
    }
}

/* Copies a player's 0x20-byte time record (0x801ED480). */
void fe_player_copy_times(u8 *src, u8 *dst) {
    u32 i;

    for (i = 0; i < 0x20; i++) {
        dst[i] = src[i];
    }
}

/* Game state 11: every level re-ranked, platinum allowed (0x801ED4B8).
 * Like fe_player_regrade_missions for all finished levels after a cheat-mode ("cmo") run:
 * re-rank them, a rank of 4 is worth one point */
void fe_player_regrade_platinum(void) {
    PlayerInfo *p = &players[playerNumber];
    u8 buf[0x20];
    LevelTimesView *info;
    u8 gained;
    u16 oldStars = p->units;
    s32 i;
    u8 rank;
    u8 level;

    fe_player_copy_times((u8 *) gBestTimes[playerNumber], buf);
    PLAYER_ASSERT(saveIt[playerNumber], 1472);
    for (level = 0; level < 0x3C; level++) {
        if (((players[playerNumber].medal[level] > 0 && players[playerNumber].medal[level] < 6) ? 1 : 0) &&
            level != 0x31 && level != 0x2F && level != 0x26) {
            info = &gLevelInfo[level];
            if (saveIt[playerNumber] != level + 1) {
                osSendMesg(&gPakRequestQueue, (OSMesg) ((level << 8) | 8 | (playerNumber << 16) | 0x1000000), OS_MESG_BLOCK);
                osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
            } else {
                fe_player_copy_times(buf, (u8 *) gBestTimes[playerNumber]);
            }
            rank = fe_stats_grade_time(gBestTimes[playerNumber][gVehicleTimeSlot[players[playerNumber].bestVehicle[level]]], level,
                                 players[playerNumber].gameState + 1);
            p->medal[level] = rank;
            if (rank == 4) {
                p->units++;
            }
        }
    }
    fe_player_copy_times(buf, (u8 *) gBestTimes[playerNumber]);
    levelno = 0;
    gained = p->units / 12 - oldStars / 12;
    p->title += gained;
    debug_printf("cmo destroy %d stars\n", gained);
}
