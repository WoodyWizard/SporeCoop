#!/usr/bin/env python3
"""Generate the Creepy & Cute -> base game creature part substitutes (BUG-0341 follow-up).

Reads the game's own files (read only): the part props (type 0x00B1B104, group 0x40626000) of
bp1content/BoosterPack_01.package and of Data/Spore_Graphics.package + Data/PatchData.package, and the
creature editor's palette pages. For every pack part it picks the base part of the same kind (the
same set of 0xB00F0FXX role properties: eye 0x0FE9, mouth 0x0FEC, grasper 0x0FE2, foot 0x0FEF, ...;
none for details) with the nearest ability levels (the 0x??C3E5XX int properties), the same cost
and editor ranks, and the most properties in common; its id has as many 0xFE bytes as the pack
part's (a model writes 0xFE as two bytes: the substitute fits in place).
docs/re/creations.md#creations-part-substitutes

  python3 -I tools/part_substitutes.py <Spore install dir>   (prints the C++ table)
"""
import pickle
import struct
import sys

def refpack(d):
    # Spore/EA RefPack (QFS)
    if len(d) < 5 or d[1] != 0xFB:
        return d
    flags = d[0]
    p = 2
    if flags & 0x80:
        size = int.from_bytes(d[p:p+4], 'big'); p += 4
    else:
        size = int.from_bytes(d[p:p+3], 'big'); p += 3
    out = bytearray()
    while p < len(d):
        b0 = d[p]
        if b0 < 0x80:
            b1 = d[p+1]; p += 2
            plain = b0 & 3; n = ((b0 & 0x1C) >> 2) + 3; off = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = d[p+1], d[p+2]; p += 3
            plain = (b1 & 0xC0) >> 6; n = (b0 & 0x3F) + 4; off = ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = d[p+1], d[p+2], d[p+3]; p += 4
            plain = b0 & 3; n = ((b0 & 0x0C) << 6) + b3 + 5; off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            plain = ((b0 & 0x1F) << 2) + 4; n = 0; off = 0; p += 1
        else:
            plain = b0 & 3; n = 0; off = 0; p += 1
            out += d[p:p+plain]; break
        out += d[p:p+plain]; p += plain
        for _ in range(n):
            out.append(out[-off])
    return bytes(out)

def entries(path):
    f = open(path, 'rb')
    h = f.read(96)
    assert h[:4] == b'DBPF', path
    n = struct.unpack_from('<I', h, 36)[0]
    isize = struct.unpack_from('<I', h, 44)[0]
    ioff = struct.unpack_from('<I', h, 64)[0]
    f.seek(ioff)
    ix = f.read(isize)
    flags = struct.unpack_from('<I', ix, 0)[0]
    p = 4
    const = {}
    for i, name in enumerate(('type', 'group', 'unk')):
        if flags & (1 << i):
            const[name] = struct.unpack_from('<I', ix, p)[0]; p += 4
    for _ in range(n):
        e = {}
        for i, name in enumerate(('type', 'group', 'unk')):
            if name in const:
                e[name] = const[name]
            else:
                e[name] = struct.unpack_from('<I', ix, p)[0]; p += 4
        e['inst'], e['off'], csize, e['size'] = struct.unpack_from('<IIII', ix, p); p += 16
        e['csize'] = csize & 0x7FFFFFFF
        e['comp'] = struct.unpack_from('<H', ix, p)[0]; p += 4
        yield e

def read(path, e):
    f = open(path, 'rb')
    f.seek(e['off'])
    d = f.read(e['csize'])
    return refpack(d) if e['comp'] == 0xFFFF else d
SIZES = {0x01: 1, 0x09: 4, 0x0A: 4, 0x0D: 4, 0x20: 16, 0x30: 8, 0x31: 16, 0x32: 16, 0x33: 16, 0x34: 16, 0x38: 56, 0x39: 24, 0x22: 0x208}
def parse(d):
    n = struct.unpack_from('>I', d, 0)[0]
    p = 4
    out = {}
    for _ in range(n):
        pid, t, fl = struct.unpack_from('>IHH', d, p); p += 8
        if fl & 0x30 or fl & 0x1C:
            cnt, isz = struct.unpack_from('>II', d, p); p += 8
            out[pid] = (t, 'arr', d[p:p + cnt * isz], cnt, isz); p += cnt * isz
            continue
        if t in (0x12, 0x13):
            ln = struct.unpack_from('>I', d, p)[0]; p += 4
            w = 2 if t == 0x13 else 1
            out[pid] = (t, d[p:p + ln * w]); p += ln * w
            continue
        sz = SIZES[t]
        raw = d[p:p + sz]; p += sz
        if t == 0x01: v = raw[0]
        elif t == 0x09: v = struct.unpack('>i', raw)[0]
        elif t == 0x0A: v = struct.unpack('>I', raw)[0]
        elif t == 0x0D: v = struct.unpack('>f', raw)[0]
        elif t == 0x20: v = struct.unpack('<III', raw[:12])
        else: v = raw
        out[pid] = (t, v)
    return out


def load(game):
    packs = [('cc', game + '/bp1content/BoosterPack_01.package'), ('base', game + '/Data/Spore_Graphics.package'),
             ('base', game + '/Data/PatchData.package')]
    rows, pages = {}, {}
    for src, path in packs:
        for e in entries(path):
            if e['type'] != 0x00B1B104:
                continue
            try:
                pr = parse(read(path, e))
            except Exception:
                continue
            if e['group'] == 0x40626000:
                rows[e['inst']] = (src, pr)
            elif 0xEEEAD734 in pr and len(pr[0xEEEAD734]) > 2:
                _, _, raw, cnt, isz = pr[0xEEEAD734]
                for i in range(cnt):
                    pages.setdefault(struct.unpack_from('<I', raw, i * isz)[0], set()).add(e['group'])
    return rows, pages


def kind(pr):
    return frozenset(k for k in pr if k & 0xFFFFFF00 == 0xB00F0F00)


def abil(pr):
    return {k: pr[k][1] for k in pr if k & 0x00FFFF00 == 0x00C3E500 and pr[k][0] == 0x09}


def iv(pr, k):
    v = pr.get(k)
    return v[1] if v and v[0] == 0x09 else None


def fe(i):
    return i.to_bytes(4, 'little').count(0xFE)


def best(c, pr, rows, pool):
    kc, ac = kind(pr), abil(pr)
    found = None
    for b in pool:
        bp = rows[b][1]
        if kind(bp) != kc or fe(b) != fe(c):
            continue
        ab = abil(bp)
        ids = set(ac) | set(ab)
        score = sum(abs(ac.get(k, 0) - ab.get(k, 0)) for k in ids) + 3 * len(set(ac) ^ set(ab))
        for k, w in ((0x3C652302, 1), (0x51FA9DFA, 1), (0x03A289AC, 2)):
            x, y = iv(pr, k), iv(bp, k)
            score += 0 if x == y else w * (abs(x - y) if x is not None and y is not None else 3)
        x, y = iv(pr, 0x02166464), iv(bp, 0x02166464)
        if x is not None and y is not None:
            score += abs(x - y) / 100
        score += 0.2 * len(set(pr) ^ set(bp))
        if found is None or score < found[0]:
            found = (score, b)
    return found


def main():
    rows, pages = load(sys.argv[1])
    pool = sorted(i for i, (s, _) in rows.items() if s == 'base' and pages.get(i, set()) != {0x406B6D0A})  # not cell parts
    print('// generated by tools/part_substitutes.py from the game files (pack part, base part, kind)')
    for c in sorted(i for i, (s, _) in rows.items() if s == 'cc'):
        pr = rows[c][1]
        f = best(c, pr, rows, pool)
        k = ','.join(sorted('%04X' % (x & 0xFFFF) for x in kind(pr))) or 'detail'
        if f:
            print('    {0x%08X, 0x%08X}, // %s' % (c, f[1], k))


if __name__ == '__main__':
    main()
