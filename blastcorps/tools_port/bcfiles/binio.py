"""Small binary helpers: big-endian reads and a declarative record type.

Every multi-byte value in the game's files is big-endian.  A Record subclass
lists its fields as (name, struct code) pairs; it packs and unpacks itself,
converts to and from plain dicts (for JSON) and compares by value.
"""
import struct


class FormatError(Exception):
    """The bytes don't follow the format the parser expects."""


def be16(b, o):
    return (b[o] << 8) | b[o + 1]


def be16s(b, o):
    v = (b[o] << 8) | b[o + 1]
    return v - 0x10000 if v & 0x8000 else v


def be32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def be32s(b, o):
    v = be32(b, o)
    return v - 0x100000000 if v & 0x80000000 else v


def hexs(b):
    return bytes(b).hex()


def unhex(s):
    return bytes.fromhex(s) if s else b""


def align(n, a):
    return (n + a - 1) // a * a


class RecordMeta(type):
    def __new__(mcs, name, bases, ns):
        cls = super().__new__(mcs, name, bases, ns)
        fields = ns.get("FIELDS")
        if fields is not None:
            cls._names = tuple(f[0] for f in fields)
            cls._struct = struct.Struct(">" + "".join(f[1] for f in fields))
            cls.SIZE = cls._struct.size
        return cls


class Record(metaclass=RecordMeta):
    """A fixed-size big-endian record.  FIELDS = [(name, code), ...] with
    struct codes (b B h H i I, or Ns for raw bytes, shown as hex in JSON)."""
    FIELDS = []
    __slots__ = ("_v",)

    def __init__(self, *args, **kw):
        if args and len(args) == len(self._names):
            vals = list(args)
        else:
            vals = [b"\0" * int(c[:-1]) if c.endswith("s") else 0 for _, c in self.FIELDS]
        for k, v in kw.items():
            vals[self._names.index(k)] = v
        object.__setattr__(self, "_v", vals)

    def __getattr__(self, k):
        try:
            return self._v[self._names.index(k)]
        except ValueError:
            raise AttributeError(k) from None

    def __setattr__(self, k, v):
        if k in self._names:
            self._v[self._names.index(k)] = v
        else:
            raise AttributeError(k)

    def __eq__(self, o):
        return type(o) is type(self) and o._v == self._v

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__, ", ".join(
            "%s=%s" % (n, v.hex() if isinstance(v, bytes) else v) for n, v in zip(self._names, self._v)))

    @classmethod
    def unpack(cls, b, o=0):
        if o + cls.SIZE > len(b):
            raise FormatError("%s at 0x%X runs past the end (0x%X)" % (cls.__name__, o, len(b)))
        return cls(*cls._struct.unpack_from(b, o))

    @classmethod
    def unpack_array(cls, b, lo, hi):
        """records over [lo, hi); returns (list, end of the last whole record)"""
        out = []
        p = lo
        while p + cls.SIZE <= hi:
            out.append(cls.unpack(b, p))
            p += cls.SIZE
        return out, p

    def pack(self):
        return self._struct.pack(*self._v)

    def to_json(self):
        d = {}
        for (n, c), v in zip(self.FIELDS, self._v):
            d[n] = v.hex() if c.endswith("s") else v
        return d

    def to_list(self):
        """compact JSON form: the values in FIELDS order"""
        return [v.hex() if isinstance(v, bytes) else v for v in self._v]

    @classmethod
    def from_list(cls, lst):
        return cls(*[bytes.fromhex(v) if c.endswith("s") else int(v) for (_, c), v in zip(cls.FIELDS, lst)])

    @classmethod
    def from_json(cls, d):
        vals = []
        for n, c in cls.FIELDS:
            v = d[n]
            vals.append(bytes.fromhex(v) if c.endswith("s") else int(v))
        return cls(*vals)


def pack_records(recs):
    return b"".join(r.pack() for r in recs)


def records_json(recs):
    return [r.to_json() for r in recs]


def records_from_json(cls, lst):
    return [cls.from_json(d) for d in lst]


# ---------------------------------------------------------------- common records

class Vtx(Record):
    """libultra Vtx (16 bytes): position, flag, texture coordinates (10.5), colour/normal."""
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("flag", "H"), ("s", "h"), ("t", "h"),
              ("r", "B"), ("g", "B"), ("b", "B"), ("a", "B")]


class Tri9(Record):
    """Nine s16: three (x, y, z) points (the start of every packed collision triangle)."""
    FIELDS = [("x0", "h"), ("y0", "h"), ("z0", "h"), ("x1", "h"), ("y1", "h"), ("z1", "h"),
              ("x2", "h"), ("y2", "h"), ("z2", "h")]
