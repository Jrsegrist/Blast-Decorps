#!/usr/bin/env python3
"""draftconv: draft a tools_port/conventions.txt line from the asm survey.

    python3 tools_port/draftconv.py math_dist3 [func_... ...] [--json funcs.json]

Reads the survey's funcs.json (key `funcs`: inputs, nonabi_inputs,
outputs_used, nonabi_outputs_definite/_conditional, clobbers_callee_saved,
asm_callers_rely_on_preserved, gp_offsets, ...) and prints a suggested line
plus notes.  It's a draft: check it against the asm, choose the real C
argument order, and edit before adding it to conventions.txt.

Rules used:
  * ABI inputs keep their slot (a0=a0, f12=f12); stack inputs S+16.. stay
    in place (stack0=stack0).
  * Non-ABI integer inputs take the lowest free a0-a3, then the next stack
    slot; non-ABI float inputs take f12/f14 if free (check o32 placement).
  * Outputs: v0 or the first integer output -> ret, f0 (or the first float)
    -> fret, every other one -> its own pointer argument (*aN).
  * A conditional output (written on some paths only) is passed in and out
    through a pointer (in R=*aN ; out R=*aN), so the C keeps the old value.
  * preserve = registers asm callers rely on surviving; clobbers = callee-saved
    registers the asm changes that aren't outputs.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_JSONS = [
    os.environ.get("FUNCS_JSON", ""),
    os.path.join(HERE, "funcs.json"),
    "/mnt/c/Users/jrseg/AppData/Local/Temp/claude/C--Users-jrseg/e9eba03f-68bb-49a2-91cf-2fbddfa31cc5"
    "/scratchpad/survey_asm/funcs.json",
]
INT_SLOTS = ["a0", "a1", "a2", "a3"]
SAVED = ["s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "fp"] + ["f%d" % i for i in range(20, 32)]
REG_ORDER = ["v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "gp", "fp", "ra"] + \
            ["f%d" % i for i in range(32)]


def is_float(r):
    return re.match(r"^f\d+$", r) is not None


def order(regs):
    return sorted(regs, key=lambda r: REG_ORDER.index(r) if r in REG_ORDER else 99)


def draft(e):
    notes = []
    ins, outs = [], []
    used_int, used_f, nstack = set(), set(), 0
    inputs = list(e.get("inputs", []))
    nonabi = set(e.get("nonabi_inputs", []))
    gp_global = bool(e.get("gp_offsets"))
    # ABI inputs and stack arguments first
    for r in inputs:
        m = re.match(r"^S\+(\d+)$", r)
        if m:
            k = (int(m.group(1)) - 16) // 4
            ins.append(("stack%d" % k, "stack%d" % k))
            nstack = max(nstack, k + 1)
        elif r in INT_SLOTS and r not in nonabi:
            ins.append((r, r))
            used_int.add(r)
        elif r in ("f12", "f14") and r not in nonabi:
            ins.append((r, r))
            used_f.add(r)

    def next_int():
        nonlocal nstack
        for s in INT_SLOTS:
            if s not in used_int:
                used_int.add(s)
                return s
        nstack += 1
        return "stack%d" % (nstack - 1)

    for r in order([r for r in inputs if r in nonabi]):
        if r == "gp" and gp_global:
            notes.append("gp is read as a global base (gp_offsets %s): not an argument; the C reads the "
                         "globals directly" % e.get("gp_offsets")[:4])
            continue
        if is_float(r):
            slot = next((s for s in ("f12", "f14") if s not in used_f), None)
            if slot is None:
                slot = next_int()
                notes.append("%s: no float slot left, passed as bits in %s (use fbits)" % (r, slot))
            else:
                used_f.add(slot)
                notes.append("%s -> %s: o32 passes a float in f12/f14 only before any integer argument; "
                             "check the C prototype's order" % (r, slot))
            ins.append((r, slot))
        else:
            ins.append((r, next_int()))
    # outputs
    used = set(e.get("outputs_used", []))
    definite = set(e.get("nonabi_outputs_definite", []))
    cond = set(e.get("nonabi_outputs_conditional", []))
    outregs = order((used | definite | cond) - {"ra", "sp"})
    have_ret = have_fret = False
    first_int = next((r for r in outregs if not is_float(r)), None)
    first_f = next((r for r in outregs if is_float(r)), None)
    if "v0" in outregs:
        first_int = "v0"
    if "f0" in outregs:
        first_f = "f0"
    for r in outregs:
        if r in cond and r not in definite:
            slot = next_int()
            ins.append((r, "*" + slot))
            outs.append((r, "*" + slot))
            notes.append("%s is written on some paths only: passed in and out through %s" % (r, slot))
        elif r == first_int and not have_ret:
            outs.append((r, "ret"))
            have_ret = True
        elif r == first_f and not have_fret:
            outs.append((r, "fret"))
            have_fret = True
        else:
            slot = next_int()
            outs.append((r, "*" + slot))
    nptr = sum(1 for _, d in outs if d.startswith("*"))
    if nptr > 2:
        notes.append("%d pointer outputs: consider one struct pointer instead (out R1=*aN,R2=*aN+4,...)" % nptr)
    outset = set(r for r, _ in outs)
    rely = set()
    for regs in (e.get("asm_callers_rely_on_preserved") or {}).values():
        rely |= set(regs)
    preserve = order(rely - outset)
    clobbers = order(set(e.get("clobbers_callee_saved", [])) - outset)
    nonabi_any = bool(nonabi - {"gp"} or definite - {"v0", "f0"} or cond)
    parts = []
    if nonabi_any:
        def key(x):
            s = x[1].lstrip("*")
            return (0, INT_SLOTS.index(s)) if s in INT_SLOTS else (1, int(s[5:])) if s.startswith("stack") \
                else (2, s)
        ins.sort(key=key)
        if ins:
            parts.append("in " + ",".join("%s=%s" % x for x in ins))
        if outs:
            parts.append("out " + ",".join("%s=%s" % x for x in outs))
    else:
        notes.append("ABI-clean interface: only preserve/clobbers matter (stubs keep those registers)")
    if preserve:
        parts.append("preserve " + ",".join(preserve))
    if clobbers:
        parts.append("clobbers " + ",".join(clobbers))
    if rely & {"v0", "v1"} == set() and nonabi_any is False and preserve:
        notes.append("the survey doesn't list v0/v1 in preserve lists: if an asm caller reuses v1 or a "
                     "stub clobber makes the reference fault, add them")
    stk = e.get("stack_args") or []
    if stk:
        notes.append("stack arguments %s are read in place" % stk)
    if e.get("flags"):
        notes.append("survey flags: %s" % e["flags"])
    readers = e.get("nonabi_outputs_read_by") or {}
    if readers:
        notes.append("asm callers reading outputs: " + ", ".join("%s(%s)" % (k, ",".join(v)) for k, v in
                                                                sorted(readers.items())))
    if e.get("c_refs", {}).get("decls"):
        notes.append("C declarations: " + "; ".join(e["c_refs"]["decls"]))
    if not parts:
        return None, notes
    return "%s: %s" % (e["name"], " ; ".join(parts)), notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("funcs", nargs="+")
    ap.add_argument("--json", help="survey funcs.json (default $FUNCS_JSON, tools_port/funcs.json, the "
                                   "survey_asm scratch copy)")
    o = ap.parse_args()
    path = o.json or next((p for p in DEFAULT_JSONS if p and os.path.exists(p)), None)
    if not path:
        sys.exit("draftconv: no funcs.json found; pass --json or set FUNCS_JSON")
    funcs = json.load(open(path))["funcs"]
    try:
        sys.path.insert(0, HERE)
        from eqcheck import parse_conv_line
    except Exception:
        parse_conv_line = None
    rc = 0
    for n in o.funcs:
        e = funcs.get(n)
        if e is None:
            print("# %s: not in the survey" % n)
            rc = 1
            continue
        line, notes = draft(e)
        print("# %s (%s, %d insns): inputs %s; outputs used %s" % (n, e.get("file"), e.get("size", 0),
                                                                 ",".join(e.get("inputs", [])) or "-",
                                                                 ",".join(e.get("outputs_used", [])) or "-"))
        for x in notes:
            print("#   " + x)
        if line is None:
            print("# (no line needed)")
        else:
            if parse_conv_line is not None:
                try:
                    parse_conv_line(line, "draft")
                except ValueError as ex:
                    print("#   draft doesn't parse: %s" % ex)
            print(line)
    return rc


if __name__ == "__main__":
    sys.exit(main())
