# ---------------------------------------------------------------------------
# The MSVC build (port/CMakeLists.txt) driven from WSL, and the checks that run
# against its exes.  See README.md ("Verification from WSL").  Included by
# Makefile.
#
#   make -C port msvc           build the MSVC exes (tools/msvc_build.sh: a copy of
#                               the sources in MSVC_DIR, CMake + Ninja in Visual
#                               Studio's x64 environment) -> MSVC_DIR/Release
#   make -C port loadcheck      the load layer against the original code (bc_loadcheck)
#   make -C port verify         check-inputs, loadcheck, then bc_headless against the emulator
#   make -C port verify-levels  every level against the emulator
#   make -C port compare DEMO=n one attract demo against the emulator
#   make -C port dist           the player package -> build/dist/
#   make -C port asan           bc_headless with AddressSanitizer (x64-asan preset)
#
# MSVC_DIR: a folder on a Windows drive (default %LOCALAPPDATA%\blastcorps-msvc);
# MSVC_CONFIG: Release (default), Debug or Asan; MSVC_PYTHON: a Windows
# python.exe if CMake can't find one; MSVC_THIRDPARTY: a Windows folder for RT64,
# DXC and SDL2 (default MSVC_DIR\src\blastcorps\port\build\thirdparty; RT64 is
# fetched with the Windows git).
MSVC_DIR ?= $(shell d=$$(cd /mnt/c && cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r'); \
	[ -n "$$d" ] && echo "$$(wslpath "$$d")/blastcorps-msvc")
MSVC_CONFIG ?= Release
MSVC_B = $(MSVC_DIR)/$(MSVC_CONFIG)
MSVC_TARGETS ?=
# the exe the emulator comparisons run (tools/compare.py CMP_EXE; its native
# runs go to ...-nativemsvc-EXEHASH folders; every cache folder name carries
# its ROM's hash, so worktrees sharing CMP_CACHE keep apart: README "The cache
# is shared by worktrees")
CMP_ENV = CMP_EXE='$(MSVC_B)/bc_headless.exe' CMP_TAG=msvc
msvc:
	@[ -n "$(MSVC_DIR)" ] || { echo "msvc: set MSVC_DIR (a folder on a Windows drive)"; exit 1; }
	MSVC_PYTHON='$(MSVC_PYTHON)' MSVC_THIRDPARTY='$(MSVC_THIRDPARTY)' BC_VERSION=$(BC_VERSION) \
		bash tools/msvc_build.sh '$(MSVC_DIR)' $(MSVC_CONFIG) $(MSVC_TARGETS)

# the byte-order layer against the original code: real assets through the
# game's loaders and consumers (src/loadcheck_main.c in bc_loadcheck.exe),
# native vs unicorn (tools/loadref.py)
#   LC_TRACE=trace.txt (tools/m64widths.py output) adds T9: every traced read
#   TB/TC: every level rebuilt from its own triangles (bcfiles geo-rebuild, in
#   build/loadcheck/rebuilt, made from the ROM here: never committed)
LCM := build/loadcheck
loadcheck:
	$(MAKE) msvc MSVC_TARGETS=bc_loadcheck
	@mkdir -p $(LCM)/rebuilt
	@if [ -n "$(LC_TRACE)" ]; then $(PYTHON) tools/widths.py facts $(LCM)/facts.txt $(LC_TRACE); fi
	$(PYTHON) ../tools_port/bcfiles geo-rebuild $(abspath $(ROM)) $(LCM)/rebuilt > $(LCM)/rebuilt.txt
	BC_LC_LEVELS='$(shell wslpath -w $(abspath $(LCM)/rebuilt) 2>/dev/null)' WSLENV=BC_LC_LEVELS \
		'$(MSVC_B)/bc_loadcheck.exe' '$(ROM_ARG)' $$([ -f $(LCM)/facts.txt ] && echo 56789ABCD \
		$$(wslpath -w $(LCM)/facts.txt) || echo 5678ABCD) > $(LCM)/out_native.txt
	cd $(ROOT) && BC_LC_LEVELS_DIR=$(abspath $(LCM)/rebuilt) $(PYTHON) port/tools/loadref.py $(abspath $(ROM)) \
		5678AB > port/$(LCM)/out_ref.txt
	@grep '^T9 total' $(LCM)/out_native.txt || true
	@echo "TB/TC: levels where the rebuilt file answers differently:" \
		$$(grep -cE '^TB [0-9]+\.diff ground ([1-9]|[0-9]+/[0-9]+ probe [1-9])|^TC [0-9]+ ground [0-9]+ diff ([1-9]|[0-9]+ probe [0-9]+ diff [1-9])' \
		$(LCM)/out_native.txt)
	$(PYTHON) tools/geocheck.py $(LCM)/out_native.txt $(LCM)/rebuilt
	$(PYTHON) tools/cmpout.py $(LCM)/out_ref.txt $(LCM)/out_native.txt

# quick regression check: the inputs against the code, loadcheck, then
# bc_headless following a cached emulator run (NM test ROM, rebuilt first; LLE
# audio and visibility tests) of the first VERIFY_FRAMES frames: timeline, RAM
# dumps (lenient and strict), visibility tests and audio tasks against the
# emulator's; fails when a number is worse than in data/verify_expect.txt
# (tools/compare.py verify)
verify:
	$(MAKE) -C $(ROOT) VERSION=$(VERSION) NON_MATCHING=1 nmrom BASEROM=$(abspath $(ROM)) > /dev/null
	$(MAKE) check-inputs
	$(MAKE) msvc MSVC_TARGETS="bc_headless bc_loadcheck"
	$(MAKE) $(GEN)/typemap_all.txt
	$(MAKE) loadcheck
	$(CMP_ENV) $(PYTHON) tools/compare.py verify --frames $(VERIFY_FRAMES) --cache $(CMP_CACHE) $(VERIFY_ARGS)

# every level (data/levels.txt): menus -> new game -> the globe centred on the
# level -> 1900 frames with tools/levels_input.py's driving pattern, in the
# emulator (cached per level in CMP_CACHE/level-nm-LL-ROMHASH, ~3.5 minutes each the
# first time) and bc_headless; timeline and RAM dumps (every 20 frames from
# frame 1000 to 1100, then every 100) against data/verify_levels_expect.txt
# (tools/compare.py verify-levels).  LEVELS=a,b-c for some; LEVEL_JOBS
# emulators at a time.
verify-levels:
	$(MAKE) -C $(ROOT) VERSION=$(VERSION) NON_MATCHING=1 nmrom BASEROM=$(abspath $(ROM)) > /dev/null
	$(MAKE) msvc MSVC_TARGETS=bc_headless
	$(CMP_ENV) $(PYTHON) tools/compare.py verify-levels --cache $(CMP_CACHE) --jobs $(LEVEL_JOBS) \
		$(if $(LEVELS),--levels $(LEVELS)) $(VERIFY_LEVELS_ARGS)

# frame-by-frame comparison with the emulator over attract demo DEMO
# (tools/compare.py; the emulator run is cached in CMP_CACHE)
compare:
	$(MAKE) msvc MSVC_TARGETS=bc_headless
	$(CMP_ENV) $(PYTHON) tools/compare.py run --demo $(DEMO) --cache $(CMP_CACHE) --kind $(CMP_KIND) $(CMP_ARGS)

# the player package from the MSVC build (CMake target `dist`): build/dist/
# BlastCorps-port-<hash>.zip and BlastCorps-port-<hash>-pdb.zip (its symbols)
dist:
	$(MAKE) msvc MSVC_TARGETS=dist
	@mkdir -p build/dist
	cp '$(MSVC_B)/dist/BlastCorps-port-$(BC_VERSION).zip' '$(MSVC_B)/dist/BlastCorps-port-$(BC_VERSION)-pdb.zip' \
		build/dist/
	@ls -l build/dist/BlastCorps-port-$(BC_VERSION).zip build/dist/BlastCorps-port-$(BC_VERSION)-pdb.zip

# bc_headless with AddressSanitizer (CMake BC_ASAN, preset x64-asan): MSVC_DIR/Asan
asan:
	$(MAKE) msvc MSVC_CONFIG=Asan MSVC_TARGETS=bc_headless

# the old names of the MSVC targets
loadcheck-msvc: loadcheck
verify-msvc: verify
verify-levels-msvc: verify-levels
dist-msvc: dist

.PHONY: msvc loadcheck verify verify-levels compare dist asan loadcheck-msvc verify-msvc verify-levels-msvc dist-msvc
