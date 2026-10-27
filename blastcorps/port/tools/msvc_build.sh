#!/bin/bash
# Build the MSVC exes (port/CMakeLists.txt) from WSL, for the make targets in
# port/msvc.mk: the sources are mirrored to a folder on the Windows side (MSVC
# compiling over \\wsl.localhost is slow), configured once and built with
# CMake + Ninja in Visual Studio's x64 developer environment.
#
#   tools/msvc_build.sh DIR CONFIG [TARGET...]
#     DIR      a folder on a Windows drive (a WSL path: /mnt/c/...)
#     CONFIG   Release, Debug or Asan (Release + AddressSanitizer, bc_headless
#              only)  -> DIR/CONFIG
#   environment: MSVC_PYTHON  a Windows python.exe (default: the one CMake finds)
#                MSVC_THIRDPARTY  BC_THIRDPARTY (a Windows path)
#                BC_VERSION   the version the exes report (default: CMake asks git)
#                JOBS         parallel compiles (default 2)
set -e
port=$(cd "$(dirname "$0")/.." && pwd)
root=$(cd "$port/.." && pwd)
dir=$1
cfg=$2
shift 2
case "$dir" in /mnt/[a-z]/*) ;; *) echo "msvc_build: $dir is not on a Windows drive" >&2; exit 1 ;; esac
[ -f "$port/inputs/addrs.txt" ] || { echo "msvc_build: no port/inputs (make -C port inputs)" >&2; exit 1; }
mkdir -p "$dir/src/blastcorps/port" "$dir/src/lib"
# mirror DEST [rsync args...]: files compared by content and a copied one gets the copy's time, not the
# source's (rsync -a would keep it: a file edited while an earlier build ran has an mtime older than the
# objects that build made from its previous version, and ninja would take them as up to date)
mirror() {
  local dest=$1
  shift
  rsync -a --no-times --checksum "$@" "$dest"
}
mirror "$dir/src/blastcorps/" --delete --exclude build --exclude __pycache__ "$root/src.us.v11" "$root/include"
# (port/inputs, the committed inputs folder, comes along)
mirror "$dir/src/blastcorps/port/" --delete --exclude /build --exclude /out --exclude __pycache__ "$port/"
# the rename map: the port's tools classify renamed symbols by their old names (tools_port/names.py)
mkdir -p "$dir/src/blastcorps/tools_port"
mirror "$dir/src/blastcorps/tools_port/" "$root/tools_port/names.py" "$root/tools_port/names.txt"
mirror "$dir/src/lib/ultralib/" -L --delete --exclude .git "$root/../lib/ultralib/"
# the repository's licence (the player package's LICENSE.txt)
mirror "$dir/src/" "$root/../LICENSE"

vswhere="/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[ -x "$vswhere" ] || { echo "msvc_build: no Visual Studio (vswhere.exe not found)" >&2; exit 1; }
vs=$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
     -property installationPath | tr -d '\r')
[ -n "$vs" ] || { echo "msvc_build: no Visual Studio with the x64 C++ tools" >&2; exit 1; }
wdir=$(wslpath -w "$dir")
b=$cfg

# RT64 (bc.exe): prepared here with WSL's git, by the same fetch_rt64.cmake
# that CMake runs at configure time (commit and patches from rt64_pin.cmake),
# so CMake then finds the tree's stamp up to date and needs no git for
# Windows.  The folder: the build's cached BC_RT64_SOURCE, else
# MSVC_THIRDPARTY\rt64, else CMake's default (port/build/thirdparty/rt64).
rt64_dest() {
  local cache="$dir/$b/CMakeCache.txt" w=""
  if [ -f "$cache" ]; then
    grep -q '^BC_GAME:BOOL=OFF' "$cache" && return 0
    w=$(sed -n 's/^BC_RT64_SOURCE:PATH=//p' "$cache" | tr -d '\r')
  fi
  [ -z "$w" ] && [ -n "$MSVC_THIRDPARTY" ] && w="$MSVC_THIRDPARTY\\rt64"
  if [ -n "$w" ]; then wslpath -u "${w//\//\\}"; else echo "$dir/src/blastcorps/port/build/thirdparty/rt64"; fi
}
if [ "$cfg" != Asan ]; then
  rt64=$(rt64_dest)
  if [ -n "$rt64" ]; then
    if command -v cmake > /dev/null && command -v git > /dev/null; then
      mkdir -p "$(dirname "$rt64")"
      cmake -DDEST="$rt64" -P "$dir/src/blastcorps/port/cmake/fetch_rt64.cmake" ||
        { echo "msvc_build: RT64 could not be prepared in $rt64" >&2; exit 1; }
    else
      echo "msvc_build: no cmake or git in WSL; CMake will prepare RT64 (needs git for Windows)" >&2
    fi
  fi
fi
# (BC_INPUTS given each time: build folders configured before port/inputs was
# committed have the old default, port/build/inputs, in their cache)
defs="-DCMAKE_BUILD_TYPE=$cfg -DBC_INPUTS=$wdir\\src\\blastcorps\\port\\inputs"
# Asan: Release with AddressSanitizer, bc_headless only (the x64-asan preset)
[ "$cfg" = Asan ] && defs="-DCMAKE_BUILD_TYPE=Release -DBC_ASAN=ON -DBC_GAME=OFF -DBC_INPUTS=$wdir\\src\\blastcorps\\port\\inputs"
[ -n "$MSVC_PYTHON" ] && defs="$defs -DPython3_EXECUTABLE=$MSVC_PYTHON"
[ -n "$MSVC_THIRDPARTY" ] && defs="$defs -DBC_THIRDPARTY=$MSVC_THIRDPARTY"
[ -n "$BC_VERSION" ] && defs="$defs -DBC_GIT_VERSION=$BC_VERSION"
tgt=""
[ $# -gt 0 ] && tgt="--target $*"
bat="$dir/build.bat"
{
  echo '@echo off'
  echo "call \"$vs\\Common7\\Tools\\VsDevCmd.bat\" -arch=amd64 -host_arch=amd64 -no_logo >nul 2>nul"
  echo "cd /d \"$wdir\""
  echo "if not exist $b\\build.ninja cmake -G Ninja -S src\\blastcorps\\port -B $b $defs || exit /b 1"
  echo "cmake $defs $b >nul || exit /b 1"
  echo "cmake --build $b -j ${JOBS:-2} $tgt"
} | sed 's/$/\r/' > "$bat"
cd "$dir"
cmd.exe /c "$(wslpath -w "$bat")" 2>&1 | grep -v "vswhere.exe' is not recognized\|^operable program or batch file"
exit "${PIPESTATUS[0]}"
