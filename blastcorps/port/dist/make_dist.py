#!/usr/bin/env python3
"""The player package of the MSVC build (port/CMakeLists.txt, target `dist`).

  make_dist.py --exe-dir DIR --out DIR --version-h version.h --rt64 SRC --dxc DIR --sdl2 DIR --readme README.txt \
      --license LICENSE

writes, in --out:
  BlastCorps-port-V/           bc.exe, bc_packtool.exe, SDL2.dll, dxcompiler.dll, dxil.dll,
                               README.txt, LICENSE.txt (the project's MIT licence, the
                               repository's LICENSE) and THIRD_PARTY_LICENSES.txt
                               (Windows line ends); no game data
  BlastCorps-port-V.zip        that folder
  BlastCorps-port-V-pdb.zip    bc.pdb, bc.map, bc.syms: the symbols that match
                               this bc.exe (crash reports name functions and
                               lines when bc.pdb sits next to bc.exe; a .dmp
                               opens in Visual Studio with it)

THIRD_PARTY_LICENSES.txt: every third-party component compiled into bc.exe or
shipped next to it, with its licence text from the source trees the build
used.
"""
import argparse
import os
import re
import shutil
import sys
import zipfile

MIT = """MIT License

{}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

HEADER = """Third-party software in this package
====================================

bc.exe is built from Blast Decorps, the Blast Corps decompilation project (the game's
decompiled C code, the decompiled N64 SDK libraries it uses, and the code written
for this Windows port; MIT licence: LICENSE.txt). It contains no game data: the game's data, graphics and
sounds are read from the player's own ROM at run time. bc_packtool.exe (the
texture-pack builder) is the port's own code with two of the components below.

The following third-party components are compiled into bc.exe or bc_packtool.exe
or shipped next to them. Their licences follow, each in full.

  Component                       Licence                      Where
  RT64 (N64 renderer)             MIT                          bc.exe
  plume (render hardware layer)   MIT                          bc.exe
  re-spirv                        MIT                          bc.exe
  SPIRV-Headers                   MIT-style (Khronos)          bc.exe
  D3D12 Memory Allocator          MIT                          bc.exe
  Vulkan Memory Allocator         MIT                          bc.exe
  volk                            MIT                          bc.exe
  Vulkan-Headers                  Apache-2.0 or MIT            bc.exe
  Dear ImGui                      MIT                          bc.exe
  ImPlot                          MIT                          bc.exe
  im3d                            MIT                          bc.exe
  HLSL++                          MIT                          bc.exe
  ddspp                           MIT                          bc.exe
  stb                             MIT or public domain         bc.exe, bc_packtool.exe
  xxHash                          BSD 2-Clause                 bc.exe
  Zstandard                       BSD 3-Clause                 bc.exe
  miniz                           MIT                          bc.exe, bc_packtool.exe
  JSON for Modern C++             MIT                          bc.exe
  utf8conv                        MIT                          bc.exe
  Native File Dialog Extended     zlib                         bc.exe
  SDL2 2.26.3                     zlib                         SDL2.dll
  DirectX Shader Compiler         LLVM Release License (NCSA)  dxcompiler.dll
                                  and MIT
  DirectX Shader Compiler (DXIL)  Microsoft Software License   dxil.dll
                                  Terms (redistributable)

bc.exe also contains Microsoft's C/C++ runtime library (linked statically by
Visual Studio's compiler, whose licence permits distributing programs built
with it).
"""


def rule():
    return "\n" + "=" * 80 + "\n"


def component(name, what, *texts):
    out = rule() + name + "\n" + what + "\n" + "-" * 80 + "\n\n"
    for t in texts:
        out += t.replace("\r\n", "\n").rstrip("\n") + "\n\n"
    return out


def read(path):
    if not os.path.isfile(path):
        sys.exit("make_dist: missing licence file %s" % path)
    return open(path, encoding="utf-8", errors="replace").read()


def first(*paths):
    for p in paths:
        if os.path.isfile(p):
            return read(p)
    sys.exit("make_dist: none of %s" % ", ".join(paths))


def licences(rt64, dxc, sdl2):
    c = os.path.join(rt64, "src", "contrib")
    p = os.path.join(c, "plume", "contrib")
    # miniz: the licence comment at the top of miniz.c
    head = read(os.path.join(c, "miniz", "miniz.c")).split("*/", 1)[0]
    m = head[head.index("Copyright"):] if "Copyright" in head else head
    miniz = "\n".join(re.sub(r"^ \* ?", "", l).rstrip() for l in m.split("\n")).strip() + "\n"
    out = HEADER
    out += component("RT64", "https://github.com/rt64/rt64 (commit 43373749, with the port's patches)",
                     read(os.path.join(rt64, "LICENSE")))
    out += component("plume", "https://github.com/renderbag/plume", read(os.path.join(c, "plume", "LICENSE")))
    out += component("re-spirv", "https://github.com/renderbag/re-spirv", read(os.path.join(c, "re-spirv", "LICENSE")))
    out += component("SPIRV-Headers", "https://github.com/KhronosGroup/SPIRV-Headers",
                     read(os.path.join(c, "re-spirv", "external", "SPIRV-Headers", "LICENSE")))
    out += component("D3D12 Memory Allocator", "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator",
                     read(os.path.join(p, "D3D12MemoryAllocator", "LICENSE.txt")))
    out += component("Vulkan Memory Allocator", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator",
                     read(os.path.join(p, "VulkanMemoryAllocator", "LICENSE.txt")))
    out += component("volk", "https://github.com/zeux/volk", read(os.path.join(p, "volk", "LICENSE.md")))
    out += component("Vulkan-Headers", "https://github.com/KhronosGroup/Vulkan-Headers (used under the MIT licence)",
                     read(os.path.join(p, "Vulkan-Headers", "LICENSE.md")),
                     read(os.path.join(p, "Vulkan-Headers", "LICENSES", "MIT.txt")))
    out += component("Dear ImGui", "https://github.com/ocornut/imgui", read(os.path.join(c, "imgui", "LICENSE.txt")))
    out += component("ImPlot", "https://github.com/epezent/implot", read(os.path.join(c, "implot", "LICENSE")))
    out += component("im3d", "https://github.com/john-chapman/im3d", read(os.path.join(c, "im3d", "LICENSE")))
    out += component("HLSL++", "https://github.com/redorav/hlslpp", read(os.path.join(c, "hlslpp", "LICENSE")))
    out += component("ddspp", "https://github.com/redorav/ddspp", read(os.path.join(c, "ddspp", "LICENSE")))
    out += component("stb", "https://github.com/nothings/stb", read(os.path.join(c, "stb", "LICENSE")))
    out += component("xxHash", "https://github.com/Cyan4973/xxHash", read(os.path.join(c, "xxHash", "LICENSE")))
    out += component("Zstandard", "https://github.com/facebook/zstd (used under the BSD licence)",
                     read(os.path.join(c, "zstd", "LICENSE")))
    out += component("miniz", "https://github.com/richgel999/miniz", miniz)
    out += component("JSON for Modern C++", "https://github.com/nlohmann/json (version 3.12.0)",
                     MIT.format("Copyright (c) 2013-2025 Niels Lohmann <https://nlohmann.me>"))
    out += component("utf8conv", "https://github.com/GiovanniDicanio/Utf8Conv",
                     MIT.format("Copyright (C) 2016-2022 Giovanni Dicanio"))
    out += component("Native File Dialog Extended", "https://github.com/btzy/nativefiledialog-extended",
                     read(os.path.join(c, "nativefiledialog-extended", "LICENSE")))
    out += component("SDL2 2.26.3 (SDL2.dll)", "https://www.libsdl.org", read(os.path.join(sdl2, "COPYING.txt")))
    out += component("DirectX Shader Compiler (dxcompiler.dll)",
                     "https://github.com/microsoft/DirectXShaderCompiler (release v1.9.2609)",
                     read(os.path.join(dxc, "LICENSE-LLVM.txt")),
                     first(os.path.join(dxc, "LICENCE-MIT.txt"), os.path.join(dxc, "LICENSE-MIT.txt")))
    out += component("DirectX Shader Compiler: DXIL signing library (dxil.dll)",
                     "Microsoft, redistributable with applications under these terms",
                     read(os.path.join(dxc, "LICENSE-MS.txt")))
    return out


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def zipdir(zpath, root, names):
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            z.write(os.path.join(root, n), n)


def main():
    ap = argparse.ArgumentParser()
    for a in ("--exe-dir", "--out", "--version-h", "--rt64", "--dxc", "--sdl2", "--readme", "--license"):
        ap.add_argument(a, required=True)
    a = ap.parse_args()
    m = re.search(r'BC_VERSION "([^"]*)"', read(a.version_h))
    a.version = m.group(1) if m else "dev"
    name = "BlastCorps-port-" + a.version
    d = os.path.join(a.out, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in ("bc.exe", "bc_packtool.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll"):
        shutil.copy2(os.path.join(a.exe_dir, f), d)
    readme = read(a.readme).replace("@VERSION@", a.version)
    open(os.path.join(d, "README.txt"), "wb").write(crlf(readme))
    open(os.path.join(d, "LICENSE.txt"), "wb").write(crlf(read(a.license)))
    open(os.path.join(d, "THIRD_PARTY_LICENSES.txt"), "wb").write(crlf(licences(a.rt64, a.dxc, a.sdl2)))
    files = sorted(os.listdir(d))
    zipdir(os.path.join(a.out, name + ".zip"), a.out, [name + "/" + f for f in files])
    syms = [f for f in ("bc.pdb", "bc.map", "bc.syms") if os.path.isfile(os.path.join(a.exe_dir, f))]
    zipdir(os.path.join(a.out, name + "-pdb.zip"), a.exe_dir, syms)
    for f in files:
        print("%10d  %s/%s" % (os.path.getsize(os.path.join(d, f)), name, f))
    for z in (name + ".zip", name + "-pdb.zip"):
        print("%10d  %s" % (os.path.getsize(os.path.join(a.out, z)), z))


if __name__ == "__main__":
    main()
