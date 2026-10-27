#!/usr/bin/env python3
"""Regression tests for tools_port/eqcheck.py.

    python3 tools_port/tests/test_eqcheck.py          # synthetic machines only (no build needed)
    python3 tools_port/tests/test_eqcheck.py --real   # also: pooled (in-process, reused machines)
                                                      # runs give the same results as fresh processes

The synthetic tests build tiny fake builds (a few hand-assembled MIPS
functions at 0x80001000..) and drive eqcheck's Machine/compare directly.
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TP = os.path.dirname(HERE)
REPO = os.path.dirname(TP)
sys.path.insert(0, TP)
import eqcheck as E          # noqa: E402

JR_RA, NOP = 0x03E00008, 0
FAILS = []


def check(cond, what):
    print("%s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def jal(a):
    return 0x0C000000 | (a & 0x0FFFFFFF) >> 2


def fake_build(label, code, funcs):
    """CODE: {addr: [words]} placed in one text section; FUNCS: {name: addr}."""
    b = E.Build.__new__(E.Build)
    b.label = label
    lo = min(code)
    hi = max(a + 4 * len(w) for a, w in code.items())
    text = bytearray(hi - lo)
    for a, ws in code.items():
        text[a - lo:a - lo + 4 * len(ws)] = struct.pack(">%dI" % len(ws), *ws)
    b.sections = [(lo, bytes(text), ".text", True), (0x80100000, 0x1000, ".bss", False)]
    b.sym = dict(funcs)
    b.size = {}
    b.addr_syms = sorted((a, n) for n, a in funcs.items())
    b._keys = [a for a, _ in b.addr_syms]
    b.funcs = {a: n for n, a in funcs.items()}
    b.abs_funcs = {}
    b.text = [(lo, hi)]
    b.cache_key = (label,)
    return b


def machine(b, func, extra=()):
    opts = E.parse_args([func, "--no-conv", "--ret", "void"] + list(extra))
    return E.Machine(b, opts=opts), opts


def plan_for(opts, b, regs):
    plan = E.make_plan(opts, 0)
    plan.regs.update(regs)
    return E.PlanView(plan, b)


# ---------------------------------------------------------------------------
def test_delay_slot_fault_reset():
    """A run that faults in a branch-likely delay slot must not leak QEMU's
    branch state into the next run (the next run used to execute one
    instruction at its entry and then jump to the old branch target)."""
    code = {
        0x80001000: [0x50000003, 0x8D280000, NOP, NOP, JR_RA, NOP],   # f1: beql zero,zero,+3 ; lw t0,0(t1)
        0x80001100: [0x24020005, JR_RA, NOP],                          # f2: v0 = 5
    }
    b = fake_build("b", code, {"f1": 0x80001000, "target": 0x80001010, "f2": 0x80001100})
    m, opts = machine(b, "f2")
    m.run(0x80001000, plan_for(opts, b, {"t1": 0x85000000}))
    check(m.fault is not None and m.fault[0] == "unmapped", "f1 faults in the beql delay slot (%s)" % (m.fault,))
    r = m.run(0x80001100, plan_for(opts, b, {}))
    check(m.fault is None and r["v0"] == 5, "next run starts clean and returns v0=5 (fault %s, v0 %X)"
          % (m.fault, r["v0"]))
    check(m.events == [], "next run makes no call to the old branch target (%s)" % (m.events,))


def test_tlb_fill_in_delay_slot():
    """A store in a taken branch's delay slot to a page not touched before:
    the TLB-fill hook fires inside the delay slot (unicorn leaves branch bits in
    hflags; _delay_slot_fix clears them) and the store is seen by the diff."""
    code = {
        0x80001000: [0x10000002, 0xAD680000, NOP, JR_RA, NOP],         # b +2 ; sw t0,0(t3) ; nop ; jr ra
        0x80001100: [0x15400002, 0xAD680000, NOP, JR_RA, NOP],         # bne t2,zero,+2 ; sw t0,0(t3)
        0x80001200: [0x03E08025, jal(0x80001300), 0xAD680000, 0x02000008, NOP],   # or s0,ra ; jal g ; sw ; jr s0
        0x80001300: [JR_RA, NOP],
    }
    b = fake_build("b", code, {"fb": 0x80001000, "fbne": 0x80001100, "fjal": 0x80001200, "g": 0x80001300})
    m, opts = machine(b, "fb", ["--follow", "g"])
    check(m.tlb_track, "TLB write tracking is on (hflags offset %s)" % E.HFLAGS)
    for name, entry, addr in (("b", 0x80001000, 0x80102000), ("bne", 0x80001100, 0x80103000),
                              ("jal", 0x80001200, 0x80104000)):
        r = m.run(entry, plan_for(opts, b, {"t0": 0x11223344, "t2": 1, "t3": addr}))
        check(m.fault is None, "%s; sw in the delay slot: returns (fault %s)" % (name, m.fault))
        check(set(range(addr, addr + 4)) <= m.written, "%s; sw in the delay slot: the store is in the diff" % name)
    # the same store again (the TLB is flushed per run, so it is seen again)
    m.run(0x80001000, plan_for(opts, b, {"t0": 0x55667788, "t3": 0x80102000}))
    check(set(range(0x80102000, 0x80102004)) <= m.written, "TLB flushed between runs: repeated store seen")


def test_both_fault_jal_delay_slot():
    """asm faults in a jal delay slot (before the call is recorded), C faults
    right after the call: both faulted identically, events compared only up
    to the shorter list."""
    ref = fake_build("build", {0x80001000: [jal(0x80001100), 0x8D280000, JR_RA, NOP],   # jal g ; lw t0,0(t1)
                               0x80001100: [JR_RA, NOP]}, {"f": 0x80001000, "g": 0x80001100})
    new = fake_build("build_nm", {0x80001000: [jal(0x80001100), NOP, 0x8D280000, JR_RA, NOP],
                                  0x80001100: [JR_RA, NOP]}, {"f": 0x80001000, "g": 0x80001100})
    mr, opts = machine(ref, "f")
    mn = E.Machine(new, opts=opts)
    amap = E.AddrMap(ref, new)
    pv = plan_for(opts, ref, {"t1": 0x85000000})
    rr, rn = mr.run(0x80001000, pv), mn.run(0x80001000, pv)
    check(len(mr.events) == 0 and len(mn.events) == 1, "asm records no call, C records one (%d, %d)"
          % (len(mr.events), len(mn.events)))
    d = E.compare(opts, mr, mn, amap, rr, rn)
    check(d == [], "both faulted identically: equivalent (%s)" % d)
    # a call both made before faulting, with different arguments, is still a difference
    new2 = fake_build("build_nm", {0x80001000: [jal(0x80001100), 0x24040007, 0x8D280000, JR_RA, NOP],  # a0 = 7
                                   0x80001100: [JR_RA, NOP]}, {"f": 0x80001000, "g": 0x80001100})
    ref2 = fake_build("build", {0x80001000: [jal(0x80001100), 0x24040008, 0x8D280000, JR_RA, NOP],    # a0 = 8
                                0x80001100: [JR_RA, NOP]}, {"f": 0x80001000, "g": 0x80001100})
    mr2, mn2 = E.Machine(ref2, opts=opts), E.Machine(new2, opts=opts)
    rr, rn = mr2.run(0x80001000, pv), mn2.run(0x80001000, pv)
    d = E.compare(opts, mr2, mn2, E.AddrMap(ref2, new2), rr, rn)
    check(len(d) == 1 and "event #0" in d[0], "different args before a common fault: still reported (%s)" % d)


def test_own_stack_slot():
    """A C rewrite that stores to its own incoming stack argument (sp+0x10) is
    not a caller-frame difference when the slot is one of its arguments."""
    ref = fake_build("build", {0x80001000: [JR_RA, NOP]}, {"f": 0x80001000})
    new = fake_build("build_nm", {0x80001000: [JR_RA, 0xAFA40010]}, {"f": 0x80001000})   # sw a0,0x10(sp)
    for extra, want in (([], 1), (["--arg", "stack0=int"], 0), (["--arg", "sp+0x10=int"], 0)):
        mr, opts = machine(ref, "f", extra)
        mn = E.Machine(new, opts=opts)
        pv = plan_for(opts, ref, {"a0": 0x12345678})
        rr, rn = mr.run(0x80001000, pv), mn.run(0x80001000, pv)
        d = E.compare(opts, mr, mn, E.AddrMap(ref, new), rr, rn)
        check(len(d) == want, "store to sp+0x10 with %s: %d difference(s) (%s)" % (extra or "no stack arg", want, d))
        if not want:
            check(opts.own_stack_hits == {E.STACK_TOP + 0x10}, "the ignored slot is noted for -v")


def test_parsing():
    c = E.parse_conv_line("f: in t4=a0:8,t5=a2 ; out t4=ret64,t6=*a3:8", "t")
    check(c.ins[0][1] == ("reg", "a0", 0, 8) and c.outs[0][1] == ("reg", "v0", 0, 8), "ret64 / a0:8 parse")
    for bad in ("f: in t4=a1:8", "f: in t4=stack1:8", "f: in t4=a0:8,t5=a1", "f: in t4=f12:8"):
        try:
            E.parse_conv_line(bad, "t")
            check(False, "rejects %r" % bad)
        except ValueError:
            check(True, "rejects %r" % bad)
    rng = E.random.Random(1)
    data, _ = E.gen_fill("hex:00 11 22 33 44", None, rng, E.Plan(), "x")
    check(data == bytes.fromhex("0011223344"), "hex: fill")
    data, _ = E.gen_fill("hex:ABCD", 6, rng, E.Plan(), "x")
    check(data == bytes.fromhex("ABCDABCDABCD"), "hex: fill repeats to SIZE")
    check(E.sig_match(["a0:1", "stack4:2"], "stack4", "stack4") == 2 and
          E.sig_match(["a0"], "a1", "a1") is None, "--sig widths")
    check(E.stack_target("stack2") == "sp+0x18", "--arg stack2 = sp+0x18")


def test_conv64_plan():
    """`in t4=a0:8 ; out t4=ret64`: the asm side gets a whole 64-bit t4, the C
    side the a0:a1 pair; outputs compare 64 bits."""
    conv = E.parse_conv_line("f: in t4=a0:8 ; out t4=ret64", "t")
    opts = E.parse_args(["f", "--conv", "f: in t4=a0:8 ; out t4=ret64", "--arg", "t4=int64:0x123456789:0x123456789"])
    plan = E.make_plan(opts, 0)
    check(plan.side_regs["asm"]["t4"] == 0x123456789 and isinstance(plan.side_regs["asm"]["t4"], E.Wide),
          "asm t4 = 64-bit value")
    check(plan.side_regs["c"]["a0"] == 1 and plan.side_regs["c"]["a1"] == 0x23456789, "C a0:a1 = high:low")
    res = {"v0": 1, "v1": 0x23456789, "_64": {"t4": 0x123456789}}
    check(E.conv_outputs(conv, "asm", None, res, None) == E.conv_outputs(conv, "c", None, res, None)
          == [0x123456789], "64-bit output: asm t4 vs C v0:v1")


def test_stub_ret_seq():
    opts = E.parse_args(["f", "--stub-ret", "g=seq:1,0,5", "--stub-ret", "h=int:3:3", "--stub-ret", "k.t0=choice:9"])
    vals = [opts.ret_for("g", k, None)[0] for k in range(4)]
    check(vals == [1, 0, 5, 1], "--stub-ret seq: per call, cycling (%s)" % vals)
    check(opts.ret_for("h", 0, None)[0] == 3, "--stub-ret int:LO:HI")
    check(opts.out_value("k", 0, 1, "t0", None) == 9, "--stub-ret NAME.REG=choice:")


# ---------------------------------------------------------------------------
def test_pool_matches_fresh():
    """Check lines run in one process (reused builds and machines, as in
    runchecks' worker pool) give the same results as fresh eqcheck processes."""
    sys.path.insert(0, TP)
    import runchecks
    checks = []
    for path in sorted(os.listdir(os.path.join(TP, "checks")))[:6]:
        checks += runchecks.parse_checks(os.path.join(TP, "checks", path))[:2]
    os.chdir(REPO)

    def key(out):
        return re.sub(r"\(\d+\.\d+s\)", "", out).strip()
    pooled = [key(E.run_captured([c[2]] + c[3] + ["-n", "30"])[1]) for c in checks + checks[::-1]]
    fresh = []
    for c in checks:
        p = subprocess.run([sys.executable, os.path.join(TP, "eqcheck.py"), c[2]] + c[3] + ["-n", "30"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        fresh.append(key(p.stdout))
    check(pooled[:len(checks)] == fresh, "%d check lines: in-process results = fresh-process results" % len(checks))
    check(pooled[len(checks):] == fresh[::-1], "same lines again in reverse order on recycled machines")


if __name__ == "__main__":
    test_delay_slot_fault_reset()
    test_tlb_fill_in_delay_slot()
    test_both_fault_jal_delay_slot()
    test_own_stack_slot()
    test_parsing()
    test_conv64_plan()
    test_stub_ret_seq()
    if "--real" in sys.argv:
        test_pool_matches_fresh()
    print("%d failure(s)" % len(FAILS))
    sys.exit(1 if FAILS else 0)
