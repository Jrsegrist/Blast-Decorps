#ifndef GAME_FUNCTIONS_H
#define GAME_FUNCTIONS_H

/*
 * Prototypes of every game function used outside the file that defines it,
 * with the types of its definition (the NON_MATCHING definition where the
 * matching build still uses the original asm). Generated from the sources
 * (scratchpad types_genhdr.py), grouped by defining file.
 *
 * LEGACY_func_X: a file whose matched code IDO compiled against an older
 * prototype defines LEGACY_func_X and keeps that prototype for the matching
 * build only (see the file's "matching build" block). The NON_MATCHING
 * build always sees these.
 */

/* Types that only the defining files complete. */
struct BcScClient;
struct BcSched;
struct BoxSpawn;
struct Dyn;
struct Dyn48D00;
struct Dyn4B5E0;
struct DynBuf;
struct PlayerSelDyn;
struct SpriteVtxBuf;
struct TrailBuf;
struct LevelObjectHead;
struct LevelObject;
struct CollisionTri;
struct VtxBuf;

/* hd_code/00000.c */
Gfx *hd_debug_dl_stub(Gfx *arg0, DynamicBuf *arg1, s32 *arg2);
void veh_draw_layer(Gfx **gfx, u8 lod);
void hd_mode_setup(void);
void hd_setup_level(s32 arg0);
void align_up(s32 *arg0, s32 arg1);
f32 hd_sinf(f32 arg0);
f32 hd_cosf(f32 arg0);

/* hd_code/12D80.c */
Gfx *gfx_begin_textured(Gfx *gfx);
Gfx *gfx_draw_textured_quad(Gfx *gfx, Vtx *vtx, void *timg, s16 fmtsiz, s32 width, s32 height, s32 zbuf);

/* hd_code/13A70.c */
void veh_add_shadow(u8 id, s32 arg1, s16 arg2, s16 arg3);
void veh_update_shadow(u8 id, s32 x, s32 y, s32 z, s32 arg4, s32 arg5, s32 arg6, s32 arg7);
s32 veh_shadow_floor_y(u8 id);
s32 veh_shadow_src_y(u8 id);
void veh_render_shadow_tex(void *cimg, s32 x, s32 y, s32 z, f32 dist, Gfx *dl, void *seg6, void *seg7);
void obj_draw_ground_shadows(Gfx **gdlp, struct VtxBuf *buf);

/* hd_code/14B30.c */
void text_init(void);
void text_begin_frame(void);
void text_shell_sort(u8 *base, s32 n, s32 size, s32 (*cmp)(void *, void *));
void text_flush(Gfx **gdlp, s32 arg1);
void text_flush_screen(Gfx **gdlp, Mtx *arg1);
void text_print(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, s32 x, s32 y, s32 w, s32 h, u8 forward, u8 r, u8 g, u8 b, u8 a);
void text_print_gradient(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, s32 x, s32 y, s32 w, s32 h, u8 forward, u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1);

/* hd_code/168B0.c */
void font_reset_cache(void);
u8 *font_get_glyph(u16 arg0);
void font_age_cache(void);
s32 font_strlen(u8 *arg0);
s32 font_wstrlen(u16 *arg0);
s32 font_strcmp(u8 *a, u8 *b);
s16 font_center_x(s16 x, u16 scale, u8 *str, s32 arg3);
u8 *font_narrow(u16 *arg0);

/* hd_code/17210.c */
void rec_start_playback(s32 arg0, s32 *arg1);
void rec_start_recording(void);
void rec_start_replay(void);
void rec_record(u16 buttons, s8 x, s8 y);
void rec_restore_start_vehicle(void);
void rec_next(void);

/* hd_code/17A70.c */
void kiunzip(u8 * N64P *arg0, u8 * N64P *arg1, s32 arg2);

/* hd_code/17E10.c */
void hud_attract_messages(void);
Gfx *title_draw_attract(Gfx *arg0, s32 arg1, u8 arg2, s32 *arg3);
void title_setup_logo(void);
void hud_level_complete(Gfx **arg0, s32 arg1, s32 arg2);
void hud_mission_failed(Gfx **arg0, s32 arg1, u8 arg2);

/* hd_code/1A630.c */
void snd_init(SndConfig *c);
u8 snd_get_state(void *arg0);
void *snd_play(void *arg0, s16 arg1, void *arg2);
void snd_stop(void *arg0);
void snd_stop_all(void);
void snd_stop_all_repeating(void);
void snd_stop_all_looping(void);
void snd_stop_slot(u8 arg0);
void snd_modify(void *arg0, s16 arg1, s32 arg2);
void snd_set_slot_volume(u8 arg0, u16 arg1);

/* hd_code/1C460.c */
void music_play_tune(u8 tune, f32 vol);
void music_set_master_volume(f32 vol);
f32 music_get_master_volume(void);
void music_push_danger_tune(void);
void music_play_done_tune(void);
void music_play_clear_tune(void);
void music_push_tune(u8 tune);
void music_pop_tune(void);
void popTuneImmediate(void);
void music_update_pop(void);
void music_update_auto_pop(void);
void music_update_push(void);
void music_update_fade(void);
void music_restore_tempo(void);
void music_fade_to(f32 arg0);
void music_init_audio(void);
void snd_play_effect(u32 id);
u8 music_tune_for_mode(u64 event);
void snd_set_mode_slot_volumes(u64 event);
void music_start_tune(u8 tune);
void music_start_tune_at(u8 tune, f32 vol);
s32 snd_random_id(s32 arg0);

/* hd_code/1D990.c */
void cam_set_level_fovy(u8 arg0);
void lvl_init_chopper_pos(u8 arg0);
void lvl_init_train_line(u8 arg0);
void hud_init_objective(u8 arg0);
void hud_update_objective(void);
Gfx *hud_draw_objective(Gfx *gdl, Gfx * N64P *arg1, u32 *arg2);
void hud_replay_turbo(void);
void hud_format_time(char *buf, u16 t, s32 arg2);
void hud_race_time_left(void);
u8 lvl_planet(u8 arg0);

/* hd_code/20460.c */
void people_init(s32 arg0);
void people_spawn(s16 arg0, s16 arg1, s16 arg2, s16 arg3, u8 arg4, s32 arg5);
void people_update(void);
void chopper_fly_to_carrier(void);
void people_draw(Gfx **gdlp, struct DynBuf *buf);

/* hd_code/22EE0.c */
void amCreateAudioMgr(ALSynConfig *c, OSPri pri);
void amStartAudioMgr(void);

/* hd_code/23C20.c */
void util_check_board_zone(void);
void util_cam_path_init(s32 id);
void util_cam_path_step(s32 path);
s32 util_find_shuttle_path(s32 id);
void util_shuttle_path_init(void);
void util_shuttle_path_step(void);
void util_unwrap_angle(f32 ref, f32 *angle);
void util_itoa(s32 n, u8 *buf);
void util_rotate_about_point(s16 x, s16 y, s16 z, s16 rx, s16 ry, Mtx *m);
void util_copy64(u64 *dst, u64 *src, s32 size);
s32 util_dist2d(s32 x1, s32 y1, s32 x2, s32 y2);
s32 util_dist3d(s32 x1, s32 y1, s32 z1, s32 x2, s32 y2, s32 z2);
s32 util_rand_range(s32 lo, s32 hi);
void util_seed_rand(void);
s32 util_rand2_range(s32 lo, s32 hi);
void util_seed_rand2(void);
void util_reset_hint_events(void);
void util_check_hint_events(void);

/* hd_code/26570.c */
u8 yoshi_show_hint(s16 arg0);
void yoshi_demand(u16 yd);
u16 yoshi_get_demand(void);
void yoshi_init(u8 arg0);
void yoshi_set_target_icons(void);
#ifndef LEGACY_yoshi_draw
Gfx *yoshi_draw(Gfx *gfx, s32 arg1, s32 *count);
#endif
s32 lowest_set_bit(u64 in);
u8 yoshi_get_target_building(char * N64P *name, s32 *arg1);
void rdu_init(s16 *pos, s16 *end);
u8 rdu_is_lit(s32 arg0);
void rdu_set_lit(s32 arg0);
void rdu_update(void);
void rdu_draw(Gfx **gfx, s32 arg1);
void parse_cmdline(u8 *cmdline);

/* hd_code/2C560.c */
void sched_create(struct BcSched *sc, void *stack, OSPri priority, u8 mode, u8 numFields);
void sched_add_client(struct BcSched *sc, struct BcScClient *c, OSMesgQueue *msgQ, s32 arg3, s32 arg4);
void sched_remove_client(struct BcSched *sc, struct BcScClient *c);
void *sched_get_cmd_queue(void *arg0);

/* hd_code/2D810.c */
Gfx *backdrop_draw(Gfx *gfx, u32 arg1, u16 level, s16 arg3, s16 arg4, s32 *height);
void backdrop_init(u16 type, u16 level);

/* hd_code/2E490.c */
void spr_reset(void);
u8 spr_load(u16 *ids, u16 *palIds, u8 count, u8 frames, u8 flags, f32 scale);
Gfx *spr_draw(Gfx *arg0, u8 slot, s16 x, s16 y, u8 alpha, u8 mode, f32 scale);
Gfx *spr_begin(Gfx *gfx);
Gfx *spr_begin_quads(Gfx *gfx);
Gfx *spr_end(Gfx *gfx);
Gfx *spr_pipe_sync(Gfx *gfx);
void spr_draw_blinking(Gfx **gfx, s32 arg1, u8 arg2, s16 arg3, s16 arg4);

/* hd_code/30430.c */
Gfx *fade_draw(s32 arg0, Gfx *gfx);
void fade_to_mode_speed(u64 next, f32 speed);
void fade_to_mode(u64 next);
s32 areWeFading(void);
s32 fade_window_held(void);

/* hd_code/30C70.c */
void hud_init_target_marker(void);
void hud_draw_target_marker(struct SpriteVtxBuf *arg0, Gfx **gfxp, u8 arg2);
Gfx *hud_sprite_setup(Gfx *gfx, u8 arg1);
s32 hud_sprite_quad(struct SpriteVtxBuf *arg0, u8 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, u8 r, u8 g, u8 b, u8 a);
s32 hud_sprite_quad_colors(struct SpriteVtxBuf *arg0, u8 arg1, s32 arg2, s32 x, s32 y, s32 w, s32 h, u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1, u8 r2, u8 g2, u8 b2, u8 a2, u8 r3, u8 g3, u8 b3, u8 a3);
void hud_project_point(void *arg0, f32 x, f32 y, f32 z, s16 *sx, s16 *sy, Mtx *arg6, Mtx *arg7, Mtx *arg8, f32 arg9);
void hud_draw_player_locator(Gfx **gfxp, void *view, u8 idx, s32 x, s32 y, s32 z);

/* hd_code/32E00.c */
void hud_panel_init(void);
void dig_coach_update(s32 now);
#ifdef NON_MATCHING
void hud_panel_start(u8 type, u8 arg1, s32 arg2, u8 sound);
#else
void hud_panel_start(); /* K&R definition in the matching build */
#endif
void hud_panel_stop(void);
void hud_panel_draw(Gfx **gfx, s32 arg1, u8 buf);

/* hd_code/34430.c */
void mb_copy_dl(Gfx *src, Gfx *end, Gfx * N64P *dstp);
void mb_init(void);
void mb_start(s32 n, f32 scale, s32 arg2);
void mb_stop(void);
void mb_cancel(void);
u8 mb_is_active(void);
void mb_push_sample(s32 x, s32 y, s32 z, s32 a, s32 b, s32 c);
void mb_render_view(s32 x, s32 y, s32 z, s32 a, s32 b, s32 c, void *dl, void *seg6, void *seg7, s32 alpha);
void mb_draw(Gfx **gfxp, s32 arg1, u8 slot);

/* hd_code/37530.c */
void trail_init(void);
void trail_add(u8 period, s32 y, s16 x1, s16 z1, s16 x2, s16 z2, s32 x, s32 z, s16 yaw, u8 halfw, u8 a, u8 b, u8 d);
void trail_draw(Gfx **gfxp, struct TrailBuf *buf);

/* hd_code/39050.c */
void bridge_update_levels(s32 arg0);
void water_init(s32 id);
void water_update(u8 buf);
s32 water_get_height(s16 x, s16 z, s16 *y);
void water_draw(Gfx **gfx, u8 buf, u8 layer);
void fx_init_drift_quads(u8 id);
void fx_draw_drift_quads(Gfx **gfx, u8 buf);
void fx_init_power_pill(s32 arg0);
void fx_update_power_pill(void);
void fx_draw_power_pill(Gfx **gfx);
void hud_init_veh_overlay(void);
void hud_draw_veh_overlay(Gfx **gfx, u8 arg1);
void fx_init_carrier_blast(void);
void fx_draw_carrier_blast(Gfx **gfx, u8 arg1);

/* hd_code/3E4C0.c */
void hud_draw_target_arrow(Gfx **gfxp, Mtx *mtx, s32 x, s32 y, s32 z, s32 x2, s32 y2, s32 z2);
void hud_draw_radar(Gfx **gfxp, Mtx *mtx, u8 idx, s32 x, s32 z, s32 x2, s32 z2);

/* hd_code/405F0.c */
void rsp_init_ucode_table(void);
void rsp_send_task(u64 *data, s32 count, u8 slot, u8 yield, s32 id, u8 writebackAll);
void rsp_wait_task(s32 id);

/* hd_code/409D0.c */
void stats_level_start(void);
u32 stats_print_status(void);
u8 stats_save_status(void);
void stats_copy_result(u8 *src, u8 *dst);
void stats_set_level_flag(u8 bit);
s32 stats_test_level_flag(u8 bit);
void stats_finish_bonus_level(s32 arg0);
void stats_show_commpt_hint(void);
void stats_award_bonuses(void);
void stats_finish_level(s32 start);
s32 stats_tenths_to_frames(u16 arg0);
u16 stats_frames_to_tenths(u32 frames);
u8 stats_level_done(s32 level);

/* hd_code/41930.c */
void academy_announce_state(void);
void academy_play_state_tune(void);
void academy_next_mode(void);
u8 academy_advance_state(void);

/* hd_code/42240.c */
void hud_init_boost_dial(void);
void hud_draw_boost_dial(Gfx **gfxp, s32 arg1, u8 frame, u8 arg3);
void hud_init_missile_icon(void);
void hud_draw_missile_icon(Gfx **gfxp, Gfx * N64P *gfxp2, u8 frame, u8 arg3);
void hud_init_ram_icon(void);
void hud_draw_ram_icon(Gfx **gfxp, Gfx * N64P *gfxp2, u8 frame, u8 arg3);

/* hd_code/43A60.c */
void ptcl_init(void);
s32 ptcl_start(u8 type, s32 x, s32 y, s32 z, s32 floor);
void ptcl_update(void);
void ptcl_draw(Gfx **gdl, u8 buf);

/* hd_code/45BB0.c */
u8 cont_init(void);
void cont_start_read(void);
void cont_wait_read(void);
void cont_update(void);
void cont_clear(void);
void cont_show_method_option(void);

/* hd_code/46C20.c */
void hd_load_front_end(void);
void hd_rom_load(u32 devAddr, u32 dest, u32 *size, u8 arg3, u8 arg4, u8 arg5);

/* hd_code/46F60.c */
void cont_steer_reset(void);
void cont_stick_to_steer(s8 *arg0, s8 *arg1, u8 arg2);
f32 math_angle_2d_deg(s16 arg0, s16 arg1, s16 arg2, s16 arg3);

/* hd_code/479D0.c */
void crate_spawn_all(struct BoxSpawn *arg0, struct BoxSpawn *arg1);
void crate_update(u8 arg0);
void crate_draw(Gfx **arg0, s32 arg1);

/* hd_code/48D00.c */
void tnt_spawn_all(u8 *arg0, u8 *arg1);
void tnt_update(u8 arg0);
void tnt_draw(Gfx **gdl, struct Dyn48D00 *dyn);
void tnt_attach_to_vehicle(u8 arg0);
void tnt_ride_vehicle(u8 arg0);
void tnt_detach_all(void);
void tnt_test_point(s32 arg0, s32 arg1, s32 arg2);
void tnt_test_sphere(s32 arg0, s32 arg1, s32 arg2, s32 arg3);
void cont_boot_check_start(void);
u8 cont_query_paks(void *arg0, u8 *arg1);

/* hd_code/4B5E0.c */
#ifdef NON_MATCHING
void block_spawn_all(u8 *arg0, u8 *arg1, s32 s1);
#else
void block_spawn_all(u8 *arg0, u8 *arg1);
#endif
void block_update(u8 arg0);
void block_draw(Gfx **gdl, struct Dyn4B5E0 *dyn);
void block_attach_to_vehicle(u8 arg0);
void block_ride_vehicle(u8 arg0);
void block_detach_all(void);
void block_test_sphere(s32 arg0, s32 arg1, s32 arg2, s32 arg3);

/* hd_code/4DA80.c */
void shell_init(void);
s32 shell_fire(s16 speed, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, u8 type, s16 arg8);
void shell_update_all(void);
void shell_draw_all(Gfx **gdl, struct Dyn *dyn);

/* hd_code/4EBE0.c */
void spline_object_matrix(s32 x, s32 y, s32 z, s32 type, Mtx *mtx, void *state, Gfx *gfx1, Gfx *gfx2, s32 idx, s32 arg9, s32 arg10, s32 arg11);

/* hd_code/50670.c */
void ghost_init(void);
void ghost_start_recording(void);
void ghost_start_playback(void);
void ghost_record(void);
void ghost_draw(Gfx **gfxp, Mtx *mtx);
void ghost_keep_if_best(u32 arg0);
void ghost_make_translucent_dl(Gfx *gdl, Gfx *end);
void cull_override_visible_cells(u8 id, s32 px, s32 pz);

/* hd_code/51690.c */
void hud_load_pad_image(void);
Gfx *hud_draw_pad(s32 arg0, Gfx *arg1, s16 x, s16 y, u8 alpha);

/* hd_code/52D70.c */
#ifdef NON_MATCHING
void academy_place_marker(u8 arg0);
#else
void academy_place_marker(); /* K&R definition in the matching build */
#endif
u8 academy_find_level(u8 arg0, u8 *arg1);
void academy_draw_marker(Gfx **arg0);
void academy_check_marker(s32 x, s32 y, s32 z);
void academy_save_markers(void);

/* hd_code/53220.c */
void academy_build_window(u8 level);
void academy_reset_window_cursor(void);
u8 academy_marker_found(u8 level);
s32 hd_inflate(void);

/* hd_code/54E30.c */
void seq_prepare(s32 seq);
void seq_enter_start_level(void);
void seq_start_level(void);
void seq_finish(s32 arg0);
u64 seq_get_loop_done(u8 level);

/* hd_code/55970.c */
void hud_load_demo_banner(void);
Gfx *hud_draw_demo_banner(s32 arg0, Gfx *arg1);

/* hd_code/55D40.c */
void credits_init(void);
Gfx *credits_draw(Gfx * N64P *gfxp, Gfx *gfx);

/* hd_code/56010.c */
void debug_printf(const char *fmt, ...);

/* hd_code/56040.c */
void veh_collision_setup(s32 z, s32 a1, s32 b2, s32 b3, s32 x, s32 y, s32 b0, s32 h1, s32 h2, s32 b4, s32 b8, u8 *veh);
void veh_prepare_escape(u8 *veh);
void veh_collide_world(s32 kind);
void veh_report_world_hits(s32 x, s32 y, s32 z, s32 r, s32 kind, s32 id);
void veh_clip_free_arc(s32 a, s32 b);
s32 veh_free_arc_length(void);
s32 math_sphere_hits_tri_edges(s32 x, s32 y, s32 z, s32 r, u8 *tri);
s32 math_sphere_hits_tri_vertex0(s32 x, s32 y, s32 z, s32 r, u8 *tri);
s32 math_point_in_tri_2d(s32 bu, s32 bv, s32 cu, s32 cv, s32 au, s32 av, s32 pu, s32 pv);
void math_tri_project_2d(u8 *tri, s32 px, s32 py, s32 pz, s32 *out);
s32 math_sphere_tri_plane(s32 x, s32 y, s32 z, s32 r, u8 *tri, s32 *hit);
void veh_register_parts(s32 tag, u8 *p, u8 *end, u32 scale);
void veh_place_parts(s32 x, s32 y, s32 z, s32 tag, u8 *p, u8 *end, u8 *base, MtxChainRegs *regs);
void veh_collide_spheres(s32 tag, u8 *veh);
s32 crane_check_part_hits(s32 s0, s32 s1, s32 s2, s32 s3);
void crane_find_test_part(Unk8029C6E4Out *o);
s32 math_spheres_overlap(s32 pz, s32 r1, s32 qx, s32 qy, s32 px, s32 py, s32 qz, s32 r2);
void veh_update_tris(s32 val, AnimChannel *tbl, s32 x, s32 z, s32 id, u8 *model, u8 *mtxBase);
s32 veh_tri_is_solid(s32 sub);
s32 veh_tris_all_solid(s32 id);
s32 veh_tris_all_solid_impl(s32 id);
void anim_clear_copy_queue(void);
void anim_run_mtx_copies(void);
void anim_tex_init(void);
void anim_tex_find_patches(u8 *dl, u8 *dlEnd, s32 key);
#ifdef NON_MATCHING
void anim_tex_update_all(u8 *param);
#else
void anim_tex_update_all(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void anim_update_model(u8 *base, u8 *other, AnimChannel *ch);
s32 anim_init_model(u32 *bufA, u32 *bufB, AnimChannel *ch, u8 *hdr);
void anim_blend_channels(s32 a, s32 b, AnimChannel *base);
void anim_merge_poses(s32 a, s32 b, AnimChannel *base);
void anim_play(AnimChannel *base, s32 idx, s32 val);
void anim_stop(s32 idx, AnimChannel *base);
void anim_rewind(s32 idx, AnimChannel *base);
void anim_set_frame(f32 f, AnimChannel *base, s32 idx, s32 val);
void anim_set_speed(AnimChannel *base, s32 idx, s32 val);
void anim_set_reverse(AnimChannel *base, s32 idx, s32 val);
void anim_set_loop_mode(AnimChannel *base, s32 idx, s32 val);
void anim_set_interp(f32 f, AnimChannel *base, s32 idx, s32 val);
void anim_get_state(s32 idx, AnimChannel *base, s32 *out);
void anim_tex_play(s32 key, s32 val);
void anim_tex_set_frame(f32 f, s32 key, s32 val);
void anim_tex_set_speed(s32 key, s32 val);
void anim_tex_set_reverse(s32 key, s32 val);
void anim_tex_set_loop_mode(s32 key, s32 val);

/* hd_code/5BF40.c */
void tex_load_table(void);
void tex_resolve_dl(u32 *dl, u32 *end);
void tex_resolve_dl_impl(u32 *dl, u32 *end, Unk802A08E4Regs *r);
void tex_load_to_heap(s32 id, u8 *param);
u32 tex_get(s32 id, u8 *param);
u32 tex_get_impl(s32 id, u8 *param);
void tex_load_palette(s32 id, void *dest);
void tex_queue_load(s32 id, u8 *dest, u8 *param);
void tex_queue_load_impl(s32 id, u8 *dest, u8 *param);
void tex_queue_palette(s32 id, void *dest);

/* hd_code/5CB60.c */
u32 get_cop0_status(void);
void veh_set_record(s32 a0Val, s32 id, s32 v0Val, s32 v1Val, u8 *obj);
void veh_add_record(s32 a0Val, s32 a1Val, s32 v0Val, s32 v1Val, u8 *hdr);
void initlevel(u8 *obj, s32 arg1);
void lvl_load_model(s32 type, Out802A396C *out);
u8 *lvl_build_coll_tri(u8 *rec, u8 *v, s32 id, s32 h52, s32 b57, s32 b56, s32 *s1io, s32 b4F, s32 b55);

/* hd_code/5FD50.c */
void cull_restart_now(void);
void cull_restart(s32 arg0);
void cull_step_delayed(s32 arg0, Gfx *arg1, Vtx *arg2, s32 arg3);
void lvl_build_visible_dls(u32 *dl0, u32 *dl1, u32 *dl2, u32 *dl3, u32 *gfx);

/* hd_code/60D50.c */
void cam_apply_height_zone(u8 *level);
void chopper_find_cruise_height(u8 *level);
s32 cam_load_level_defaults(void);

/* hd_code/60F60.c */
void tex_reset_decode_queue(void);
void tex_queue_decode(s32 a, s32 b, s32 c, s32 d);
void tex_decode_next(void);
s32 tex_decode(u8 *rec);
void fx_finish_all(void);
s32 fx_count_active(void);
void fx_reset(void);
void fx_alloc_texture_caches(void);
s32 fx_spawn(Io802A6274 *io, u8 *def, s32 data, s32 type, s32 x, s32 y, s32 z, s32 w24, s32 w28, s32 w18, s32 w1C, s32 w2C, s32 b35);
void fx_draw_all(void);

/* hd_code/62740.c */
void veh_widen_bands(u8 *veh);
s32 veh_free_arc_mid(void);
void veh_hit_slowdown(u8 *veh, s32 limit);
void veh_set_target_heading(u8 *veh, s16 *angle);
void veh_reverse_if_behind(u8 *veh);
s32 veh_turn_into_free_arc(u8 *veh, s32 cur, s32 target, s32 *curOut, f32 scale);
s32 veh_step_heading(u8 *veh, s32 delta, s32 v1, s32 *targetOut);
void veh_reset_state(u8 *veh);
void veh_snapshot(u8 *veh, u8 *src, s32 *w0, s32 *w1, s32 *w2);
u64 *veh_rollback(u8 *veh, u8 *dst, s32 *w0, s32 *w1, s32 *w2, u64 *src, u64 *dst2, s32 size, u64 **dst2End);
void veh_sync_buffers(u64 *a, u64 *b, s32 size);
void veh_coast(u8 *veh);
void veh_steer_throttle(s32 rate, u16 *angle, u8 *veh, s16 *speed, s32 mode, u8 *flags, s16 *bands, s32 delta);
void veh_throttle(u8 *veh, s16 *speed, s32 mode, u8 *flags, s16 *bands, s32 delta);
s32 veh_near_top_speed(u8 *veh, s32 range);
void veh_steer(s32 rate, u16 *angle);
void veh_turn_heading(u8 *veh, u16 *heading, s32 rate, s16 *speedp, u16 *target, u16 *out, s8 *flag, s32 sound);
f32 veh_slope(s16 *div, u8 *f, s32 *p, f32 *out);
void veh_apply_slope(u8 *veh, s16 *speed, s32 kind, s8 *f, s32 *p, s32 clamp, f32 div);
s32 veh_move_along_heading(f32 f, s32 angle, s16 *len, s32 *px, s32 *pz, Out802A860C *out);
void veh_ground_contact(u8 *veh, s32 id, s32 *px, s32 *py, s32 *pz, s32 x, s32 z, s32 divB, s32 divA, s16 *angle, u8 *flags, s16 *tbl, s32 *a, s32 *b, s32 *c, s32 *ys, Regs802A8768 *r, TriSideOut *f);
s32 veh_update_stand_on(s32 x, s32 z, s16 *tbl, s16 *angle, s32 *ys, s32 key, u8 *veh, s32 fpIn, TriSideOut *f);
s32 veh_wheel_offset(s32 index, s16 *tbl, s16 *angle, s32 *dz);
s32 *veh_init_ground(s16 *tbl, s32 y, s32 x, s32 z, s32 *dst, s32 *mid, s16 *angle, s32 key, s32 fpIn, u8 *veh, TriSideOut *f);
s32 veh_reground(s16 *tbl, s32 y, s32 x, s32 z, s32 *dst, s32 *mid, s16 *angle, s32 key, s32 fp, u8 *veh, TriSideOut *f, Out802A9A60 *out);
s32 ground_slot_height(s32 index, s32 x, s32 z, s32 y, s32 skip, u8 *veh, s32 fpIn, TriSideOut *f);
s32 ground_scan_part_tris(s32 x, s32 z, s32 y, TriSideOut *f, s32 *fpOut);
s32 ground_scan_moveable(s32 x, s32 z, s32 y, s32 skip, TriSideOut *f, s32 *idOut, s32 *a2Out);
s32 ground_scan_grid(s32 x, s32 z, s32 y, TriSideOut *f, s32 *t3io, s32 *fpio);
s32 ground_tri_contains(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2, TriSideOut *out);
s32 ground_tri_box_contains(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2);
void mtx_build_model_rot(s32 x, s32 y, s32 z, s16 rx, s16 ry, s16 rz, s32 scale, Mtx *m);
void mtx_build_model(s32 x, s32 y, s32 z, s32 scale, s32 *m);
void mtx_copy_at(u8 *src, u8 *dst, s32 off);
s32 mtx_chain_transform(s32 count, s32 *offsets, u8 *base, s32 x, s32 y, s32 z, MtxChainRegs *regs);
s32 veh_deck_transform(s32 id, u16 *desc, u8 *base, MtxChainRegs *regs, s16 **vertsOut);
void veh_deck_to_local_c(s32 id, s32 x, s32 z, s16 *uOut, s16 *wOut);
void veh_deck_to_local(s32 id, s32 x, s32 z, InterpRegs *r);
void veh_deck_to_world_c(s32 id, s32 x, s32 z, s32 *uOut, s32 *wOut);
void veh_deck_to_world(s32 id, s32 x, s32 z, InterpRegs *r);
s32 veh_is_riding(s32 id);
s32 veh_rides_on(s32 key0, s32 key1);
void veh_attach_riders(s32 id);
void veh_move_riders(s32 id);
s32 veh_has_partial_rider(s32 id);
s32 veh_follow_heading(s16 *tbl, s32 x1, u16 *angle, s32 id, s32 z1, s32 x2, s32 z2, s32 *s3Out, InterpRegs *r);
s32 veh_follow_angle(s32 ax, s32 az, s32 dx, s32 dz, s32 bx, s32 bz);
void veh_points_transform(s32 id, s16 *p, s16 *end, u8 *base, MtxChainRegs *regs);
s32 veh_find_point(s32 id, s32 n, u8 **recOut);
s32 math_dist3(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz);
s32 veh_zone_alpha(s32 id, s32 x, s32 y, s32 z, ZoneScanRegs *r);
s32 ground_over_object(s32 x, s32 y, s32 z);
s32 ground_scan_guard_tris(s32 x, s32 z, s32 y, TriSideOut *f, TriScanRegs *r);

/* hd_code/679E0.c */
void obj_blast_around_carrier(s32 radius);
void veh_check_wrap(s32 *px, s32 *py, s32 *pz);
void veh_maze_object_hits(s32 z, s32 a1, s32 x, s32 y, s32 b0, s32 id, u8 *vehicle);
s32 ground_in_triangle(s32 px, s32 pz, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2);
void fx_spawn_node_hit(s32 x, s32 y, s32 z);
void fx_spawn_at(s32 x, s32 y, s32 z, s32 kind, s32 data);
void fx_spawn_at_late(s32 x, s32 y, s32 z, s32 kind, s32 data);
s32 veh_serialize_state(u8 *dst, u8 *src, u32 *words);
void veh_deserialize_state(u8 *src, u8 *dst, u32 *words);
void mtx_to_n64(u16 *m);
void mtx_translate(s32 x, s32 y, s32 z, s32 *m);
void mtx_rotate_y(s32 angle, s32 *m);
void mtx_rotate_z(s32 angle, s32 *m);
void mtx_rotate_x(s32 angle, s32 *m);
void mtx_scale(s32 x, s32 y, s32 z, s32 *m);
void mtx_mul(s32 *b, s32 *m);
s32 math_rotate_xz_inv(s32 x, s32 z, s32 angle, s32 *t1out);
s32 math_atan_c(s32 x);
s32 math_atan(u32 x);

/* hd_code/69BB0.c */
void driver_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f);
void driver_stop_blend_anim(void);
s32 driver_try_exit(s32 dist);
void driver_update(void);
void driver_load_state(void *src);

/* hd_code/6B4A0.c */
void sideswipe_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp);
void sideswipe_enter(void);
s32 sideswipe_exit_check(void);
void sideswipe_leave(void);
#ifdef NON_MATCHING
void sideswipe_reground(s32 fp);
#else
void sideswipe_reground(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
#ifdef NON_MATCHING
void sideswipe_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
#else
void sideswipe_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void sideswipe_load_state(void *src);

/* hd_code/6C5E0.c */
void thunderfist_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f);
s32 thunderfist_exit_check(void);
void thunderfist_leave(void);
void thunderfist_enter(void);
#ifdef NON_MATCHING
void thunderfist_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26);
#else
void thunderfist_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void thunderfist_load_state(void *src);

/* hd_code/6E200.c */
void skyfall_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp);
void skyfall_enter(void);
s32 skyfall_exit_check(void);
void skyfall_leave(void);
#ifdef NON_MATCHING
void skyfall_reground(s32 fp);
#else
void skyfall_reground(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 skyfall_ride_attach(s32 id, InterpRegs *r);
s32 skyfall_ride_follow(s32 id, InterpRegs *r);
#ifdef NON_MATCHING
void skyfall_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
#else
void skyfall_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void skyfall_load_state(void *src);
void ramdozer_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp);
void ramdozer_enter(void);
s32 ramdozer_exit_check(void);
void ramdozer_leave(void);
#ifdef NON_MATCHING
void ramdozer_reground(s32 fp);
#else
void ramdozer_reground(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 ramdozer_ride_attach(s32 id, InterpRegs *r);
s32 ramdozer_ride_follow(s32 id, InterpRegs *r);
#ifdef NON_MATCHING
void ramdozer_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
#else
void ramdozer_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void ramdozer_load_state(void *src);

/* hd_code/71140.c */
void backlash_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp);
void backlash_enter(void);
s32 backlash_exit_check(void);
void backlash_leave(void);
void backlash_reground(void);
void backlash_ride_attach(s32 id, InterpRegs *r);
s32 backlash_ride_follow(s32 id, TriSideOut *f);
void backlash_update(void);
void backlash_load_state(void *src);

/* hd_code/72B80.c */
void amdream_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f);
#ifdef NON_MATCHING
void amdream_enter(s32 arg0);
#else
void amdream_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 amdream_exit_check(void);
void amdream_leave(void);
void amdream_reground(void);
void amdream_ride_attach(s32 id, InterpRegs *r);
void amdream_ride_follow(s32 id, f32 *f24);
void amdream_update(void);
void chopper_spawn(s32 a1Val, u8 *hdr);
void chopper_update_sound(void);
void chopper_update(void);
void chopper_update_mode(void);

/* hd_code/75490.c */
void carrier_spawn(u8 *model, s32 x, s32 z, s32 speed, s32 limit, s32 heading, s32 fp);
void carrier_update_sound(void);
void carrier_update(void);
void crane_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp);
void crane_enter(void);
s32 crane_exit_check(void);
void crane_leave(void);
void crane_update(void);

/* hd_code/772A0.c */
void train_spawn(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp);
#ifdef NON_MATCHING
void train_enter(s32 arg0);
#else
void train_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 train_exit_check(void);
void train_leave(void);
void train_update(void);

/* hd_code/77E20.c */
void obj_check_level_end(void);
void obj_clear_delayed_damage(void);
void obj_update_delayed_damage(void);
void veh_contacts_clear(void);
void veh_prev_contacts_clear(void);
void veh_contacts_add(s32 value);
s32 veh_contacts_has(s32 value);
s32 obj_find_nearest_target(void);
void obj_calc_target_time(s32 arg0);
void obj_draw_all(u32 *gA, u32 *gB, u32 *gC, u32 *gD, s32 mtxA, s32 mtxB, u32 *gE, u32 *gF);
s32 obj_end_object_hidden(void);
void obj_move_model(struct LevelObjectHead *model, s32 dx, s32 dy, s32 dz);
void obj_collide_vehicle(s32 id, u8 *vehicle);
void obj_mark_part_gone(struct LevelObject *e, s32 id);
void obj_tri_clip_free_arc(struct CollisionTri *t);
void obj_spawn_people(struct LevelObject *e);
void obj_check_destroyed(struct LevelObject *e);
void obj_drop_unsupported_parts(struct LevelObject *e);
void obj_spawn_break_effects(struct LevelObject *e, s32 part, s32 level);
void obj_clear_effect_queue(void);
void obj_queue_effect(u32 *src);
void obj_run_effect_queue(void);
void obj_start_part_fade(s32 id, struct LevelObject *e);
void obj_start_sinking_part(s32 id, struct LevelObject *e);
void obj_start_topple(struct LevelObject *e, s32 index);
void obj_blast_damage(s32 x, s32 y, s32 z, s32 radius, s32 amount);
s32 obj_path_buildings_destroyed(void);
#ifndef LEGACY_obj_count_destroyed_path_buildings
s32 obj_count_destroyed_path_buildings(void);
#endif
u32 obj_find_carrier_target(void);
void obj_tally(s32 onlyFlagged);
s16 *lvl_group_get(s32 index);
void lvl_group_move_part(s32 group, s32 part, s32 x, s32 y, s32 z);
void lvl_groups_update_tris(void);

/* hd_code/7F8B0.c */
void lzss_decompress(u32 *src, u32 *dst, u32 work, u8 type);
void kiunzip_entry(u8 **src, u8 **dst, s32 work);
void veh_start_engine_sound(s32 arg0, s32 arg1);
void veh_stop_engine_sounds(void);
void veh_set_engine_level(s32 level);
void veh_update_horn(s32 sfx);

/* hd_code/7FB50.c */
s32 record_status(u8 *out);
void apply_status(void *in);
u32 create_status(void *outp, u8 level);
void jbomb_spawn(s32 x, s32 y, s32 z, s32 heading, u8 *model, s32 fp);
s32 jbomb_exit_check(void);
void jbomb_leave(void);
void jbomb_enter(void);
#ifdef NON_MATCHING
void jbomb_reground(s32 fp);
#else
void jbomb_reground(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void jbomb_ride_attach(s32 id, InterpRegs *r);
void jbomb_ride_follow(s32 id, s32 *s3, InterpRegs *r);
void jbomb_update(void);
void jbomb_load_state(void *src);

/* hd_code/83910.c */
void barge_spawn(s32 type, s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp);
#ifdef NON_MATCHING
void barge_enter(s32 arg0);
#else
void barge_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 barge_exit_check(void);
void barge_leave(s32 type);
void barge_update(s32 type);

/* hd_code/853D0.c */
void ballista_spawn(s32 x, s32 y, s32 z, s32 heading, u8 *model, s32 fp);
void ballista_enter(void);
s32 ballista_exit_check(void);
void ballista_leave(void);
void ballista_ride_attach(s32 id, InterpRegs *r);
void ballista_ride_follow(s32 id, s32 *s3, InterpRegs *r);
void ballista_update(void);
void ballista_load_state(void *src);

/* hd_code/86ED0.c */
void veh_start_turbo(u8 *state);

/* hd_code/86F60.c */
void police_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f);
#ifdef NON_MATCHING
void police_enter(s32 arg0);
#else
void police_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 police_exit_check(void);
void police_leave(void);
void police_reground(void);
s32 police_ride_attach(s32 id, InterpRegs *r);
s32 police_ride_follow(s32 id, TriSideOut *f);
void police_update(void);

/* hd_code/88160.c */
void ateam_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f);
#ifdef NON_MATCHING
void ateam_enter(s32 arg0);
#else
void ateam_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 ateam_exit_check(void);
void ateam_leave(void);
void ateam_reground(void);
s32 ateam_ride_attach(s32 id, InterpRegs *r);
s32 ateam_ride_follow(s32 id, TriSideOut *f);
void ateam_update(void);

/* hd_code/89250.c */
void coll_spawn_blast_fx(s32 x, s32 y, s32 z);
void coll_blast_at_probe(s32 amount, s32 radius);
s32 coll_probe_objects(s32 r, s32 amount);
s32 coll_probe_vehicles(s32 r);
void coll_block_toward(s32 x1, s32 z1, s32 x2, s32 z2);
s32 coll_heading_in_ring(s32 idx);
void coll_begin_probe(s32 x, s32 y, s32 z);
void coll_probe_tris(s32 x, s32 y, s32 z, s32 r, s32 kind, s32 tag);
void coll_step_xz(s32 x, s32 z, s32 len, s32 angle);
s32 coll_ground_height(s32 x, s32 z, s32 y);

/* hd_code/8A080.c */
void radar_clear_markers(void);
void radar_set_marker(s32 id, s32 x, s32 y, s32 z, s32 size);
void radar_remove_marker(s32 id);
s32 radar_has_marker(s32 id);
void coll_reset_dyn_tris(void);
#ifdef NON_MATCHING
void coll_add_dyn_tris(u8 *items, s32 n, s32 h52, s32 id, s32 b57, s32 b4F, s32 s1);
#else
void coll_add_dyn_tris(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void coll_disable_tris(u8 *start, u8 *end);

/* hd_code/8A2E0.c */
void commpt_setup(u8 *obj);
Gfx *commpt_draw(Gfx *gdl, s32 cur, Gfx *dl2, Mtx *mtx);
void commpt_update(void);
void commpt_save_flags(void);
void commpt_load_flags(void);

/* hd_code/8AEE0.c */
void starski_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f);
#ifdef NON_MATCHING
void starski_enter(s32 arg0);
#else
void starski_enter(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 starski_exit_check(void);
void starski_leave(void);
#ifdef NON_MATCHING
void starski_reground(s32 fpIn, s32 a3, f32 f12, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26);
#else
void starski_reground(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
s32 starski_ride_attach(s32 id, InterpRegs *r);
s32 starski_ride_follow(s32 id, InterpRegs *r);
#ifdef NON_MATCHING
void starski_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26);
#else
void starski_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void cyclone_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f);
s32 cyclone_exit_check(void);
void cyclone_leave(void);
void cyclone_enter(void);
#ifdef NON_MATCHING
void cyclone_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26);
#else
void cyclone_update(); /* hd.c passes nothing: the asm reads leaked registers */
#endif
void cyclone_load_state(void *src);

/* hd_code/8DDB0.c */
void shuttle_spawn(u8 *hdr);
void shuttle_update(void);

/* hd_code/8FD90.c, 93B50.c, 96300.c: osDpSetStatus, alCSPGetVol and
 * osDpGetStatus (declared by <PR/os.h> / <PR/libaudio.h>) */

/* hd_front_end/00000.c */
s32 fe_digger_init(void);
void fe_digger_frame(void);

/* hd_front_end/01C40.c */
void fe_player_panel_init(u8 arg0);
void fe_player_ticker_reset(u8 arg0);
void fe_player_ticker_set_text(u8 slot, u8 arg1);
s32 fe_player_ticker_at_end(void);
Gfx *fe_player_draw_panel(Gfx *arg0, struct PlayerSelDyn *dyn, s32 arg2);
void fe_player_new_game(u8 slot, u8 send, u8 newGame);
void fe_player_load_slots(void);
void fe_player_build_load_menu(void);
void fe_player_build_erase_menu(void);
void fe_player_name_entry_open(char *title, u16 *glyphs, u8 arg2, u8 width, char *buf);
Gfx *fe_player_name_entry_draw(Gfx *arg0, struct PlayerSelDyn *dyn, s32 *count);
void fe_player_medals_show_quiet(u8 arg0);
void fe_player_medals_init(u8 arg0);
Gfx *fe_player_draw_medals(Gfx *start, void *gfxp, s32 *count);
u64 fe_player_loop_done_mode(u8 level);
void fe_player_load_level_save(void);
void fe_player_save_pending(void);
void fe_player_update_level_kinds(void);
void fe_player_regrade_missions(void);
void fe_player_regrade_platinum(void);

/* hd_front_end/06790.c */
void fe_promo_init(void);
Gfx *fe_promo_draw(Gfx *arg0, FeDyn *dyn, u8 arg2, s32 *arg3);

/* hd_front_end/07390.c */
void fe_pak_request_hook(void);
void fe_pak_error_frame(s32 arg0);

/* hd_front_end/07800.c */
u8 fe_stats_grade_level(u8 *arg0, u8 arg1, u8 arg2);
s8 fe_stats_count_unlocks(void);
#ifdef NON_MATCHING
u8 fe_stats_grade_time(u16 arg0, u8 arg1, u8 arg2);
#else
u8 fe_stats_grade_time(); /* K&R definition in the matching build */
#endif
void fe_boot_screen_init(s32 arg0);
void fe_boot_screen_frame(void);

/* hd_front_end/09570.c */
Gfx *fe_lvlsel_build_globe_dl(void);
Gfx *fe_lvlsel_build_academy_dl(void);
Gfx *fe_lvlsel_build_level_icons_dl(void);
Gfx *fe_lvlsel_build_starfield_dl(void);

/* hd_front_end/0C450.c */
Gfx *fe_solar_draw(Gfx *gdl, u8 *dyn);

/* hd_front_end/0DE70.c */
void fe_scene_load(u8 arg0);
Gfx *fe_scene_draw(FeDyn *dyn, Gfx *arg1);
Gfx *fe_comm_point_draw(FeDyn *dyn, Gfx *arg1);
void fe_comm_point_load(void);

/* hd_front_end/0E7B0.c */
void fe_pak_start_thread(void);
s32 fe_pak_list_files(void);
s32 fe_pak_has_files(void);

/* hd_front_end/10850.c */
void fe_best_build_menu(void);
void fe_best_next_player(void);
void fe_best_color_items(void);
void fe_best_clear_times(u8 pn);
Gfx *fe_best_draw_icon(s32 arg0, Gfx *gfx);

/* hd_front_end/11530.c */
void fe_globe_init(s32 level);
void fe_globe_frame(void);
void fe_globe_quad_at_level(Vtx *v, u8 level, f32 dlat, f32 dlon, u8 w, u8 h, f32 scale, u8 flip);
void fe_globe_latlon_to_xyz(f32 *lat, f32 *lon, f32 *x, f32 *y, f32 *z, f32 r);
void fe_globe_respawn_star(Vtx *v, s32 idx, s32 z);
void fe_load_vehicle_icons(void);
void fe_open_reachable_levels(u8 rank);
Gfx *fe_globe_draw_earth(Gfx *arg0, s32 arg1);
#ifdef NON_MATCHING
s32 fe_globe_level_locked(u8 level);
#else
s32 fe_globe_level_locked(); /* K&R definition in the matching build */
#endif

/* hd_front_end/17990.c */
void fe_back_loop(void);

/* hd_front_end/196F0.c */
void fe_bg_load(u8 mode);
Gfx *fe_bg_draw(Gfx *gfx, s32 arg1, s32 *count);

/* hd_front_end/1A240.c */
void fe_attract_pic_load(s32 arg0);
Gfx *fe_attract_pic_draw(s32 arg0, Gfx *arg1);

/* hd_front_end/1AE80.c */
s32 fe_pak_restore_from_eeprom(void);

/* hd_front_end/1B100.c */
void fe_load_model(s32 type, u8 * N64P *model, u8 * N64P *heap, u8 * N64P *dl);
void fe_anim_update_model(void *ch, void *base, void *other);
void fe_anim_init_model(u8 *hdr, u32 * N64P *bufs, void *ch);
void fe_anim_start_channel(void *base, s32 idx, s32 v040C, s32 v0480, f32 f, s32 v039C, s32 v03D4);
void fe_veh_reset_tex_anims(s32 kind);
#ifndef LEGACY_fe_veh_set_heading_tex_anims
void fe_veh_set_heading_tex_anims(s32 kind, s32 val);
#endif

/* (hd_code/69000.c, 69930.c: fixed-point trig in the asm blobs) */
s32 math_asin_c(s32 sine);
s32 math_asin(u32 sine);
s32 math_cos(s32 angle);
s32 math_sin(s32 angle);

/* (libultra functions the game calls by address names; aliased in undefined_syms.hd_code.us.v11.txt) */

#endif
