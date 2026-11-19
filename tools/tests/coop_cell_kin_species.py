#!/usr/bin/env python3
"""BUG-1257 (WP-L1 of docs/plan-0.7.16.md): Cell: the wild kin near each player are of that player's own cell.

Cell fixture; the guest plays a cell of its own (its newest own cell, else a premade one chosen with
`coop.creations choose cell`). The kin are the cells of the players' species the population makes (6-8 per level,
docs/re/cell.md#cell-kin-species); the owner's decision of 2026-11-18: each player's kin swim near that player.
Read from the game's own cell objects (`coop.pool kin <r>`: every live creature whose model +0xFC is one of the
players' cells, with the nearest player, its resource and the AI's targets), on the host (its pool) and on the guest
(its mirrors of the host's pool):

  1. apart: the guest goes --dist m from the host (`coop.pool tp`), a fresh area the host populates for it. Within
     --r m of each player the kin are of that player's cell, in the host's pool AND among the guest's mirrors; each
     avatar is still its own cell (and each game draws the partner with the partner's cell). Key frames: the two
     places on the two screens, then both players at the guest's place, then both at the host's place.
  2. the AI of the guest's kin in the host's game: the same resource as the host's kin (the AI's data), and they
     move and pick targets as the host's kin do (sampled for 20 s, no command forces them).
  3. the host's save (a new slot, `coop.save`) holds no key of the guest's cell.
  4. --editor: the guest's own cell editor (its mating call, a part dragged onto the cell, Done): after it, the guest
     goes to another fresh area: the kin near it there are of its new cell, on both games.

  coop_cell_kin_species.py [--dist 120] [--r 35] [--editor]
FAIL twin: SPORE_FIX_OFF=cell.kin_species (every kin is the host's cell: none of the guest's near the guest).
"""
import math
import os
import re
import sys
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path)
import lane

PREMADE_CELL = 'BAF1AD40'  # a premade player cell in Spore_Content (coop_creations_cell.py)
PLAYER_RE = re.compile(r'^kin player (\d+)( \(me\))? cell=([0-9A-F]{8}) drawn=([0-9A-F]{8}) made=(\d+) pos=\(([-\d.]+) ([-\d.]+)\)', re.M)
CELL_RE = re.compile(r'^kin cell ([0-9A-F]{8}) model=([0-9A-F]{8}) mirror=(\d) near=(-?\d+) d=([\d.]+) res=([0-9A-F]{8}) '
                     r'flee=([0-9A-F]{8}) chase=([0-9A-F]{8}) pos=\(([-\d.]+) ([-\d.]+)\)', re.M)
HEAD_RE = re.compile(r'^kin fix=(\d) own=(\d+) partner=(\d+) no_key=(\d+) unknown=(\d+)', re.M)


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def kin(con, r=1e9):
    out = con.call(f'coop.pool kin {r:.0f}')['out']
    players = {int(m.group(1)): {'me': bool(m.group(2)), 'cell': m.group(3), 'drawn': m.group(4), 'made': int(m.group(5)),
                                 'pos': (float(m.group(6)), float(m.group(7)))} for m in PLAYER_RE.finditer(out)}
    cells = [{'idx': m.group(1), 'model': m.group(2), 'mirror': m.group(3) == '1', 'near': int(m.group(4)),
              'd': float(m.group(5)), 'res': m.group(6), 'flee': m.group(7) != '00000000', 'chase': m.group(8) != '00000000',
              'pos': (float(m.group(9)), float(m.group(10)))} for m in CELL_RE.finditer(out)]
    h = HEAD_RE.search(out)
    head = h.group(0) if h else out.splitlines()[0] if out else ''
    return players, cells, head


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    return int(m.group(1)) if m else 1


def counts(cells, who, r):
    """{model: n} of the kin within r m whose nearest player is `who` (a tuple: any of them)"""
    out = {}
    for c in cells:
        if c['near'] in (who if isinstance(who, tuple) else (who,)) and c['d'] <= r:
            out[c['model']] = out.get(c['model'], 0) + 1
    return out


def apart(host, guest, dx, dy, want):
    """the guest `dx dy` m from the host (`coop.pool tp`, again while a join's placement puts it back, BUG-0352)"""
    d, t0 = -1, time.time()
    while time.time() - t0 < 50:
        guest.call(f'coop.pool tp {dx:.0f} {dy:.0f}')
        time.sleep(4)
        hp, gp = kin(host, 0)[0], kin(guest, 0)[0]
        h = next((p['pos'] for p in hp.values() if p['me']), None)
        g = next((p['pos'] for p in gp.values() if p['me']), None)
        d = math.hypot(h[0] - g[0], h[1] - g[1]) if h and g else -1
        if d > want:
            return d
    return d


def place_check(ck, host, guest, gid, hkey, gkey, r, what):
    """the kin near each player are of that player's cell, on the host (pool) and on the guest (mirrors)"""
    hp, hc, hh = kin(host, r)
    gp, gc, gh = kin(guest, r)
    print(f'  {what}: host {hh} | guest {gh}', flush=True)
    for name, cells in (('host', hc), ('guest', gc)):
        print(f'    {name}: near the host {counts(cells, 0, r)}, near the guest {counts(cells, gid, r)}', flush=True)
    h_host, h_guest = counts(hc, 0, r), counts(hc, gid, r)
    g_host, g_guest = counts(gc, 0, r), counts(gc, gid, r)
    ck(h_guest.get(gkey, 0) >= 1 and h_guest.get(hkey, 0) == 0,
       f"{what}: the kin near the guest in the host's pool are of the guest's cell {gkey} ({h_guest})")
    ck(h_host.get(hkey, 0) >= 1 and h_host.get(gkey, 0) == 0,
       f"{what}: the kin near the host in the host's pool are of the host's cell {hkey} ({h_host})")
    ck(g_guest.get(gkey, 0) >= 1 and g_guest.get(hkey, 0) == 0,
       f"{what}: the guest's mirrors near the guest are of the guest's cell {gkey} ({g_guest})")
    if g_host:  # (the guest gets the host's cells around the host only while it sees them)
        ck(g_host.get(gkey, 0) == 0, f"{what}: the guest's mirrors near the host are not of the guest's cell ({g_host})")
    me_h = next((p for p in hp.values() if p['me']), None)
    me_g = next((p for p in gp.values() if p['me']), None)
    ck(me_h and me_h['drawn'] == hkey, f"{what}: the host's avatar is still its own cell ({me_h and me_h['drawn']})")
    ck(me_g and gp.get(gid, {}).get('cell') == gkey, f"{what}: the guest's own cell is still {gkey} in its game "
       f"(player key {gp.get(gid, {}).get('cell')}, avatar {me_g and me_g['drawn']})")
    ck(hp.get(gid, {}).get('drawn') == gkey, f"{what}: the host draws the guest with the guest's cell ({hp.get(gid, {}).get('drawn')})")
    if 0 in gp:
        ck(gp[0]['drawn'] == hkey, f"{what}: the guest draws the host with the host's cell ({gp[0]['drawn']})")
    return hc


def ai_check(ck, host, gid, hkey, gkey):
    """the guest's kin in the host's game behave as the host's: the same resource (the AI's data), moving and
    picking targets by themselves (sampled 20 s)"""
    seen = {}
    t0 = time.time()
    while time.time() - t0 < 20:
        for c in kin(host)[1]:
            s = seen.setdefault(c['idx'], {'model': c['model'], 'res': c['res'], 'p0': c['pos'], 'move': 0.0, 'target': False})
            s['move'] = max(s['move'], math.hypot(c['pos'][0] - s['p0'][0], c['pos'][1] - s['p0'][1]))
            s['target'] |= c['flee'] or c['chase']
        time.sleep(1)
    stats = {}
    for key in (hkey, gkey):
        cs = [s for s in seen.values() if s['model'] == key]
        stats[key] = {'n': len(cs), 'moved': sum(s['move'] > 1.0 for s in cs), 'targets': sum(s['target'] for s in cs),
                      'res': sorted({s['res'] for s in cs})}
        print(f'  AI of the kin of {key}: {stats[key]}', flush=True)
    h, g = stats[hkey], stats[gkey]
    ck(g['n'] >= 1 and set(g['res']) <= set(h['res']),
       f"the guest's kin in the host's game use the resource of the host's kin (the same AI data: {g['res']} in {h['res']})")
    ck(g['moved'] >= 1 if h['moved'] else True, f"the guest's kin swim by themselves as the host's do ({g['moved']} of {g['n']} "
       f"moved; host's {h['moved']} of {h['n']})")
    ck(g['targets'] >= 1 if h['targets'] else True, f"the guest's kin pick flee / chase targets as the host's do "
       f"({g['targets']} of {g['n']}; host's {h['targets']} of {h['n']})")


def keys_in_save(d, inst):
    """the records of the profile's newest save holding the key instance (big or little endian)"""
    import glob
    import part_substitutes as ps
    fs = sorted(glob.glob(os.path.join(d, 'Games', '*', '*.spo')), key=os.path.getmtime)
    if not fs:
        return ['no save']
    out = []
    for e in ps.entries(fs[-1]):
        data = ps.read(fs[-1], e)
        if inst.to_bytes(4, 'big') in data or inst.to_bytes(4, 'little') in data:
            out.append('%08X' % e['type'])
    return out


def own_cell(guest):
    out = guest.call('coop.creations')['out']
    m = re.search(r'mine cell: ([0-9A-F]{8})', out)
    return m.group(1) if m else '00000000'


def main():
    dist, r = float(arg('--dist', '120')), float(arg('--r', '35'))
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    for con in (host, guest):
        con.call('coop.pool invulnfor 900')
    gid = guest_id(guest)
    if own_cell(guest) == '00000000':
        print('  guest: no own cell, ' + guest.call(f'coop.creations choose cell {PREMADE_CELL}')['out'].strip(), flush=True)
        time.sleep(15)  # the guest's level rebuild hatches the cell again
    hkey = gkey = None
    t0 = time.time()
    while time.time() - t0 < 60:
        hp = kin(host, 0)[0]
        hkey = next((p['drawn'] for p in hp.values() if p['me']), None)
        gkey = hp.get(gid, {}).get('cell')
        if gkey and gkey != '00000000' and kin(guest, 0)[0].get(gid, {}).get('cell') == gkey and hkey != gkey:
            break
        time.sleep(2)
    print(f"  host's cell {hkey}, guest's cell {gkey} (host's game), guest's game {kin(guest, 0)[0].get(gid)}", flush=True)
    ck(gkey and gkey != '00000000' and hkey != gkey, f'the guest plays its own cell {gkey} (the host {hkey})')

    # 1. apart: a fresh area around the guest
    d = apart(host, guest, dist, 0, dist * 0.7)
    print(f'  the players are {d:.1f} m apart', flush=True)
    ck(d > dist * 0.7, f'the guest is far from the host ({d:.1f} m)')
    time.sleep(20)  # the host's partner pass populates the guest's area, the guest mirrors it
    place_check(ck, host, guest, gid, hkey, gkey, r, 'apart')
    keyframe('apart', f"Cell, the players {dist:.0f} m apart: the host's screen shows the host's cell with wild kin of the "
             f"host's cell {hkey} around it; the guest's screen shows the guest's cell {gkey} with wild kin of the guest's "
             "own cell (its look) around it")

    # 2. the AI of the guest's kin
    ai_check(ck, host, gid, hkey, gkey)

    # both screens at the guest's place, then both at the host's place
    hp0, gp0 = kin(host, 0)[0], kin(host, 0)[0][gid]
    h_at = next(p['pos'] for p in hp0.values() if p['me'])
    off = (h_at[0] - gp0['pos'][0], h_at[1] - gp0['pos'][1])
    host.call('coop.pool tp 6 0')
    time.sleep(6)
    hp, hc, _ = kin(host, r)
    near = counts(hc, (0, gid), r)
    print(f"  both at the guest's place: kin near the host now {near}", flush=True)
    ck(near.get(gkey, 0) >= 1, f"both players at the guest's place: the guest's kin are still there ({near})")
    keyframe('at_guest_place', f"Cell, both players at the guest's place: both screens show wild kin of the guest's cell {gkey} "
             "(the guest's look) around the two players")
    host.call(f'coop.pool tp {off[0]:.0f} {off[1]:.0f}')
    time.sleep(3)
    guest.call('coop.pool tp 6 0')
    time.sleep(8)
    gp, gc, _ = kin(guest, r)
    gnear = counts(gc, (0, gid), r)
    print(f"  both at the host's place: the guest's mirrors near it {gnear}", flush=True)
    ck(gnear.get(hkey, 0) >= 1 and gnear.get(gkey, 0) == 0,
       f"both players at the host's place: the guest's mirrors there are the host's kin ({gnear})")
    keyframe('at_host_place', f"Cell, both players at the host's place: both screens show wild kin of the host's cell {hkey} "
             "around the two players (none of the guest's look)")

    # 3. the host's save holds no key of the guest's cell
    out = host.call('coop.save')['out']
    print('  host:', out.strip(), flush=True)
    time.sleep(3)
    found = keys_in_save(lane.HOST_DIR, int(gkey, 16))
    ck('saved' in out and not found, f"the host's new save holds no key of the guest's cell {gkey} ({found})")

    # 4. the guest's own cell editor: the next kin near it are of its new cell
    if '--editor' in sys.argv:
        from coop_evo_screens import edit_cell, mating_call
        from harness import mode
        reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
        ck(reached, "the guest reaches its mate: its own cell editor opens")
        time.sleep(15)
        edit_cell(guest, 'Bobkin')
        back = False
        t0 = time.time()
        while time.time() - t0 < 40 and not back:
            back = 'Cell' in mode(guest)
            time.sleep(2)
        ck(back, 'the guest is back in its pool after its editor')
        nkey, t0 = None, time.time()
        while time.time() - t0 < 60:
            nkey = kin(host, 0)[0].get(gid, {}).get('cell')
            if nkey and nkey not in (gkey, '00000000') and kin(guest, 0)[0].get(gid, {}).get('cell') == nkey:
                break
            time.sleep(2)
        print(f"  the guest's new cell {nkey} (was {gkey})", flush=True)
        ck(nkey and nkey != gkey, f"the guest's new cell reached the host's game ({nkey})")
        guest.call('coop.pool invulnfor 900')
        d = apart(host, guest, -dist, dist * 0.5, dist * 0.7)
        ck(d > dist * 0.7, f'the guest is far from the host again, in another fresh area ({d:.1f} m)')
        time.sleep(20)
        place_check(ck, host, guest, gid, hkey, nkey, r, "after the guest's editor")
        keyframe('after_editor', f"Cell, after the guest's own cell editor, the players apart: the guest's screen shows wild "
                 f"kin of its NEW cell {nkey} around it; the host's screen its own kin of {hkey}")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
