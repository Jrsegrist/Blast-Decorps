"""asm-differ's settings for this project (tools/asm-differ/diff.py, run from blastcorps/).

The function's address picks the segment it is in; DIFF_SEG=init|hd_code|hd_front_end
overrides that.  The version is us.v11 unless only another version's build is there.
"""
import os
import re

# vram ranges of the segments that aren't hd_code
OTHER_SEGMENTS = {
    "hd_front_end": (0x801E7000, 0x80210E90),
    "init": (0x8021ED00, 0x80224A00),
}


def segment_of(args):
    if os.environ.get("DIFF_SEG"):
        return os.environ["DIFF_SEG"]
    found = re.search(r"[0-9A-Fa-f]{8}", str(getattr(args, "start", None) or ""))
    if found:
        addr = int(found.group(0), 16)
        for name, (lo, hi) in OTHER_SEGMENTS.items():
            if lo <= addr < hi:
                return name
    return "hd_code"


def apply(config, args):
    seg = segment_of(args)
    for version, build in (("us.v11", "build"), ("us.v10", "build/us.v10")):
        if os.path.exists("%s/%s.%s.bin" % (build, seg, version)):
            break
    else:
        version, build = "us.v11", "build"
    config["baseimg"] = "%s.%s.bin" % (seg, version)
    config["myimg"] = "%s/%s.%s.bin" % (build, seg, version)
    config["mapfile"] = "%s/%s.%s.map" % (build, seg, version)
    config["source_directories"] = ["src.us.v11", "include"]
