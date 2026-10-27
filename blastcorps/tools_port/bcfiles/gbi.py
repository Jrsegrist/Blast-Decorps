"""Fast3D (F3D) display lists as the game's files hold them.

Commands are 8 bytes (two big-endian words).  In the files, G_SETTIMG's
address word is a texture-table id (tex_resolve_dl replaces it with the
texture's address at load), G_VTX addresses are segmented (segment 8 = the
level's vertices at file + 0xC8, 6 = a model's vertex section, 9 = a placed
object's vertices at + 0x50), and the level's G_DL addresses are offsets
from its display-list block (lvl_relocate_dls adds the block's address).
"""
import struct

NAMES = {
    0x00: "G_SPNOOP", 0x01: "G_MTX", 0x03: "G_MOVEMEM", 0x04: "G_VTX", 0x06: "G_DL", 0x09: "G_SPRITE2D",
    0xB1: "G_TRI2?", 0xB2: "G_RDPHALF_CONT", 0xB3: "G_RDPHALF_2", 0xB4: "G_RDPHALF_1", 0xB5: "G_LINE3D",
    0xB6: "G_CLEARGEOMETRYMODE", 0xB7: "G_SETGEOMETRYMODE", 0xB8: "G_ENDDL", 0xB9: "G_SETOTHERMODE_L",
    0xBA: "G_SETOTHERMODE_H", 0xBB: "G_TEXTURE", 0xBC: "G_MOVEWORD", 0xBD: "G_POPMTX", 0xBE: "G_CULLDL",
    0xBF: "G_TRI1", 0xC0: "G_NOOP", 0xE4: "G_TEXRECT", 0xE5: "G_TEXRECTFLIP", 0xE6: "G_RDPLOADSYNC",
    0xE7: "G_RDPPIPESYNC", 0xE8: "G_RDPTILESYNC", 0xE9: "G_RDPFULLSYNC", 0xEA: "G_SETKEYGB",
    0xEB: "G_SETKEYR", 0xEC: "G_SETCONVERT", 0xED: "G_SETSCISSOR", 0xEE: "G_SETPRIMDEPTH",
    0xEF: "G_RDPSETOTHERMODE", 0xF0: "G_LOADTLUT", 0xF2: "G_SETTILESIZE", 0xF3: "G_LOADBLOCK",
    0xF4: "G_LOADTILE", 0xF5: "G_SETTILE", 0xF6: "G_FILLRECT", 0xF7: "G_SETFILLCOLOR", 0xF8: "G_SETFOGCOLOR",
    0xF9: "G_SETBLENDCOLOR", 0xFA: "G_SETPRIMCOLOR", 0xFB: "G_SETENVCOLOR", 0xFC: "G_SETCOMBINE",
    0xFD: "G_SETTIMG", 0xFE: "G_SETZIMG", 0xFF: "G_SETCIMG",
}

G_LIGHTING = 0x00020000
G_CULL_FRONT = 0x00001000
G_CULL_BACK = 0x00002000


def commands(buf, lo, hi):
    """[(offset, w0, w1)] over [lo, hi)"""
    return [(q,) + struct.unpack_from(">II", buf, q) for q in range(lo, hi - 7, 8)]


def describe(w0, w1):
    op = w0 >> 24
    n = NAMES.get(op, "0x%02X" % op)
    if op == 0x04:
        return "%s n=%d v0=%d seg=%X off=0x%X" % (n, ((w0 >> 20) & 0xF) + 1, (w0 >> 16) & 0xF, w1 >> 24, w1 & 0xFFFFFF)
    if op == 0xBF:
        return "%s %d %d %d" % (n, ((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10)
    if op == 0x06:
        return "%s %s 0x%X" % (n, "branch" if (w0 >> 16) & 1 else "call", w1)
    if op == 0xFD:
        return "%s fmt=%d siz=%d width=%d id/addr=0x%X" % (n, (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0xFFF) + 1, w1)
    if op == 0xF5:
        return "%s fmt=%d siz=%d line=%d tmem=%d tile=%d pal=%d cmt=%d maskt=%d cms=%d masks=%d" % (
            n, (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 >> 9) & 0x1FF, w0 & 0x1FF, (w1 >> 24) & 7, (w1 >> 20) & 0xF,
            (w1 >> 18) & 3, (w1 >> 14) & 0xF, (w1 >> 8) & 3, (w1 >> 4) & 0xF)
    if op == 0xF2:
        return "%s tile=%d uls=%g ult=%g lrs=%g lrt=%g" % (n, (w1 >> 24) & 7, ((w0 >> 12) & 0xFFF) / 4,
                                                        (w0 & 0xFFF) / 4, ((w1 >> 12) & 0xFFF) / 4, (w1 & 0xFFF) / 4)
    if op == 0xF3:
        return "%s tile=%d texels=%d dxt=%d" % (n, (w1 >> 24) & 7, ((w1 >> 12) & 0xFFF) + 1, w1 & 0xFFF)
    if op == 0xBB:
        return "%s on=%d s=%04X t=%04X" % (n, w0 & 0xFF, w1 >> 16, w1 & 0xFFFF)
    if op in (0xB6, 0xB7):
        return "%s %08X" % (n, w1)
    return "%s %08X %08X" % (n, w0, w1)


class TexState:
    __slots__ = ("timg", "timg_fmt", "timg_siz", "tlut", "fmt", "siz", "line", "cms", "cmt", "masks",
                 "maskt", "shifts", "shiftt", "uls", "ult", "lrs", "lrt", "sscale", "tscale", "on", "palette",
                 "level", "dxt", "tlutn")

    def __init__(self):
        for k in self.__slots__:
            setattr(self, k, 0)
        self.timg = None
        self.tlut = None
        self.dxt = -1

    @property
    def mips(self):
        """the levels of detail drawn (G_TEXTURE's level + 1 after a G_LOADBLOCK with dxt 0: a mipmapped slot)"""
        return self.level + 1 if self.level and self.dxt == 0 else 1

    def key(self):
        return (self.timg, self.fmt, self.siz, self.width, self.height, self.cms, self.cmt, self.tlut)

    @property
    def width(self):
        w = (self.lrs - self.uls) // 4 + 1
        return w if w > 0 else (1 << self.masks if self.masks else 32)

    @property
    def height(self):
        h = (self.lrt - self.ult) // 4 + 1
        return h if h > 0 else (1 << self.maskt if self.maskt else 32)


class Walker:
    """Follows display lists the way the RSP would (G_DL calls/branches, G_ENDDL)
    and collects triangles with their vertices and texture state.

    resolve(addr) -> (buffer, offset) for a segmented G_VTX address;
    dl_target(addr) -> offset in `buf` for a G_DL address (None: skip)."""

    def __init__(self, buf, resolve, dl_target=None):
        self.buf = buf
        self.resolve = resolve
        self.dl_target = dl_target or (lambda a: None)
        self.vbuf = [None] * 32
        self.tex = TexState()
        self.geom = 0
        self.tris = []          # (vertex tuple x3, TexState snapshot key, geometry mode)
        self.textures = {}      # key -> dict of the state
        self.max_steps = 200000

    def _snap(self):
        t = self.tex
        if not t.on or t.timg is None:
            return None
        k = t.key()
        if k not in self.textures:
            self.textures[k] = {"id": t.timg, "fmt": t.fmt, "siz": t.siz, "width": t.width, "height": t.height,
                                "cms": t.cms, "cmt": t.cmt, "masks": t.masks, "maskt": t.maskt,
                                "shifts": t.shifts, "shiftt": t.shiftt, "uls": t.uls, "ult": t.ult,
                                "tlut": t.tlut, "mips": t.mips, "palette": t.palette, "tlutn": t.tlutn}
        return k

    def run(self, pc, end=None):
        """execute from pc; `end`: stop there (a range, not a list ending in G_ENDDL)"""
        stack = []
        steps = 0
        buf = self.buf
        while steps < self.max_steps and pc + 8 <= len(buf):
            if end is not None and pc >= end and not stack:
                return
            steps += 1
            w0, w1 = struct.unpack_from(">II", buf, pc)
            pc += 8
            op = w0 >> 24
            t = self.tex
            if op == 0x04:
                n = ((w0 >> 20) & 0xF) + 1
                v0 = (w0 >> 16) & 0xF
                r = self.resolve(w1)
                if r is None:
                    continue
                vb, vo = r
                for i in range(n):
                    o = vo + 16 * i
                    if o + 16 <= len(vb) and v0 + i < len(self.vbuf):
                        self.vbuf[v0 + i] = struct.unpack_from(">hhhHhhBBBB", vb, o)
            elif op == 0xBF:
                ia, ib, ic = ((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10
                vs = [self.vbuf[i] if i < len(self.vbuf) else None for i in (ia, ib, ic)]
                if None not in vs:
                    self.tris.append((vs[0], vs[1], vs[2], self._snap(), self.geom, (t.sscale, t.tscale)))
            elif op == 0x06:
                tgt = self.dl_target(w1)
                if tgt is None:
                    continue
                if not (w0 >> 16) & 1:
                    if len(stack) >= 10:
                        return
                    stack.append((pc, end))
                    end = None
                pc = tgt
            elif op == 0xB8:
                if not stack:
                    return
                pc, end = stack.pop()
            elif op == 0xB7:
                self.geom |= w1
            elif op == 0xB6:
                self.geom &= ~w1
            elif op == 0xBB:
                t.on = w0 & 0xFF
                t.level = (w0 >> 11) & 7
                t.sscale, t.tscale = w1 >> 16, w1 & 0xFFFF
            elif op == 0xFD:
                t.timg = w1
                t.timg_fmt, t.timg_siz = (w0 >> 21) & 7, (w0 >> 19) & 3
            elif op == 0xF3:
                t.dxt = w1 & 0xFFF
            elif op == 0xF0:
                t.tlut = t.timg
                t.tlutn = ((w1 >> 14) & 0x3FF) + 1
            elif op == 0xF5:
                tile = (w1 >> 24) & 7
                if tile == 0:
                    t.fmt, t.siz, t.line = (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 >> 9) & 0x1FF
                    t.palette = (w1 >> 20) & 0xF
                    t.cmt, t.maskt, t.shiftt = (w1 >> 18) & 3, (w1 >> 14) & 0xF, (w1 >> 10) & 0xF
                    t.cms, t.masks, t.shifts = (w1 >> 8) & 3, (w1 >> 4) & 0xF, w1 & 0xF
            elif op == 0xF2:
                if (w1 >> 24) & 7 == 0:
                    t.uls, t.ult = (w0 >> 12) & 0xFFF, w0 & 0xFFF
                    t.lrs, t.lrt = (w1 >> 12) & 0xFFF, w1 & 0xFFF
