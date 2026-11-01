#!/usr/bin/env python3
"""Generate the Creepy & Cute -> base game creature paint substitutes (BUG-0369).

A creature model (bem) names its three paint layers (base, coat, detail) by their skin effect ids
(the instances the paint props list in property 0xB0E066A7, one key per layer). The pack's 48 paint
props (type 0x00B1B104, group 0x406A0200 of bp1content/BoosterPack_01.package) use effects only the
pack has (compiled into its effect directories): a game without the pack cannot paint that layer.
For each pack effect of a layer this picks the effect of a base game creature paint of the same layer
(`skinpaint_base_*` / `skinpaint_coat_*` / `skinpaint_detail_*` of Data/Spore_Game.package and
Data/PatchData.package) whose default colour of that layer is the nearest, and whose effect source
(type 0x497767B9, group 0xEA5118B1) is in Data/Spore_Graphics.package; its id has as many 0xFE bytes
(a model writes 0xFE as two bytes: the substitute fits in place).
docs/re/creations.md#creations-paint-substitutes

  python3 -I tools/paint_substitutes.py <Spore install dir>   (prints the C++ table)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import part_substitutes as ps  # noqa: E402  (the DBPF / RefPack reader)

PROP, PAINT_GROUP = 0x00B1B104, 0x406A0200
EFFECTS, NAME, COLOURS = 0xB0E066A7, 0x00B2CCCA, 0xB0E066A8
EFFECT_SRC = (0x497767B9, 0xEA5118B1)
LAYERS = ('base', 'coat', 'detail')


def props(d):
    """{id: (type, value)}; arrays as raw bytes"""
    n = struct.unpack_from('>I', d, 0)[0]
    p, out = 4, {}
    for _ in range(n):
        pid, t, fl = struct.unpack_from('>IHH', d, p)
        p += 8
        if fl & 0x30:
            cnt, isz = struct.unpack_from('>II', d, p)
            p += 8
            out[pid] = (t, d[p:p + cnt * isz])
            p += cnt * isz
            continue
        if t in (0x12, 0x13):
            ln = struct.unpack_from('>I', d, p)[0]
            p += 4
            w = 2 if t == 0x13 else 1
            out[pid] = (t, d[p:p + ln * w].decode('utf-16le' if w == 2 else 'latin1', 'replace'))
            p += ln * w
            continue
        sz = ps.SIZES[t]
        out[pid] = (t, d[p:p + sz])
        p += sz
    return out


def paints(path):
    """{paint instance: (name, [effect per layer], [colour per layer])}"""
    out = {}
    for e in ps.entries(path):
        if e['type'] != PROP or e['group'] != PAINT_GROUP:
            continue
        try:
            pr = props(ps.read(path, e))
        except (struct.error, KeyError):
            continue
        if EFFECTS not in pr:
            continue
        raw = pr[EFFECTS][1]
        effs = [struct.unpack_from('<I', raw, 12 * k)[0] for k in range(len(raw) // 12)]
        cols = []
        if COLOURS in pr:
            c = pr[COLOURS][1]
            cols = [struct.unpack_from('<3f', c, 12 * k) for k in range(len(c) // 12)]
        out[e['inst']] = (pr.get(NAME, (0, ''))[1], effs, cols)
    return out


def fe(v):
    return sum(((v >> (8 * k)) & 0xFF) == 0xFE for k in range(4))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    cc = paints(os.path.join(root, 'bp1content', 'BoosterPack_01.package'))
    base = {}
    for p in ('Spore_Game.package', 'PatchData.package'):
        base.update(paints(os.path.join(root, 'Data', p)))
    have = {e['inst'] for e in ps.entries(os.path.join(root, 'Data', 'Spore_Graphics.package'))
            if (e['type'], e['group']) == EFFECT_SRC}
    cands = {k: [] for k in range(3)}  # layer -> [(colour, effect, name)]
    for name, effs, cols in base.values():
        for k, layer in enumerate(LAYERS):
            if name.startswith(f'skinpaint_{layer}_') and k < len(effs) and effs[k] in have:
                cands[k].append((cols[k] if k < len(cols) else (0.5, 0.5, 0.5), effs[k], name))
    pack_effects = set()
    for name, effs, cols in cc.values():
        for k, v in enumerate(effs[:3]):
            if v and v not in have:
                pack_effects.add((k, v, cols[k] if k < len(cols) else (0.5, 0.5, 0.5), name))
    table = {}
    for k, v, col, name in sorted(pack_effects, key=lambda x: (x[1], x[3])):
        if v in table:
            continue
        pool = [c for c in cands[k] if fe(c[1]) == fe(v)]
        best = min(pool, key=lambda c: sum((a - b) ** 2 for a, b in zip(c[0], col)))
        table[v] = (best[1], LAYERS[k], name, best[2])
    print(f'// {len(table)} pack skin effects (paint layers) and the base game effect shown instead')
    for v, (b, layer, name, bname) in sorted(table.items()):
        print(f'    {{0x{v:08X}, 0x{b:08X}}}, // {layer}: {name} -> {bname}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
