# Blast Corps' three code segments, rebuilt from the C in src.us.v11/ and the
# hand-written assembly splat leaves in asm/, and checked against the ROM's:
#
#   init          the boot code (ROM 0x1000, runs at 0x8021ED00)
#   hd_code       the game, inflated by init to 0x802447C0
#   hd_front_end  the front end, inflated by hd_code to 0x801E7000
#
#   make VERSION=us.v11 extract        split the inflated segments (the top-level
#                                      `make extract` writes them here) into asm/
#   make VERSION=us.v11 [-j8]          build/<segment>.<version>.bin: three ": OK" lines
#   make VERSION=us.v11 gz             the compressed text and data as the ROM holds them
#   make VERSION=us.v11 NON_MATCHING=1 the NON_MATCHING C (functionally equivalent
#                                      rewrites, #ifdef NON_MATCHING) in build_nm/,
#                                      no checksum; `nmrom`: a bootable test ROM
#                                      (tools_port/README.md)
#   make VERSION=us.v11 SHIFT=1        a whole ROM whose code, data and files can
#                                      grow, shrink and move (docs/shiftable.md);
#                                      `shift-test` checks it
#   make VERSION=us.v10 port-version   another version's files from us.v11's
#
# Every version builds the one source tree, src.us.v11 (us.v11 is the reference;
# code that differs is under #if VERSION_US_V10).  us.v11 uses asm/, assets/ and
# build/; another version asm/<version>/, assets/<version>/ and build/<version>/,
# with its own yamls, symbol files and linker scripts (docs/versions.md).

VERSION := us.v11
REF_VERSION := us.v11
NON_MATCHING ?= 0
SHIFT ?= 0
SEGMENTS := init hd_code hd_front_end

SRC_DIR := src.us.v11
VERSION_DEFINE := VERSION_$(shell echo $(VERSION) | tr a-z. A-Z_)
ifeq ($(VERSION),$(REF_VERSION))
ASM_DIR := asm
ASSET_DIR := assets
else
ASM_DIR := asm/$(VERSION)
ASSET_DIR := assets/$(VERSION)
endif
# build/, or build_nm/ (NON_MATCHING=1), or build_shift/ (SHIFT=1)
BUILD_DIR := build$(if $(filter 1,$(NON_MATCHING)),_nm,$(if $(filter 1,$(SHIFT)),_shift))
ifneq ($(VERSION),$(REF_VERSION))
ifneq ($(NON_MATCHING)$(SHIFT),00)
$(error NON_MATCHING=1 and SHIFT=1 are us.v11 only so far (docs/versions.md))
endif
BUILD_DIR := build/$(VERSION)
endif

# ---- tools ------------------------------------------------------------------
TOOLS := ../tools
PYTHON := python3
# IDO 5.3, statically recompiled (decompals/ido-static-recomp; `make setup` at the top)
IDO_DIR := $(TOOLS)/ido5.3
CC := $(IDO_DIR)/cc
ASM_PROCESSOR := $(TOOLS)/asm-processor
SPLAT = $(PYTHON) $(TOOLS)/splat/split.py
BINUTILS := mips-linux-gnu-
AS := $(BINUTILS)as
LD := $(BINUTILS)ld
OBJCOPY := $(BINUTILS)objcopy
# our gzip member writer (tools_port/bcfiles/deflate.py: the ROM's deflate, byte for byte)
GZPACK = $(PYTHON) tools_port/gzpack.py

# ---- what each segment is made of -------------------------------------------
# a segment's objects: splat's asm (asm/<seg>/*.s), the C, the data blobs splat
# cut out (assets/<seg>/*.bin)
seg_objs = $(patsubst %,$(BUILD_DIR)/%.o,$(wildcard $(ASM_DIR)/$(1)/*.s) \
	$(wildcard $(SRC_DIR)/$(1)/*.c) $(wildcard $(ASSET_DIR)/$(1)/*.bin))
$(foreach s,$(SEGMENTS),$(eval OBJS_$(s) := $(call seg_objs,$(s))))
ALL_OBJS := $(foreach s,$(SEGMENTS),$(OBJS_$(s)))
BUILD_SUBDIRS := $(foreach s,$(SEGMENTS),$(addprefix $(BUILD_DIR)/,$(SRC_DIR)/$(s) $(ASM_DIR)/$(s) $(ASSET_DIR)/$(s)))
# the C files with GLOBAL_ASM pragmas go through asm-processor
GLOBAL_ASM_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(shell grep -l GLOBAL_ASM $(SRC_DIR)/*/*.c </dev/null 2>/dev/null))

# the linker scripts (splat's, and the .bss ones of hd_code and the front end)
# and the symbol files, in link order; NON_MATCHING links rewritten copies
# (tools_port/nm_ldscript.py) from $(BUILD_DIR)
ifeq ($(NON_MATCHING),1)
GEN_PREFIX := $(BUILD_DIR)/
endif
LDSCRIPTS_init := $(GEN_PREFIX)init.$(VERSION).ld
LDSCRIPTS_hd_code := $(GEN_PREFIX)hd_code_bss.$(VERSION).ld $(GEN_PREFIX)hd_code.$(VERSION).ld
LDSCRIPTS_hd_front_end := $(GEN_PREFIX)hd_front_end_bss.$(VERSION).ld $(GEN_PREFIX)hd_front_end.$(VERSION).ld
# undefined_syms_hd_code_auto: hd_code's named symbols for the front end's calls
# into it (written by `extract`)
SYMFILES_init := undefined_syms_auto undefined_funcs_auto undefined_syms
SYMFILES_hd_code := $(SYMFILES_init)
SYMFILES_hd_front_end := undefined_syms_auto undefined_funcs_auto undefined_syms_hd_code_auto undefined_syms
$(foreach s,$(SEGMENTS),$(eval SYMS_$(s) := $(foreach f,$(SYMFILES_$(s)),$(GEN_PREFIX)$(f).$(s).$(VERSION).txt)))

# ---- compiler flags ---------------------------------------------------------
# The SDK headers are ultralib's (lib/ultralib, decompals' decompilation of
# libultra), configured as SDK 2.0I; include/n64ptr.h empties the port's
# pointer marks.
UL_DIR := ../lib/ultralib
SDK_DEFS := -D_MIPS_SZLONG=32 -DBUILD_VERSION=VERSION_I -DBUILD_VERSION_STRING=\"2.0I\"
INCLUDE_CFLAGS := -I . -I include -I $(UL_DIR)/include -I $(UL_DIR)/include/PR \
	-I $(UL_DIR)/include/compiler/ido $(SDK_DEFS)
ASFLAGS := -EB -march=vr4300 -mtune=vr4300 -mabi=32 -I include
# -woff 649,838: no warnings about anonymous structs and unions
# IDO's options as the original build used them (G 0: no small-data section)
IDO_FLAGS := -G 0 -Xfullwarn -Xcpluscomm -signed -nostdinc -non_shared -Wab,-r4300_mul
CFLAGS := $(IDO_FLAGS) -D_LANGUAGE_C -D_FINALROM -D$(VERSION_DEFINE)=1 -woff 649,838 $(INCLUDE_CFLAGS)
ifeq ($(NON_MATCHING),1)
CFLAGS += -DNON_MATCHING
endif
OPT_FLAGS := -O1
ISA_FLAGS := -mips2 -o32
OBJCOPYFLAGS := --output-target=binary

# The game files compiled at -O2 in the original build: IDO emits branch-likely
# instructions (beql/bnel/...) only at -O2, and none of the -O1 files have any.
O2_HD_CODE := 56040 5CB60 5FD50 60F60 62740 69BB0 75490 77E20 89250 909C0 93DF0 996B0 9C930 9DB80 926F0 \
	92E50 92F30 95070
$(foreach f,$(O2_HD_CODE),$(BUILD_DIR)/$(SRC_DIR)/hd_code/$(f).c.o): OPT_FLAGS := -O2

# The libultra/libaudio objects (ul_*.c) are one-line wrappers that #include
# ultralib's source, built with ultralib's own flags for SDK versions D-I:
# os/io at -O1, libc/gu/audio at -O3, ll at -mips3.
UL_CFLAGS := -G 0 -nostdinc -Xcpluscomm -non_shared -Wab,-r4300_mul -woff 516,649,838,712 \
	$(SDK_DEFS) -DNDEBUG -D_FINALROM -DF3DEX_GBI -I $(UL_DIR) -I $(UL_DIR)/include \
	-I $(UL_DIR)/include/compiler/ido -I $(UL_DIR)/include/PR
UL_O3_HD_CODE := ul_auxbus ul_bnkf ul_cents2ratio ul_copy ul_cosf ul_coss ul_cseq ul_cspgetstate ul_cspgettempo \
	ul_csplayer ul_cspplay ul_cspsetbank ul_cspsetseq ul_cspsettempo ul_cspsetvol ul_cspstop ul_drvrnew ul_env \
	ul_event ul_filter ul_heapalloc ul_heapinit ul_ldiv ul_load ul_lookat ul_lookatref ul_mainbus ul_mtxcatf \
	ul_mtxcatl ul_mtxutil ul_normalize ul_ortho ul_perspective ul_resample ul_reverb ul_rotate ul_save \
	ul_scale ul_seq ul_seqplayer ul_sinf ul_sins ul_sl ul_string ul_synaddplayer ul_synallocfx ul_synallocvoice \
	ul_synfreevoice ul_synsetfxmix ul_synsetpan ul_synsetpitch ul_synsetpriority ul_synsetvol ul_synstartvoice \
	ul_synstartvoiceparam ul_synstopvoice ul_synthesizer ul_translate
$(foreach s,$(SEGMENTS),$(BUILD_DIR)/$(SRC_DIR)/$(s)/ul_%.c.o): CFLAGS := $(UL_CFLAGS)
$(foreach f,$(UL_O3_HD_CODE),$(BUILD_DIR)/$(SRC_DIR)/hd_code/$(f).c.o): OPT_FLAGS := -O3
$(foreach f,ul_ll ul_llcvt,$(BUILD_DIR)/$(SRC_DIR)/hd_code/$(f).c.o): ISA_FLAGS := -mips3 -32
$(BUILD_DIR)/$(SRC_DIR)/init/ul_ll.c.o: ISA_FLAGS := -mips3 -32

# hd_code / front-end files that own a .rodata subsegment: `extract` moves their
# float, double and jump-table rodata to .late_rodata (tools/late_rodata.py)
LATE_RODATA_hd_code := 00000 1A630 1D990 26570 2C560 2D810 30C70 32E00 34430 37530 39050 405F0 409D0 41930
LATE_RODATA_hd_front_end := 0E7B0 196F0 11530
ifeq ($(VERSION),$(REF_VERSION))
LATE_RODATA := $(PYTHON) $(TOOLS)/late_rodata.py
else
LATE_RODATA := $(PYTHON) tools_port/late_rodata_ver.py $(TOOLS) $(VERSION)
endif

# The four compressed code files: gzip members named <member>.raw with the
# ROM's time stamps (MTIME), hd_code's and the front end's text and data.
GZ_MEMBERS := hd_code_text hd_code_data hd_front_end_text hd_front_end_data
GZ_TIMES.us.v11 := hd_code_text=855919890 hd_code_data=855919891 hd_front_end_text=855919891 \
	hd_front_end_data=855919891
GZ_TIMES.us.v10 := hd_code_text=855502464 hd_code_data=855502465 hd_front_end_text=855502466 \
	hd_front_end_data=855502466
gz_time = $(patsubst $(1)=%,%,$(filter $(1)=%,$(GZ_TIMES.$(VERSION))))
# the expected SHA-1s (sha1sum --check format, one line per version and segment)
SEG_SHA1 := segments.sha1
ROM_SHA1 = $(shell grep -F '  build/blastcorps.$(VERSION).z64' ../roms.sha1 | cut -d' ' -f1)

# ---- targets ----------------------------------------------------------------
.DEFAULT_GOAL := all
ifeq ($(NON_MATCHING),1)
all: $(foreach s,$(SEGMENTS),$(BUILD_DIR)/$(s).$(VERSION).bin)
	@echo "NON_MATCHING build done (no checksum): $(foreach s,$(SEGMENTS),$(BUILD_DIR)/$(s).$(VERSION).elf)"
else ifeq ($(SHIFT),1)
all: shiftrom
else
all: $(foreach s,$(SEGMENTS),sha1-$(s))
endif

$(SEGMENTS): %: $(BUILD_DIR)/%.$(VERSION).bin

$(addprefix sha1-,$(SEGMENTS)): sha1-%: $(BUILD_DIR)/%.$(VERSION).bin
	@grep -F '  $<' $(SEG_SHA1) | sha1sum --check

gz: $(foreach m,$(GZ_MEMBERS),$(BUILD_DIR)/$(m).$(VERSION).gz)

# every C file without GLOBAL_ASM, compiled: needs no ROM (the CI's compiler check)
compile-c: $(filter-out $(GLOBAL_ASM_OBJS),$(patsubst %,$(BUILD_DIR)/%.o,$(wildcard $(SRC_DIR)/*/*.c)))

dirs:
	@mkdir -p $(BUILD_SUBDIRS)

# splat over the inflated segments; the front end gets its own names and hd_code's
# (for its calls into hd_code), hd_code's also as a linker symbol file
FE_SEGSYMS := tools_port/segsyms.py symbol_addrs.hd_code.$(VERSION).txt --exclude 0x801E7000:0x8021ED00 \
	--drop-names symbol_addrs.hd_front_end.$(VERSION).txt
extract:
	$(SPLAT) init.$(VERSION).yaml
	$(SPLAT) hd_code.$(VERSION).yaml
	cp symbol_addrs.hd_front_end.$(VERSION).txt symbol_addrs_auto.hd_front_end.$(VERSION).txt
	$(PYTHON) $(FE_SEGSYMS) --splat >> symbol_addrs_auto.hd_front_end.$(VERSION).txt
	$(PYTHON) $(FE_SEGSYMS) > undefined_syms_hd_code_auto.hd_front_end.$(VERSION).txt
	$(SPLAT) hd_front_end.$(VERSION).yaml
	$(LATE_RODATA) $(addprefix $(ASM_DIR)/nonmatchings/hd_code/,$(LATE_RODATA_hd_code))
	$(LATE_RODATA) $(addprefix $(ASM_DIR)/nonmatchings/hd_front_end/,$(LATE_RODATA_hd_front_end))
	@# (splat writes no symbol file it has nothing for; the links want them all)
	@touch $(foreach s,$(SEGMENTS),$(foreach f,$(SYMFILES_$(s)),$(f).$(s).$(VERSION).txt))

# Another version's yamls, symbol_addrs, undefined_syms and .bss linker scripts,
# generated from us.v11's (tools_port/vermap.py; needs both baseroms in .. and
# the us.v11 matching build's ELFs in build/).  Run it after changing a us.v11
# yaml or symbol file, then `make VERSION=$(VERSION) extract`.
port-version:
ifeq ($(VERSION),$(REF_VERSION))
	$(error port-version makes another version's files from $(REF_VERSION)'s: pass e.g. VERSION=us.v10)
endif
	$(PYTHON) tools_port/vermap.py port-all --ref $(REF_VERSION) --other $(VERSION)
# which functions are identical, moved or changed between us.v11 and VERSION
version-report:
	$(PYTHON) tools_port/vermap.py report -v --ref $(REF_VERSION) --other $(VERSION)

# the headers, preprocessed, as m2c's context (tools/m2c.sh)
ctx.i: ctx.c
	cpp -D_LANGUAGE_C -D_FINALROM $(INCLUDE_CFLAGS) $< > $@

# (build_nm/ and build_shift/ stay: `rm -rf` them by hand)
clean:
	rm -rf asm assets build
	rm -f *_auto.*.txt ctx.i

# ---- recipes ----------------------------------------------------------------
define seg_link
$(BUILD_DIR)/$(1).$(VERSION).elf: $$(OBJS_$(1)) $$(LDSCRIPTS_$(1)) $$(SYMS_$(1))
	@$$(LD) $$(addprefix -T ,$$(LDSCRIPTS_$(1))) -Map $$(@:.elf=.map) $$(addprefix -T ,$$(SYMS_$(1))) \
		--no-check-sections --no-warn-mismatch -o $$@
endef
$(foreach s,$(SEGMENTS),$(eval $(call seg_link,$(s))))

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) $(OBJCOPYFLAGS) $< $@

# text and data as separate files (the ROM's two gzip members per segment)
$(BUILD_DIR)/%_text.$(VERSION).bin: $(BUILD_DIR)/%.$(VERSION).elf
	$(OBJCOPY) -O binary --only-section .$* $< $@
$(BUILD_DIR)/%_data.$(VERSION).bin: $(BUILD_DIR)/%.$(VERSION).elf
	$(OBJCOPY) -O binary --only-section .$*_data $< $@
$(BUILD_DIR)/%.$(VERSION).gz: $(BUILD_DIR)/%.$(VERSION).bin tools_port/gzpack.py
	$(GZPACK) $< $@ --name $*.raw --time $(call gz_time,$*)

ifeq ($(ASM_DIR),asm)
ASM_SRC = $<
ASM_PREP = @:
else
# the sources' pragmas name asm/nonmatchings/...: this version's asm instead
ASM_SRC = $(BUILD_DIR)/$<.asm.c
ASM_PREP = sed 's@GLOBAL_ASM("asm/@GLOBAL_ASM("$(ASM_DIR)/@' $< > $(ASM_SRC)
endif

# C with GLOBAL_ASM: asm-processor puts the asm in after IDO (not when the
# permuter builds a file: PERMUTER=1)
ifndef PERMUTER
$(GLOBAL_ASM_OBJS): $(BUILD_DIR)/%.c.o: %.c | dirs
	$(ASM_PREP)
	$(PYTHON) $(ASM_PROCESSOR)/asm_processor.py $(OPT_FLAGS) $(ASM_SRC) > $(BUILD_DIR)/$<
	$(CC) -c -32 $(CFLAGS) $(OPT_FLAGS) $(ISA_FLAGS) -o $@ $(BUILD_DIR)/$<
	$(PYTHON) $(ASM_PROCESSOR)/asm_processor.py $(OPT_FLAGS) $(ASM_SRC) --post-process $@ \
		--assembler "$(AS) $(ASFLAGS)" --asm-prelude $(ASM_PROCESSOR)/prelude.s
endif
$(BUILD_DIR)/%.c.o: %.c | dirs
	$(CC) -c $(CFLAGS) $(OPT_FLAGS) $(ISA_FLAGS) -o $@ $<
$(BUILD_DIR)/%.s.o: %.s | dirs
	$(AS) $(ASFLAGS) -o $@ $<
$(BUILD_DIR)/%.bin.o: %.bin | dirs
	$(LD) -r -b binary -o $@ $<

# ---- NON_MATCHING -------------------------------------------------------------
ifeq ($(NON_MATCHING),1)
# Rewritten functions change size, so the NON_MATCHING link moves hd_code's text
# to NM_TEXT_VRAM (clear of the data segment, which must stay where it is), pins
# the data blobs, and catches any new .rodata/.data/.bss at NM_EXTRA_VRAM; the
# front end the same way (text moved, .hd_front_end_data pinned, its own
# catch-all .nm_extra_fe; its calls into hd_code resolve to the NM hd_code).
# tools_port/eqcheck.py maps by symbol.
NM_TEXT_VRAM ?= 0x80800000
NM_EXTRA_VRAM ?= 0x80A00000
NM_FE_TEXT_VRAM ?= 0x80900000
NM_FE_EXTRA_VRAM ?= 0x80A80000
NM_LDSCRIPT = $(PYTHON) tools_port/nm_ldscript.py --build-dir $(BUILD_DIR)
# the symbol files as PROVIDE()s, so a moved function's real definition wins over
# its original address; the front end's with hd_code's functions at their NM addresses
$(BUILD_DIR)/%.txt: %.txt tools_port/nm_ldscript.py | dirs
	$(NM_LDSCRIPT) $< $@ --provide
$(BUILD_DIR)/%.hd_front_end.$(VERSION).txt: %.hd_front_end.$(VERSION).txt tools_port/nm_ldscript.py \
		$(BUILD_DIR)/hd_code.$(VERSION).elf | dirs
	$(NM_LDSCRIPT) $< $@ --provide --resolve-elf $(BUILD_DIR)/hd_code.$(VERSION).elf
$(BUILD_DIR)/init.$(VERSION).ld $(BUILD_DIR)/hd_code_bss.$(VERSION).ld $(BUILD_DIR)/hd_front_end_bss.$(VERSION).ld: \
		$(BUILD_DIR)/%.ld: %.ld tools_port/nm_ldscript.py | dirs
	$(NM_LDSCRIPT) $< $@
$(BUILD_DIR)/hd_front_end.$(VERSION).ld: hd_front_end.$(VERSION).ld tools_port/nm_ldscript.py | dirs
	$(NM_LDSCRIPT) $< $@ --relocate .hd_front_end=$(NM_FE_TEXT_VRAM) --pin-data .hd_front_end_data \
		--extra-vram $(NM_FE_EXTRA_VRAM) --extra-name .nm_extra_fe \
		--extra-glob '$(BUILD_DIR)/$(SRC_DIR)/hd_front_end/*.o'
# The data tables inside hd_code's .text bins: the matching build keeps the bin
# bytes in the text; NON_MATCHING defines them in C (ADDR:SIZE=FILE: FILE.c's
# .data, nothing else) and links them back at their original addresses
# (nm_ldscript.py --pin-object; nm_symaudit.py compares them with the bins).
NM_PIN_HD_CODE := 0x802ACFD0:0x802=69000 0x802AD880:0x884=69930 0x802C2190:0x1EE0=7F8B0 \
                  0x802C48A0:0x1A0=7FB50 0x802D30D0:0xF50=8DDB0
NM_PIN_HD_CODE_FLAGS = $(foreach p,$(NM_PIN_HD_CODE),--pin-object \
	$(word 1,$(subst =, ,$(p)))=$(SRC_DIR)/hd_code/$(word 2,$(subst =, ,$(p))).c.o)
# (their LMAs fall inside the moved text's: keep them out of the flat .bin)
OBJCOPYFLAGS += -R '.nm_pin_*'
$(BUILD_DIR)/hd_code.$(VERSION).ld: hd_code.$(VERSION).ld tools_port/nm_ldscript.py Makefile | dirs
	$(NM_LDSCRIPT) $< $@ --relocate .hd_code=$(NM_TEXT_VRAM) --pin-data .hd_code_data \
		--extra-vram $(NM_EXTRA_VRAM) --extra-glob '$(BUILD_DIR)/$(SRC_DIR)/hd_code/*.o' \
		$(NM_PIN_HD_CODE_FLAGS)

# Bootable test ROM running the NON_MATCHING hd_code (tools_port/nm_rom.py,
# README "Test ROM"): the same objects relinked with the text in the Expansion
# Pak RAM (NM_ROM_TEXT_VRAM) and .nm_extra right behind it, appended to the
# base ROM with a patched init boot tail that DMAs it in. Needs the matching
# build (build/) and BASEROM.  make VERSION=us.v11 NON_MATCHING=1 nmrom
NM_ROM_TEXT_VRAM ?= 0x80400000
BASEROM ?= ../baserom.$(VERSION).z64
NM_ROM_ELF = $(BUILD_DIR)/hd_code.rom.$(VERSION).elf
NM_ROM_LD_SCRIPT = $(BUILD_DIR)/hd_code.rom.$(VERSION).ld
NM_ROM = $(BUILD_DIR)/blastcorps.nm.$(VERSION).z64
$(NM_ROM_LD_SCRIPT): hd_code.$(VERSION).ld tools_port/nm_ldscript.py Makefile | dirs
	$(NM_LDSCRIPT) $< $@ --relocate .hd_code=$(NM_ROM_TEXT_VRAM) --pin-data .hd_code_data \
		--extra-vram '((ADDR(.hd_code) + SIZEOF(.hd_code) + 0xF) & ~0xF)' \
		--extra-glob '$(BUILD_DIR)/$(SRC_DIR)/hd_code/*.o' $(NM_PIN_HD_CODE_FLAGS)
$(NM_ROM_ELF): $(OBJS_hd_code) $(NM_ROM_LD_SCRIPT) $(BUILD_DIR)/hd_code_bss.$(VERSION).ld $(SYMS_hd_code)
	@$(LD) -T $(BUILD_DIR)/hd_code_bss.$(VERSION).ld -T $(NM_ROM_LD_SCRIPT) -Map $(@:.elf=.map) \
		$(addprefix -T ,$(SYMS_hd_code)) --no-check-sections --no-warn-mismatch -o $@
# the NM front end for the ROM: text in Expansion Pak RAM after the NM hd_code
# (nm_rom.py DMAs both as image A), data pinned, calls into the ROM-linked hd_code
NM_ROM_FE_TEXT_VRAM ?= 0x80500000
NM_ROM_FE_ELF = $(BUILD_DIR)/hd_front_end.rom.$(VERSION).elf
NM_ROM_FE_LD_SCRIPT = $(BUILD_DIR)/hd_front_end.rom.$(VERSION).ld
NM_ROM_FE_SYMS = $(foreach f,$(SYMFILES_hd_front_end),$(BUILD_DIR)/$(f).hd_front_end.rom.$(VERSION).txt)
$(BUILD_DIR)/%.hd_front_end.rom.$(VERSION).txt: %.hd_front_end.$(VERSION).txt tools_port/nm_ldscript.py \
		$(NM_ROM_ELF) | dirs
	$(NM_LDSCRIPT) $< $@ --provide --resolve-elf $(NM_ROM_ELF)
$(NM_ROM_FE_LD_SCRIPT): hd_front_end.$(VERSION).ld tools_port/nm_ldscript.py | dirs
	$(NM_LDSCRIPT) $< $@ --relocate .hd_front_end=$(NM_ROM_FE_TEXT_VRAM) --pin-data .hd_front_end_data \
		--extra-vram '((ADDR(.hd_front_end) + SIZEOF(.hd_front_end) + 0xF) & ~0xF)' --extra-name .nm_extra_fe \
		--extra-glob '$(BUILD_DIR)/$(SRC_DIR)/hd_front_end/*.o'
$(NM_ROM_FE_ELF): $(OBJS_hd_front_end) $(NM_ROM_FE_LD_SCRIPT) $(BUILD_DIR)/hd_front_end_bss.$(VERSION).ld \
		$(NM_ROM_FE_SYMS)
	@$(LD) -T $(BUILD_DIR)/hd_front_end_bss.$(VERSION).ld -T $(NM_ROM_FE_LD_SCRIPT) -Map $(@:.elf=.map) \
		$(addprefix -T ,$(NM_ROM_FE_SYMS)) --no-check-sections --no-warn-mismatch -o $@
nmrom: hd_code hd_front_end $(NM_ROM_ELF) $(NM_ROM_FE_ELF)
	$(PYTHON) tools_port/nm_rom.py --baserom $(BASEROM) --elf $(NM_ROM_ELF) \
		--orig-elf build/hd_code.$(VERSION).elf --init-elf build/init.$(VERSION).elf \
		--front-end hd_front_end.$(VERSION).bin --conventions tools_port/conventions.txt \
		--fe-elf $(NM_ROM_FE_ELF) --fe-orig-elf build/hd_front_end.$(VERSION).elf \
		--fe-src-dir $(SRC_DIR)/hd_front_end --gzpack tools_port/gzpack.py \
		--fe-time $(call gz_time,hd_front_end_text) $(call gz_time,hd_front_end_data) \
		--src-dir $(SRC_DIR)/hd_code --sha1 $(ROM_SHA1) -o $(NM_ROM)
.PHONY: nmrom
endif

# ---- SHIFT ----------------------------------------------------------------------
ifeq ($(SHIFT),1)
ifeq ($(NON_MATCHING),1)
$(error SHIFT=1 builds the matching sources; NON_MATCHING=1 SHIFT=1 is not supported yet)
endif
# The shiftable ROM (docs/shiftable.md): tools_port/shift/shiftbuild.py links
# build_shift's objects with generated linker scripts (every input section a
# unit that can move), patches the raw references the matching layout has
# (tools_port/shift/relocscan.py, overrides.txt), compresses hd_code and the
# front end again and lays out the whole ROM (tools_port/romsplit.py's units)
# with the files of BASEROM. The matching build (build/) is the reference for
# where everything was: it is built first. Unchanged sources give BASEROM.
BASEROM ?= ../baserom.$(VERSION).z64
SHIFT_ROM ?= $(BUILD_DIR)/blastcorps.shift.$(VERSION).z64
SHIFT_PADS ?=
shift-reference:
	$(MAKE) VERSION=$(VERSION) SHIFT=0 NON_MATCHING=0
shiftrom: shift-reference $(ALL_OBJS)
	$(PYTHON) tools_port/shift/shiftbuild.py --ref build --objs $(BUILD_DIR) --out $(BUILD_DIR)/rom \
		--baserom $(BASEROM) $(if $(SHIFT_PADS),--pads $(SHIFT_PADS)) -o $(SHIFT_ROM)
# the shift test: the unshifted ROM must be the original; a ROM with every
# segment padded (tools_port/shift/pads_test.json) must pass shiftcheck.py and
# run the attract demos like it in mupen64plus (RAM compared object by object)
shift-test: shift-reference $(ALL_OBJS)
	$(PYTHON) tools_port/shift/shifttest.py all --baserom $(BASEROM) --ref build --objs $(BUILD_DIR) \
		--work $(BUILD_DIR)/test --sha1 $(ROM_SHA1)
.PHONY: shiftrom shift-test shift-reference
endif

.SECONDARY:
.PHONY: all clean compile-c dirs extract gz port-version version-report $(SEGMENTS) $(addprefix sha1-,$(SEGMENTS))
SHELL := /bin/bash -eo pipefail
