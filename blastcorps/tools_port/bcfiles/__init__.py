"""bcfiles: Blast Corps level and model files, read from the player's ROM and
written back (docs/tools.md).  Standard library only.

    from bcfiles import Rom, level, model
    rom = Rom.load("baserom.us.v11.z64")
    f = rom.level(2)                       # a RomFile: gzip members + metadata
    lv = level.parse(f.data, f.split)      # typed sections
    data, split = lv.write()               # == (f.data, f.split) for every US level
"""
from .romfs import Rom, RomFile  # noqa: F401
from . import gz, lzss, texture, level, model, gbi  # noqa: F401
