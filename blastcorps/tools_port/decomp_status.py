#!/usr/bin/env python3
"""decomp_status: how far the matching decompilation is, per segment.

Every function in the matching build falls in exactly one category:

  matched C       compiled from C in the matching build (game C, ultralib wrappers)
  hand+NM C       permanent hand-written asm (GLOBAL_ASM) with a NON_MATCHING C version
  hand, no C      permanent hand-written asm with no C (e.g. libultra .s the port replaces)
  near-miss       IDO C still behind GLOBAL_ASM, with a NON_MATCHING C draft
  unmatched C     IDO C still behind GLOBAL_ASM, no C at all

"named" counts the functions with a real name (SDK names and tools_port/names.txt renames)
rather than a func_XXXXXXXX address name.

"Permanent hand-written" comes from tools_port/hand_asm.txt.  Functions are counted from the
matching build's ELFs (build/<segment>.us.v11.elf: defined FUNC symbols, without jump-table
L-labels), and the GLOBAL_ASM side from the #pragma lines in src.us.v11/<segment>/*.c and the
glabels of each pragma's .s.  A pragma counts as having NON_MATCHING C when it sits on the
matching-only side of an `#ifdef NON_MATCHING` / `#ifndef NON_MATCHING` block whose other side
defines the function.

usage: python3 tools_port/decomp_status.py [-v] [--markdown]
  run from the repo dir (blastcorps/) after a matching build.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

SEGMENTS = ['init', 'hd_code', 'hd_front_end']
VERSION = 'us.v11'
CATS = ['matched', 'hand_nm', 'hand', 'near', 'unmatched']
CAT_TITLES = {
    'matched': 'matched C',
    'hand_nm': 'hand asm + NM C',
    'hand': 'hand asm, no C',
    'near': 'near-miss',
    'unmatched': 'unmatched IDO C',
}
LABEL_RE = re.compile(r'^L[0-9A-F]{8}(_[0-9A-F]+)?$')
PRAGMA_RE = re.compile(r'^\s*#\s*pragma\s+GLOBAL_ASM\(\s*"([^"]+)"\s*\)')
COND_RE = re.compile(r'^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)')
GLABEL_RE = re.compile(r'^\s*glabel\s+(\S+)')
ADDR_NAME_RE = re.compile(r'^func_[0-9A-F]{8}$')   # still an address name (not in tools_port/names.txt)


def load_hand_list(path):
    """{(segment, file): set of names or None for every pragma}"""
    hand = {}
    for ln, line in enumerate(open(path), 1):
        line = line.split('#', 1)[0].split()
        if not line:
            continue
        key = line[0]
        if '/' not in key or len(line) < 2:
            sys.exit('%s:%d: expected "<segment>/<file> * | name..."' % (path, ln))
        seg, f = key.split('/', 1)
        names = None if line[1:] == ['*'] else set(line[1:])
        hand[(seg, f)] = names
    return hand


def pragma_funcs(spath):
    """function names defined by a GLOBAL_ASM .s (its non-label glabels in .text)"""
    if not os.path.exists(spath):
        return [os.path.splitext(os.path.basename(spath))[0]]
    names = []
    section = '.text'
    for line in open(spath):
        s = line.strip()
        if s.startswith('.section'):
            section = s.split()[1]
            continue
        if s in ('.text', '.late_rodata', '.rodata', '.data', '.bss'):
            section = s
            continue
        m = GLABEL_RE.match(line)
        if m and section == '.text' and not LABEL_RE.match(m.group(1)):
            names.append(m.group(1))
    return names or [os.path.splitext(os.path.basename(spath))[0]]


def scan_source(cpath):
    """[(pragma path, has_nm_c)] for one .c file"""
    lines = open(cpath).read().split('\n')
    # conditional stack entries: [is_nm, nm_when_true, in_true_branch, nm_text, pragmas]
    stack = []
    out = []
    for line in lines:
        m = COND_RE.match(line)
        if m:
            d, rest = m.group(1), m.group(2).strip()
            if d in ('ifdef', 'ifndef', 'if'):
                is_nm = bool(re.match(r'^(defined\s*\(?\s*)?NON_MATCHING\b', rest)) or \
                    (d == 'if' and re.match(r'^defined\s*\(\s*NON_MATCHING\s*\)\s*$', rest) is not None)
                nm_when_true = d != 'ifndef'
                stack.append([is_nm, nm_when_true, True, [], []])
            elif d in ('else', 'elif') and stack:
                stack[-1][2] = False
            elif d == 'endif' and stack:
                is_nm, nm_true, _, nm_text, prags = stack.pop()
                text = '\n'.join(nm_text)
                for p in prags:
                    p['nm_text'].append(text)
            continue
        m = PRAGMA_RE.match(line)
        if m:
            entry = {'path': m.group(1), 'nm_text': []}
            for st in stack:
                is_nm, nm_true, in_true = st[0], st[1], st[2]
                if is_nm and nm_true != in_true:
                    st[4].append(entry)  # pragma on the matching-only side
            out.append(entry)
            continue
        for st in stack:
            if st[0] and st[1] == st[2]:
                st[3].append(line)
    res = []
    for e in out:
        res.append((e['path'], e['nm_text']))
    return res


def elf_funcs(elf):
    try:
        txt = subprocess.run(['mips-linux-gnu-readelf', '-sW', elf], capture_output=True, text=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit('decomp_status: cannot read %s (%s); run the matching build first' % (elf, e))
    names = set()
    for line in txt.split('\n'):
        f = line.split()
        if len(f) < 8 or f[3] != 'FUNC' or f[6] in ('UND', 'ABS'):
            continue
        if LABEL_RE.match(f[7]):
            continue
        names.add(f[7])
    return names


def defines(text, name):
    """does `text` define function `name` (a column-0 definition, ANSI or K&R)?"""
    return re.search(r'^[A-Za-z_][\w \t*]*?\b%s\s*\([^;{}]*\)\s*(\{|[A-Za-z_])' % re.escape(name), text,
                     re.M) is not None


SHARED_HEADERS = ['include/game/types.h', 'include/game/object.h']
UNNAMED_FIELD_RE = re.compile(r'^(unk|pad)[0-9A-Fa-f]*$')
# the other named-constant families: (header, (prefixes...))
CONST_HEADERS = (('include/game/types.h', ('VEH_', 'SURFACE_')),
                 ('include/game/object.h', ('OBJ_KIND_', 'OBJ_TOUGH_', 'DMG_RULE_', 'TRI_AXIS_', 'TRI_SIDE_')),
                 ('include/game/game.h', ('LEVEL_KIND_', 'YW_', 'YOSHI_DEMAND_', 'NO_YOSHI_')),
                 ('include/game/sound.h', ('EFFECT_KIND_',)))
NOT_CONSTS = ('VEH_STATE_SIZE',)   # a size, not one of the named values


def naming_counts():
    """the rename map's entries, the shared structs' fields (named / all) and the named constants"""
    funcs = data = 0
    for line in open('tools_port/names.txt'):
        s = line.split('#', 1)[0].strip()
        if not s or s.startswith(('field ', 'renamed ')) or '=' not in s:
            continue
        if s.startswith('func_'):
            funcs += 1
        else:
            data += 1
    structs = []   # (name, named fields, fields)
    for h in SHARED_HEADERS:
        if not os.path.exists(h):
            continue
        text = open(h).read()
        for m in re.finditer(r'typedef struct(?: \w+)? \{(.*?)\} (\w+);', text, re.S):
            body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
            fields = re.findall(r'(\w+)\s*(?:\[[^\]]*\])*\s*;', body)
            if fields:
                named = [f for f in fields if not UNNAMED_FIELD_RE.match(f)]
                structs.append((m.group(2), len(named), len(fields)))
    consts = {}
    for h, prefixes in (('include/game/game.h', ('MODE_',)), ('include/game/sound.h', ('SFX_', 'MUS_', 'SND_GROUP_')),
                        ('include/game/types.h', ('VS_',))) + CONST_HEADERS:
        if os.path.exists(h):
            for m in re.finditer(r'^#define ((?:%s)\w*)\s' % '|'.join(prefixes), open(h).read(), re.M):
                if m.group(1) in NOT_CONSTS:
                    continue
                p = next(x for x in prefixes if m.group(1).startswith(x))
                consts[p] = consts.get(p, 0) + 1
    return funcs, data, structs, consts


DATA_ADDR_RE = re.compile(r'(?<![\w$])D_[0-9A-F]{8}(?![\w$])')


def data_address_names():
    """the D_XXXXXXXX names still in the C sources and headers: (all, statics)"""
    left, statics = set(), set()
    for top in ('src.us.v11', 'include'):
        for d, _, fs in os.walk(top):
            for f in fs:
                if not f.endswith(('.c', '.h')):
                    continue
                for line in open(os.path.join(d, f), errors='replace'):
                    found = DATA_ADDR_RE.findall(line)
                    left.update(found)
                    if found and re.match(r'\s*static\b', line):
                        statics.update(n for n in found if re.search(r'\b%s\b\s*(?:N64_KEEP\s*)?(\[|=|;)' % n, line))
    return left, statics


def print_naming(markdown):
    funcs, data, structs, consts = naming_counts()
    named = sum(s[1] for s in structs)
    total = sum(s[2] for s in structs)
    left, statics = data_address_names()
    lines = ['rename map (tools_port/names.txt): %d functions, %d data symbols' % (funcs, data),
             'data named: %d; address names (D_XXXXXXXX) left in src/include: %d (%d of them static, kept by rule)'
             % (data, len(left), len(statics)),
             'shared structs (%s): %d of %d fields named, in %d structs' % (', '.join(SHARED_HEADERS), named, total,
                                                                          len(structs)),
             'named constants: ' + ', '.join('%s* %d' % (k, v) for k, v in sorted(consts.items()))]
    print()
    for l in lines:
        print(('- ' if markdown else '') + l)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-v', '--verbose', action='store_true',
                    help='list the hand-asm-without-C, near-miss and unmatched functions')
    ap.add_argument('--markdown', action='store_true', help='print a Markdown table')
    ap.add_argument('--naming', action='store_true',
                    help='also count the rename map, the shared structs\' named fields and the named constants')
    args = ap.parse_args()

    hand = load_hand_list('tools_port/hand_asm.txt')
    used_hand = set()
    problems = []
    rows = {}
    named = {}
    lists = {'hand': [], 'near': [], 'unmatched': []}
    for seg in SEGMENTS:
        counts = dict.fromkeys(CATS, 0)
        asm_names = set()
        for cpath in sorted(glob.glob('src.%s/%s/*.c' % (VERSION, seg))):
            f = os.path.splitext(os.path.basename(cpath))[0]
            hl = hand.get((seg, f), False)
            for spath, nm_texts in scan_source(cpath):
                for name in pragma_funcs(spath):
                    asm_names.add(name)
                    is_hand = hl is None or (hl and name in hl)
                    if is_hand:
                        used_hand.add((seg, f, name if hl else '*'))
                    has_nm = any(defines(t, name) for t in nm_texts)
                    if nm_texts and not has_nm:
                        problems.append('%s/%s: %s is under NON_MATCHING but no C defines it' % (seg, f, name))
                    cat = ('hand_nm' if has_nm else 'hand') if is_hand else ('near' if has_nm else 'unmatched')
                    counts[cat] += 1
                    if cat in lists:
                        lists[cat].append('%s/%s %s' % (seg, f, name))
        funcs = elf_funcs('build/%s.%s.elf' % (seg, VERSION))
        missing = asm_names - funcs
        if missing:
            problems.append('%s: GLOBAL_ASM functions missing from the ELF: %s' % (seg, ' '.join(sorted(missing))))
        counts['matched'] = len(funcs - asm_names)
        rows[seg] = counts
        named[seg] = len([n for n in funcs | asm_names if not ADDR_NAME_RE.match(n)])
    for (seg, f), names in hand.items():
        for n in (['*'] if names is None else names):
            if (seg, f, n) not in used_hand:
                problems.append('hand_asm.txt: %s/%s %s is not a GLOBAL_ASM function (matched? stale entry?)'
                                % (seg, f, n))

    total = dict.fromkeys(CATS, 0)
    for seg in SEGMENTS:
        for c in CATS:
            total[c] += rows[seg][c]
    rows['total'] = total
    named['total'] = sum(named[s] for s in SEGMENTS)

    def pct(r):
        ido = r['matched'] + r['near'] + r['unmatched']
        return 100.0 * r['matched'] / ido if ido else 100.0

    head = ['segment', 'functions'] + [CAT_TITLES[c] for c in CATS] + ['IDO C matched', 'named']
    body = []
    for seg in SEGMENTS + ['total']:
        r = rows[seg]
        n = sum(r.values())
        body.append([seg, str(n)] + [str(r[c]) for c in CATS] + ['%.1f%%' % pct(r),
                    '%d (%.0f%%)' % (named[seg], 100.0 * named[seg] / n if n else 0)])
    if args.markdown:
        print('| ' + ' | '.join(head) + ' |')
        print('|' + '|'.join(['---'] + ['---:'] * (len(head) - 1)) + '|')
        for b in body:
            print('| ' + ' | '.join(b) + ' |')
    else:
        w = [max(len(x[i]) for x in [head] + body) for i in range(len(head))]
        fmt = '  '.join('%-' + str(w[0]) + 's' if i == 0 else '%' + str(w[i]) + 's' for i in range(len(head)))
        print(fmt % tuple(head))
        print('  '.join('-' * x for x in w))
        for b in body:
            if b[0] == 'total':
                print('  '.join('-' * x for x in w))
            print(fmt % tuple(b))
    if args.naming:
        print_naming(args.markdown)
    if args.verbose:
        for c in ('hand', 'near', 'unmatched'):
            if lists[c]:
                print('\n%s:' % CAT_TITLES[c])
                for x in lists[c]:
                    print('  ' + x)
    for p in problems:
        print('warning: ' + p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
