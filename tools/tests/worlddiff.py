#!/usr/bin/env python3
"""Divergence detector: the game's own world on two machines, compared (docs/sync-architecture-plan.md L0).

`coop.census all <r>` (mods/coop/census.h) lists every placed game object of a game near its own player
(creatures with their net ids, Cell cells, and any other cGameData with a position: nests, eggs, fruit,
pickups, tools, buildings, vehicles, ...) and the count of every noun type. This module compares the
host's dump with a guest's, around the guest's own player (what the guest sees), and returns findings
with a stable signature, so many runs can be counted together (tools/detector_report.py).

  dump(con, r)                  -> World of one machine
  diff(host, guest)             -> [Finding] (empty: the two games agree in the area)
  SPORE_LANE=N python3 tools/tests/worlddiff.py [r]   diff of the lane's host and guest now

Findings are data for triage, not a gate (yet): a kind on one machine only, a count far apart, a field
that differs on matched objects (alive, scale, hp, model). Matching: by net id where the mod has one
(creature npc/proxy, Cell cell index), else by noun and the nearest position.
"""
import collections
import math
import re
import sys

NOUNS = {  # known noun types (mods/coop/*: k...Noun); unknown ones print as hex
    0x018EB45E: 'creature', 0x018EB4B7: 'citizen', 0x02A034CD: 'egg', 0x02C9CC91: 'fruit',
    0x01BE418E: 'herd', 0x52AA6122: 'nest', 0x0403DF5C: 'node', 0x1406A572: 'species_goal',
    0x018C6D19: 'tribe',
}

OBJ = re.compile(r'obj ([a-z])(-?\d+) key=([0-9A-F]{8}) alive=(-?\d+) hp=(\S+) ai=(-?\d+) own=(\S*) owner=(-?\d+) '
                 r'pos=\(([-\d.e+na]+) ([-\d.e+na]+) ([-\d.e+na]+)\)(?: noun=([0-9A-F]{8}) ser=(\d+) scale=(\S+))?')
HEAD = re.compile(r'census v1 role=(\S+) me=(\d+) mode=(.*?) modeid=([0-9A-F]+) world=([0-9A-F]+) t=([\d.]+)')
ME = re.compile(r'me pos=\(([-\d.e+na]+) ([-\d.e+na]+) ([-\d.e+na]+)\) ok=(\d)')

# compare area around the guest's own player (m) and the position tolerance of a match, per stage
AREA = {'Cell': 40.0, 'Creature': 70.0, 'Tribe': 90.0, 'Civilization': 160.0, 'Space': 120.0}
TOL_STATIC, TOL_MOVING = 3.0, 15.0
MOVING = {'creature', 'citizen', 'herd'}


def noun_name(n):
    return NOUNS.get(n, f'{n:08X}')


def _f(x):
    try:
        return float(x)
    except ValueError:
        return float('nan')


class World:
    def __init__(self, text, who=''):
        self.who, self.role, self.mode, self.modeid, self.world, self.t = who, None, None, None, None, 0.0
        self.me, self.rows, self.nouns, self.complete = None, [], {}, False
        self.vts = {}  # noun -> Ghidra address of a vtable of its class
        for line in text.splitlines():
            line = line.strip()
            m = HEAD.match(line)
            if m:
                self.role, self.mode, self.modeid, self.world, self.t = (m.group(1), m.group(3), m.group(4),
                                                                         m.group(5), float(m.group(6)))
                continue
            m = OBJ.match(line)
            if m:
                self.rows.append(dict(kind=m.group(1), id=int(m.group(2)), key=m.group(3), alive=int(m.group(4)),
                                      hp=_f(m.group(5)), ai=int(m.group(6)), own=m.group(7), owner=int(m.group(8)),
                                      pos=tuple(_f(m.group(i)) for i in (9, 10, 11)),
                                      noun=int(m.group(12), 16) if m.group(12) else 0,
                                      ser=int(m.group(13)) if m.group(13) else 0,
                                      scale=_f(m.group(14)) if m.group(14) else float('nan')))
                continue
            m = ME.match(line)
            if m:
                if m.group(4) == '1':
                    self.me = tuple(_f(m.group(i)) for i in (1, 2, 3))
                continue
            if line.startswith('nouns'):
                for kv in line.split()[1:]:
                    k, _, v = kv.partition('=')
                    v, _, vt = v.partition('@')
                    self.nouns[int(k, 16)] = self.nouns.get(int(k, 16), 0) + int(v)
                    if vt:
                        self.vts[int(k, 16)] = int(vt, 16)
                continue
            if line.startswith('end objects='):
                self.complete = int(line.split('=')[1]) == len(self.rows)


def dump(con, r=250):
    return World(con.call(f'coop.census all {r}')['out'])


class Finding(dict):
    """sig: stable signature (stage:what:kind), text: one line, n: objects involved"""

    def __str__(self):
        return f'{self["sig"]} n={self["n"]}: {self["text"]}'


def _near(p, c, r):
    return c is not None and all(map(math.isfinite, p)) and math.dist(p, c) <= r


def _fmt(p):
    return f'({p[0]:.0f} {p[1]:.0f} {p[2]:.0f})'


def _match(a, b, tol):
    """greedy nearest pairs of two row lists within tol: (pairs, unmatched a, unmatched b)"""
    cand = sorted((math.dist(x['pos'], y['pos']), i, j) for i, x in enumerate(a) for j, y in enumerate(b))
    ua, ub, pairs = set(range(len(a))), set(range(len(b))), []
    for d, i, j in cand:
        if d > tol:
            break
        if i in ua and j in ub:
            ua.discard(i)
            ub.discard(j)
            pairs.append((a[i], b[j]))
    return pairs, [a[i] for i in sorted(ua)], [b[j] for j in sorted(ub)]


def _rel(x, y):
    if not (math.isfinite(x) and math.isfinite(y)):
        return 0.0
    m = max(abs(x), abs(y), 1e-6)
    return abs(x - y) / m


def diff(host, guest, area=None):
    out = []
    stage = host.mode or '?'

    def add(what, kind, n, text):
        out.append(Finding(sig=f'{stage}:{what}:{kind}', stage=stage, n=n, text=text))

    if host.modeid != guest.modeid or host.world != guest.world:
        return [Finding(sig='apart', stage=stage, n=0, text=f'not in the same place: host {host.mode} {host.world}, '
                                                            f'guest {guest.mode} {guest.world}')]
    c = guest.me
    if c is None:
        return [Finding(sig='nocentre', stage=stage, n=0, text='the guest has no own player position')]
    r = area or AREA.get(stage, 70.0)
    inner = r * 0.8  # a host object near the edge may be just outside on the guest (dumps are not simultaneous)

    def rows(w, kind, own=None, lim=r):
        return [x for x in w.rows if x['kind'] == kind and (own is None or x['own'] in own) and _near(x['pos'], c, lim)]

    # creatures: host npc <-> guest proxy by net id
    for kind, hown, gown, label in (('c', ('npc',), ('proxy',), 'creature'), ('l', ('npc',), ('proxy',), 'cell')):
        h = {x['id']: x for x in rows(host, kind, hown, lim=r * 1.5)}
        g = {x['id']: x for x in rows(guest, kind, gown, lim=r * 1.5)}
        for i, x in h.items():
            if i not in g and _near(x['pos'], c, inner):
                add('host_only', label, 1, f'{label} {i} key={x["key"]} at {_fmt(x["pos"])} '
                                          f'{math.dist(x["pos"], c):.0f} m from the guest: the guest has no proxy')
        for i, y in g.items():
            if i not in h and _near(y['pos'], c, inner):
                add('guest_only', label, 1, f'{label} proxy {i} key={y["key"]} at {_fmt(y["pos"])}: the host has no such object here')
        for i in set(h) & set(g):
            x, y = h[i], g[i]
            if not (_near(x['pos'], c, r) or _near(y['pos'], c, r)):
                continue
            if x['alive'] != y['alive']:
                add('alive', label, 1, f'{label} {i}: alive={x["alive"]} on the host, {y["alive"]} on the guest')
            if _rel(x['scale'], y['scale']) > 0.12:
                add('scale', label, 1, f'{label} {i} key={x["key"]}: scale {x["scale"]:.2f} on the host, {y["scale"]:.2f} on the guest')
            if x['key'] != y['key']:
                add('model', label, 1, f'{label} {i}: model {x["key"]} on the host, {y["key"]} on the guest')
            if x['alive'] and y['alive'] and math.isfinite(x['hp']) and math.isfinite(y['hp']) and abs(x['hp'] - y['hp']) > max(2.0, 0.2 * max(x['hp'], y['hp'])):
                add('hp', label, 1, f'{label} {i}: hp {x["hp"]:.0f} on the host, {y["hp"]:.0f} on the guest')
            d = math.dist(x['pos'], y['pos'])
            if d > (TOL_MOVING if label == 'creature' else 6.0):
                add('pos', label, 1, f'{label} {i}: {d:.0f} m apart (host {_fmt(x["pos"])}, guest {_fmt(y["pos"])})')
    # objects without a net id on one machine: the guest's own (local) creatures and cells in the area, and
    # the host's local creatures (not tracked by the mod)
    for kind, label in (('c', 'creature'), ('l', 'cell')):
        hl = [x for x in rows(host, kind, ('local',)) if x['ai'] != 0]
        gl = [x for x in rows(guest, kind, ('local',)) if x['ai'] != 0]
        pairs, uh, ug = _match(hl, gl, TOL_MOVING)
        if ug:
            keys = collections.Counter(x['key'] for x in ug)
            add('guest_local', label, len(ug), f'{len(ug)} {label}(s) of the guest\'s own game near it, not on the host '
                                               f'(keys {", ".join(f"{k}x{v}" for k, v in keys.most_common(4))})')
        if uh:
            keys = collections.Counter(x['key'] for x in uh)
            add('host_local', label, len(uh), f'{len(uh)} {label}(s) of the host not tracked by the mod near the guest, '
                                              f'not on the guest (keys {", ".join(f"{k}x{v}" for k, v in keys.most_common(4))})')
    # every other placed object, per noun
    hg = collections.defaultdict(list)
    gg = collections.defaultdict(list)
    for x in rows(host, 'g', lim=r):
        hg[x['noun']].append(x)
    for x in rows(guest, 'g', lim=r):
        gg[x['noun']].append(x)
    for noun in sorted(set(hg) | set(gg)):
        name = noun_name(noun)
        vt = host.vts.get(noun) or guest.vts.get(noun)
        vts = f' [vtable 0x{vt:X}]' if vt and noun not in NOUNS else ''
        a, b = hg.get(noun, []), gg.get(noun, [])
        tol = TOL_MOVING if name in MOVING else TOL_STATIC
        pairs, ua, ub = _match(a, b, tol)
        ua = [x for x in ua if _near(x['pos'], c, inner)]
        ub = [x for x in ub if _near(x['pos'], c, inner)]
        if ua and not b:
            add('host_only', name, len(ua), f'{len(ua)} {name} near the guest on the host, none on the guest '
                                            f'(e.g. at {_fmt(ua[0]["pos"])}){vts}')
        elif ub and not a:
            add('guest_only', name, len(ub), f'{len(ub)} {name} on the guest, none on the host (e.g. at {_fmt(ub[0]["pos"])}){vts}')
        elif ua or ub:
            add('unmatched', name, len(ua) + len(ub), f'{name}: {len(a)} on the host, {len(b)} on the guest, '
                                                       f'{len(ua)} host / {len(ub)} guest without a partner within {tol:.0f} m{vts}')
        sc = [(x, y) for x, y in pairs if _rel(x['scale'], y['scale']) > 0.12]
        if sc:
            x, y = sc[0]
            add('scale', name, len(sc), f'{len(sc)} {name} with another scale (e.g. {x["scale"]:.2f} host, {y["scale"]:.2f} guest)')
    # the whole lists: noun types one game has and the other has none of (any distance)
    for noun in sorted(set(host.nouns) | set(guest.nouns)):
        h, g = host.nouns.get(noun, 0), guest.nouns.get(noun, 0)
        if (h == 0) != (g == 0) and max(h, g) >= 1:
            vt = host.vts.get(noun) or guest.vts.get(noun)
            add('noun_only_host' if g == 0 else 'noun_only_guest', noun_name(noun), max(h, g),
                f'{noun_name(noun)}: {h} in the host\'s game, {g} in the guest\'s (whole list)'
                + (f', vtable 0x{vt:X}' if vt else ''))
    return out


def main():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    sys.path.insert(0, os.path.dirname(here))
    from sporectl import Console  # noqa: E402
    import lane  # noqa: E402
    r = float(sys.argv[1]) if len(sys.argv) > 1 else 250
    h, g = Console(lane.HOST, timeout=20), Console(lane.GUEST, timeout=20)
    hw, gw = dump(h, r), dump(g, r)
    print(f'host {hw.mode} rows={len(hw.rows)} nouns={len(hw.nouns)}; guest {gw.mode} rows={len(gw.rows)} nouns={len(gw.nouns)}')
    for f in diff(hw, gw):
        print(f)


if __name__ == '__main__':
    main()
