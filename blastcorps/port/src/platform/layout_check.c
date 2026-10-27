/* Record layouts the native build depends on, checked at compile time (every
 * compiler: MSVC x64, the clang check's x86_64) and once at start-up.
 *
 * The GBI's display-list packets and the audio ABI's commands are unions of
 * bit-field structs.  MSVC lays bit-fields out by Microsoft's rules (a field
 * whose type has another size, or a plain member after a bit-field, starts a
 * new storage unit), gcc by the System V ones (-mno-ms-bitfields in the MinGW
 * builds); the port's copies of ultralib's gbi.h and rdb.h write the few
 * packets where that differs with one storage type under MSVC (PORT_BF_*:
 * port_n64ptr.h, data/ultralib_n64ptr.txt).  The sizes and offsets are
 * asserted here; the bit positions (low bits first on all of them) by
 * port_layout_check at start-up (plat_core.c). */
#include "plat.h"
#include <PR/abi.h>
#include <PR/rdb.h>

#define SIZE(T, n) typedef char size_##T[(sizeof(T) == (n)) ? 1 : -1]
#define OFF(T, f, n) typedef char off_##T##_##f[(offsetof(T, f) == (n)) ? 1 : -1]
#ifndef offsetof
#if defined(__GNUC__)
#define offsetof(T, f) __builtin_offsetof(T, f)
#else
#define offsetof(T, f) ((size_t) & ((T *) 0)->f)
#endif
#endif

SIZE(Gwords, 8);
SIZE(Gdma, 8);
SIZE(Gtri, 8);
SIZE(Gline3D, 8);
SIZE(Gpopmtx, 8);
SIZE(Gsegment, 8);
SIZE(GsetothermodeH, 8);
SIZE(GsetothermodeL, 8);
SIZE(Gtexture, 8);
SIZE(Gperspnorm, 8);
SIZE(Gsetimg, 8);
SIZE(Gsetcombine, 8);
SIZE(Gsetcolor, 8);
SIZE(Gfillrect, 8);
SIZE(Gsettile, 8);
SIZE(Gloadtile, 8);
SIZE(Gtexrect, 16);
SIZE(Gfx, 8);
SIZE(Mtx, 64);
SIZE(Vtx, 16);
SIZE(Light, 16);
SIZE(Lights1, 24);
SIZE(Vp, 16);
SIZE(Acmd, 8);
SIZE(Aadpcm, 8);
SIZE(Apolef, 8);
SIZE(Aenvelope, 8);
SIZE(Aclearbuff, 8);
SIZE(Ainterleave, 8);
SIZE(Aloadbuff, 8);
SIZE(Aenvmixer, 8);
SIZE(Amixer, 8);
SIZE(Apan, 8);
SIZE(Aresample, 8);
SIZE(Areverb, 8);
SIZE(Asavebuff, 8);
SIZE(Asegment, 8);
SIZE(Asetbuff, 8);
SIZE(Asetvol, 8);
SIZE(Admemmove, 8);
SIZE(Aloadadpcm, 8);
SIZE(Asetloop, 8);
SIZE(rdbPacket, 4);
OFF(Gdma, addr, 4);
OFF(Gtri, tri, 4);
OFF(Gline3D, line, 4);
OFF(Gtexture, s, 4);
OFF(Gperspnorm, scale, 6);
OFF(Gsetimg, dram, 4);
OFF(Gsetcolor, color, 4);
OFF(rdbPacket, buf, 1);

/* bit positions (low bits first): 0 if right, else the line of the first wrong one */
int port_layout_check(void) {
    Gfx g;
    Acmd a;
    rdbPacket r;
#define BITS(cond)          \
    if (!(cond)) return __LINE__
    g.words.w0 = 0xFA123456u, g.words.w1 = 0x89ABCDEFu;
    BITS(g.setcolor.cmd == 0x56 && g.setcolor.pad == 0x34 && g.setcolor.prim_min_level == 0x12 &&
         g.setcolor.prim_level == 0xFA && g.setcolor.color == 0x89ABCDEFu);
    BITS(g.popmtx.cmd == 0x56 && g.popmtx.pad1 == (int) 0xFFFA1234 && g.popmtx.pad2 == (int) 0xFFABCDEF &&
         g.popmtx.param == 0x89);
    BITS(g.dma.cmd == 0x56 && g.dma.par == 0x34 && g.dma.len == 0xFA12 && g.dma.addr == 0x89ABCDEFu);
    BITS(g.segment.cmd == 0x56 && g.segment.number == (int) 0xFFFFFFFA && g.segment.base == (int) 0xFF89ABCD);
    BITS(g.setimg.cmd == 0x56 && g.setimg.fmt == 4 && g.setimg.siz == 2 && g.setimg.wd == 0xFA1 &&
         g.setimg.dram == 0x89ABCDEFu);
    BITS(g.settile.cmd == 0x56 && g.settile.line == 0x48 && g.settile.tmem == 0x1F4 && g.settile.tile == 7 &&
         g.settile.palette == 0xD && g.settile.maskt == 0xF && g.settile.shiftt == 0xA && g.settile.cs == 0 &&
         g.settile.ms == 1 && g.settile.masks == 9 && g.settile.shifts == 8);
    BITS(g.loadtile.cmd == 0x56 && g.loadtile.sl == 0x234 && g.loadtile.tl == 0xFA1 && g.loadtile.tile == 7 &&
         g.loadtile.sh == 0xBCD && g.loadtile.th == 0x89A);
    BITS(g.fillrect.cmd == 0x56 && g.fillrect.x0 == 0x234 - 0x400 && g.fillrect.pad == 0xEF &&
         g.fillrect.y1frac == -2);
    BITS(g.setcombine.cmd == 0x56 && g.setcombine.muxs0 == 0xFA1234 && g.setcombine.muxs1 == 0x89ABCDEFu);
    a.words.w0 = 0xFA123456u, a.words.w1 = 0x89ABCDEFu;
    BITS(a.adpcm.cmd == 0x56 && a.adpcm.flags == 0x34 && a.adpcm.gain == 0xFA12 && a.adpcm.addr == 0x89ABCDEFu);
    BITS(a.setvol.cmd == 0x56 && a.setvol.flags == 0x34 && a.setvol.vol == 0xFA12 && a.setvol.voltgt == 0xCDEF &&
         a.setvol.volrate == 0x89AB);
    BITS(a.segment.pad2 == 3 && a.segment.number == 0xB && a.segment.base == 0x26AF37);
    r.type = 0x3F, r.length = 1, r.buf[0] = 7;
    BITS(*(u8 *) &r == 0x7F && ((u8 *) &r)[1] == 7);
#undef BITS
    return 0;
}
