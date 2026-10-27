#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gEepromPresent
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define gMainThread ((u8 *) &gMainThread)
#define players ((PlayerInfo *) players)
#define pakBuffer ((u8 *) pakBuffer)
#ifdef NON_MATCHING
#define gEepromPresent (*(s32 *) &gEepromPresent)
#endif
/* end of views */

/*
 * pfsHandler.c: the Controller Pak / EEPROM save thread. Player records
 * (0x100 bytes each) and best times (0x20 bytes each) go to a "NBCE"/"01"
 * Controller Pak file, or to EEPROM, each block followed by a 4-byte CRC.
 */

u8 __osContDataCrc(u8 *data);
s32 osPfsInitPak(OSMesgQueue *, OSPfs *, int);

#define PFS_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "pfsHandler.c", line)
#define PFS_FILE_SIZE 0xE00

/*
 * The old-SDK EEPROM calls, by address: undefined_syms.hd_front_end defines
 * these func_ names as absolute symbols at the functions' original addresses.
 * The names in symbol_addrs (osEepromWrite ...) are right, but calling them by
 * name makes the NON_MATCHING build jump to its relocated copies instead of the
 * original addresses, which changes the NM ROM; so the macros stay.
 */
s32 func_80204C10(OSMesgQueue *, u8, u8 *);
s32 func_802050F0(OSMesgQueue *, u8, u8 *);
s32 func_802042D0(OSMesgQueue *, u8, u8 *, int);
s32 func_80204410(OSMesgQueue *, u8, u8 *, int);
#define osEepromWrite func_80204C10
#define osEepromRead func_802050F0
#define osEepromLongWrite func_802042D0
#define osEepromLongRead func_80204410

typedef struct {
    u32 file_size;
    u32 game_code;
    u16 company_code;
    u8 ext_name[4];
    u8 game_name[16];
} OldPfsState; /* the old SDK's OSPfsState: no pad after company_code */

/* (players[]: PlayerInfo, game/types.h) */

/* (gLevelInfo[]: LevelInfo, game/types.h) */

extern OSPfs gPfs;      /* hd_code .bss: the Controller Pak file system */
extern u8 gPakGameName[];       /* game name */
extern u8 gPakExtName[];       /* extension name */
extern u8 gPakCharset[];       /* character set (0x45 entries) */
extern OldPfsState gPakFileStates[]; /* the pak's files */
extern s32 gPakFileCount;        /* number of files listed */
extern u8 gPakFileNames[][17];   /* file game names */
extern u8 gPakFileExts[][5];    /* file extensions */
extern char gPakFileLines[][40]; /* file menu lines */
extern s32 gPakFreeBytes;        /* free bytes */
extern char gPakFreeText[];
extern char gPakNeededText[];
extern u8 gDeleteFileJText[];
extern s32 gPakFileNo[];      /* file_no per player */
extern u8 gPakThreadStack[];       /* thread stack (0x1000) */
extern OSMesg gPakRequestMesgs[];
extern OSMesgQueue gPakRetraceQueue;
extern OSMesg gPakRetraceMesgs[];
extern OSMesg gPakReplyMesgs[];
#ifndef NON_MATCHING
extern s32 gEepromPresent;
#endif
extern u8 gSaveImage[];
extern u8 gDummyEeprom[];

int sprintf(char *, const char *, ...);
void bcopy(const void *, void *, int);

#define playerInfo (players[0])

#ifdef NON_MATCHING
void fe_pak_thread(void *);
#else
void fe_pak_thread();
#endif
void fe_pak_chars_to_ascii(u8 *str);
void fe_pak_ascii_to_chars(u8 *str);
s32 fe_pak_add_crc(u8 *data, s32 size);
s32 fe_pak_check_crc(u8 *data, s32 size);
s32 fe_pak_get_semaphore(u8 player, u64 *sem);

/* The save thread: queues, scheduler client, file names (0x801F57B0). */
void fe_pak_start_thread(void) {
    s32 filesize;

    filesize = 0xDE0;
    osCreateThread(&gPakThread, 2, fe_pak_thread, NULL, gPakThreadStack + 0x1000, 0xB);
    osCreateMesgQueue(&gPakRequestQueue, gPakRequestMesgs, 8);
    osCreateMesgQueue(&gPakRetraceQueue, gPakRetraceMesgs, 1);
    osCreateMesgQueue(&pakToGameMessageQ, gPakReplyMesgs, 8);
    fe_pak_ascii_to_chars(gPakGameName);
    fe_pak_ascii_to_chars(gPakExtName);
    debug_printf("current pak file size is %d bytes\n", filesize);
    debug_printf("current playerInfo size is %d bytes\n", 0x100);
    sched_add_client(&gScheduler, &gPakSchedClient, &gPakRetraceQueue, 1, 3);
    PFS_ASSERT(filesize<PFS_FILE_SIZE, 104);
    osStartThread(&gPakThread);
}

/*
 * fe_pak_thread, the save thread's main loop (447 insns). Matched (Oct 2026): the target
 * ends `case 1:` (the last case body) with a `b` to the very next instruction, the switch
 * end, carrying `sb done` in its delay slot. That branch comes from an empty test in an
 * else branch (`else { if (tries) { } }`): IDO drops the side-effect-free condition but
 * keeps the jump that skipped it. A constant condition (`if (0)`), a plain empty else and
 * ~45 other spellings (see git history) drop the branch. The callers' fe_pak_create_file,
 * fe_pak_delete_file and fe_pak_get_file_state return values are used here, and
 * fe_pak_thread takes no argument (the NON_MATCHING build keeps osCreateThread's
 * `void *` parameter). Checks: tools_port/checks/fe_0E7B0.txt.
 */
extern s32 gPakErrorShown;     /* a Yoshi window was opened for a pak error */
extern s32 gPakErrorWindow;     /* Yoshi window of the last pak error */
s32 fe_pak_is_missing(void);
s32 fe_pak_init_pak(void);
s32 fe_pak_create_file(u8 player);
s32 fe_pak_delete_file(s32 arg0);
s32 fe_pak_delete_listed_file(u8 item);
s32 fe_pak_rw_player(u8 player, u8 rw);
s32 fe_pak_rw_times(u8 player, u8 slot, u8 rw);
s32 fe_pak_rw_eeprom_times(u8 pn, u8 level, u8 rw);
s32 fe_pak_put_semaphore(u8 player, u64 sem);
s32 fe_pak_rw_status(u8 player, u8 nlevels, u8 rw);
s32 fe_pak_get_file_state(u8 arg0);

/* One command per message, retries, error windows, reply (0x801F58E8).
 * The save thread: one pak/EEPROM command per message (cmd | arg1 << 8 |
 * player << 16 | reply << 24), retried until it settles, the result sent
 * back on pakToGameMessageQ when `reply` is set. */
#ifdef NON_MATCHING
void fe_pak_thread(void *arg) {
#else
void fe_pak_thread(void) { /* the original takes no parameter (no $a0 home) */
#endif
    OSMesg msg;
#ifdef NON_MATCHING
    s32 ret = 0; /* a "Nonsense pak message" keeps the previous command's result; on the first pass the original reads an indeterminate register */
#else
    s32 ret;
#endif
    s32 lastRet;
    u8 pad33;
    u8 player;
    u8 arg1;
    u8 reply;
    u8 cmd;
    u8 next;
    u8 done;
    s32 yoshi;
    u32 tries;

    while (1) {
        pakBusy = 0;
        tries = 0;
        osSetEventMesg(OS_EVENT_SI, &gSiMesgQueue, 0);
        osRecvMesg(&gPakRequestQueue, &msg, OS_MESG_BLOCK);
        osSetEventMesg(OS_EVENT_SI, &gSiMesgQueue, 0);
        while (gPreNmi != 0) {
        }
        cmd = (u32) msg & 0xFF;
        arg1 = ((u32) msg >> 8) & 0xFF;
        player = ((u32) msg >> 16) & 0xFF;
        reply = ((u32) msg >> 24) & 0xFF;
        gPakExtName[0] = player + 0x11;
        pakBusy = 1;
        cont_wait_read();
        fe_pak_request_hook();
        gPakErrorShown = 0;
        do {
            done = 0;
            next = 0;
            yoshi = 0x5B;
            lastRet = 0;
            switch (cmd) {
                case 1:
                case 2:
                    ret = fe_pak_init_pak();
                    break;
                case 3:
                    ret = fe_pak_create_file(player);
                    break;
                case 4:
                    ret = fe_pak_delete_file(player);
                    break;
                case 5:
                    ret = fe_pak_delete_listed_file(player);
                    break;
                case 6:
                    ret = fe_pak_rw_player(player, 0);
                    break;
                case 7:
                    ret = fe_pak_rw_player(player, 1);
                    break;
                case 8:
                    ret = fe_pak_rw_times(player, arg1, 0);
                    break;
                case 9:
                    ret = fe_pak_rw_times(player, arg1, 1);
                    break;
                case 10:
                    ret = fe_pak_rw_eeprom_times(player, arg1, 0);
                    break;
                case 11:
                    ret = fe_pak_rw_eeprom_times(player, arg1, 1);
                    break;
                case 12:
                    ret = fe_pak_rw_status(player, arg1, 0);
                    break;
                case 13:
                    ret = fe_pak_rw_status(player, arg1, 1);
                    break;
                case 14:
                    ret = osPfsFreeBlocks(&gPfs, &gPakFreeBytes);
                    break;
                case 15:
                    ret = fe_pak_is_missing();
                    break;
                case 16:
                    done = 1;
                    ret = osEepromProbe(&gSiMesgQueue);
                    break;
                case 17:
                    ret = fe_pak_get_file_state(player);
                    break;
                case 18:
                    ret = osPfsChecker(&gPfs);
                    break;
                case 19:
                    ret = 10;
                    break;
                case 20:
                    ret = fe_pak_put_semaphore(player, 0x2704197125121981);
                    break;
                case 21:
                    ret = fe_pak_put_semaphore(player, 0x87569AB6CD076AEC);
                    break;
                case 22:
                    ret = 0;
                    break;
                default:
                    /* ret keeps its previous value (unset on the first pass) */
                    debug_printf("Nonsense pak message\n");
                    break;
            }
            debug_printf("pak command %d returned %d\n", cmd, ret);
            switch (ret) {
                case 0x6E382:
                    if ((gGameMode & (MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_PAK_CHECK)) || (gNextGameMode & MODE_BOOT_CHECK)) {
                        done = 1;
                        break;
                    }
                    // fallthrough
                case 6:
                case 10:
                case 11:
                    if (tries >= 4) {
                        if (cmd != 0x13) {
                            if (ret == 0x6E382) {
                                gPakErrorWindow = 0x5D;
                            } else {
                                gPakErrorWindow = 0x5C;
                            }
                            fe_pak_put_semaphore(player, 0x2704197125121981);
                            next = 0x13;
                        }
                        yoshi = gPakErrorWindow;
                    } else {
                        yoshi = 0;
                        tries++;
                    }
                    break;
                case 0:
                case 5:
                case 9:
                    done = 1;
                    break;
                case 8:
                    if (!(gGameMode & (MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_PAK_CHECK)) || fe_pak_is_missing() != 0) {
                        break;
                    }
                    // fallthrough
                case 7:
                    gPakFullSlot = (player < gPakFullSlot) ? player : gPakFullSlot;
                    done = 1;
                    break;
                case 3:
                    if (tries >= 4) {
                        if (cmd != 0x13) {
                            fe_pak_put_semaphore(player, 0x2704197125121981);
                            next = 0x13;
                        }
                        yoshi = 0x5C;
                    } else {
                        debug_printf("trying to fix pak ...\n");
                        if (cmd != 0x12) {
                            osSendMesg(&gPakRequestQueue, (OSMesg) (cmd | (arg1 << 8) | (player << 16) | (reply << 24)),
                                       OS_MESG_NOBLOCK);
                        }
                        next = 0x12;
                        reply = 0;
                        tries++;
                    }
                    break;
                case 2:
                    /* never true: lastRet is reset at the top of every pass */
                    if (lastRet == 8 && !(gGameMode & (MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_PAK_CHECK))) {
                        PFS_ASSERT(1==0, 342);
                        done = 1;
                    }
                    break;
                case 1:
                    if (gNextGameMode & MODE_BOOT_CHECK) {
                        done = 1;
                    } else {
                        /* an empty test: IDO drops the condition but keeps the `b` to the
                         * switch end (the target's branch to the next instruction) */
                        if (tries) {
                        }
                    }
                    break;
            }
            lastRet = ret;
            if (gPreNmi == 0 && yoshi != 0 && !done && !next && pakToGameMessageQ.mtqueue == (OSThread *) gMainThread) {
                fe_pak_error_frame(yoshi);
                gPakErrorShown = 1;
            }
            if (next) {
                cmd = next;
                next = 0;
            }
            osRecvMesg(&gPakRetraceQueue, NULL, OS_MESG_BLOCK);
        } while (!done);
        if (gPakErrorShown) {
            yoshiState = 1;
            currentYoshiWindow = NO_YOSHI_WINDOW;
        }
        if (reply) {
            osSendMesg(&pakToGameMessageQ, (OSMesg) ret, OS_MESG_BLOCK);
        }
    }
}

/* Command 15: 1 if no Controller Pak is in port 1 (0x801F5FE4). */
s32 fe_pak_is_missing(void) {
    s32 ret;
    u8 status;
    OSMesg msg;

    msg = 0;
    if (gSiMesgQueue.validCount != 0) {
        PFS_ASSERT(1==0, 400);
        osRecvMesg(&gSiMesgQueue, &msg, OS_MESG_NOBLOCK);
    }
    if (cont_query_paks(&gSiMesgQueue, &status)) {
        ret = 1;
    } else if (!(status & 1)) {
        ret = 1;
    } else {
        ret = 0;
    }
    if (msg != 0) {
        PFS_ASSERT(1==0, 414);
        osSendMesg(&gSiMesgQueue, msg, OS_MESG_NOBLOCK);
    }
    return ret;
}

/* Commands 1/2: osPfsInitPak if a pak is present (0x801F60C8). */
s32 fe_pak_init_pak(void) {
    u8 status;
    s32 ret;

    ret = 0;
    gPakFullSlot = 4;
    if (cont_query_paks(&gSiMesgQueue, &status)) {
        ret = 1;
    } else if (!(status & 1)) {
        ret = 1;
    }
    if (ret == 0) {
        ret = osPfsInitPak(&gSiMesgQueue, &gPfs, 0);
    }
    cont_init();
    return ret;
}

/* Command 3: allocates the player's 0xE00-byte game file (0x801F6160). */
s32 fe_pak_create_file(u8 player) {
    return osPfsAllocateFile(&gPfs, 0x3031, 0x4E424345, gPakGameName, gPakExtName, PFS_FILE_SIZE,
                             &gPakFileNo[player]);
}

/* Command 4: deletes the current player's game file (0x801F61C8). */
s32 fe_pak_delete_file(s32 arg0) {
    return osPfsDeleteFile(&gPfs, 0x3031, 0x4E424345, gPakGameName, gPakExtName);
}

/* Command 5: deletes the file of a menu item (0x801F6210).
 * delete the file behind menu item `item` (file list starts at item 37) */
s32 fe_pak_delete_listed_file(u8 item) {
    s32 ret;

    ret = osPfsDeleteFile(&gPfs, gPakFileStates[item - 37].company_code, gPakFileStates[item - 37].game_code,
                          gPakFileStates[item - 37].game_name, gPakFileStates[item - 37].ext_name);
    return ret;
}

/* Commands 6/7: read (rw 0) or write (rw 1) the player record (0x801F6264).
 * write (rw 1) or read (rw 0) player record `player` */
s32 fe_pak_rw_player(u8 player, u8 rw) {
    s32 ret;
    s32 pad38;
    u32 i;
    s32 pad30;
    s32 tries;
    u8 *p;
    u64 sem;

    ret = 0;
    p = (u8 *) &players[player];
    PORT_SAVE_BEGIN(p, PORT_SAVE_PLAYER, 0x100);
    for (i = 0; i < 0x100 && rw == 1; i++) {
        debug_printf("0x%x, ", p[i]);
    }
    debug_printf("\n");
    PFS_ASSERT(sizeof(playerInfo)<=512, 0);
    if (rw == 1) {
        fe_pak_add_crc(p, 0x100);
        PORT_SAVE_SYNC(p, PORT_SAVE_PLAYER);
    }
    if (gUseEeprom != 0 || gGameMode == MODE_ERASE_SAVE) {
        if (gEepromPresent == 0 || gDebugRamEeprom != 0) {
            for (i = 0; i < 0x100; i++) {
                if (rw == 1) {
                    gSaveImage[i] = p[i];
                    gDummyEeprom[i] = p[i];
                } else {
                    gSaveImage[i] = gDummyEeprom[i];
                    p[i] = gSaveImage[i];
                }
            }
        } else if (rw == 1) {
            osEepromLongWrite(&gSiMesgQueue, 0, p, 0x100);
        } else {
            osEepromLongRead(&gSiMesgQueue, 0, p, 0x100);
        }
    } else {
        tries = 0;
        do {
            ret = osPfsFindFile(&gPfs, 0x3031, 0x4E424345, gPakGameName, gPakExtName, &gPakFileNo[player]);
            tries++;
        } while (ret != 0 && tries < 3);
        if (ret == 0) {
            tries = 0;
            do {
                ret = osPfsReadWriteFile(&gPfs, gPakFileNo[player], rw, 0, 0x100, p);
                tries++;
            } while (ret != 0 && tries < 3);
        }
    }
    if (ret == 0 && rw == 0) {
        ret = fe_pak_check_crc(p, 0x100);
    }
    PORT_SAVE_END(p, PORT_SAVE_PLAYER, 0x100);
    if (ret == 0 && rw == 0) {
        fe_pak_get_semaphore(player, &sem);
        if (sem != 0x87569AB6CD076AEC) {
            ret = 0x6E382;
        }
    }
    return ret;
}

/* Commands 8/9: a level's best-time block (0x801F65C4).
 * read/write best-time block `slot` of player `player` */
s32 fe_pak_rw_times(u8 player, u8 slot, u8 rw) {
    s32 ret;
    s32 i;
    s32 pad;
    u32 start;
    u8 *p;

    ret = 0;
    p = (u8 *) gBestTimes[player];
    PORT_SAVE_BEGIN(p, PORT_SAVE_TIMES, 0x20);
    start = slot * 0x20 + 0x100;
    if (rw == 1) {
        fe_pak_add_crc(p, 0x20);
        PORT_SAVE_SYNC(p, PORT_SAVE_TIMES);
    }
    if (gUseEeprom != 0) {
        for (i = start; i < start + 0x20; i++) {
            if (rw == 1) {
                gSaveImage[i] = p[i - start];
            } else {
                p[i - start] = gSaveImage[i];
            }
        }
    } else {
        ret = osPfsFindFile(&gPfs, 0x3031, 0x4E424345, gPakGameName, gPakExtName, &gPakFileNo[player]);
        if (ret == 0) {
            ret = osPfsReadWriteFile(&gPfs, gPakFileNo[player], rw, start, 0x20, p);
        }
        for (i = 0; i < 14; i++) {
            debug_printf("%d TIME %d = %d\n", rw, i, gBestTimes[player][gVehicleTimeSlot[i]]);
        }
    }
    if (ret == 0 && rw == 0) {
        ret = fe_pak_check_crc(p, 0x20);
    }
    PORT_SAVE_END(p, PORT_SAVE_TIMES, 0x20);
    return ret;
}

/* Commands 10/11: two levels' EEPROM time words (0x801F67E4).
 * EEPROM best times of levels `level` and `level` + 1: write (rw 1) or read back and check (rw 0) */
s32 fe_pak_rw_eeprom_times(u8 pn, u8 level, u8 rw) {
    s32 ret;
    s32 i;
    u8 idx;
    u16 *p;

    ret = 0;
    idx = level * 2;
    p = &gEepTimes[idx & ~3];
    if (gUseEeprom != 0) {
        PFS_ASSERT(pn==playerNumberAtStart, 604);
        if (rw == 1) {
            gEepTimes[idx] = gBestTimes[pn][gVehicleTimeSlot[players[pn].bestVehicle[level]]];
            gEepTimes[idx + 1] = gEepTimes[idx] ^ 0x55AA;
            debug_printf("%d %d EEWRITE %x %x\n", level, gEepTimes[idx], (u32) (idx * 2 + 0x100) >> 3, p);
            PORT_SAVE_BEGIN(p, PORT_SAVE_EETIMES, 8);
            osEepromWrite(&gSiMesgQueue, (u32) (idx * 2 + 0x100) >> 3, (u8 *) p);
            PORT_SAVE_END(p, PORT_SAVE_EETIMES, 8);
        } else {
            PORT_SAVE_BEGIN(p, PORT_SAVE_EETIMES, 8);
            osEepromRead(&gSiMesgQueue, (u32) (idx * 2 + 0x100) >> 3, (u8 *) p);
            PORT_SAVE_END(p, PORT_SAVE_EETIMES, 8);
            for (i = 0; i < 2; i++, level++) {
                if (((players[pn].medal[level] > 0 && players[pn].medal[level] < 6) ? 1 : 0)
                    && level != 0x31 && level != 0x2F && level != 0x26) {
                    gBestTimes[pn][gVehicleTimeSlot[players[pn].bestVehicle[level]]] = gEepTimes[level * 2];
                    debug_printf("%d EETIMES: %d %d\n", level, gEepTimes[level * 2], gEepTimes[level * 2 + 1] ^ 0x55AA);
                    if (gEepTimes[level * 2] != (gEepTimes[level * 2 + 1] ^ 0x55AA)) {
                        ret = 0x6E382;
                    }
                }
            }
        }
    }
    return ret;
}

/* "PUTTING SEMAPHORE": invalid (20) / valid (21) (0x801F6AF4). */
s32 fe_pak_put_semaphore(u8 player, u64 sem) {
    s32 ret;
    u8 *p;

    ret = 0;
    p = (u8 *) &players[player];
    PORT_SAVE_BE64(sem);
    if (gUseEeprom != 0 || gGameMode == MODE_ERASE_SAVE) {
        osEepromWrite(&gSiMesgQueue, 0x3F, (u8 *) &sem);
    } else {
        debug_printf("PUTTING SEMAPHORE %llu\n", sem);
        /* 0x20 bytes from &sem: the semaphore and 24 bytes of stack */
        ret = osPfsReadWriteFile(&gPfs, gPakFileNo[player], PFS_WRITE, 0xDE0, 0x20,
                                 PORT_SAVE_SEMBLOCK((u8 *) &sem));
    }
    return ret;
}

/* "Getting SEMAPHORE" (0x801F6BD0). */
s32 fe_pak_get_semaphore(u8 player, u64 *sem) {
    s32 ret;
    u8 *p;
    u8 buf[0x20];

    ret = 0;
    p = (u8 *) &players[player];
    if (gUseEeprom != 0) {
        osEepromRead(&gSiMesgQueue, 0x3F, (u8 *) sem);
        PORT_SAVE_BE64(*sem);
    } else {
        ret = osPfsReadWriteFile(&gPfs, gPakFileNo[player], PFS_READ, 0xDE0, 0x20, buf);
        *sem = *(u64 *) buf;
        PORT_SAVE_BE64(*sem);
        debug_printf("Getting SEMAPHORE %llu\n", *sem);
    }
    return ret;
}

/* Commands 12/13: the level status block (0x801F6CA4).
 * read/write the 0x40-byte block pakBuffer stored after the records of the first `nlevels` levels */
s32 fe_pak_rw_status(u8 player, u8 nlevels, u8 rw) {
    s32 ret;
    s32 tmp;
    s32 i;
    s32 count;
    s32 start;

    ret = 0;
    count = 0;
    for (i = 0; i < nlevels; i++) {
        if (gLevelInfo[i].kind == LEVEL_KIND_MISSION && i != 0x31 && i != 0x2F && i != 0x26) {
            count++;
        }
    }
    start = count * 0x40 + 0x880;
    PORT_SAVE_SWAP(pakBuffer, PORT_SAVE_STATUS, 0x40);
    if (rw == 1) {
        fe_pak_add_crc(pakBuffer, 0x40);
    }
    if (gUseEeprom != 0) {
        for (i = start; i < start + 0x40; i++) {
            if (rw == 1) {
                gSaveImage[i] = pakBuffer[i - start];
            } else {
                pakBuffer[i - start] = gSaveImage[i];
            }
        }
    } else {
        ret = tmp = osPfsFindFile(&gPfs, 0x3031, 0x4E424345, gPakGameName, gPakExtName, &gPakFileNo[player]);
        if (tmp == 0) {
            ret = osPfsReadWriteFile(&gPfs, gPakFileNo[player], rw, start, 0x40, pakBuffer);
        }
    }
    if (ret == 0 && rw == 0) {
        ret = fe_pak_check_crc(pakBuffer, 0x40);
    }
    PORT_SAVE_SWAP(pakBuffer, PORT_SAVE_STATUS, 0x40);
    return ret;
}

/* Command 17: a file's state into the file list (0x801F6ED4). */
s32 fe_pak_get_file_state(u8 arg0) {
    return osPfsFileState(&gPfs, arg0, (OSPfsState *) &gPakFileStates[gPakFileCount]);
}

/* The pak file menu: names, pages, "PAGES FREE" (0x801F6F18).
 * list the pak's files into menu items 37 + n; returns whether there are any */
s32 fe_pak_list_files(void) {
    s32 i;
    s32 pad40;
    s32 pad3C;
    OSMesg msg;
    s32 pages;
    s32 flag;
    s32 empty;

    gPakFileCount = 0;
    for (i = 0; i < 16; i++) {
        do {
            osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 16) | 0x11 | 0x1000000), OS_MESG_BLOCK);
            osRecvMesg(&pakToGameMessageQ, &msg, OS_MESG_BLOCK);
            if (msg != 0) {
                i++;
            }
        } while (msg != 0 && i < 16);
        if (i < 16) {
            bcopy(gPakFileStates[gPakFileCount].game_name, gPakFileNames[gPakFileCount], 17);
            bcopy(gPakFileStates[gPakFileCount].ext_name, gPakFileExts[gPakFileCount], 5);
            gPakFileNames[gPakFileCount][16] = 0;
            gPakFileExts[gPakFileCount][4] = 0;
            pages = gPakFileStates[gPakFileCount].file_size / 32 / 8;
            flag = gPakFileExts[gPakFileCount][0];
            fe_pak_chars_to_ascii(gPakFileNames[gPakFileCount]);
            fe_pak_chars_to_ascii(gPakFileExts[gPakFileCount]);
            sprintf(gPakFileLines[i], "%16s%c%-4s (%d)", gPakFileNames[gPakFileCount], flag ? '.' : ' ',
                    gPakFileExts[gPakFileCount], pages);
            debug_printf("%s\n", gPakFileLines[i]);
            if (pages < 99) {
                sprintf(gPakFileLines[i], "%s ", gPakFileLines[i]);
            }
            if (pages < 9) {
                sprintf(gPakFileLines[i], "%s ", gPakFileLines[i]);
            }
            gYoshiFeItems[37 + gPakFileCount].text = gPakFileLines[i];
            gPakFileCount++;
        }
    }
    if (gPakFileCount == 0) {
        sprintf(gPakFileLines[0], "%s", "PAK EMPTY!");
        gYoshiFeItems[37].text = gPakFileLines[0];
        empty = 1;
    } else {
        empty = 0;
    }
    gYoshiWindows[YW_PAK_FILES].sel = (gPakFileCount + empty + 1) / 2 + 0x24;
    gYoshiWindows[YW_PAK_FILES].itemCount = gPakFileCount + empty + 4;
    osSendMesg(&gPakRequestQueue, (OSMesg) 0x100000E, OS_MESG_BLOCK);
    osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
    sprintf(gPakFreeText, "%d PAGES FREE", gPakFreeBytes / 32 / 8);
    gYoshiFeItems[35].text = gPakFreeText;
    sprintf(gPakNeededText, "%d NEEDED PER PLAYER", 14);
    gYoshiFeItems[36].text = gPakNeededText;
    gYoshiFeItems[10].text = "DELETE THIS FILE?";
    gYoshiFeItems[10].jtext = (u16 *) gDeleteFileJText;
    return gPakFileCount != 0;
}

/* Whether fe_pak_list_files found any file (0x801F73FC). */
s32 fe_pak_has_files(void) {
    return gPakFileCount != 0;
}

/* Pak character codes to ASCII (0x801F7410).
 * character indices -> ASCII */
void fe_pak_chars_to_ascii(u8 *str) {
    s32 i;

    for (i = 0; i < font_strlen(str); i++) {
        if (str[i] < 0x45) {
            str[i] = gPakCharset[str[i]];
        } else {
            str[i] = '-';
        }
    }
}

/* ASCII to pak character codes (0x801F74B0).
 * ASCII -> character indices (0xF if not in the set) */
void fe_pak_ascii_to_chars(u8 *str) {
    s32 i;
    s32 j;

    for (i = 0; i < font_strlen(str); i++) {
        for (j = 0; j < 0x45 && gPakCharset[j] != str[i]; j++) {
        }
        if (j != 0x45) {
            str[i] = j;
        } else {
            str[i] = 0xF;
        }
    }
}

/* 4 CRC bytes at a block's end (0x801F75A4).
 * store a 4-byte CRC (one byte per 0x20-byte lane) at the end of the block */
s32 fe_pak_add_crc(u8 *data, s32 size) {
    s32 i;
    s32 j;
    s32 nblocks;
    u8 crc[4];
    s32 off;

    nblocks = (size + 0x7F) >> 7;
    for (i = 0; i < 4; i++) {
        crc[i] = 0;
        data[size + i - 4] = crc[i];
    }
    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < 4; j++) {
            off = i * 0x80 + j * 0x20;
            if (off < size) {
                crc[j] += __osContDataCrc(data + off);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        data[size + i - 4] = crc[i];
    }
    return 0;
}

/* 0, or 0x6E382 on a mismatch (0x801F76E4).
 * check the CRC written by fe_pak_add_crc */
s32 fe_pak_check_crc(u8 *data, s32 size) {
    s32 i;
    s32 j;
    s32 nblocks;
    u8 saved[4];
    u8 crc[4];
    s32 off;

    nblocks = (size + 0x7F) >> 7;
    for (i = 0; i < 4; i++) {
        saved[i] = data[size + i - 4];
        crc[i] = 0;
        data[size + i - 4] = crc[i];
    }
    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < 4; j++) {
            off = i * 0x80 + j * 0x20;
            if (off < size) {
                crc[j] += __osContDataCrc(data + off);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        if (crc[i] != saved[i]) {
            return 0x6E382;
        }
    }
    return 0;
}
