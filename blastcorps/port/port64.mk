# ---------------------------------------------------------------------------
# The clang check: the game, ultralib and platform sources built as a clang
# x86_64 bc_headless (x86_64-w64-mingw32, mingw-w64's binutils and CRT).  Nobody
# plays it: it is a second compiler for the same sources, with checks MSVC
# doesn't have.  See README.md ("The clang check").  Included by Makefile.
#
#   make -C port clang-check      all of the below, then its per-frame traces
#                                 against the MSVC bc_headless (tools/tracecmp.py:
#                                 the attract demos and every level)
#   make -C port headless64       build/headless64/bc_headless.exe (+ align64.py)
#   make -C port ptrcheck64       pointer-width audit + record layouts (i686 vs x86_64)
#   make -C port overflow-census  signed overflows at run time (-fsanitize)
H64 := build/headless64
CC64 := clang-18 --target=x86_64-w64-mingw32
LD64 := x86_64-w64-mingw32-gcc
NM64 := x86_64-w64-mingw32-nm
# -fms-extensions: __ptr32 (N64P); -mno-ms-bitfields: GCC bitfield packing (the
# GBI's Gfx); -fdata-sections: one section per object, for coffpin.py;
# -fno-auto-import: direct (RIP-relative) access to data, no .refptr stubs;
# -ffp-contract=off: no FMA contraction.
# Errors: a pointer that changes address space under another pointer
# (`u8 **p = D_x` with D_x an N64P array) and pointer/integer mixups.
# Not warned: int <-> pointer casts of 32-bit values (N64 addresses); the
# dangerous kind, a signed 32-bit int cast to a pointer (it sign-extends:
# N64_IPTR), is what ptrcheck64 reports as SEXT.
C64_COMMON := -fms-extensions -mno-ms-bitfields -fdata-sections -fno-addrsig -fno-auto-import -ffp-contract=off \
	-Werror=incompatible-pointer-types -Werror=int-conversion -Wno-pointer-to-int-cast \
	-Wno-void-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-int-to-void-pointer-cast
# Code that sees N64 data is compiled through LLVM IR: the x86-64 ABI gives a
# global array of 16 bytes or more 16-byte alignment and clang assumes it for
# every array, extern ones too, so it used movaps on gCamPathBasis (an N64 array at
# an 8-aligned address: a fault).  tools/llalign64.py lowers every alignment
# above 4 on memory accesses and N64 globals in the optimised IR, then the code
# is generated (no further IR optimisation); tools/align64.py checks the
# objects: no 16-byte-aligned access except on the stack or constants.
# $(call cc64ir,FLAGS): $< -> $@
cc64ir = $(CC64) $(1) -S -emit-llvm -o $@.ll $< && \
	$(PYTHON) tools/llalign64.py $@.ll $@.a.ll $(H64)/addrs.txt && \
	$(CC64) $(1) -Wno-unused-command-line-argument -Xclang -disable-llvm-optzns -c -o $@ $@.a.ll && \
	rm -f $@.ll $@.a.ll
IR64_DEPS := tools/llalign64.py $(H64)/addrs.txt
# The game C: -DNON_MATCHING -DPORT_HOST (the game's port hooks,
# include/game/port.h), the port's <ultratypes.h> shim forced in, ultralib's
# headers with the N64 pointer marks ($(H64)/ulinc), the SDK's sinf/cosf kept
# (-fno-builtin-*).
# -fno-optimize-sibling-calls: a call's return address names its real caller
# (the keys of --clock and --sync, see os_time.c / os_thread.c; crash reports).
# -fno-inline-functions: only `inline` functions are inlined: clang inlines far
# more than the N64's compiler (lvl_load_object_table's PI wait came out inside
# lvl_spawn_objects: 8627 sync mismatches in verify), so game functions keep their
# own bodies.  HL_EXTRA: extra flags for debugging.
HL64_CFLAGS := -std=gnu89 -nostdinc -I$(ROOT) -I$(ROOT)/include -I$(H64)/ulinc/include \
	-I$(H64)/ulinc/include/PR -I$(H64)/ulinc/include/compiler/modern_gcc -include include/port_ultratypes.h \
	-D_LANGUAGE_C -D_FINALROM -DNON_MATCHING -DPORT_HOST -D_MIPS_SZLONG=32 -D_MIPS_SIM=1 \
	-DBUILD_VERSION=VERSION_I -DBUILD_VERSION_STRING=\"2.0I\" \
	-O2 -fno-strict-aliasing -fwrapv -fno-common \
	-fno-builtin-sinf -fno-builtin-cosf -fno-builtin-sin -fno-builtin-cos \
	-Wno-unknown-pragmas -DPORT_HOST \
	$(C64_COMMON) -g -fno-optimize-sibling-calls -fno-inline-functions -Wno-incompatible-library-redeclaration \
	$(HL_EXTRA)
# game functions call __cyg_profile_func_enter (os_thread.c): with --sync,
# the entries of the functions that read the game's retrace counter are
# switch points too (port/tools/cmp_spec.py lists them)
HL_GAME_INSTR := -finstrument-functions
# ultralib: its own headers, from the marked copies (ulhdr64.py) in $(H64)/ulinc
HL64_UL_CFLAGS := -I$(H64)/ulinc -std=gnu89 -nostdinc -I$(UL_DIR) -I$(H64)/ulinc/include \
	-I$(H64)/ulinc/include/compiler/modern_gcc -I$(H64)/ulinc/include/PR -I$(H64)/ulinc/src/libc \
	-D_MIPS_SZLONG=32 -DBUILD_VERSION=VERSION_I -DBUILD_VERSION_STRING=\"2.0I\" -DNDEBUG -D_FINALROM \
	-DF3DEX_GBI -D_LANGUAGE_C -O2 -fno-strict-aliasing -fwrapv -mlong-double-64 -fno-common -fno-builtin -g -w \
	$(C64_COMMON) -include include/port_n64ptr.h
HL64_PLAT_CFLAGS := $(HL64_CFLAGS) -Isrc/platform -Wall -Wno-unknown-pragmas
HL64_HOST_CFLAGS := -O2 -Wall -Isrc -Isrc/platform -g -fno-addrsig -Wno-pointer-to-int-cast
# the port's own host C (platform host side, the load layer, audio) is clean with -Wextra too (the generated
# tables, swaptab.c / copytab.c / ptrtab.c, are compiled without it)
HL64_HOST_WARN := -Wextra
# a fixed base below RDRAM (RIP-relative reach), no relocations, no ASLR: every
# host address the game can be handed (image, heap, fiber stacks) is below 4 GB
LDFLAGS64 := -Wl,--image-base,0x40000000 -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va \
	-Wl,--disable-reloc-section

HL64_GAME_OBJS := $(HL_HD:%=$(H64)/game/hd/%.o) $(HL_FE:%=$(H64)/game/fe/%.o) \
	$(HL_UL:%=$(H64)/game/ul/%.o) $(HL_UL_LE:%=$(H64)/game/ul/%.o) $(HL_ULFE:%=$(H64)/game/ulfe/%.o) \
	$(HL_PLAT:%=$(H64)/game/plat/%.o)
HL64_RECS := $(HL64_GAME_OBJS:.o=.rec)
HL64_HOST_OBJS := $(HL_HOST:%=$(H64)/host/%.o) $(H64)/host/rdram.o $(H64)/host/inflate.o \
	$(HL_LOAD:%=$(H64)/host/load/%.o) $(H64)/host/swaptab.o $(HL_AUDIO:%=$(H64)/host/audio/%.o)

$(H64)/addrs.txt: $(GEN)/addrs.txt
	@mkdir -p $(@D)
	cp $< $@

# ultralib's public headers with the N64 pointer marks (tools/ulhdr64.py,
# data/ultralib_n64ptr.txt): the ul_* objects share these records with the game
# ... and its sources with the function pointers kept in N64 memory used
# through N64FN_SET/N64FN_GET (tools/ulsrc64.py): $(H64)/ulsrc/ul_*.c
UL64_SRCS := $(HL_UL:%=$(GAME_SRC)/%.c) $(HL_ULFE:%=$(FE_SRC)/%.c)
$(H64)/ulinc.stamp: data/ultralib_n64ptr.txt tools/ulhdr64.py tools/ulsrc64.py $(UL64_SRCS)
	rm -rf $(H64)/ulinc $(H64)/ulsrc
	@mkdir -p $(H64)/ulinc
	$(PYTHON) tools/ulhdr64.py $(UL_DIR) data/ultralib_n64ptr.txt $(H64)/ulinc
	$(PYTHON) tools/ulsrc64.py $(UL_DIR) $(H64)/ulinc $(H64)/ulsrc $(UL64_SRCS)
	touch $@
$(H64)/ulsrc/%.c: $(H64)/ulinc.stamp
	@test -f $@

$(H64)/game/hd/%.raw.o: $(GAME_SRC)/%.c include/port_ultratypes.h include/port_n64ptr.h $(H64)/ulinc.stamp \
		$(IR64_DEPS) Makefile port64.mk
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_CFLAGS) $(HL_GAME_INSTR))
$(HL_CLOCK_CALLERS:%=$(H64)/game/hd/%.raw.o): HL64_CFLAGS += -fno-inline
$(H64)/game/fe/%.raw.o: $(FE_SRC)/%.c include/port_ultratypes.h include/port_n64ptr.h $(H64)/ulinc.stamp \
		$(IR64_DEPS) Makefile port64.mk
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_CFLAGS) $(HL_GAME_INSTR))
$(H64)/game/ul/%.raw.o: $(H64)/ulsrc/%.c $(H64)/ulinc.stamp $(IR64_DEPS) Makefile port64.mk
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_UL_CFLAGS))
$(H64)/game/ul/ul_sinf.raw.o: $(GEN)/le/sinf.c $(H64)/ulinc.stamp $(IR64_DEPS)
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_UL_CFLAGS) -I$(UL_DIR)/src/gu)
$(H64)/game/ul/ul_cosf.raw.o: $(GEN)/le/cosf.c $(H64)/ulinc.stamp $(IR64_DEPS)
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_UL_CFLAGS) -I$(UL_DIR)/src/gu)
$(H64)/game/ul/ul_xldtob.raw.o: $(GEN)/le/xldtob.c $(H64)/ulinc.stamp $(IR64_DEPS)
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_UL_CFLAGS))
$(H64)/game/ul/ul_xprintf.raw.o: $(GEN)/le/xprintf.c $(H64)/ulinc.stamp $(IR64_DEPS)
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_UL_CFLAGS))
$(H64)/game/ulfe/%.raw.o: $(H64)/ulsrc/%.c $(GEN)/le/PRinternal/controller.h $(H64)/ulinc.stamp $(IR64_DEPS) \
		Makefile port64.mk
	@mkdir -p $(@D)
	$(call cc64ir,-I$(GEN)/le $(HL64_UL_CFLAGS))
$(H64)/game/ulfe/ul_pfsallocatefile.raw.o: $(GEN)/le/pfsallocatefile.c $(GEN)/le/PRinternal/controller.h \
		$(H64)/ulinc.stamp $(IR64_DEPS)
	@mkdir -p $(@D)
	$(call cc64ir,-I$(GEN)/le $(HL64_UL_CFLAGS))
$(H64)/game/plat/%.raw.o: src/platform/%.c src/platform/plat.h src/platform/plat_host.h src/platform/port_dma.h \
		src/audio/port_audio.h include/port_n64ptr.h $(H64)/ulinc.stamp $(IR64_DEPS) Makefile port64.mk
	@mkdir -p $(@D)
	$(call cc64ir,$(HL64_PLAT_CFLAGS))

# pin the object's N64 data (tools/coffpin.py: works on the COFF object)
$(H64)/%.o $(H64)/%.rec: $(H64)/%.raw.o $(H64)/addrs.txt tools/coffpin.py
	$(PYTHON) tools/coffpin.py $(H64)/addrs.txt $< $(H64)/$*.o $(H64)/$*.rec

$(H64)/host/%.o: src/platform/%.c src/platform/plat_host.h src/platform/port_dma.h src/rdram.h \
		src/audio/port_audio.h Makefile port64.mk
	@mkdir -p $(@D)
	$(CC64) $(HL64_HOST_CFLAGS) $(HL64_HOST_WARN) -c -o $@ $<
$(H64)/host/audio/%.o: src/audio/%.c src/audio/aspmain.h src/audio/port_audio.h src/platform/plat_host.h src/rdram.h \
		Makefile port64.mk
	@mkdir -p $(@D)
	$(CC64) $(HL64_HOST_CFLAGS) $(HL64_HOST_WARN) -Wno-unused-parameter -c -o $@ $<
$(H64)/host/%.o: src/%.c src/rdram.h Makefile port64.mk
	@mkdir -p $(@D)
	$(CC64) $(HL64_HOST_CFLAGS) $(HL64_HOST_WARN) -c -o $@ $<
$(H64)/host/load/%.o: src/load/%.c src/rdram.h src/platform/port_dma.h $(wildcard src/load/*.h) Makefile port64.mk
	@mkdir -p $(@D)
	$(CC64) $(HL64_HOST_CFLAGS) $(HL64_HOST_WARN) -Isrc/load -c -o $@ $<
$(H64)/host/swaptab.o: $(GEN)/swaptab.c src/load/port_load.h
	@mkdir -p $(@D)
	$(CC64) $(HL64_HOST_CFLAGS) -c -o $@ $<

# the N64 data symbols the objects use, as absolute symbols in a COFF object
# (tools/rdramobj.py), and a trap for every function nothing defines
$(H64)/abs_syms.o $(H64)/stubs.c &: $(HL64_GAME_OBJS) $(HL64_HOST_OBJS) tools/gensyms.py tools/rdramobj.py
	cd $(ROOT) && NM=$(NM64) $(PYTHON) port/tools/gensyms.py link64 port/$(H64) \
		$(addprefix port/,$(HL64_GAME_OBJS) $(HL64_HOST_OBJS))
$(H64)/copytab.c: $(HL64_RECS) $(H64)/addrs.txt tools/gensyms.py
	$(PYTHON) tools/gensyms.py copytab64 $@ $(H64)/addrs.txt $(HL64_RECS)
# every pointer slot of the pinned objects: the NON_MATCHING ELF's value
# (N64_DPTR initialisers are 0 natively); read from the ELFs here, from
# inputs/nmptrs.txt in the MSVC build
$(H64)/ptrtab.c: $(HL64_RECS) $(NM_ELFS) tools/gensyms.py
	cd $(ROOT) && $(PYTHON) port/tools/gensyms.py ptrtab port/$@ $(addprefix port/,$(HL64_RECS))
$(H64)/stubs.o: $(H64)/stubs.c
	$(CC64) -O2 -w -c -o $@ $<
$(H64)/copytab.o: $(H64)/copytab.c src/rdram.h
	$(CC64) $(HL64_HOST_CFLAGS) -Isrc -c -o $@ $<
$(H64)/ptrtab.o: $(H64)/ptrtab.c src/rdram.h
	$(CC64) $(HL64_HOST_CFLAGS) -Isrc -c -o $@ $<

HL64_LINK_OBJS := $(HL64_GAME_OBJS) $(HL64_HOST_OBJS) $(H64)/stubs.o $(H64)/copytab.o $(H64)/ptrtab.o $(H64)/abs_syms.o
$(H64)/align.ok: $(HL64_GAME_OBJS) tools/align64.py
	$(PYTHON) tools/align64.py x86_64-w64-mingw32-objdump $(HL64_GAME_OBJS) > $(H64)/align.txt || \
		{ cat $(H64)/align.txt; exit 1; }
	@tail -1 $(H64)/align.txt
	@touch $@
$(H64)/bc_headless.exe: $(HL64_LINK_OBJS) $(H64)/align.ok
	$(LD64) -o $@ $(HL64_LINK_OBJS) $(LDFLAGS64) -Wl,-Map=$(H64)/bc_headless.map
	$(NM64) -n $@ > $(H64)/bc_headless.syms
	@head -3 $(H64)/link_report.txt

headless64: $(H64)/bc_headless.exe

# the pointer-width audit (tools/ptrcheck64.py: host-width pointers in N64
# records and pinned globals, casts to host-width pointer pointers, signed
# int -> pointer casts) and the record layouts i686 vs x86_64
# (tools/layout64.py); data/ptrcheck64_allow.txt lists what is host-only.
# ptrcheck64 also reports N64 pointers passed through varargs or to a function
# without a prototype (VARG / KNR); tools/test_ptrcheck64.py first proves on
# small snippets that it catches them.
P64_ALLOW := data/ptrcheck64_allow.txt
P64_GAME := $(HL_HD:%=$(GAME_SRC)/%.c) $(HL_FE:%=$(FE_SRC)/%.c)
P64_PLAT := $(HL_PLAT:%=src/platform/%.c)
P64_UL := $(HL_UL:%=$(H64)/ulsrc/%.c) $(HL_ULFE:%=$(H64)/ulsrc/%.c)
# the little-endian edits of ultralib (printf's _Printf/_Ldtob, sinf/cosf)
P64_LE := $(GEN)/le/xprintf.c $(GEN)/le/xldtob.c $(GEN)/le/sinf.c $(GEN)/le/cosf.c
ptrcheck64: $(H64)/addrs.txt $(H64)/ulinc.stamp $(GEN)/le/PRinternal/controller.h $(P64_LE)
	@rc=0; \
	$(PYTHON) tools/test_ptrcheck64.py $(CC64) -fms-extensions -std=gnu89 || rc=1; \
	$(PYTHON) tools/ptrcheck64.py $(H64)/addrs.txt $(P64_ALLOW) $(H64)/ptrcheck_le.txt -- \
		$(CC64) $(HL64_UL_CFLAGS) -I$(UL_DIR)/src/gu -w -- $(P64_LE) || rc=1; \
	$(PYTHON) tools/ptrcheck64.py $(H64)/addrs.txt $(P64_ALLOW) $(H64)/ptrcheck_game.txt -- \
		$(CC64) $(HL64_CFLAGS) -w -- $(P64_GAME) || rc=1; \
	$(PYTHON) tools/ptrcheck64.py $(H64)/addrs.txt $(P64_ALLOW) $(H64)/ptrcheck_plat.txt -- \
		$(CC64) $(HL64_PLAT_CFLAGS) -w -- $(P64_PLAT) || rc=1; \
	$(PYTHON) tools/ptrcheck64.py $(H64)/addrs.txt $(P64_ALLOW) $(H64)/ptrcheck_ul.txt -- \
		$(CC64) -I$(GEN)/le $(HL64_UL_CFLAGS) -w -- $(P64_UL) || rc=1; \
	$(PYTHON) tools/layout64.py $(P64_ALLOW) $(H64)/layout_game.txt -- $(CC64) $(HL64_CFLAGS) -- $(P64_GAME) || rc=1; \
	$(PYTHON) tools/layout64.py $(P64_ALLOW) $(H64)/layout_plat.txt -- $(CC64) $(HL64_PLAT_CFLAGS) -- $(P64_PLAT) \
		|| rc=1; \
	$(PYTHON) tools/layout64.py $(P64_ALLOW) $(H64)/layout_ul.txt -- $(CC64) -I$(GEN)/le $(HL64_UL_CFLAGS) -- \
		$(P64_UL) || rc=1; \
	exit $$rc

# the signed-overflow census (tools/overflow_census.py; MSVC has no -fwrapv and
# no signed-overflow sanitizer): a bc_headless built here with -fno-wrapv
# -fsanitize=signed-integer-overflow (build/hl64ubsan, tools/ubsan_minimal.c),
# run over the attract demos and every level; fails if any signed add/multiply
# overflows that the source doesn't wrap explicitly (PORT_WRAP_*,
# include/game/port.h).  OC_ARGS: e.g. --levels 0-9
OC := build/hl64ubsan
overflow-census:
	@mkdir -p $(OC)
	x86_64-w64-mingw32-gcc -O2 -c -o $(OC)/ubsan_minimal.o tools/ubsan_minimal.c
	$(MAKE) H64=$(OC) HL_EXTRA="-fno-wrapv -fsanitize=signed-integer-overflow -fsanitize-minimal-runtime" \
		LDFLAGS64="$(LDFLAGS64) $(OC)/ubsan_minimal.o" $(OC)/bc_headless.exe
	$(PYTHON) tools/overflow_census.py $(OC)/bc_headless.exe '$(ROM_ARG)' data/levels_input.txt $(OC)/runs $(OC_ARGS)

# the clang check: the audit, the alignment check (headless64), then the clang
# exe's per-frame traces against the MSVC exe's: the attract demos and every
# level (tools/tracecmp.py, both on the free-running virtual clock; no
# emulator).  CLANG_CHECK_ARGS: e.g. --levels 0-9 --attract 2000
clang-check: ptrcheck64 headless64
	$(MAKE) msvc MSVC_TARGETS=bc_headless
	$(PYTHON) tools/tracecmp.py $(H64)/bc_headless.exe '$(MSVC_B)/bc_headless.exe' '$(ROM_ARG)' \
		data/levels_input.txt build/clang-check --jobs $(LEVEL_JOBS) $(CLANG_CHECK_ARGS)

.PHONY: headless64 ptrcheck64 overflow-census clang-check
