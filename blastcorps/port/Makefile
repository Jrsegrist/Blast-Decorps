# The Windows port's WSL side.  The game itself is built with MSVC
# (CMakeLists.txt: Visual Studio, or `make -C port msvc` from here); this
# Makefile holds what that build and its checks need from the decomp:
#
#   make -C port inputs          port/inputs/: the facts the MSVC build takes from the
#                                NON_MATCHING build (committed; check-inputs)
#   make -C port msvc            the MSVC exes (msvc.mk, tools/msvc_build.sh)
#   make -C port verify / verify-levels / loadcheck / compare / dist
#                                the checks against the MSVC exes (msvc.mk)
#   make -C port clang-check     the clang x86_64 headless check (port64.mk)
#
# Needs the NON_MATCHING ELFs (make VERSION=us.v11 NON_MATCHING=1) for
# addresses, the matching ELFs (build/) for loadcheck's reference, the project
# venv (pyelftools, unicorn), host gcc (typemap.py) and the ROM at run time
# (ROM=..., default ../baserom.us.v11.z64 or ~/blastcorps/baserom.us.v11.z64).

MAKEFLAGS += -r
.SUFFIXES:
ROOT    := ..
VERSION := us.v11
PYTHON  ?= $(shell [ -x $(ROOT)/../.env/bin/python3 ] && echo $(abspath $(ROOT)/../.env/bin/python3) || echo python3)
ROM     ?= $(firstword $(wildcard $(ROOT)/../baserom.$(VERSION).z64 $(HOME)/blastcorps/baserom.$(VERSION).z64))
# the exes are Windows programs: hand them a Windows path (WSL interop)
ROM_ARG  = $(shell wslpath -w $(abspath $(ROM)) 2>/dev/null || echo $(ROM))

all: msvc

NM_ELFS := $(ROOT)/build_nm/hd_code.$(VERSION).elf $(ROOT)/build_nm/hd_front_end.$(VERSION).elf
GAME_SRC := $(ROOT)/src.$(VERSION)/hd_code
FE_SRC := $(ROOT)/src.$(VERSION)/hd_front_end
UL_DIR := $(ROOT)/../lib/ultralib

# ---------------------------------------------------------------------------
# What the game is made of (the same lists as CMakeLists.txt): every hd_code
# and front-end game file, the ultralib objects compiled natively and the
# platform files that see the game's <ultra64.h>
ALL_GAME := $(sort $(basename $(notdir $(filter-out $(GAME_SRC)/ul_%,$(wildcard $(GAME_SRC)/*.c)))))
# hd_code files that are only SDK hand-written asm (GLOBAL_ASM: empty natively)
# or SDK C poking RCP registers; the platform layer replaces them
HL_HD_SDK := 8FD90 906E0 91820 91F50 91F80 92EA0 95060 96300 96F70 96FF0 971D0 971F0 972A0 \
	97B70 98330 9DB70 9DB80 9E6F0 A0A30
HL_HD := $(filter-out $(HL_HD_SDK),$(ALL_GAME))
HL_FE := $(sort $(basename $(notdir $(filter-out $(FE_SRC)/ul_%,$(wildcard $(FE_SRC)/*.c)))))
# ultralib objects compiled natively (gu, libm, libc's printf, libaudio).  The
# os/io/si/vi/pi/sp objects are replaced by the platform layer.
HL_UL := ul_lookat ul_lookatref ul_mtxcatf ul_mtxcatl ul_mtxutil ul_normalize ul_ortho \
	ul_perspective ul_rotate ul_scale ul_translate ul_sins ul_coss \
	ul_sprintf ul_xlitob ul_ldiv \
	ul_auxbus ul_bnkf ul_cents2ratio ul_copy ul_cseq ul_cspgetstate ul_cspgettempo ul_csplayer \
	ul_cspplay ul_cspsetbank ul_cspsetseq ul_cspsettempo ul_cspsetvol ul_cspstop ul_drvrnew \
	ul_env ul_event ul_filter ul_heapalloc ul_heapinit ul_load ul_mainbus ul_resample ul_reverb \
	ul_save ul_seq ul_seqplayer ul_sl ul_synaddplayer ul_synallocfx ul_synallocvoice \
	ul_synfreevoice ul_synsetfxmix ul_synsetpan ul_synsetpitch ul_synsetpriority ul_synsetvol \
	ul_synstartvoice ul_synstartvoiceparam ul_synstopvoice ul_synthesizer
# ... plus four with little-endian edits (le/, tools/lesrc.py)
HL_UL_LE := ul_xprintf ul_xldtob ul_sinf ul_cosf
# the front end's own SDK objects used natively: 0E7B0 calls __osContDataCrc,
# and its Controller Pak file system runs over the PIF emulation (pif.c)
HL_ULFE := ul_crc ul_contpfs ul_contramread ul_contramwrite ul_pfsallocatefile ul_pfschecker \
	ul_pfsdeletefile ul_pfsfilestate ul_pfsfreeblocks ul_pfsinitpak ul_pfsisplug ul_pfsreadwritefile \
	ul_pfssearchfile
# platform files that see the game's <ultra64.h>
HL_PLAT := os_thread os_time os_hw os_si input plat_core save pif layout_check
# host-side platform files (<windows.h>, the C runtime)
HL_HOST := plat_host headless_main crash
# the load layer (byte order, port_on_dma: src/load/)
HL_LOAD := $(basename $(notdir $(wildcard src/load/*.c)))
# audio: the microcode interpreter and the AI output (src/audio/)
HL_AUDIO := aspmain port_audio
# the callers of osGetTime/osGetCount keep their own functions (no inlining),
# so --clock can give each caller the emulator's values (os_time.c)
HL_CLOCK_CALLERS := 00000 1C460 20460 22EE0 23C20 2C560
# loadcheck's game files (src/loadcheck_main.c)
LC_FILES := 00000 46C20 17A70 53220 7F8B0 5BF40 60F60 5CB60 60D50 32E00 62740 89250 56040 77E20

# ---------------------------------------------------------------------------
# Generated from the decomp, shared by the builds and the checks: build/gen/
#   addrs.txt        every data symbol at an N64 address (NM ELFs)
#   typemap.txt      the declared scalar leaves of every symbol of the data
#                    images (tools/typemap.py: host gcc's DWARF of the game C)
#   typemap_all.txt  the same for every symbol (compare.py's strict mode)
#   swaptab.c        the byte-order table of the data images (tools/swaptab.py)
#   le/              little-endian copies of five SDK sources (tools/lesrc.py)
GEN := build/gen
GAME_C_DEPS := $(wildcard $(GAME_SRC)/*.c $(FE_SRC)/*.c) $(shell find $(ROOT)/include -name '*.h') \
	include/port_ultratypes.h

$(GEN)/addrs.txt: $(NM_ELFS) tools/gensyms.py
	@mkdir -p $(@D)
	cd $(ROOT) && $(PYTHON) port/tools/gensyms.py addrs port/$@

# byte-order schema of the data images: C declarations (DWARF) + the widths
# the code reads them at (data/image_widths.txt, from tools/m64widths.py)
$(GEN)/typemap.txt: $(GEN)/addrs.txt tools/typemap.py $(GAME_C_DEPS)
	cd $(ROOT) && $(PYTHON) port/tools/typemap.py port/$(GEN)/addrs.txt port/$@ > port/$(GEN)/typemap_report.txt
SWAPTAB_DATA := data/image_widths.txt data/image_static.txt data/image_overrides.txt
$(GEN)/swaptab.c: $(GEN)/typemap.txt $(SWAPTAB_DATA) tools/swaptab.py $(NM_ELFS)
	cd $(ROOT) && $(PYTHON) port/tools/swaptab.py port/$(GEN)/typemap.txt port/data/image_widths.txt port/$@ \
		port/$(GEN)/swaptab_report.txt --static port/data/image_static.txt port/$(GEN)/addrs.txt \
		> port/$(GEN)/swaptab_coverage.txt
	@cat $(GEN)/swaptab_coverage.txt

# byte-order schema coverage report
typemap: $(GEN)/swaptab.c
	@cat $(GEN)/typemap_report.txt

# the strict (typed) comparison's layouts: every symbol's declared leaves,
# and with LC_TRACE (tools/m64widths.py output) the traced reads of every asset
strict-data: $(GEN)/typemap_all.txt
	@if [ -n "$(LC_TRACE)" ]; then WIDTHS_NO_DECODERS=1 $(PYTHON) tools/widths.py facts $(GEN)/facts.txt $(LC_TRACE); fi
$(GEN)/typemap_all.txt: $(GEN)/addrs.txt tools/typemap.py $(GAME_C_DEPS)
	cd $(ROOT) && TYPEMAP_ALL=1 $(PYTHON) port/tools/typemap.py port/$(GEN)/addrs.txt port/$@ > /dev/null

# little-endian copies of SDK sources (the edits are described in tools/lesrc.py)
$(GEN)/le/PRinternal/controller.h: $(UL_DIR)/include/PRinternal/controller.h tools/lesrc.py
	@mkdir -p $(@D)
	$(PYTHON) tools/lesrc.py controller $< $@
$(GEN)/le/pfsallocatefile.c: $(UL_DIR)/src/io/pfsallocatefile.c tools/lesrc.py
	@mkdir -p $(@D)
	$(PYTHON) tools/lesrc.py pfsalloc $< $@
$(GEN)/le/xldtob.c: $(UL_DIR)/src/libc/xldtob.c tools/lesrc.py
	@mkdir -p $(@D)
	$(PYTHON) tools/lesrc.py xldtob $< $@
$(GEN)/le/xprintf.c: $(GAME_SRC)/ul_xprintf.c tools/lesrc.py
	@mkdir -p $(@D)
	$(PYTHON) tools/lesrc.py xprintf $< $@
$(GEN)/le/%f.c: $(UL_DIR)/src/gu/%f.c tools/lesrc.py
	@mkdir -p $(@D)
	$(PYTHON) tools/lesrc.py gudouble $< $@

# RSP microcode hashes (the matching build's ELFs)
ucode:
	cd $(ROOT) && $(PYTHON) port/tools/ucode_id.py build

# ---------------------------------------------------------------------------
# port/inputs/: what the MSVC build (CMakeLists.txt) takes from the
# NON_MATCHING build, as text files, so a Windows clone builds without WSL,
# IDO or pyelftools: data addresses (addrs.txt), every ELF symbol's address,
# kind and size (elfsyms.txt), the N64 pointer slots of the C objects' data
# (nmptrs.txt), the declared types (typemap.txt) and the two NM link maps.
# Facts of the decomp's own code and its NM link only: no ROM bytes.  They
# are committed; `make -C port inputs` rewrites them after a change to the
# game code, and `check-inputs` (part of verify) fails if they are stale.
INPUTS := inputs
define make_inputs
	@mkdir -p $(1)
	cd $(ROOT) && $(PYTHON) port/tools/gensyms.py inputs port/$(1)
	cp $(GEN)/typemap.txt $(1)/
	cp $(ROOT)/build_nm/hd_code.$(VERSION).map $(ROOT)/build_nm/hd_front_end.$(VERSION).map $(1)/
endef
inputs: $(GEN)/typemap.txt $(NM_ELFS) tools/gensyms.py
	$(call make_inputs,$(INPUTS))
	@git status --short -- $(INPUTS) | sed 's/^/inputs: /'
# the committed inputs against the code: the NON_MATCHING build is brought up
# to date, the inputs are made again in build/inputs-check and compared
check-inputs:
	$(MAKE) -C $(ROOT) VERSION=$(VERSION) NON_MATCHING=1 > /dev/null
	$(MAKE) $(GEN)/typemap.txt
	rm -rf build/inputs-check
	$(call make_inputs,build/inputs-check)
	@if diff -r -q $(INPUTS) build/inputs-check; then echo "check-inputs: OK"; else \
		echo "check-inputs: FAILED: port/inputs is out of date with the code: run make -C port inputs and commit it"; \
		exit 1; fi

# What can be checked without the ROM (the CI's Linux job, .github/workflows/ci.yml).
# The NON_MATCHING build needs the ROM (splat's asm and the assets come from it),
# so the data addresses come from the committed inputs/addrs.txt instead, in a
# folder of its own (build/norom; -o: its addrs.txt is never remade from the ELFs):
#   - typemap.txt made again from the game C (host gcc's DWARF) must equal
#     inputs/typemap.txt: the part of check-inputs that needs no NM build;
#   - ptrcheck64 (pointer widths, record layouts) and the clang game objects'
#     alignment check (align64.py).  The clang exe isn't linked (its pointer
#     table comes from the NM ELFs) and nothing runs (that needs the ROM);
#   - tools/readme_check.py: the READMEs and the options' comments against the
#     code (bc.ini's keys, bc.exe's and bc_headless's options).
NOROM := build/norom
NOROM_MAKE = $(MAKE) GEN=$(NOROM)/gen H64=$(NOROM)/headless64 -o $(NOROM)/gen/addrs.txt
norom-check:
	@mkdir -p $(NOROM)/gen
	cp $(INPUTS)/addrs.txt $(NOROM)/gen/addrs.txt
	$(NOROM_MAKE) $(NOROM)/gen/typemap.txt
	@if cmp -s $(INPUTS)/typemap.txt $(NOROM)/gen/typemap.txt; then echo "norom-check: typemap.txt OK"; else \
		diff $(INPUTS)/typemap.txt $(NOROM)/gen/typemap.txt | head -20; \
		echo "norom-check: FAILED: inputs/typemap.txt differs from the game C's declarations:" \
			"run make -C port inputs and commit it"; exit 1; fi
	$(NOROM_MAKE) ptrcheck64 $(NOROM)/headless64/align.ok
	$(PYTHON) tools/readme_check.py .
	@echo "norom-check: OK"

# ---------------------------------------------------------------------------
# the version the exes report (log, file properties, dist folder name)
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
GIT_DIRTY := $(shell git diff --quiet HEAD -- . 2>/dev/null || echo -modified)
BC_VERSION := $(GIT_HASH)$(GIT_DIRTY)

# the emulator comparisons (tools/compare.py; the emulator runs are cached in
# CMP_CACHE): see msvc.mk
CMP_CACHE ?= $(HOME)/cmp_cache
DEMO ?= 0
CMP_KIND ?= nm
VERIFY_FRAMES ?= 1500
LEVEL_JOBS ?= 2

FORCE:

include msvc.mk
include port64.mk

clean:
	rm -rf build

.PHONY: all clean typemap ucode strict-data inputs check-inputs norom-check FORCE
.SECONDARY:
