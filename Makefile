# Blast Corps: the whole ROM.  The code segments are built in blastcorps/
# (blastcorps/Makefile); this level splits the baserom and puts it together again.
#
#   make setup                     once: submodules, the Python venv (.env) and
#                                  IDO 5.3 (tools/ido5.3, decompals/ido-static-recomp)
#   make VERSION=us.v11 extract    check the baserom, split it (splat, with the
#                                  yaml tools_port/romsplit.py makes from the ROM)
#                                  and inflate the code segments into blastcorps/
#   make VERSION=us.v11            link the ROM from the split files:
#                                  "build/blastcorps.us.v11.z64: OK"
#   make VERSION=us.v11 CODE=1     the same, with init and the four compressed code
#                                  files from blastcorps/build (our compiler output,
#                                  compressed by tools_port/gzpack.py)
#
# us.v11 splits into asm/ and assets/, another version into asm/<version>/ and
# assets/<version>/ (docs/versions.md).

VERSION := us.v11
REF_VERSION := us.v11
PYTHON := python3
BINUTILS := mips-linux-gnu-
AS := $(BINUTILS)as
LD := $(BINUTILS)ld
OBJCOPY := $(BINUTILS)objcopy
ROMSPLIT := $(PYTHON) blastcorps/tools_port/romsplit.py

ifeq ($(VERSION),$(REF_VERSION))
ASM_DIR := asm
ASSET_DIR := assets
CODE_BUILD := blastcorps/build
else
ASM_DIR := asm/$(VERSION)
ASSET_DIR := assets/$(VERSION)
CODE_BUILD := blastcorps/build/$(VERSION)
endif
OUT_DIR := build
ROM := $(OUT_DIR)/blastcorps.$(VERSION).z64
BASEROM := baserom.$(VERSION).z64
SPLIT_YAML := $(OUT_DIR)/blastcorps.$(VERSION).yaml
LD_SCRIPT := blastcorps.$(VERSION).ld
OBJS := $(patsubst %,$(OUT_DIR)/%.o,$(wildcard $(ASM_DIR)/*.s) $(wildcard $(ASSET_DIR)/*.bin))
# the inflated code segments blastcorps/ builds against
CODE_MEMBERS := hd_code_text hd_code_data hd_front_end_text hd_front_end_data
INFLATED := $(foreach s,init hd_code hd_front_end,blastcorps/$(s).$(VERSION).bin)
# this version's line of roms.sha1
SHA1_LINE = grep -F '  $(ROM)' roms.sha1

# IDO 5.3, statically recompiled: decompals/ido-static-recomp's release, checked
IDO_VERSION := v1.2
IDO_SHA256 := ab5c741561f80913d58c8b074771f23941a3edd312505a8ebed6d1dfeb65e506
IDO_DIR := tools/ido5.3

.DEFAULT_GOAL := all
all: $(ROM)
	@$(SHA1_LINE) | sha1sum --check

setup: setup-ido
	git submodule update --init --recursive
	$(PYTHON) -m venv .env
	.env/bin/pip install -q -r requirements.txt
setup-ido:
	mkdir -p $(IDO_DIR)
	curl -sSfL -o $(IDO_DIR).tar.gz \
		https://github.com/decompals/ido-static-recomp/releases/download/$(IDO_VERSION)/ido-5.3-recomp-linux.tar.gz
	echo "$(IDO_SHA256)  $(IDO_DIR).tar.gz" | sha256sum --check
	tar xzf $(IDO_DIR).tar.gz -C $(IDO_DIR)
	rm $(IDO_DIR).tar.gz

.baserom.$(VERSION).ok: $(BASEROM) roms.sha1
	@$(SHA1_LINE) | sed 's#  $(ROM)#  $(BASEROM)#' | sha1sum --check
	@touch $@

extract: .baserom.$(VERSION).ok
	@mkdir -p $(OUT_DIR)
	$(ROMSPLIT) splat --version $(VERSION) --rom $(BASEROM) -o $(SPLIT_YAML)
	$(PYTHON) tools/splat/split.py $(SPLIT_YAML)
	$(MAKE) VERSION=$(VERSION) decompress

decompress: $(INFLATED)
blastcorps/init.$(VERSION).bin: $(ASSET_DIR)/init.$(VERSION).bin
	cp $< $@
# a segment's text and data members, inflated back to back
blastcorps/%.$(VERSION).bin: $(ASSET_DIR)/%_text.$(VERSION).bin $(ASSET_DIR)/%_data.$(VERSION).bin
	gzip -dc < $(word 1,$^) > $@
	gzip -dc < $(word 2,$^) >> $@

clean:
	rm -rf asm assets $(OUT_DIR) .splat_cache
	rm -f $(INFLATED) blastcorps.*.ld .baserom.*.ok

# ---- recipes ----------------------------------------------------------------
$(ROM:.z64=.elf): $(OBJS) $(LD_SCRIPT)
	@$(LD) -T $(LD_SCRIPT) -Map $(@:.elf=.map) --no-check-sections -o $@
$(ROM): $(ROM:.z64=.elf)
	$(OBJCOPY) -O binary $< $@

$(OUT_DIR)/%.s.o: %.s
	@mkdir -p $(@D)
	$(AS) -EB -mtune=vr4300 -march=vr4300 -mabi=32 -o $@ $<

ifeq ($(CODE),1)
# init and the code files from the build in blastcorps/ instead of the split ROM
CODE_BINS := $(ASSET_DIR)/init.$(VERSION).bin $(foreach m,$(CODE_MEMBERS),$(ASSET_DIR)/$(m).$(VERSION).bin)
code-build:
	$(MAKE) -C blastcorps VERSION=$(VERSION) all gz
$(CODE_BUILD)/init.$(VERSION).bin $(foreach m,$(CODE_MEMBERS),$(CODE_BUILD)/$(m).$(VERSION).gz): code-build
$(OUT_DIR)/$(ASSET_DIR)/init.$(VERSION).bin.o: $(CODE_BUILD)/init.$(VERSION).bin
	@mkdir -p $(@D)
	cp $< $(@:.o=)
	$(LD) -r -b binary -o $@ $(@:.o=)
$(foreach m,$(CODE_MEMBERS),$(OUT_DIR)/$(ASSET_DIR)/$(m).$(VERSION).bin.o): \
		$(OUT_DIR)/$(ASSET_DIR)/%.$(VERSION).bin.o: $(CODE_BUILD)/%.$(VERSION).gz
	@mkdir -p $(@D)
	cp $< $(@:.o=)
	$(LD) -r -b binary -o $@ $(@:.o=)
.PHONY: code-build
endif
build/%.bin.o: %.bin
	@mkdir -p $(@D)
	$(LD) -r -b binary -o $@ $<

.SECONDARY:
.PHONY: all setup setup-ido extract decompress clean
SHELL := /bin/bash -eo pipefail
