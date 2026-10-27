#ifndef GAME_VARIABLES_H
#define GAME_VARIABLES_H

/*
 * Every game variable used by more than one file, declared once. Most are
 * absolute symbols (undefined_syms*.txt) at their N64 addresses; a few are
 * defined by a game file. The types follow the code's real use; a file that
 * reads one differently says so where it does (a view macro in NON_MATCHING
 * code, or a LEGACY_ declaration the matching build keeps for IDO).
 * First generated (types_genvar.py), kept by hand since; tools_port/names.txt
 * has a line on each name.
 */

/* low memory and others */
extern u16 gFrameBuffers[][320*240];

/* hd_code .text (tables inside the code) */
extern u8 gRamdozerTrackTexAnimA[]; /* the first of its three texture animations (anim_tex_*) */
extern u8 gRamdozerTrackTexAnimB[];
extern u8 gRamdozerHeadingTexAnim[];
extern u8 gBacklashYawTexAnimA[]; /* keys of this vehicle's anim_tex_find entries */
extern u8 gBacklashYawTexAnimB[];
extern u8 gFxDefKind04[]; /* effect definitions in the 7D9D0 text blob */
extern u8 gFxExhaustSmoke[]; /* definition handed to fx_spawn (7D9D0 text blob) */
extern u8 gFxSparks[]; /* definition handed to fx_spawn (7D9D0 text blob) */
extern u8 gFxDefJetFlame[]; /* definition handed to fx_spawn */
extern u8 gFxDefNodeHit[]; /* effect definitions also listed in gFxCachedDefs */
extern u8 gFxSkyfallBoost[];
extern u8 gFxDefBlast[];
extern u8 gFxDefKind20[];
extern u8 gFxDefKind21[];
extern u8 * N64P gFxDefsByKind[]; /* definition pointers, in the 7D9D0 text blob */
extern u8 gRspBootText[]; /* RSP boot ucode, ends at gAspMainText */
extern u8 gAspMainText[];

/* front end .data / .bss (0x80208040-0x8021ED00) */
extern RankTitle gRankTitles[31]; /* the 31 rank titles, "ROOKIE WRECKER" .. "YOU CAN STOP NOW." */
extern u8 gRankTitleLine1Words[0x20]; /* number of words on the rank name's first line */
#ifndef LEGACY_gYoshiFeItems
extern u8 gYoshiFeItems[]; /* the window items (0x1C each) of the front end's windows */
#endif
extern GlobeLevel gGlobeLevels[60]; /* the globe's level entries (0x30 each) */
extern u8 gLoadMenuFullSlot;
extern u8 * N64P gLvlselMarkerFrames[]; /* plane icon animation frames, 32x32 RGBA32 */
extern u8 * N64P gLvlselGlowTex; /* glow textures */
extern u8 * N64P gLvlselGlowTex2;
extern u8 * N64P gLvlselSunTex;
extern s32 gGlobePlanet; /* the solar-system node the camera looks at */
#ifndef LEGACY_gSolarNodeMtx
extern Mtx gSolarNodeMtx[][4];
#endif
extern Mtx gFeSceneRot[];
extern OSThread gPakThread; /* the save thread (front-end .bss) */
extern BcScClient gPakSchedClient; /* the save thread's scheduler client */
extern OSMesgQueue gPakRequestQueue; /* requests to the pak/EEPROM thread */
extern OSMesgQueue pakToGameMessageQ; /* replies of the pak/EEPROM thread */
extern u64 gBestTimesReturnMode;
extern u8 gVehicleIconTex;
extern f32 gGlobeCamDist;
extern f32 gGlobeCamLon;
extern f32 gGlobeCamLat;

/* hd_code .data / .rodata (0x802E8BD0-0x8030F660) */
extern u8 gPaused;         /* updates stop, the last picture is held (pause, loading) */
extern u8 gUnpauseRequest;
extern u8 gPauseRequest;
extern s32 levelno;        /* the current level, 0-59 */
extern u8 gShakeTime; /* screen shake time */
extern s32 gShakeSize; /* screen shake size */
extern s32 gRecDemoNumber;
extern u8 gRecDemoNoLogo;
extern u8 gUseEeprom;
extern u8 gVehicleTimeSlot[];
extern f32 gFontAdvance[]; /* per-font advance */
extern u16 gFontNoAdvanceChar[]; /* per-font characters that draw nothing */
extern u16 gFontAltBlankChar[];
extern u16 gTextSpace[];
extern u16 gTextTerminator[]; /* string terminator */
extern u8 gAcademyMarkers[][8];
#ifndef LEGACY_gLevelInfo
extern u8 gLevelInfo[][0x44]; /* one 0x44-byte record per level; +0 its kind */
#endif
#ifndef LEGACY_gYoshiColors
extern u8 gYoshiColors[][8]; /* [32]: top RGBA + bottom RGBA per window colour id (16-20 pulse) */
#endif
extern u8 gYoshiResultsWindowByKind[];
extern u8 gYoshiReplayWindowByKind[];
extern u8 gYoshiSprites[]; /* 75 animated sprite definitions, 0x30 each (window items, objective icons) */
extern u8 gYoshiGameItems[]; /* the window items (0x1C each) of the in-game windows */
#ifndef LEGACY_gYoshiWindows
extern u8 gYoshiWindows[]; /* the text-window definitions */
#endif
extern s32 gDebugFlagD; /* -d */
extern s32 gDebugRamEeprom; /* -l: saves go to gDummyEeprom, not the EEPROM */
extern s32 gDebugCheats; /* -c, -C */
extern s32 gDebugStepGameState; /* -C */
extern Vtx gFadeVtx[][4];
extern u8 gHudArrowTex[]; /* 32x32 IA8 texture */
extern u8 gHudFrameTex[]; /* 32x32 RGBA32 frame texture */
extern u8 D_802FDB14;
extern u8 gBootPakManager;
extern u8 gBootEraseSave;
extern u32 gObjSolidKinds[]; /* size 0xC */

/* hd_code .bss (0x8030F660-0x80400000) */
extern OSThread gMainThread; /* the main (game) thread, hd_main_thread */
extern OSMesgQueue gDmaMesgQueue;
extern OSMesgQueue textureDmaMessageQ; /* texture DMA queue */
extern OSMesgQueue gTaskDoneQueue; /* the main thread's RSP task done messages (rsp_wait_task) */
extern BcSched gScheduler; /* the scheduler (sched.c, 2C560.c); the game reads its retrace counters
                            * gScheduler.retraceCount / .frameCount */
extern BcScClient gMainSchedClient; /* the main thread's scheduler client (its RSP tasks' client, 405F0.c) */
extern u8 gPausedBufferIndex;
#ifndef LEGACY_gDynamicBufs
extern DynamicBuf gDynamicBufs[]; /* [2]: the frame's matrices, vertices and top-level display list */
#endif
extern u32 gFrameBufferPhys[]; /* the frame buffers' physical addresses */
#ifndef LEGACY_gDepthBufferPhys
extern u32 gDepthBufferPhys; /* the depth buffer's physical address */
#endif
extern u8 gBufferIndex; /* which frame buffer / DynamicBuf / list buffers this frame draws into (0, 1) */
extern u32 gFramesInMode; /* frames since the mode's setup (hd_mode_setup) */
extern s32 gPlayFrames;
extern s32 gUnpausedFrames;
extern u8 * N64P gStaticSegment; /* the static segment (segment 1), loaded at 0x803FF600 */
#ifndef LEGACY_gHeapPtr
extern u8 * N64P gHeapPtr; /* the heap's next free byte (a bump allocator from 0x8004B400) */
#endif
#ifndef LEGACY_gLevelData
extern u8 * N64P gLevelData; /* the level's inflated file, on the heap */
#endif
extern s32 gFrameDlLen; /* top-level display list length */
extern u16 gPerspNorm; /* projection scale, 65535 = 1.0 */
extern s32 nextdma; /* queued-DMA counter (next OSIoMesg slot) */
extern s32 no_palette_dmas; /* no_palette_dmas */
#ifndef LEGACY_gVehicleShadows
extern VehShadow * N64P gVehicleShadows; /* the vehicle shadow records */
#endif
#ifndef LEGACY_gVehicleShadowsEnd
extern VehShadow * N64P gVehicleShadowsEnd; /* one past the last */
#endif
extern u8 gDiggerVehicle;
extern u8 gLevelFirstClear;
extern u8 gCarrierHit;        /* the mission failed: the carrier hit something */
extern u8 gLevelDone;         /* the mission is complete */
extern u8 gCarrierHitSeen;
extern u8 cmo_hit_request;
extern u8 gLevelDoneRequest;
extern u8 gHaveCarrier;       /* the level has the missile carrier (type 0xFF) */
extern u8 gHaveChopper;       /* ... the BCT chopper (0xFE) */
extern s32 gPlayerX; /* the player's position (the camera's subject), 1/32 units */
extern s32 gPlayerY;
extern s32 gPlayerZ;
extern s32 gCamEyeX; /* the camera eye x, y, z (<< 11), eased toward the goal; also the sound listener */
extern s32 gCamEyeY;
extern s32 gCamEyeZ;
extern s32 gCamFixedEyeX; /* fixed eye x, y, z */
extern s32 gCamFixedEyeY;
extern s32 gCamFixedEyeZ;
extern u8 gCamFixedEye;
extern u8 gCamNoHeightZone;
extern u8 gCamSnap; /* camera: snap to the new position this frame */
extern f32 gCamHeading; /* camera heading, degrees */
extern u8 gCamLift;
extern u32 gCamLiftMax;
extern u16 gCamLiftStep;
extern s32 gCamLiftRadius;
extern f32 gCamFovy;
extern s16 gPlayerSpeed; /* the player vehicle's speed (VehState.speed, published each frame) */
extern s16 gPlayerMoveHeading; /* the player vehicle's move heading (VehState +0x4E), 4095 = 360 degrees */
extern s16 gPlayerHeading;
extern s16 gCamDistance;
extern s16 gCamHeight;
extern s16 gCamHeightBase;
extern s16 gCamYaw; /* camera / player angle (0..0xFFF) */
extern u8 gCurVehicle; /* the type of the vehicle the player drives (0: on foot) */
#ifndef LEGACY_gLevelSegment8
extern u8 * N64P gLevelSegment8; /* the level file's display data (segment 8) */
#endif
extern u8 gVehicles[][0x74]; /* one record per loaded vehicle model (00000.c's VehRecord) */
#ifndef LEGACY_gVehiclesEnd
extern u8 * N64P gVehiclesEnd; /* one past the last record in use */
#endif
extern u64 gFrameDrawTime; /* osGetTime() at the frame's draw (shake, part jitter) */
extern s16 gPartJitterX; /* x, y, z stored for a part that falls */
extern s16 gPartJitterY;
extern s16 gPartJitterZ;
extern s32 gDriverHidden; /* the driver (type 0) is skipped: in a vehicle, level end (draw, collision) */
extern u8 gNextVehicle;
extern u8 gVehJustSwitched;
extern u32 gLevelMoney; /* the level's money total ($; players[].money) */
extern s32 gValuePopupNew; /* value to queue (a destroyed object's value; 0 = none) */
extern u8 gReplayMenuShown;
extern s32 gLevelStartTime;
extern s32 gPromoCueDelay;
extern u8 gHudDialLoaded; /* dial loaded */
extern u8 gHaveSkyfall;
extern u8 gHudMissileIconLoaded; /* icon 1 loaded */
extern u8 gHaveBallista;
extern u8 gHudRamIconLoaded; /* icon 2 loaded */
extern u8 gHaveSideswipe;
extern u8 gDefaultVehAlpha; /* the vehicles' env alpha outside every alpha zone */
extern s8 gUnlockCount;
extern s16 gCamTiltMax; /* tilt limits */
extern u8 gPlayerHidden;
extern u8 gRouteRevealFlags;
extern u64 gPrevGameMode; /* previous game mode */
extern u64 gGameMode; /* game mode flags */
extern u64 gNextGameMode; /* next game mode */
extern u32 gLevelKind; /* gLevelInfo[levelno]'s kind: 1 = main mission */
extern u8 gHaveShuttle;       /* ... the shuttle (0xFD) */
extern u8 playerNumber; /* current player */
extern u8 gLoadedPlayer;
extern u8 playerNumberAtStart;
#ifndef LEGACY_players
extern u8 players[][0x100]; /* per-player progress, 0x100 bytes each */
#endif
extern u16 gBestTimes[][16]; /* per player: saved level times */
extern u16 gEepTimes[]; /* EEPROM best-time words (time, time ^ 0x55AA) */
extern u8 gSlotState[]; /* per slot: 0 no save, 1 saved game, 2 new game */
extern u8 * N64P gObjShadowTex;
extern u8 gRecReplaySprite;
extern u8 gRecVehicle;
extern s32 gRecVehiclePending;
extern u8 * N64P hd_inbuf; /* hd_code's inflate: gzip's inbuf (input) */
extern u8 * N64P hd_slide; /* inflate.c's slide (output) */
extern s32 hd_inptr; /* gzip's inptr */
extern s32 hd_outcnt; /* gzip's outcnt */
extern s16 gRecDemoLength; /* recording header word 0: frames before the demo's end fade */
extern u16 gLogoState;
extern u8 gAttractStartOk;
extern u8 gMusicPopState;
extern u8 gMusicPushState;
extern u8 gMusicFading;
extern s32 gMusicSavedTempo;
extern u8 gTuneStackEmpty;
#ifndef LEGACY_gMusicPlayer
extern ALCSPlayer * N64P gMusicPlayer; /* the music ALCSPlayer */
#endif
#ifndef LEGACY_gSfxBank
extern void * N64P gSfxBank; /* the sound-effects bank (ALBank *), snd_play's first argument */
#endif
extern u32 gDoneTuneStartTime;
extern u16 gTurboStartState;
extern s16 gHudAlpha; /* HUD alpha cap */
extern u16 gTimeLeft;
extern u8 gTurboPending;
extern u8 gTurboActive;
extern u8 gWideFovLevel;
extern s32 gChopperLandY;
extern s32 gPeopleRestored;
extern s32 gChopperRetargetZ;
extern u64 gYieldBuffer[]; /* the RSP tasks' yield data (0x900 bytes) */
extern u8 gCamPathActive;
extern s32 gCamPathX;
extern s32 gCamPathY;
extern s32 gCamPathZ;
extern u8 gShuttleAborted;
extern u8 gShuttleCamFollow;
extern s32 gRandSeed;
extern u8 gHintBuildingHit;
#ifndef LEGACY_gHintLastHitObject
extern struct LevelObject * N64P gHintLastHitObject; /* the object last hit (for gHintBuildingHit) */
#endif
extern char gStatusLines[]; /* status screen lines, 0x20 each */
extern u16 yoshiSelection;
extern s16 gYoshiPendingWindow;
extern s16 gYoshiTypeState;
extern s16 gYoshiAlpha;
#ifndef LEGACY_gYoshiHeapItems
extern u8 * N64P gYoshiHeapItems; /* window items built on the heap (53220.c, bestTimes.c) */
#endif
extern f32 gYoshiScale;
extern u16 gRduLitOrder[];
#ifndef LEGACY_gRdus
extern u8 * N64P gRdus;
#endif
extern s32 gPreNmi;
extern OSTime gLastRetraceTime;
extern s16 * N64P gNearestTargetPts;
#ifndef LEGACY_gMarkedTargetPts
extern s16 * N64P gMarkedTargetPts;
#endif
extern s32 gCarrierEta; /* time left; under 500 the target turns red */
extern u8 gMarkerArrowsOnScreen;
extern s16 gDigHitDamage;
extern s16 gDigHitDamagePerPart;
extern u8 gDigHitVehicle;
extern u8 gDigHit;
extern u8 gWaterOn; /* level has water */
extern u8 gWaterLayer;
extern s32 gPowerPillSound;
extern s16 gPowerPillTimer;
extern s8 gPowerPillEaten;
extern u8 gRspSlotBusy[]; /* slot busy flags */
#ifndef LEGACY_gRdpOutputBuffer
extern u64 * N64P gRdpOutputBuffer; /* RDP output buffer (0xA000 bytes) */
#endif
#ifndef LEGACY_gScoreOld
extern Score gScoreOld; /* printed "sold" (hd.c) / "old" (07800.c) */
#endif
#ifndef LEGACY_nss
extern Score nss; /* this run's level result (master_switch.c) */
#endif
extern u8 gScoreBuildings;
extern u8 gScorePeople;
extern u16 gScoreRdus;
extern Score gScoreRes; /* printed "sres" / "res" */
extern Score gScoreRs2; /* printed "srs2" / "rs2": the run kept for the replay */
extern u16 gRduCount;
extern u8 gBuildingCount;
extern u8 gPeopleTotal;
extern u8 gTargetsCleared;
extern u8 gBonusLevelExit;
extern OSMesgQueue gSiMesgQueue; /* hd_code: SI message queue */
extern s8 gContStickX;
extern s8 gContStickY;
extern s8 gContPrevStickX;
extern s8 gContPrevStickY;
extern u8 gHeldLeft; /* 1 while the button is held (this frame's processed pad) */
extern u8 gHeldRight;
extern u8 gHeldL;
extern u8 gHeldR;
extern u8 gHeldA;
extern u8 gHeldB;
extern u8 gHeldCLeft;
extern u8 gHeldCRight;
extern u8 gHeldZ;
extern u8 gHeldBOrZ;
extern u8 gPrevHeldCLeft;
extern u8 gPrevHeldCRight;
extern u16 gContButtons; /* controller buttons held */
extern u16 gContPrevButtons; /* controller buttons, previous frame */
extern s8 gPadStickX; /* the processed stick */
extern s8 gPadStickY;
extern s8 gPrevPadStickX; /* stick x last frame */
extern u16 gRecButtons; /* the pad played back from a recording */
extern s8 gRecStickX;
extern s8 gRecStickY;
extern u8 gSteerAirborne;
extern u8 gControlMethod; /* 0 or 1: the player's control method for the vehicle */
extern s32 gSwallowB; /* swallow a held B */
extern u8 frontEndPresent; /* front end loaded */
extern OSIoMesg gDmaIoMesg;
extern u16 gSteerHeading;
extern s16 gSteerRate;
extern u8 gSteerTurnAround;
extern u8 pakBusy; /* pak thread busy */
#ifndef LEGACY_gEepromPresent
extern void * N64P gEepromPresent; /* osEepromProbe's result ("NO EE PRESENT! - USING DUMMY EE") */
#endif
extern u64 pakBuffer[]; /* 0x100 bytes */
extern u8 gPakFullSlot;
extern u8 saveIt[]; /* per slot: 1 + level to save, 0 = nothing pending */
extern u8 saveLevel;
extern u8 gIgnorePak;
extern u8 gBoxTargets[]; /* the target boxes (obj_find_nearest_target kind 2) */
extern u8 gBoxTargetCount; /* number of gBoxTargets entries */
extern u8 gGhostRetry;
extern u8 gGhostPlaying;
extern u8 gGhostRecording;
extern u8 gGhostModelType;
extern s16 gPadDisplayAlpha;
extern u8 gPadDisplayLoaded;
extern s16 gAcademyMarkerX;
extern s16 gAcademyMarkerY;
extern s16 gAcademyMarkerZ;
extern u8 gAcademyMarkerIndex;
extern u8 gAcademyMarkerOn;
extern u8 * N64P gAcademyMarkerDl;
extern u8 * N64P gAcademyMarkerVtx;
extern s32 gHdInflateHuftArena; /* huft arena */
extern u8 gSeqSkippable;
extern u8 gVehPartSpheres[]; /* 0x14-byte spheres: x, y, z, r, value, tag, state */
extern u8 gVehBoundSpheres[]; /* 0x14-byte vehicle spheres: x, y, z, r words, byte 0x10 = kind, 0x11 = state (0xFF = end) */
extern s32 gCollX; /* the collision probe point x, y, z (the colliding vehicle, a shell ...) */
extern s32 gCollY;
extern s32 gCollZ;
extern s8 * N64P gNonBlockingParts; /* part ids whose wall hits don't clip the free arc */
extern s32 gBlockedFrame; /* gUnpausedFrames of the last arc clip */
extern s16 gFreeArcStart; /* the free heading arc: start (12-bit; 0 / 0xFFF = no wall) */
extern s16 gFreeArcEnd; /* the free heading arc: end */
extern u8 gCollHit;
extern u8 gCollBlocked;
extern s8 gCarrierContact;
extern u8 gImpactEffectOn;
extern u8 gProbeSkipPart;
extern u8 gTrainContact;
extern u8 gFreeArcCollapsed;
extern u8 gTrainContactFrames;
extern u8 gTexTableLoaded; /* the table below is loaded */
extern u8 gCollTris[]; /* the 0x60-byte collision triangles (objects and moving vehicles) */
extern u8 gVehGates[]; /* 0xDC-byte vehicle gate groups (level section 0x68) */
extern u8 * N64P gCollTrisEnd; /* end of the gCollTris records */
extern u8 * N64P gVehGatesEnd; /* end of the gate groups in use */
extern u8 * N64P gSwitchTris; /* switch-group collision triangles on the heap (0x60 each) */
extern u8 * N64P gSwitchTrisEnd;
extern u8 gSwitchGroups[]; /* 0xFC-byte switch groups: kind count at 0, kinds from 1, tri pointers from 8 (count at 0xF8), flag 0xF9 */
extern u8 * N64P gGuardTris; /* 0x14-byte triangles (as ground_scan_grid's) */
extern u8 * N64P gGuardTrisEnd; /* their end */
extern u8 * N64P gGhostMtx;
extern u8 * N64P gGhostVtx;
extern Gfx * N64P gGhostDl;
extern u8 * N64P gGroundCells[]; /* per-cell triangle lists (cell i spans [i] .. [i + 1] - 4) */
extern u8 * N64P gCollCellsA[];
extern u8 * N64P gCollCellsB[]; /* per-cell triangle lists (cell c spans [c] .. [c + 1]) */
extern u8 * N64P gAlphaZonesEnd; /* end of the zone list that starts at gAlphaZones */
extern u8 gAlphaZones[]; /* 0x24-byte zones: s32 x, y, z, radius; u8 flags at +0x10.. */
extern Gfx * N64P gLevelFileDl0; /* level file header +0x90: drawn after gLevelDl0 */
extern Gfx * N64P gLevelFileDl1; /* header +0x94, after gLevelDl1 */
extern Gfx * N64P gLevelFileDl2; /* header +0x98, after gLevelDl2 */
extern Gfx * N64P gLevelFileDl3; /* header +0x9C, after gLevelDl3 */
extern u8 * N64P gRespawnPoints; /* 9-byte respawn records: u8 key, s16 x, y, z (big-endian) */
#ifndef LEGACY_gCarrierPanelEvents
extern u8 * N64P gCarrierPanelEvents; /* level section 0x58: the carrier-progress panel events (32E00.c) */
#endif
#ifndef LEGACY_gCarrierPanelEventsEnd
extern u8 * N64P gCarrierPanelEventsEnd; /* their end */
#endif
extern u8 * N64P gPathParts;
extern s32 gLevelCellW;
extern s32 gLevelCellD;
extern s16 gLevelCols;
extern u16 gLevelRows;
extern u32 gGroundCellW; /* grid cell width */
extern u32 gGroundCellD; /* grid cell depth */
extern u16 gGroundCols; /* grid columns */
extern u16 gGroundRows;
extern s32 gCollCellW;
extern s32 gCollCellD;
extern s16 gCollCols;
extern s16 gBoundsMinX; /* level box: x min, x max, z min, z max (>> 5 units) */
extern s16 gBoundsMaxX;
extern s16 gBoundsMinZ;
extern s16 gBoundsMaxZ;
extern u8 gFailTrigger;
extern u8 gLevelDlProfile;
extern u8 gStartVehicle; /* the vehicle type the player starts in (lvl_find_start_vehicle) */
extern u16 gVisibleCells[]; /* visible object ids, ended by -1 */
extern Gfx gFxDlA0[]; /* display lists A / B, frame 0 */
extern Gfx gFxDlA1[]; /* display lists A / B, frame 1 */
extern Gfx gFxDlB0[];
extern Gfx gFxDlB1[];
extern u8 * N64P gMoveableTrisEnd; /* end of the used gMoveableTris records */
extern f32 gLevelGravity;
extern f32 gGravity;
extern s32 gObjectFloorY;
extern s32 gGuardTrisBestY; /* height of ground_scan_guard_tris's best triangle */
extern u8 gVehPoints[]; /* 0x10-byte records, id byte at +0xC */
extern u8 gMoveableTris[]; /* 0x38-byte triangle records, u16 id at +0x36 */
extern s16 gModelRot[]; /* s16[3]: the x, y, z rotation mtx_build_model builds the vehicle's model with */
extern s16 gModelRotY; /* rotation angle y (x and z: gModelRot, D_803ED394_) */
extern s32 gSlotGroundY[]; /* ground heights of the three wheel slots */
extern u8 gStandOn[]; /* 4-byte records {u8 key, u8 kinds[3]}, word -1 ends */
extern u8 gRiderPass;
extern u8 gBounceThreshold;
extern u8 gBounceDamping;
extern s16 gLastSpeed;
extern s16 gSteerSnapAngle;
extern u8 gSteerHeadingSnap;
extern u8 gBounceSound;
extern u8 gVehSlowed;
extern u8 gSurfaceMax;
extern u8 gHaveRideables;
#ifndef LEGACY_gDriverPos
extern u32 gDriverPos[]; /* 0xC bytes */
#endif
extern s32 gDriverY;
extern s32 gDriverZ;
extern u8 gDriverCanExit; /* the walk-out after leaving a vehicle is allowed (switch planes flip it) */
extern u8 gDriverExitWalk; /* the exit walk is running */
extern s16 gSideswipeRamCharges; /* side-ram uses left (crates add 10; HUD icon) */
extern u8 gSkyfallBoostMeter; /* dial value, 0..100 */
extern s32 gChopperX; /* the BCT chopper's position x, y, z */
extern s32 gChopperY;
extern s32 gChopperZ; /* z */
extern s32 gChopperCruiseY; /* result height (world units) */
extern s32 gChopperTargetX; /* where it is flying to, x, z (the people code sets it) */
extern s32 gChopperTargetZ;
extern s32 gChopperDropX; /* its model point 1 x, y, z (veh_find_point): the driver is put here */
extern s32 gChopperDropY;
extern s32 gChopperDropZ;
extern s16 gChopperShadowHeading;
extern s16 gChopperCamSpeed;
extern s16 gChopperCamHeading;
extern u8 gChopperMode;
extern u8 gChopperTouchdown;
extern u8 gChopperLiftAnimDone;
extern s32 gCarrierX; /* the missile carrier's position x, y, z */
extern s32 gCarrierY;
extern s32 gCarrierZ;
extern s32 gCarrierRouteLen;
extern s32 gCarrierStartZ;
extern s16 gCarrierTargetSpeed; /* the carrier's speed rises by 8 a frame toward it (level records set it) */
extern u8 gCarrierExplodeDone; /* effect finished */
extern s32 gTrainLineX0; /* x interpolation: x0, x1 */
extern s32 gTrainLineZ0; /* z interpolation: z0, z1 */
extern s32 gTrainLineX1;
extern s32 gTrainLineZ1;
extern u8 gTrainOnLine; /* x follows z along the line (gTrainLineX0..) */
extern u8 gTrainClosed;
extern u8 gSinkingPart[]; /* size 0xA30 */
extern u8 gFadingParts[]; /* 4 entries of 0x4B8 bytes */
extern u8 gTopplingParts[]; /* 2 entries of 0x478 bytes */
extern u8 gObjHits[];
extern u8 * N64P gObjHitsEnd; /* end of the (object, part) hit list */
extern u8 gEffectScratch[];
extern u8 gObjects[]; /* the level's objects (buildings ...), 0xFC-byte records (77E20.c) */
#ifndef LEGACY_gObjectsEnd
extern u8 * N64P gObjectsEnd; /* one past the last object record */
#endif
extern s32 gCarrierTargetDist; /* the carrier's distance to its next target (9999999 = none) */
extern s32 gSplinePosX; /* position objects with a pose are moved to */
extern s32 gSplinePosY;
extern s32 gSplinePosZ;
extern s32 gCarrierTargetX; /* the carrier's next target x, y, z (obj_find_carrier_target) */
extern s32 gCarrierTargetY;
extern s32 gCarrierTargetZ;
extern s16 gEndObjectX;
extern s16 gEndObjectY;
extern s16 gEndObjectZ;
extern s32 gPathBuilding;
extern s32 gCheatAllDestroyed;
extern u8 * N64P gCollVehicleAnim; /* the colliding vehicle's animation channel table (set before obj_collide_vehicle) */
extern s32 gObjHitFrame;
extern s16 gHitSpeed; /* the colliding vehicle's speed (veh_collision_setup) */
extern u8 gVehDamageMode;
extern u8 gVehAttacking;
extern u8 gEndObjectTouched; /* the player on foot touched a kind-0x38 end object */
extern u8 gPathBuildingsDestroyed;
extern u8 gNearestTargetKind;
extern u8 gMarkedTargetKind; /* kind of the target (as gNearestTargetKind: 2 and 3 have pos at +0) */
extern u8 gObjRockSign; /* last sign of gHitSpeed (-1, 0, 1) */
extern u8 gObjRockRun; /* run length, 0..0x14 */
extern u8 gObjRockHit; /* result: set for one call when a run of 0x14 completes with a change */
extern u8 gPartSinking;
extern u8 gCollExitProbe;
extern u8 * N64P gObjPartMtx0; /* part matrices, frame 0 (segment 10) */
extern u8 * N64P gObjPartMtx1; /* frame 1 */
extern u8 * N64P gGroupTris; /* triangle list: 0x28-byte records, 9 s32 coords + u8 at +0x24 */
extern u8 * N64P gGroupTrisEnd; /* its end */
extern s16 gEngineSoundLevel; /* last |level| set by veh_set_engine_level (-1 = none yet) */
extern void * N64P gEngineSound; /* the engine loop's handle */
extern s32 gJbombLiftFadeY;
extern s32 gJbombMaxY;
extern s16 gContSteerError;
extern u8 gJbombAirborne;
extern u8 gJbombCeilingAbove;
extern s16 gBallistaMissiles; /* missiles left (crates add 10; HUD icon) */
extern s32 gCollStepX;
extern s32 gCollStepZ;
extern u8 gGroundTriId;
extern u8 gProbeHitCarrier;
extern u8 gProbeHitPlayer;
extern u8 gDynTris[]; /* 0x60-byte triangles */
#ifndef LEGACY_gDynTrisEnd
extern u8 * N64P gDynTrisEnd; /* the write cursor of gDynTris (coll_reset_dyn_tris / coll_add_dyn_tris) */
#endif
extern u8 gRadarMarkers[]; /* the 25 0x14-byte radar markers of 8A080.c: word 0 = id (-1 = free), x, y, z, r */
extern u8 gCommPts[]; /* 0x14-byte communication points */
extern u8 gCommPtCount; /* the level's communication points */
extern s32 gShuttlePos; /* the shuttle's position x, y, z */
extern s32 gShuttlePosY;
extern s32 gShuttlePosZ;
extern s32 gShuttleTriggerY;
extern s16 gShuttleHeading;
extern s16 gShuttleRotX; /* angles x, y, z */
extern s16 gShuttleRotY;
extern s16 gShuttleRotZ;
extern u8 gShuttlePathEvent; /* event this frame */
extern u8 gShuttlePathKind; /* the path's kind: 1 = a sequence (fades to MODE_SEQ_END, no loop) */

/* ROM addresses (DMA sources) */
extern u8 romBootBanner[];
extern u8 romWorldTexturesRaw[]; /* worldtextures.raw: the globe's textures */
extern u8 romAttractPictures[]; /* four 160x120 RGBA16 attract pictures (fe_attract_pic_load) */
extern u8 romTrafficLights[];
extern u8 romControllerPicture[];
extern u8 romDemoRecordings[];
extern u8 romFeBgPicture1[]; /* compressed pictures (ROM) */
extern u8 romUsaStarScene[];

/* segment addresses */
extern Gfx seg1RspInitDl[]; /* segment 1 */
extern Gfx seg1RdpInitDl[];
extern Mtx seg2Base[]; /* segment 2: the frame's DynamicBuf (Mtx / Vtx / LookAt by index) */

/* named */
extern s16 currentYoshiWindow;
extern s16 yoshiState;

#endif
