# The Windows port built with MSVC (x64): bc_headless.exe and bc.exe.
# See README.md, "Build on Windows with Visual Studio".
#
#   cmake --preset x64-release && cmake --build --preset x64-release
#
# Prerequisites: Visual Studio 2022 or later with "Desktop development with
# C++" (MSVC x64, the Windows SDK, CMake, Ninja), Python 3 (standard library
# only), and the inputs folder from the decomp side (BC_INPUTS below).
#
# How the build works (the same steps as port/Makefile and port64.mk, on
# Windows):
#   - the game C (src.us.v11/hd_code, hd_front_end), the ultralib objects and
#     the platform files that see <ultra64.h> are compiled with the game's
#     headers (/X: no C runtime headers) into object libraries;
#   - tools/coffpin.py rewrites those objects: every data object at an N64
#     address keeps its bytes as __native_<name> and its users go to the
#     address itself (an absolute symbol from tools/gensyms.py linkcoff);
#   - generated tables: the copy of the native initialisers (copytab), the
#     pointer slots of N64 data (ptrtab), the byte order of the data images
#     (swaptab), traps for undefined functions (stubs);
#   - the exe is linked at a fixed base (0x40000000) without ASLR, so N64
#     memory at 0x80000000 is reachable RIP-relative and every host address the
#     game stores is below 4 GB.
cmake_minimum_required(VERSION 3.25)
project(BlastCorpsPort LANGUAGES C CXX ASM_MASM RC)

if(NOT MSVC OR NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project builds with MSVC for x64 only (Visual Studio's \"x64\" environment).")
endif()

# ---- locations and inputs ------------------------------------------------------
set(BC_PORT ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(BC_ROOT "${BC_PORT}/.." ABSOLUTE)
set(BC_HD_SRC ${BC_ROOT}/src.us.v11/hd_code)
set(BC_FE_SRC ${BC_ROOT}/src.us.v11/hd_front_end)
set(BC_UL_DIR "${BC_ROOT}/../lib/ultralib" CACHE PATH "ultralib (the lib/ultralib submodule)")
# What the build takes from the NON_MATCHING (IDO) build of the decomp: data
# addresses, symbol kinds, the N64 pointer slots, the C declarations' types and
# the NM link maps.  Committed in port/inputs (made in WSL by
# `make -C port inputs`; `make -C port check-inputs` checks it against the code).
set(BC_INPUTS "${BC_PORT}/inputs" CACHE PATH "the decomp inputs folder (port/inputs, make -C port inputs)")
# bc.exe's third-party code, downloaded at configure time (not in the repo): RT64
# (git, at a pinned commit, with port/rt64/patches), the official DXC and SDL2
# releases.  Shared by every build folder.
set(BC_THIRDPARTY "${BC_PORT}/build/thirdparty" CACHE PATH "downloads (RT64, DXC, SDL2)")
option(BC_GAME "also build bc.exe (downloads RT64, DXC, SDL2)" ON)
# AddressSanitizer (the x64-asan preset): the port's host code (load layer,
# platform, audio: everything that uses the C heap) is built /fsanitize=address,
# so a heap overrun, use after free or double free stops with ASan's report.
# The game C isn't instrumented (it has no heap; ASan's redzones around globals
# would cover N64 data that follows them in RDRAM).  See README.md.
option(BC_ASAN "build the host code with AddressSanitizer" OFF)

foreach(f addrs.txt elfsyms.txt nmptrs.txt typemap.txt hd_code.us.v11.map hd_front_end.us.v11.map)
    if(NOT EXISTS "${BC_INPUTS}/${f}")
        message(FATAL_ERROR "BC_INPUTS (${BC_INPUTS}) has no ${f}.\n"
            "port/inputs is part of the repository; if it is missing, make it in WSL: `make -C port inputs` "
            "(it needs the NON_MATCHING build of the decomp).")
    endif()
endforeach()
if(NOT EXISTS "${BC_UL_DIR}/include/PR/ultratypes.h")
    message(FATAL_ERROR "BC_UL_DIR (${BC_UL_DIR}) is not ultralib: run `git submodule update --init lib/ultralib`.")
endif()

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
set(PY "${Python3_EXECUTABLE}")
set(TOOLS ${BC_PORT}/tools)
set(GEN ${CMAKE_BINARY_DIR}/gen)
set(BC_IMAGE_BASE 0x40000000)
file(MAKE_DIRECTORY ${GEN})

# ---- the version (git hash) the exes report ---------------------------------------
set(BC_GIT_VERSION "" CACHE STRING "the version the exes report (default: git's short hash of the sources)")
add_custom_target(bc_version
    COMMAND ${CMAKE_COMMAND} -DOUT=${GEN}/version.h -DSRC=${BC_PORT} -DOVERRIDE=${BC_GIT_VERSION}
        -P ${BC_PORT}/cmake/version.cmake
    BYPRODUCTS ${GEN}/version.h)

# ---- source lists (as port/Makefile) ----------------------------------------------
# hd_code files that are only SDK asm or RCP-register C: the platform replaces them
set(HL_HD_SDK 8FD90 906E0 91820 91F50 91F80 92EA0 95060 96300 96F70 96FF0 971D0 971F0 972A0
    97B70 98330 9DB70 9DB80 9E6F0 A0A30)
file(GLOB _hd RELATIVE ${BC_HD_SRC} CONFIGURE_DEPENDS ${BC_HD_SRC}/*.c)
file(GLOB _fe RELATIVE ${BC_FE_SRC} CONFIGURE_DEPENDS ${BC_FE_SRC}/*.c)
set(HD_SRCS)
foreach(f ${_hd})
    get_filename_component(b ${f} NAME_WE)
    if(NOT b MATCHES "^ul_" AND NOT b IN_LIST HL_HD_SDK)
        list(APPEND HD_SRCS ${BC_HD_SRC}/${f})
    endif()
endforeach()
set(FE_SRCS)
foreach(f ${_fe})
    if(NOT f MATCHES "^ul_")
        list(APPEND FE_SRCS ${BC_FE_SRC}/${f})
    endif()
endforeach()
# the callers of osGetTime/osGetCount keep their own functions (--clock)
set(HL_CLOCK_CALLERS 00000 1C460 20460 22EE0 23C20 2C560)
# ultralib objects compiled natively (gu, libm, printf, libaudio)
set(HL_UL ul_lookat ul_lookatref ul_mtxcatf ul_mtxcatl ul_mtxutil ul_normalize ul_ortho
    ul_perspective ul_rotate ul_scale ul_translate ul_sins ul_coss
    ul_sprintf ul_xlitob ul_ldiv
    ul_auxbus ul_bnkf ul_cents2ratio ul_copy ul_cseq ul_cspgetstate ul_cspgettempo ul_csplayer
    ul_cspplay ul_cspsetbank ul_cspsetseq ul_cspsettempo ul_cspsetvol ul_cspstop ul_drvrnew
    ul_env ul_event ul_filter ul_heapalloc ul_heapinit ul_load ul_mainbus ul_resample ul_reverb
    ul_save ul_seq ul_seqplayer ul_sl ul_synaddplayer ul_synallocfx ul_synallocvoice
    ul_synfreevoice ul_synsetfxmix ul_synsetpan ul_synsetpitch ul_synsetpriority ul_synsetvol
    ul_synstartvoice ul_synstartvoiceparam ul_synstopvoice ul_synthesizer)
# the front end's own SDK objects (Controller Pak file system over pif.c)
set(HL_ULFE ul_crc ul_contpfs ul_contramread ul_contramwrite ul_pfsallocatefile ul_pfschecker
    ul_pfsdeletefile ul_pfsfilestate ul_pfsfreeblocks ul_pfsinitpak ul_pfsisplug ul_pfsreadwritefile
    ul_pfssearchfile)
list(REMOVE_ITEM HL_ULFE ul_pfsallocatefile)   # (from le/pfsallocatefile.c, below)
set(HL_PLAT os_thread os_time os_hw os_si input plat_core save pif)

# ---- generated sources: ultralib with N64 pointer marks, little-endian edits -----
set(UL_WRAPPERS)
set(UL_GEN)
foreach(n ${HL_UL})
    list(APPEND UL_WRAPPERS ${BC_HD_SRC}/${n}.c)
    list(APPEND UL_GEN ${GEN}/ulsrc/${n}.c)
endforeach()
set(ULFE_GEN)
foreach(n ${HL_ULFE})
    list(APPEND UL_WRAPPERS ${BC_FE_SRC}/${n}.c)
    list(APPEND ULFE_GEN ${GEN}/ulsrc/${n}.c)
endforeach()
string(REPLACE ";" "\n" _w "${UL_WRAPPERS}")
file(WRITE ${GEN}/ul_wrappers.txt "${_w}\n")
add_custom_command(OUTPUT ${GEN}/ulinc.stamp ${UL_GEN} ${ULFE_GEN}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${GEN}/ulinc ${GEN}/ulsrc
    COMMAND ${PY} ${TOOLS}/ulhdr64.py ${BC_UL_DIR} ${BC_PORT}/data/ultralib_n64ptr.txt ${GEN}/ulinc
    COMMAND ${PY} ${TOOLS}/ulsrc64.py ${BC_UL_DIR} ${GEN}/ulinc ${GEN}/ulsrc @${GEN}/ul_wrappers.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/ulinc.stamp
    DEPENDS ${TOOLS}/ulhdr64.py ${TOOLS}/ulsrc64.py ${BC_PORT}/data/ultralib_n64ptr.txt ${UL_WRAPPERS}
    COMMENT "ultralib with N64 pointer marks (ulhdr64.py, ulsrc64.py)")
function(lesrc kind in out)
    add_custom_command(OUTPUT ${out} COMMAND ${PY} ${TOOLS}/lesrc.py ${kind} ${in} ${out}
        DEPENDS ${in} ${TOOLS}/lesrc.py COMMENT "little-endian copy: ${out}")
endfunction()
lesrc(xldtob ${BC_UL_DIR}/src/libc/xldtob.c ${GEN}/le/xldtob.c)
lesrc(xprintf ${BC_HD_SRC}/ul_xprintf.c ${GEN}/le/xprintf.c)
lesrc(gudouble ${BC_UL_DIR}/src/gu/sinf.c ${GEN}/le/sinf.c)
lesrc(gudouble ${BC_UL_DIR}/src/gu/cosf.c ${GEN}/le/cosf.c)
lesrc(controller ${BC_UL_DIR}/include/PRinternal/controller.h ${GEN}/le/PRinternal/controller.h)
lesrc(pfsalloc ${BC_UL_DIR}/src/io/pfsallocatefile.c ${GEN}/le/pfsallocatefile.c)

# ---- compiler flags -------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# a PDB for everything (crash reports, the debugger)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")
# the game's optimisation flags are set per target (BC_OPT, /Ob1); RT64 keeps
# CMake's defaults (bc.exe section)
set(BC_C_FLAGS_RELEASE_DEFAULT "${CMAKE_C_FLAGS_RELEASE}")
set(BC_C_FLAGS_DEBUG_DEFAULT "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "/O[12xd]|/Ob[0-3]|/RTC1|/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REGEX REPLACE "/O[12xd]|/Ob[0-3]|/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(BC_OPT $<IF:$<CONFIG:Debug>,/Od,/O2>)

# Game code (and the platform files that see <ultra64.h>): N64 layouts and the
# game's own headers.
#   /X                    no C runtime headers (as -nostdinc)
#   /Gw /Gy               one section per variable and function (coffpin.py)
#   /GS-                  no stack cookies
#   /Ob1 (game files)     only `inline` functions are inlined: --sync and --clock
#                         key the emulator's values on the calling game function
#   /GH (game files)      a _pexit call (empty, entry_x64.asm) before every return:
#                         keeps MSVC from turning calls in tail position into jumps,
#                         so a return address names its real caller (as gcc's
#                         -fno-optimize-sibling-calls)
#   /FUNCTIONPADMIN (link): free bytes before every function (and x64 code always
#                         starts a function with an instruction of 2+ bytes), where plat_host.c
#                         patches in --sync's function-entry switch points and
#                         --calls (the role of -finstrument-functions)
#   no -fwrapv, -fno-strict-aliasing, -malign-double, -mno-ms-bitfields: MSVC does
#   no type-based alias analysis, aligns 8-byte members naturally, and the GBI's
#   bitfields are written with one storage type (checked: layout_check.c)
#   the SDK headers: ultralib's, the copies with the N64 pointer marks (ulhdr64.py),
#   with the MSVC <stdarg.h> (include/msvc)
set(GAME_INC ${BC_ROOT} ${BC_ROOT}/include ${BC_PORT}/include/msvc ${GEN}/ulinc/include ${GEN}/ulinc/include/PR)
set(GAME_DEFS _LANGUAGE_C _FINALROM NON_MATCHING PORT_HOST _MIPS_SZLONG=32 _MIPS_SIM=1
    BUILD_VERSION=VERSION_I "BUILD_VERSION_STRING=\"2.0I\"")
# (/U_DEBUG: the debug C runtime defines _DEBUG, which turns on ultralib's debug code)
set(GAME_OPTS /X /std:c17 /W1 /wd4311 /wd4312 /wd4068 /GS- /Gw /Gy /U_DEBUG ${BC_OPT}
    /FI${BC_PORT}/include/port_ultratypes.h)
set(UL_INC ${GEN}/ulinc ${BC_UL_DIR} ${GEN}/ulinc/include ${BC_PORT}/include/msvc
    ${GEN}/ulinc/include/compiler/modern_gcc ${GEN}/ulinc/include/PR ${GEN}/ulinc/src/libc)
# (_SIZE_T_DEF: MSVC predefines size_t, which ultralib's headers would redefine
# as a 32-bit type)
set(UL_DEFS _MIPS_SZLONG=32 BUILD_VERSION=VERSION_I "BUILD_VERSION_STRING=\"2.0I\"" NDEBUG _FINALROM F3DEX_GBI
    _LANGUAGE_C _SIZE_T_DEF)
set(UL_OPTS /X /std:c17 /w /GS- /Gw /Gy /Oi- /U_DEBUG ${BC_OPT} /FI${BC_PORT}/include/port_n64ptr.h
    /FI${BC_PORT}/include/msvc/ul_msvc.h)

function(game_lib name)
    cmake_parse_arguments(A "" "" "SOURCES;INC;DEFS;OPTS;DEPS" ${ARGN})
    add_library(${name} OBJECT ${A_SOURCES})
    target_include_directories(${name} PRIVATE ${A_INC})
    target_compile_definitions(${name} PRIVATE ${A_DEFS})
    target_compile_options(${name} PRIVATE ${A_OPTS})
    if(A_DEPS)
        add_dependencies(${name} ${A_DEPS})
    endif()
endfunction()

add_custom_target(bc_ulgen DEPENDS ${GEN}/ulinc.stamp ${GEN}/le/PRinternal/controller.h)
game_lib(bc_hd SOURCES ${HD_SRCS} INC ${GAME_INC} DEFS ${GAME_DEFS}
    OPTS ${GAME_OPTS} $<$<NOT:$<CONFIG:Debug>>:/Ob1> /GH DEPS bc_ulgen)
game_lib(bc_fe SOURCES ${FE_SRCS} INC ${GAME_INC} DEFS ${GAME_DEFS}
    OPTS ${GAME_OPTS} $<$<NOT:$<CONFIG:Debug>>:/Ob1> /GH DEPS bc_ulgen)
foreach(n ${HL_CLOCK_CALLERS})
    set_source_files_properties(${BC_HD_SRC}/${n}.c PROPERTIES COMPILE_OPTIONS /Ob0)
endforeach()
game_lib(bc_ul SOURCES ${UL_GEN} ${GEN}/le/xldtob.c ${GEN}/le/xprintf.c ${GEN}/le/sinf.c ${GEN}/le/cosf.c
    INC ${UL_INC} ${BC_UL_DIR}/src/gu DEFS ${UL_DEFS} OPTS ${UL_OPTS} DEPS bc_ulgen)
game_lib(bc_ulfe SOURCES ${ULFE_GEN} ${GEN}/le/pfsallocatefile.c
    INC ${GEN}/le ${UL_INC} DEFS ${UL_DEFS} OPTS ${UL_OPTS} DEPS bc_ulgen)
set(PLAT_SRCS)
foreach(n ${HL_PLAT})
    list(APPEND PLAT_SRCS ${BC_PORT}/src/platform/${n}.c)
endforeach()
game_lib(bc_plat SOURCES ${PLAT_SRCS} ${BC_PORT}/src/platform/layout_check.c
    INC ${GAME_INC} ${BC_PORT}/src/platform DEFS ${GAME_DEFS} OPTS ${GAME_OPTS} DEPS bc_ulgen)

# loadcheck (the load layer against the original code, `make -C port loadcheck-msvc`):
# its game files on their own, without the platform's hooks
set(LC_FILES 00000 46C20 17A70 53220 7F8B0 5BF40 60F60 5CB60 60D50 32E00 62740 89250 56040 77E20)
set(LC_SRCS)
foreach(n ${LC_FILES})
    list(APPEND LC_SRCS ${BC_HD_SRC}/${n}.c)
endforeach()
game_lib(bc_lc SOURCES ${LC_SRCS} INC ${GAME_INC} DEFS ${GAME_DEFS} OPTS ${GAME_OPTS} DEPS bc_ulgen)

# ---- pinning: coffpin.py over each object library's objects -----------------------
# ${lib}_PINNED / ${lib}_RECS: the pinned objects and their records
set(PIN ${CMAKE_BINARY_DIR}/pin/$<CONFIG>)
function(pin_lib lib)
    get_target_property(srcs ${lib} SOURCES)
    set(objs)
    set(recs)
    foreach(s ${srcs})
        get_filename_component(stem ${s} NAME_WE)
        list(APPEND objs ${PIN}/${lib}/${stem}.obj)
        list(APPEND recs ${PIN}/${lib}/${stem}.rec)
    endforeach()
    file(GENERATE OUTPUT ${PIN}/${lib}.objs.txt CONTENT "$<JOIN:$<TARGET_OBJECTS:${lib}>,\n>\n")
    add_custom_command(OUTPUT ${objs} ${recs}
        COMMAND ${PY} ${TOOLS}/coffpin.py --base ${BC_IMAGE_BASE} --elfsyms ${BC_INPUTS}/elfsyms.txt
            --batch ${BC_INPUTS}/addrs.txt ${PIN}/${lib} ${PIN}/${lib}.objs.txt
        DEPENDS ${lib} $<TARGET_OBJECTS:${lib}> ${TOOLS}/coffpin.py ${BC_INPUTS}/addrs.txt
            ${BC_INPUTS}/elfsyms.txt
        COMMENT "pinning ${lib}'s N64 data (coffpin.py)" VERBATIM)
    set(${lib}_PINNED ${objs} PARENT_SCOPE)
    set(${lib}_RECS ${recs} PARENT_SCOPE)
endfunction()
foreach(l bc_hd bc_fe bc_ul bc_ulfe bc_plat bc_lc)
    pin_lib(${l})
endforeach()

# ---- host code -------------------------------------------------------------------
set(HOST_INC ${BC_PORT}/src ${BC_PORT}/src/platform ${BC_PORT}/src/load)
# the port's own code builds warning-free at /W4 (the conversion warnings 4244/4267 and the pointer-size casts
# 4311/4312 of N64 addresses aside); third-party headers are SYSTEM includes (/external:W0)
set(BC_WARN /W4 /external:W0)
set(HOST_OPTS ${BC_WARN} /wd4996 /wd4244 /wd4267 /wd4311 /wd4312 /GS- /Gy ${BC_OPT})
set(HOST_DEFS _CRT_SECURE_NO_WARNINGS)
if(BC_ASAN)
    list(APPEND HOST_OPTS /fsanitize=address)
    list(APPEND HOST_DEFS BC_ASAN)
endif()
function(host_lib name)
    add_library(${name} OBJECT ${ARGN})
    target_include_directories(${name} PRIVATE ${HOST_INC} ${GEN})
    target_compile_definitions(${name} PRIVATE ${HOST_DEFS})
    target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:${HOST_OPTS}>)
endfunction()
# the byte-order table of the data images (swaptab.py)
add_custom_command(OUTPUT ${GEN}/swaptab.c
    COMMAND ${PY} ${TOOLS}/swaptab.py ${BC_INPUTS}/typemap.txt ${BC_PORT}/data/image_widths.txt ${GEN}/swaptab.c
        ${GEN}/swaptab_report.txt --static ${BC_PORT}/data/image_static.txt ${BC_INPUTS}/addrs.txt
        --maps ${BC_INPUTS}/hd_code.us.v11.map ${BC_INPUTS}/hd_front_end.us.v11.map
    DEPENDS ${TOOLS}/swaptab.py ${BC_INPUTS}/typemap.txt ${BC_PORT}/data/image_widths.txt
        ${BC_PORT}/data/image_static.txt ${BC_PORT}/data/image_overrides.txt ${BC_INPUTS}/addrs.txt
    COMMENT "byte-order table of the data images (swaptab.py)" VERBATIM)
# RDRAM, the ROM and the load layer (byte order)
host_lib(bc_core ${BC_PORT}/src/rdram.c ${BC_PORT}/src/inflate.c ${BC_PORT}/src/load/swap.c
    ${BC_PORT}/src/load/port_assets.c ${BC_PORT}/src/load/gfx_fix.c ${BC_PORT}/src/load/level_mod.c
    ${BC_PORT}/src/load/level_geo.c ${BC_PORT}/src/load/tex_codec.c ${BC_PORT}/src/load/sha1.c
    ${BC_PORT}/src/load/port_mods.c ${GEN}/swaptab.c)
# the platform's host side: fibers, logging, crash reports, the entry hook, audio
host_lib(bc_host ${BC_PORT}/src/platform/plat_host.c ${BC_PORT}/src/platform/crash.c
    ${BC_PORT}/src/platform/entry_x64.asm ${BC_PORT}/src/audio/aspmain.c ${BC_PORT}/src/audio/port_audio.c)
# (MASM: RT64's zstd enables CMake's generic ASM language, which claims .asm too)
set_source_files_properties(${BC_PORT}/src/platform/entry_x64.asm PROPERTIES LANGUAGE ASM_MASM)
host_lib(bc_headless_main ${BC_PORT}/src/platform/headless_main.c)
if(BC_ASAN)
    # N64 memory as a DLL's only section at 0x80000000 (rdram_asan_dll.c): ASan's
    # shadow memory takes the range at 0x80000000 before the exe could reserve it
    add_library(bc_rdram SHARED ${BC_PORT}/src/rdram_asan_dll.c)
    target_compile_options(bc_rdram PRIVATE /GS- /Zl)
    target_link_options(bc_rdram PRIVATE /NOENTRY /NODEFAULTLIB /BASE:0x7FFF0000 /FIXED /DYNAMICBASE:NO
        /ALIGN:65536 /INCREMENTAL:NO /DEBUG:NONE)
endif()
host_lib(bc_lc_main ${BC_PORT}/src/loadcheck_main.c)

# ---- link-time tables, per exe -----------------------------------------------------
# link_tables(NAME LIBS pinned game libraries.. HOST other object libraries..):
#   NAME_tables (object library: stubs.c, copytab.c, ptrtab.c) and NAME_LINK (the
#   pinned objects and abs_syms.obj, for the exe's sources)
function(link_tables name)
    cmake_parse_arguments(A "" "" "LIBS;HOST" ${ARGN})
    set(lt ${CMAKE_BINARY_DIR}/link/$<CONFIG>/${name})
    set(pinned)
    set(recs)
    foreach(l ${A_LIBS})
        list(APPEND pinned ${${l}_PINNED})
        list(APPEND recs ${${l}_RECS})
    endforeach()
    string(REPLACE ";" "\n" _p "${pinned}")
    file(GENERATE OUTPUT ${lt}/pinned.txt CONTENT "${_p}\n")
    string(REPLACE ";" "\n" _r "${recs}")
    file(GENERATE OUTPUT ${lt}/recs.txt CONTENT "${_r}\n")
    set(hostobjs)
    set(hostdeps)
    foreach(h ${A_HOST})
        string(APPEND hostobjs "$<JOIN:$<TARGET_OBJECTS:${h}>,\n>\n")
        list(APPEND hostdeps ${h} $<TARGET_OBJECTS:${h}>)
    endforeach()
    file(GENERATE OUTPUT ${lt}/host.txt CONTENT "${hostobjs}")
    add_custom_command(OUTPUT ${lt}/abs_syms.obj ${lt}/stubs.c ${lt}/link_report.txt
        COMMAND ${PY} ${TOOLS}/gensyms.py linkcoff ${lt} ${BC_INPUTS}/elfsyms.txt ${BC_IMAGE_BASE}
            --game ${lt}/pinned.txt --host ${lt}/host.txt
        DEPENDS ${pinned} ${hostdeps} ${TOOLS}/gensyms.py ${TOOLS}/rdramobj.py ${TOOLS}/coffpin.py
            ${BC_INPUTS}/elfsyms.txt
        COMMENT "${name}: absolute N64 symbols and stubs (gensyms.py linkcoff)" VERBATIM)
    add_custom_command(OUTPUT ${lt}/copytab.c
        COMMAND ${PY} ${TOOLS}/gensyms.py copytab64 ${lt}/copytab.c ${BC_INPUTS}/addrs.txt @${lt}/recs.txt
        DEPENDS ${recs} ${TOOLS}/gensyms.py COMMENT "${name}: copy table (gensyms.py copytab64)" VERBATIM)
    add_custom_command(OUTPUT ${lt}/ptrtab.c
        COMMAND ${PY} ${TOOLS}/gensyms.py ptrtab-in ${lt}/ptrtab.c ${BC_INPUTS}/nmptrs.txt @${lt}/recs.txt
        DEPENDS ${recs} ${TOOLS}/gensyms.py ${BC_INPUTS}/nmptrs.txt
        COMMENT "${name}: pointer table (gensyms.py ptrtab-in)" VERBATIM)
    # (the tables are compiled as the exe's own sources: an object library of
    # per-configuration generated files confuses the Visual Studio generator)
    set(${name}_LINK ${pinned} ${lt}/abs_syms.obj ${lt}/stubs.c ${lt}/copytab.c ${lt}/ptrtab.c PARENT_SCOPE)
    set(${name}_REPORT ${lt}/link_report.txt PARENT_SCOPE)
endfunction()

# the exes: a fixed base below N64 memory (RIP-relative reach to 0x80000000), no
# relocations, no ASLR (every host address the game stores is below 4 GB), no
# identical-code folding (game functions keep their own addresses), a PDB and a
# map (-> EXE.syms, `nm -n` style: --syms, compare.py)
set(BC_LINK_OPTS /BASE:${BC_IMAGE_BASE} /FIXED /DYNAMICBASE:NO /HIGHENTROPYVA:NO /LARGEADDRESSAWARE
    /INCREMENTAL:NO /DEBUG /OPT:REF /OPT:NOICF /FUNCTIONPADMIN:8 /IGNORE:4281)
function(bc_exe name)
    cmake_parse_arguments(A "" "REPORT" "SOURCES;OPTS;LIBS" ${ARGN})
    add_executable(${name} ${A_SOURCES})
    # (its own C sources: the generated tables)
    target_include_directories(${name} PRIVATE ${HOST_INC})
    target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:C>:/w /GS- ${BC_OPT}>)
    target_link_options(${name} PRIVATE ${BC_LINK_OPTS} /MAP:$<TARGET_FILE_DIR:${name}>/${name}.map ${A_OPTS})
    target_link_libraries(${name} PRIVATE ${A_LIBS})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${PY} ${TOOLS}/mapsyms.py $<TARGET_FILE_DIR:${name}>/${name}.map
            $<TARGET_FILE_DIR:${name}>/${name}.syms
        COMMAND ${CMAKE_COMMAND} -E cat ${A_REPORT}
        VERBATIM)
    if(BC_ASAN)
        # (ASan's run-time DLL, from the compiler's own folder, next to the exe)
        get_filename_component(cldir "${CMAKE_C_COMPILER}" DIRECTORY)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cldir}/clang_rt.asan_dynamic-x86_64.dll
                $<TARGET_FILE_DIR:${name}>
            VERBATIM)
    endif()
endfunction()

# ---- bc_headless.exe ---------------------------------------------------------------
set(HL_LIBS bc_hd bc_fe bc_ul bc_ulfe bc_plat)
link_tables(bc_headless LIBS ${HL_LIBS} HOST bc_core bc_host bc_headless_main)
bc_exe(bc_headless REPORT ${bc_headless_REPORT} OPTS /SUBSYSTEM:CONSOLE LIBS winmm $<$<BOOL:${BC_ASAN}>:bc_rdram>
    SOURCES $<TARGET_OBJECTS:bc_core> $<TARGET_OBJECTS:bc_host> $<TARGET_OBJECTS:bc_headless_main>
        ${bc_headless_LINK})

# ---- bc_loadcheck.exe (loadcheck_main.c: the load layer through the game's loaders) ----
link_tables(bc_loadcheck LIBS bc_lc HOST bc_core bc_lc_main)
bc_exe(bc_loadcheck REPORT ${bc_loadcheck_REPORT} OPTS /SUBSYSTEM:CONSOLE LIBS $<$<BOOL:${BC_ASAN}>:bc_rdram>
    SOURCES $<TARGET_OBJECTS:bc_core> $<TARGET_OBJECTS:bc_lc_main> ${bc_loadcheck_LINK})
set_target_properties(bc_loadcheck PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---- bc.exe: the game in a window (SDL2, RT64) ---------------------------------------
if(BC_GAME)
    # RT64 at the pinned commit with the port's patches (both listed in
    # cmake/rt64_pin.cmake).  A tree already prepared with the same commit and
    # patches (its .bc_stamp; tools/msvc_build.sh prepares it from WSL) is used
    # as it is, without git.
    include(${BC_PORT}/cmake/rt64_pin.cmake)
    set(BC_RT64_PATCHES)
    foreach(n ${BC_RT64_PATCH_NAMES})
        list(APPEND BC_RT64_PATCHES ${BC_PORT}/rt64/patches/${n})
    endforeach()
    set(BC_RT64_SOURCE "${BC_THIRDPARTY}/rt64" CACHE PATH "RT64 sources (fetched with git when missing)")
    execute_process(COMMAND ${CMAKE_COMMAND} -DDEST=${BC_RT64_SOURCE} -DCOMMIT=${BC_RT64_COMMIT}
        "-DPATCHES=${BC_RT64_PATCHES}" -P ${BC_PORT}/cmake/fetch_rt64.cmake RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "RT64 could not be prepared in ${BC_RT64_SOURCE} (needs git; or set BC_GAME=OFF)")
    endif()
    # the official DXC (Microsoft, its licence files are in the zip) and SDL2 (zlib)
    # releases, where RT64 expects its contrib copies
    function(bc_download name url sha256)
        set(zip ${BC_THIRDPARTY}/${name}.zip)
        if(NOT EXISTS ${BC_THIRDPARTY}/${name}/.done)
            file(DOWNLOAD ${url} ${zip} EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS)
            file(REMOVE_RECURSE ${BC_THIRDPARTY}/${name})
            file(ARCHIVE_EXTRACT INPUT ${zip} DESTINATION ${BC_THIRDPARTY}/${name})
            file(TOUCH ${BC_THIRDPARTY}/${name}/.done)
        endif()
    endfunction()
    bc_download(dxc
        https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
        ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1)
    bc_download(sdl2 https://github.com/libsdl-org/SDL/releases/download/release-2.26.3/SDL2-devel-2.26.3-VC.zip
        3b561bfcd7676d42f2b84ebde12d7433d176035ed28335c4fb9e554ef2764ec5)
    set(BC_DXC ${BC_THIRDPARTY}/dxc)
    set(BC_SDL2 ${BC_THIRDPARTY}/sdl2/SDL2-2.26.3)
    set(rt64c ${BC_RT64_SOURCE}/src/contrib)
    file(COPY ${BC_DXC}/bin/x64/dxc.exe ${BC_DXC}/bin/x64/dxcompiler.dll ${BC_DXC}/bin/x64/dxil.dll
        DESTINATION ${rt64c}/dxc/bin/x64)
    file(COPY ${BC_DXC}/lib/x64/dxcompiler.lib DESTINATION ${rt64c}/dxc/lib/x64)
    file(COPY ${BC_DXC}/inc/ DESTINATION ${rt64c}/dxc/inc)
    file(COPY ${BC_SDL2}/include/ DESTINATION ${rt64c}/mupen64plus-win32-deps/SDL2-2.26.3/include)
    file(COPY ${BC_SDL2}/lib/x64/ DESTINATION ${rt64c}/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64)

    # RT64 itself: static, CMake's own optimisation flags
    set(RT64_STATIC ON CACHE BOOL "" FORCE)
    set(RT64_NATIVE_HOST_LAYOUT ON CACHE BOOL "" FORCE)
    set(_c_rel "${CMAKE_C_FLAGS_RELEASE}")
    set(_c_dbg "${CMAKE_C_FLAGS_DEBUG}")
    set(CMAKE_C_FLAGS_RELEASE "${BC_C_FLAGS_RELEASE_DEFAULT}")
    set(CMAKE_C_FLAGS_DEBUG "${BC_C_FLAGS_DEBUG_DEFAULT}")
    # zstd enables the ASM language for its GNU-syntax decoder loop, which it
    # leaves out with MSVC; CMake 4.3 then lacks a compile rule for cl as an
    # assembler (no ASM source is ever compiled)
    set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> /nologo /c <DEFINES> <INCLUDES> <FLAGS> /Fo<OBJECT> <SOURCE>")
    # (binary folder "r": RT64's zstd objects sit deep below it, and object paths must stay under 250 characters)
    add_subdirectory(${BC_RT64_SOURCE} r EXCLUDE_FROM_ALL)
    set(CMAKE_C_FLAGS_RELEASE "${_c_rel}")
    set(CMAKE_C_FLAGS_DEBUG "${_c_dbg}")

    # the exe's icon (tools/make_icon.py) and resources (res/bc.rc: icon, version, manifest)
    add_custom_command(OUTPUT ${GEN}/bc.ico COMMAND ${PY} ${TOOLS}/make_icon.py ${GEN}/bc.ico
        DEPENDS ${TOOLS}/make_icon.py COMMENT "bc.ico (make_icon.py)" VERBATIM)
    set_source_files_properties(${BC_PORT}/res/bc.rc PROPERTIES OBJECT_DEPENDS
        "${GEN}/bc.ico;${GEN}/version.h;${BC_PORT}/res/bc.manifest")
    # the platform's entry with the live (bc.exe) hooks
    host_lib(bc_game_main ${BC_PORT}/src/platform/headless_main.c)
    target_compile_definitions(bc_game_main PRIVATE PORT_LIVE)
    # src/live: SDL2 window and input, the RT64 renderer, bc.ini
    add_library(bc_live OBJECT ${BC_PORT}/src/live/live_rt64.cpp ${BC_PORT}/src/live/live_frametimes.cpp
        ${BC_PORT}/src/live/live_shots.cpp ${BC_PORT}/src/live/live_start.cpp ${BC_PORT}/src/live/live_setup.cpp
        ${BC_PORT}/src/live/live_menu.cpp ${BC_PORT}/src/live/live_input.cpp ${BC_PORT}/src/live/live_interp.cpp
        ${BC_PORT}/src/live/live_orbit.cpp ${BC_PORT}/src/live/orbit_math.cpp ${BC_PORT}/src/live/live_wide.cpp
        ${BC_PORT}/src/live/live_texdump.cpp ${BC_PORT}/src/live/tmem_decode.cpp ${BC_PORT}/src/live/live_throttle.cpp
        ${BC_PORT}/src/live/live_viewer.cpp ${BC_PORT}/src/live/viewer_edit.cpp
        ${BC_PORT}/src/live/viewer_terrain.cpp ${BC_PORT}/src/live/viewer_panels.cpp
        ${BC_PORT}/src/live/viewer_input.cpp ${BC_PORT}/src/live/viewer_script.cpp
        ${BC_PORT}/src/live/viewer_records.cpp ${BC_PORT}/src/live/level_edit.cpp ${BC_PORT}/src/live/terrain.cpp)
    add_dependencies(bc_live bc_version rt64)
    target_compile_features(bc_live PRIVATE cxx_std_17)
    target_compile_definitions(bc_live PRIVATE RT64_NATIVE_HOST_LAYOUT SDL_MAIN_HANDLED HLSL_CPU FFX_GCC
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES NOMINMAX __PRFCHWINTRIN_H _WIN32_WINNT=0x0A00 WINVER=0x0A00
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bc_live PRIVATE /EHsc ${BC_WARN} /wd4244 /wd4267 /wd4996 /GS- /FI${GEN}/version.h)
    target_include_directories(bc_live PRIVATE ${BC_PORT}/src ${BC_PORT}/src/platform)
    target_include_directories(bc_live SYSTEM PRIVATE ${BC_SDL2}/include
        ${BC_RT64_SOURCE}/src ${rt64c} ${rt64c}/plume ${rt64c}/imgui ${rt64c}/hlslpp/include ${rt64c}/dxc/inc
        ${CMAKE_BINARY_DIR}/src ${rt64c}/plume/contrib/volk ${rt64c}/plume/contrib/Vulkan-Headers/include
        ${rt64c}/plume/contrib/VulkanMemoryAllocator/include ${rt64c}/plume/contrib/D3D12MemoryAllocator/include)

    # bc_inputtest.exe: tests of the controller handling (live_input.cpp) with SDL's
    # virtual controllers; no window, ROM or renderer (exit status 0: all pass)
    add_executable(bc_inputtest ${BC_PORT}/src/live/input_test.cpp ${BC_PORT}/src/live/live_input.cpp
        ${BC_PORT}/src/live/live_setup.cpp ${BC_PORT}/src/load/sha1.c)
    target_compile_features(bc_inputtest PRIVATE cxx_std_17)
    target_compile_definitions(bc_inputtest PRIVATE SDL_MAIN_HANDLED NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bc_inputtest PRIVATE /EHsc ${BC_WARN} /wd4244 /wd4267)
    target_include_directories(bc_inputtest PRIVATE ${BC_PORT}/src ${BC_PORT}/src/platform)
    target_include_directories(bc_inputtest SYSTEM PRIVATE ${BC_SDL2}/include)
    target_link_libraries(bc_inputtest PRIVATE ${BC_SDL2}/lib/x64/SDL2.lib comdlg32)
    target_link_options(bc_inputtest PRIVATE /SUBSYSTEM:CONSOLE)

    # bc_cameratest.exe: tests of the orbit camera's math (orbit_math.cpp: limits,
    # recentring, zoom, collision pull-in), of the level viewer's record parsing and
    # camera (viewer_records.cpp) and of the level editor's records, change list and mod
    # files (src/load/level_mod.c, edit_test.cpp), all on synthetic level data; no
    # window, ROM or renderer (exit 0: all pass)
    add_executable(bc_cameratest ${BC_PORT}/src/live/orbit_test.cpp ${BC_PORT}/src/live/orbit_math.cpp
        ${BC_PORT}/src/live/viewer_test.cpp ${BC_PORT}/src/live/viewer_records.cpp
        ${BC_PORT}/src/live/edit_test.cpp ${BC_PORT}/src/live/level_edit.cpp ${BC_PORT}/src/load/level_mod.c
        ${BC_PORT}/src/load/level_geo.c ${BC_PORT}/src/live/terrain.cpp ${BC_PORT}/src/live/terrain_test.cpp
        ${BC_PORT}/src/load/tex_codec.c ${BC_PORT}/src/load/sha1.c ${BC_PORT}/src/inflate.c)
    target_include_directories(bc_cameratest PRIVATE ${BC_PORT}/src)
    target_compile_features(bc_cameratest PRIVATE cxx_std_17)
    target_compile_definitions(bc_cameratest PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bc_cameratest PRIVATE /EHsc ${BC_WARN})
    target_link_options(bc_cameratest PRIVATE /SUBSYSTEM:CONSOLE)

    # bc_packtool.exe: makes an RT64 texture pack from edited textures of bc.exe's
    # texture dump (src/packtool; RT64's copies of stb_image and miniz), shipped in
    # the player package.  bc_texturetest.exe: its tests and the texture dump's TMEM
    # decoder's, on synthetic data (exit 0: all pass)
    foreach(t bc_packtool bc_texturetest)
        if(t STREQUAL bc_packtool)
            add_executable(${t} ${BC_PORT}/src/packtool/packtool_main.cpp ${BC_PORT}/src/packtool/packtool.cpp
                ${rt64c}/miniz/miniz.c)
        else()
            add_executable(${t} ${BC_PORT}/src/packtool/texture_test.cpp ${BC_PORT}/src/packtool/packtool.cpp
                ${BC_PORT}/src/live/tmem_decode.cpp ${rt64c}/miniz/miniz.c)
        endif()
        target_compile_features(${t} PRIVATE cxx_std_17)
        target_compile_definitions(${t} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
        target_compile_options(${t} PRIVATE /O2 $<$<COMPILE_LANGUAGE:CXX>:/EHsc /W4 /external:W0 /utf-8>)
        target_include_directories(${t} SYSTEM PRIVATE ${rt64c})
        target_link_libraries(${t} PRIVATE shell32)
        target_link_options(${t} PRIVATE /SUBSYSTEM:CONSOLE)
    endforeach()

    link_tables(bc LIBS ${HL_LIBS} HOST bc_core bc_host bc_game_main bc_live)
    bc_exe(bc REPORT ${bc_REPORT} OPTS /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
        LIBS rt64 ${BC_SDL2}/lib/x64/SDL2.lib d3d12 dxgi dxguid shcore dwmapi imm32 version setupapi winmm
        SOURCES $<TARGET_OBJECTS:bc_core> $<TARGET_OBJECTS:bc_host> $<TARGET_OBJECTS:bc_game_main>
            $<TARGET_OBJECTS:bc_live> ${BC_PORT}/res/bc.rc ${bc_LINK})
    target_include_directories(bc PRIVATE ${GEN} ${BC_PORT}/res)
    # (RT64 names SDL2 by library name; res/bc.rc has the exe's manifest)
    target_link_directories(bc PRIVATE ${BC_SDL2}/lib/x64)
    target_link_options(bc PRIVATE /MANIFEST:NO)
    add_dependencies(bc bc_version)
    # the run-time DLLs next to the exe
    add_custom_command(TARGET bc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BC_SDL2}/lib/x64/SDL2.dll ${BC_DXC}/bin/x64/dxcompiler.dll
            ${BC_DXC}/bin/x64/dxil.dll $<TARGET_FILE_DIR:bc>
        VERBATIM)

    # the player package: dist/BlastCorps-port-VERSION/ and its zip (bc.exe, the
    # DLLs, README.txt, LICENSE.txt, THIRD_PARTY_LICENSES.txt; no game data), and the matching
    # symbols (bc.pdb ...) in BlastCorps-port-VERSION-pdb.zip (dist/make_dist.py)
    add_custom_target(dist
        COMMAND ${PY} ${BC_PORT}/dist/make_dist.py --exe-dir $<TARGET_FILE_DIR:bc> --out ${CMAKE_BINARY_DIR}/dist
            --version-h ${GEN}/version.h --rt64 ${BC_RT64_SOURCE} --dxc ${BC_DXC} --sdl2 ${BC_SDL2}
            --readme ${BC_PORT}/dist/README.txt --license ${BC_ROOT}/../LICENSE
        DEPENDS bc bc_packtool ${BC_PORT}/dist/make_dist.py ${BC_PORT}/dist/README.txt ${BC_ROOT}/../LICENSE
        COMMENT "the player package (dist/make_dist.py)" VERBATIM)
endif()
