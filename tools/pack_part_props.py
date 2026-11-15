#!/usr/bin/env python3
"""BUG-0729: the part property lists (type 0x00B1B104) of the packs against the base game's: the properties only pack
parts have, and the key-typed properties of the pack parts (an animation or effect bound to a pack part would show
here). Read only.

  python3 -I tools/pack_part_props.py <Spore install dir>
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import part_substitutes as ps  # noqa: E402

G = sys.argv[1] if len(sys.argv) > 1 else '.'
srcs = [('cc', G + '/bp1content/BoosterPack_01.package'), ('p03', G + '/Data/Spore_Pack_03.package'),
        ('base', G + '/Data/Spore_Graphics.package'), ('base', G + '/Data/PatchData.package'), ('base', G + '/Data/Spore_Game.package')]
parts = collections.defaultdict(dict)  # group -> inst -> (src, props)
for src, path in srcs:
    try:
        es = ps.entries(path)
    except Exception as ex:
        print('skip', path, ex); continue
    for e in es:
        if e['type'] != 0x00B1B104 or e['group'] not in (0x40626000, 0x40616000, 0x40646000, 0x40636000):
            continue
        try:
            pr = ps.parse(ps.read(path, e))
        except Exception:
            continue
        parts[e['group']].setdefault(e['inst'], (src, pr))
for g, rows in parts.items():
    base = collections.Counter(); pack = collections.Counter(); ex = {}
    for inst, (src, pr) in rows.items():
        for k, v in pr.items():
            (base if src == 'base' else pack)[k] += 1
            if src != 'base':
                ex.setdefault(k, (inst, v))
    nb = sum(1 for s, _ in rows.values() if s == 'base'); npk = len(rows) - nb
    print(f'group {g:08X}: {nb} base parts, {npk} pack parts')
    for k, n in sorted(pack.items()):
        if base[k] == 0:
            inst, v = ex[k]
            print(f'   only in pack parts: prop {k:08X} type {v[0]:#x} in {n} parts, e.g. {inst:08X}: {str(v[1:])[:120]}')
print('--- 13C6A371 values')
rows = parts[0x40626000]
for inst, (src, pr) in rows.items():
    if 0x13C6A371 in pr:
        v = pr[0x13C6A371][1]
        k = v[0] if isinstance(v, tuple) and v and isinstance(v[0], int) else v
        ref = v[0] if isinstance(v, tuple) else None
        print(f'  {src} {inst:08X} -> {v} ref_is_part={ref in rows if ref else None} ref_src={rows[ref][0] if ref in rows else "-"}')
# key-typed props of pack parts overall
print('--- key props (type 0x20) in pack parts')
c = collections.Counter()
for inst, (src, pr) in rows.items():
    if src != 'base':
        for k, v in pr.items():
            if v[0] == 0x20:
                c[k] += 1
for k, n in c.items():
    print(f'  {k:08X} in {n} pack parts; in base parts: {sum(1 for s, p in rows.values() if s == "base" and k in p)}')
