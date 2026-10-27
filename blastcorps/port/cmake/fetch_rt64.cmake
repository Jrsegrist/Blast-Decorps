# cmake -DDEST=DIR [-DCOMMIT=SHA -DPATCHES=a.patch;b.patch] -P fetch_rt64.cmake
#
# RT64 (github.com/rt64/rt64, MIT) at COMMIT with its submodules and the
# port's patches (port/rt64/patches) into DEST, for the MSVC build
# (CMakeLists.txt runs this at configure time; any later run with the same
# commit and patches does nothing).  Without COMMIT, the commit and patches
# come from rt64_pin.cmake (tools/msvc_build.sh runs it that way from WSL, on
# the Windows-side folder, so a Windows build driven from WSL needs no git for
# Windows).  Needs git.  The DXC and SDL2 submodules are not fetched: the build
# puts the official releases in their place.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED COMMIT)
    include(${CMAKE_CURRENT_LIST_DIR}/rt64_pin.cmake)
    set(COMMIT ${BC_RT64_COMMIT})
    set(PATCHES)
    foreach(n ${BC_RT64_PATCH_NAMES})
        list(APPEND PATCHES ${CMAKE_CURRENT_LIST_DIR}/../rt64/patches/${n})
    endforeach()
endif()
set(skip src/contrib/dxc src/contrib/mupen64plus-win32-deps)
set(stamp "${COMMIT}")
foreach(p ${PATCHES})
    file(SHA1 ${p} h)
    string(APPEND stamp " ${h}")
endforeach()
if(EXISTS ${DEST}/.bc_stamp)
    file(READ ${DEST}/.bc_stamp old)
    if(old STREQUAL stamp)
        return()
    endif()
endif()
find_package(Git REQUIRED)
# The same tree from any git: core.autocrlf=false (Git for Windows installs with
# true, which would check the text out with CRLF where Linux git writes LF;
# the patches are LF either way: port/.gitattributes), core.symlinks=false
# (the few symlinks, zstd test scripts, become small text files, as Git for
# Windows makes them by default).  core.longpaths: Git for Windows otherwise
# stops at 260 characters, and some submodules' files and nested .git/modules
# paths are long: spirv-cross's test shaders, plume's allocators.  (-c settings
# reach the submodule gits too.)
function(git)
    execute_process(COMMAND ${GIT_EXECUTABLE} -c core.longpaths=true -c core.autocrlf=false
        -c core.symlinks=false ${ARGN} RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "fetch_rt64: git ${ARGN} failed")
    endif()
endfunction()
message(STATUS "RT64: preparing ${DEST} at ${COMMIT} + patches")
if(NOT EXISTS ${DEST}/.git)
    git(clone -q --no-checkout https://github.com/rt64/rt64.git ${DEST})
endif()
execute_process(COMMAND ${GIT_EXECUTABLE} -C ${DEST} cat-file -e ${COMMIT}^{commit} RESULT_VARIABLE have)
if(NOT have EQUAL 0)
    git(-C ${DEST} fetch -q origin)
endif()
git(-C ${DEST} checkout -q -f --detach ${COMMIT})
git(-C ${DEST} clean -q -fdx)
# every submodule (gitlink) but the skipped ones, recursively
execute_process(COMMAND ${GIT_EXECUTABLE} -C ${DEST} ls-files -s OUTPUT_VARIABLE lines OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "\n" ";" lines "${lines}")
set(paths)
foreach(l ${lines})
    if(l MATCHES "^160000 [0-9a-f]+ [0-9]+\t(.*)$")
        if(NOT CMAKE_MATCH_1 IN_LIST skip)
            list(APPEND paths ${CMAKE_MATCH_1})
        endif()
    endif()
endforeach()
git(-C ${DEST} submodule -q sync --recursive)
git(-C ${DEST} submodule update -q --init --recursive -f -- ${paths})
git(-C ${DEST} submodule -q foreach --recursive "git reset -q --hard && git clean -q -fdx")
foreach(p ${PATCHES})
    git(-C ${DEST} apply --whitespace=nowarn ${p})
endforeach()
file(WRITE ${DEST}/.bc_stamp "${stamp}")
