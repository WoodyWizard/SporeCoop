#!/usr/bin/env python3
"""BUG-1257 (WP-L1 of docs/plan-0.7.16.md): Cell: the wild kin near each player are of that player's own cell.

Cell fixture, the host's food raised to --food (924: level 10, a level with kin: 6-8 cells of the player's species
next to the level's own, docs/re/cell.md#cell-own-species; level 0 has none, lab 20261118-180414) while both play the
host's cell, then the guest plays a cell of its own (a premade one chosen with `coop.creations choose cell` when its
game plays the host's; its species line's Cell key, `coop.species key <id> cell`, is that cell on both games). The kin
are the cells of the players' species the population makes
(docs/re/cell.md#cell-kin-species); the owner's decision of 2026-11-18: each player's kin swim near that player.
Read from the game's own cell objects (`coop.pool kin <r>`: every live creature whose model +0xFC is one of the
players' cells, with the nearest player, its resource and the AI's targets), on the host (its pool) and on the guest
(its mirrors of the host's pool):

  1. apart: the guest goes --dist m from the host (`coop.pool tp`), a fresh area the host populates for it. Within
     --r m of each player the kin are of that player's cell, in the host's pool AND among the guest's mirrors; each
     avatar is still its own cell (and each game draws the partner with the partner's cell). Key frames: the two
     places on the two screens, each player next to its nearest own kin (the kin swim mostly off screen), then both
     players at the guest's place, then both at the host's place. The kin checks sample for up to 45 s.
  2. the AI of the guest's kin in the host's game: the same resource as the host's kin (the AI's data), and they
     move and pick targets as the host's kin do (sampled for 20 s, no command forces them).
  3. --editor: the guest's own cell editor (its mating call, a part dragged onto the cell, Done): after it, the guest
     goes to another fresh area: the kin near it there are of its new cell, on both games.
  4. the host's save (a new slot, `coop.save`) holds no key of the guest's new cell (a 0x7C...... key of the mod's
     own, after --editor; the premade cell's key is printed only: a premade's id may be in any save).

Distances are in view-box widths (--dist, --r): one width is the width of the game's own visible box
(Simulator::Cell::ViewBoxes, `box=` of `coop.pool kin`), 20 cell units at every level (docs/re/cell.md#cell-kin-test-units).
The first lab run (20261119-095414) took one width as 5 / world scale (G+0x514C): 15000 at level 10 made it 0.0003,
so the players were "apart" at 0.7 units and no kin was within the radius (a wrong test, not the mod).

  coop_cell_kin_species.py [--food 924] [--dist 6] [--r 1.75] [--editor]
FAIL twin: SPORE_FIX_OFF=cell.kin_species (every kin is the host's cell: none of the guest's near the guest).
(SPORE_FIX_OFF=species.line fails too: no game has a Cell line for the guest, step 0.)
"""
import math
import os
import re
import sys
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path)
import lane

# the Cell wizard's herbivore premade (docs/re/cell.md#cell-kin-test-units): its own cell editor saves. The first
# choice, BAF1AD40 (coop_creations_cell.py), never left the editor: Done did nothing (lab 20261119-095414, lane 10 probes)
PREMADE_CELL = 'E219E5D2'
PLAYER_RE = re.compile(r'^kin player (\d+)( \(me\))? cell=([0-9A-F]{8}) drawn=([0-9A-F]{8}) made=(\d+) pos=\(([-\d.]+) ([-\d.]+)\)', re.M)
CELL_RE = re.compile(r'^kin cell ([0-9A-F]{8}) model=([0-9A-F]{8}) mirror=(\d) near=(-?\d+) d=([\d.]+) res=([0-9A-F]{8}) '
                     r'flee=([0-9A-F]{8}) chase=([0-9A-F]{8}) pos=\(([-\d.]+) ([-\d.]+)\)', re.M)
HEAD_RE = re.compile(r'^kin fix=(\d) own=(\d+) partner=(\d+) no_key=(\d+) unknown=(\d+)', re.M)
BOX_RE = re.compile(r'^kin fix=.* box=([\d.]+) ([\d.]+)', re.M)


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def key_of(m):
    """the host's pool index of a `kin cell` line (the same cell on every game: a mirror's host index)"""
    k = re.search(r' key=([0-9A-F]{8})', m.string[m.end():m.string.find('\n', m.end())])
    return k.group(1) if k else m.group(1)


def kin(con, r=1e9):
    out = con.call(f'coop.pool kin {r:.3f}')['out']
    players = {int(m.group(1)): {'me': bool(m.group(2)), 'cell': m.group(3), 'drawn': m.group(4), 'made': int(m.group(5)),
                                 'pos': (float(m.group(6)), float(m.group(7)))} for m in PLAYER_RE.finditer(out)}
    cells = [{'idx': m.group(1), 'model': m.group(2), 'mirror': m.group(3) == '1', 'near': int(m.group(4)),
              'd': float(m.group(5)), 'res': m.group(6), 'flee': m.group(7) != '00000000', 'chase': m.group(8) != '00000000',
              'pos': (float(m.group(9)), float(m.group(10))), 'key': key_of(m)} for m in CELL_RE.finditer(out)]
    h = HEAD_RE.search(out)
    head = h.group(0) if h else out.splitlines()[0] if out else ''
    return players, cells, head


def line_cell(con, pid):
    """the Cell key of player pid's species line on this game when it is known there ('00000000': none)"""
    m = re.search(r'key=([0-9A-F]{8})/\S+ known=(\d)', con.call(f'coop.species key {pid} cell')['out'] or '')
    return m.group(1) if m and m.group(2) == '1' else '00000000'


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    return int(m.group(1)) if m else 1


def counts(cells, who, r, skip=frozenset()):
    """{model: n} of the kin within r m whose nearest player is `who` (a tuple: any of them), leaving out the cells whose
    host key is in `skip`"""
    out = {}
    for c in cells:
        if c['near'] in (who if isinstance(who, tuple) else (who,)) and c['d'] <= r and c['key'] not in skip:
            out[c['model']] = out.get(c['model'], 0) + 1
    return out


def on_screen(con, unit):
    """{model: n} of the kin inside this game's visible box around its own cell (20 x 15 at every level): what its key
    frame can show (the kin swim mostly 1-2 box widths from the player, off screen)"""
    ps, cs, _ = kin(con)
    me = next((p['pos'] for p in ps.values() if p['me']), None)
    out = {}
    if me:
        for c in cs:
            if abs(c['pos'][0] - me[0]) <= unit * 0.5 and abs(c['pos'][1] - me[1]) <= unit * 0.375:
                out[c['model']] = out.get(c['model'], 0) + 1
    return out


def screens(host, guest, unit):
    return f"(on screen now: host {on_screen(host, unit)}, guest {on_screen(guest, unit)})"


def close_to_kin(host, guest, gid, hkey, gkey, unit, tag, note):
    """a key frame with kin on the screens: the kin swim mostly off screen, so each player is put 0.15 view widths
    from its nearest kin of its own cell (`coop.pool tp`, relative to the partner), then both games are shot"""
    hp, hc, _ = kin(host)
    h = next((p['pos'] for p in hp.values() if p['me']), None)
    g = hp.get(gid, {}).get('pos')
    if not h or not g:
        return
    near = lambda at, key: min((c for c in hc if c['model'] == key and not c['mirror']),
                               key=lambda c: math.hypot(c['pos'][0] - at[0], c['pos'][1] - at[1]), default=None)
    kg, kh = near(g, gkey), near(h, hkey)
    off = unit * 0.15
    if kg:
        guest.call(f"coop.pool tp {kg['pos'][0] + off - h[0]:.2f} {kg['pos'][1] - h[1]:.2f}")
        time.sleep(2)
    if kh:
        g2 = kin(host)[0].get(gid, {}).get('pos') or g
        host.call(f"coop.pool tp {kh['pos'][0] + off - g2[0]:.2f} {kh['pos'][1] - g2[1]:.2f}")
    time.sleep(3)
    keyframe(tag, note + ' ' + screens(host, guest, unit))


def apart(host, guest, dx, dy, want):
    """the guest `dx dy` m from the host (`coop.pool tp`, again while a join's placement puts it back, BUG-0352)"""
    d, t0 = -1, time.time()
    while time.time() - t0 < 50:
        guest.call(f'coop.pool tp {dx:.2f} {dy:.2f}')
        time.sleep(4)
        hp, gp = kin(host, 0)[0], kin(guest, 0)[0]
        h = next((p['pos'] for p in hp.values() if p['me']), None)
        g = next((p['pos'] for p in gp.values() if p['me']), None)
        d = math.hypot(h[0] - g[0], h[1] - g[1]) if h and g else -1
        if d > want:
            return d
    return d


def kin_keys(con):
    """the host keys of every kin cell this game has now"""
    return frozenset(c['key'] for c in kin(con)[1])


def place_check(ck, host, guest, gid, hkey, gkey, r, what, secs=45, old=frozenset(), unit=0.0, apart_from=0.0):
    """the kin near each player are of that player's cell, on the host (pool) and on the guest (mirrors). The game makes
    6-8 kin a level over the whole populated range and culls them as tiles go (#cell-kin-test-units), so one sample can
    hold none near a player (lab 20261119-114512: none of the host's within 35 units after the guest's editor): sampled
    every 3 s for up to `secs` s (the host swims a little meanwhile); each player's own kin must be seen in some sample, the other player's kin in none.
    `old`: the kin alive when the players parted (host keys). Owner decision 2026-11-18: where the players are together
    the kin are a mix, and a wild cell does not follow a player, so a kin made while they were together may still swim
    near the host afterwards; there "none of the guest's" is asserted for the kin made since they parted (lab
    20261119-115019: two kin of the guest's cell made at the host's place, where the guest called its mate, were still
    there after it left)
    `apart_from` > 0: the players must stay that far apart while it samples; when the guest is closer to the host in a
    sample (a knocked-out guest respawns next to the host, BUG-1334), nothing is checked and 'lost' is returned (the
    caller parts them again)"""
    seen = {k: {} for k in ('h_host', 'h_guest', 'g_host', 'g_guest')}
    fresh = {k: {} for k in seen}  # the same without the `old` kin
    t0, n = time.time(), 0
    while True:
        hp, hc, hh = kin(host, r)
        gp, gc, gh = kin(guest, r)
        if apart_from > 0:
            me_at = next((p['pos'] for p in hp.values() if p['me']), None)
            g_at = hp.get(gid, {}).get('pos')
            gd = math.hypot(me_at[0] - g_at[0], me_at[1] - g_at[1]) if me_at and g_at else -1
            if 0 <= gd < apart_from:
                print(f"  {what}: the guest is {gd:.1f} m from the host after {time.time() - t0:.0f} s of sampling (knocked "
                      "out and respawned next to the host, see the guest's log 'ko: own cell state'): not an apart "
                      "sample, nothing checked", flush=True)
                return 'lost'
        now = {'h_host': counts(hc, 0, r), 'h_guest': counts(hc, gid, r), 'g_host': counts(gc, 0, r),
               'g_guest': counts(gc, gid, r)}
        # (only near the host: the guest's place is a fresh area, every kin there was made with the guest there)
        new = {'h_host': counts(hc, 0, r, old), 'h_guest': counts(hc, gid, r), 'g_host': counts(gc, 0, r, old),
               'g_guest': counts(gc, gid, r)}
        for k, v in now.items():
            for m, n in v.items():
                seen[k][m] = max(seen[k].get(m, 0), n)
        for k, v in new.items():
            for m, n in v.items():
                fresh[k][m] = max(fresh[k].get(m, 0), n)
        if (seen['h_guest'].get(gkey, 0) and seen['h_host'].get(hkey, 0) and seen['g_guest'].get(gkey, 0)) or \
                time.time() - t0 > secs:
            break
        n += 1
        if n % 3 == 0 and not seen['h_host'].get(hkey, 0) and unit:
            # the game makes kin only as new tiles enter the populated range: a player who stands still for a minute
            # has none left near it (lab 20261119-115626: none of the host's within 35 units for 31 s after the
            # guest's editor), so the host swims half a view width, back and forth, as a player does
            host.call(f"coop.pool goto {unit * 0.5 * (1 if n % 6 else -1):.2f} 0")
        time.sleep(3)
    print(f'  {what} ({time.time() - t0:.0f} s sampled): host {hh} | guest {gh}', flush=True)
    for name, k in (('host', 'h'), ('guest', 'g')):
        print(f"    {name}: near the host {seen[k + '_host']}, near the guest {seen[k + '_guest']} (most of each model in one "
              f"sample); made since they parted: near the host {fresh[k + '_host']}, near the guest {fresh[k + '_guest']}", flush=True)
    h_host, h_guest, g_host, g_guest = seen['h_host'], seen['h_guest'], seen['g_host'], seen['g_guest']
    f_host, f_guest, fg_host, fg_guest = fresh['h_host'], fresh['h_guest'], fresh['g_host'], fresh['g_guest']
    ck(h_guest.get(gkey, 0) >= 1 and f_guest.get(hkey, 0) == 0,
       f"{what}: the kin near the guest in the host's pool are of the guest's cell {gkey} ({h_guest}; made since: {f_guest})")
    ck(h_host.get(hkey, 0) >= 1 and f_host.get(gkey, 0) == 0,
       f"{what}: the kin near the host in the host's pool are of the host's cell {hkey} ({h_host}; made since: {f_host})")
    ck(g_guest.get(gkey, 0) >= 1 and fg_guest.get(hkey, 0) == 0,
       f"{what}: the guest's mirrors near the guest are of the guest's cell {gkey} ({g_guest}; made since: {fg_guest})")
    if g_host:  # (the guest gets the host's cells around the host only while it sees them)
        ck(fg_host.get(gkey, 0) == 0, f"{what}: the guest's mirrors near the host are not of the guest's cell ({g_host}; made since: {fg_host})")
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


def world_scale(con):
    m = re.search(r'scale=([-\d.e+]+)', con.call('coop.pool scalepos')['out'])
    return float(m.group(1)) if m else 0.0


def box_width(con):
    """the width of this game's visible box (Simulator::Cell::ViewBoxes), 0 when unknown"""
    m = BOX_RE.search(con.call('coop.pool kin 0')['out'])
    return float(m.group(1)) if m else 0.0


def main():
    dist_w, r_w, food = float(arg('--dist', '6')), float(arg('--r', '1.75')), int(arg('--food', '924'))
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    for con in (host, guest):
        con.call('coop.pool invulnfor 900')
    gid = guest_id(guest)
    for step in range(0, food, 100):  # level by level, as play does (coop_cell_own_level10.py)
        host.call(f'coop.pool food {min(100, food - step)}')
        time.sleep(3)
    time.sleep(8)
    hk0 = next((p['drawn'] for p in kin(host, 0)[0].values() if p['me']), None)
    gk0 = kin(host, 0)[0].get(gid, {}).get('cell')
    sc = world_scale(host)
    unit = box_width(host)
    ck(unit > 0, f"the host's game has its view box ({unit:.2f} wide)")
    unit = unit or 20.0
    dist, r = dist_w * unit, r_w * unit
    print(f"  food {food}: world scale {sc:.4g}, a box width {unit:.2f} m: apart {dist:.1f} m, near {r:.1f} m; the host's "
          f"pool {kin(host)[2]}; host's cell {hk0}, the guest's {gk0}", flush=True)
    if not gk0 or gk0 in ('00000000', hk0):  # the guest plays the host's cell (no own cell in its profile)
        print('  guest: ' + guest.call(f'coop.creations choose cell {PREMADE_CELL}')['out'].strip(), flush=True)
        time.sleep(15)  # the guest's level rebuild hatches the cell again
    hkey = gkey = None
    t0 = time.time()
    while time.time() - t0 < 60:
        hp = kin(host, 0)[0]
        hkey = next((p['drawn'] for p in hp.values() if p['me']), None)
        gkey = hp.get(gid, {}).get('cell')
        if gkey and gkey != '00000000' and kin(guest, 0)[0].get(gid, {}).get('cell') == gkey and hkey != gkey and \
                line_cell(host, gid) == gkey == line_cell(guest, gid):
            break
        time.sleep(2)
    print(f"  host's cell {hkey}, guest's cell {gkey} (host's game), guest's game {kin(guest, 0)[0].get(gid)}", flush=True)
    # again: the level ups and the guest's rebuild for its chosen cell hatch new avatars without it (lab 20261119-113757:
    # the guest was knocked out at level 10 and respawned next to the host before the both-at-the-guest's-place check)
    for con in (host, guest):
        con.call('coop.pool invulnfor 900')
    ck(gkey and gkey != '00000000' and hkey != gkey, f'the guest plays its own cell {gkey} (the host {hkey})')
    ck(line_cell(host, gid) == gkey and line_cell(guest, gid) == gkey,
       f"the guest's species line names that cell on both games (host {line_cell(host, gid)}, guest "
       f"{line_cell(guest, gid)}) and the host has none ({line_cell(host, 0)})")
    ck(line_cell(host, 0) == '00000000', "the host has no line (its kin are the game's own)")

    # 1. apart: a fresh area around the guest. BUG-1334: the guest alone in a fresh level-10 area is often knocked out
    # there (invulnfor does not stop a much bigger predator: Bite's lethal / swallow branch has no +0x111 check,
    # docs/re/cell.md) and respawns next to the host; samples taken then measure the host's place, not the guest's
    # (final check 20261119-214823: knocked out at s 84.6, just as the sampling began; every kin "near the guest" was
    # the host's). So a sampling that loses the guest parts the players again, at most twice; the last try is checked.
    for attempt in range(3):
        for con in (host, guest):
            con.call('coop.pool invulnfor 900')
        d = apart(host, guest, dist, 0, dist * 0.7)
        # the kin alive now were made while the guest was still near the host: after its rebuild for the chosen cell the
        # mod puts the guest back next to the host ("our new cell joined the host", tries 1 and 2, BUG-0352) while the
        # test moves it away (lab 20261119-120210: three kin of the guest's cell made then, near the host)
        time.sleep(4)
        d = apart(host, guest, dist, 0, dist * 0.7)
        old = kin_keys(host)
        print(f'  the players are {d:.1f} m apart (try {attempt + 1})', flush=True)
        time.sleep(20)  # the host's partner pass populates the guest's area, the guest mirrors it
        guard = dist * 0.5 if attempt < 2 and d > dist * 0.7 else 0.0
        if place_check(ck, host, guest, gid, hkey, gkey, r, 'apart', old=old, unit=unit,
                       apart_from=guard) != 'lost':
            break
    ck(d > dist * 0.7, f'the guest is far from the host ({d:.1f} m)')
    keyframe('apart', f"Cell, the players {dist:.1f} m ({dist_w:g} view widths) apart: the host's screen shows the host's cell with wild kin of the "
             f"host's cell {hkey} around it; the guest's screen shows the guest's cell {gkey} with wild kin of the guest's "
             "own cell (its look) around it" + ' ' + screens(host, guest, unit))

    close_to_kin(host, guest, gid, hkey, gkey, unit, 'apart_kin_close', f"Cell, apart, each player next to its nearest wild "
                 f"kin: the host's screen shows a kin of the host's cell {hkey} (the host's look) beside the host; the guest's screen "
                 f"a kin of the guest's cell {gkey} (the guest's look) beside the guest")

    # 2. the AI of the guest's kin
    ai_check(ck, host, gid, hkey, gkey)

    # both screens at the guest's place, then both at the host's place
    hp0, gp0 = kin(host, 0)[0], kin(host, 0)[0][gid]
    h_at = next(p['pos'] for p in hp0.values() if p['me'])
    off = (h_at[0] - gp0['pos'][0], h_at[1] - gp0['pos'][1])
    host.call(f'coop.pool tp {unit * 0.3:.2f} 0')
    time.sleep(6)
    hp, hc, _ = kin(host, r)
    near = counts(hc, (0, gid), r)
    print(f"  both at the guest's place: kin near the host now {near}", flush=True)
    ck(near.get(gkey, 0) >= 1, f"both players at the guest's place: the guest's kin are still there ({near})")
    keyframe('at_guest_place', f"Cell, both players at the guest's place: both screens show wild kin of the guest's cell {gkey} "
             "(the guest's look) around the two players" + ' ' + screens(host, guest, unit))
    host.call(f'coop.pool tp {off[0]:.2f} {off[1]:.2f}')
    time.sleep(3)
    # the host's place before the guest comes: the kin made since the players first parted (older ones may be from the
    # start, where both played together)
    before = {c['key']: c['model'] for c in kin(host, r)[1] if c['near'] == 0 and c['key'] not in old}
    guest.call(f'coop.pool tp {unit * 0.3:.2f} 0')
    time.sleep(8)
    gp, gc, _ = kin(guest, r)
    gnear = counts(gc, (0, gid), r)
    # the kin made there while the host was alone are the host's, and the guest draws them so; kin made since the guest
    # came may be either's (owner decision 2026-11-18: a mix where the players are together)
    kept = {c['key']: c['model'] for c in gc if c['key'] in before}
    print(f"  both at the host's place: the guest's mirrors near it {gnear}; of the kin there before the guest came "
          f"(host's pool {sorted(set(before.values()))}) the guest draws {len(kept)} as {sorted(set(kept.values()))}", flush=True)
    ck(gnear.get(hkey, 0) >= 1 and set(before.values()) <= {hkey} and set(kept.values()) <= {hkey},
       f"both players at the host's place: the kin there are the host's kin on both games ({gnear}; before the guest "
       f"came {sorted(set(before.values()))}, drawn by the guest as {sorted(set(kept.values()))})")
    keyframe('at_host_place', f"Cell, both players at the host's place: both screens show wild kin of the host's cell {hkey} "
             "around the two players (none of the guest's look)" + ' ' + screens(host, guest, unit))

    # 3. the guest's own cell editor: the next kin near it are of its new cell
    nkey = None
    if '--editor' in sys.argv:
        from coop_evo_screens import edit_cell, mating_call
        from harness import mode
        reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
        ck(reached, "the guest reaches its mate: its own cell editor opens")
        time.sleep(15)
        from twoinst import shot
        shot(guest, 'kin_editor_before.png')
        edit_cell(guest, 'Bobkin')
        time.sleep(3)
        shot(guest, 'kin_editor_done.png')
        back = False
        t0 = time.time()
        while time.time() - t0 < 40 and not back:
            back = 'Cell' in mode(guest)
            time.sleep(2)
        if not back:
            print('  guest ui after done: ' + guest.call('coop.uistate')['out'][:1500], flush=True)
        ck(back, 'the guest is back in its pool after its editor')
        nkey, t0 = None, time.time()
        while time.time() - t0 < 60:
            nkey = kin(host, 0)[0].get(gid, {}).get('cell')
            if nkey and nkey not in (gkey, '00000000') and kin(guest, 0)[0].get(gid, {}).get('cell') == nkey:
                break
            time.sleep(2)
        print(f"  the guest's new cell {nkey} (was {gkey})", flush=True)
        ck(nkey and nkey != gkey, f"the guest's new cell reached the host's game ({nkey})")
        t0 = time.time()
        while time.time() - t0 < 20 and line_cell(host, gid) != nkey:
            time.sleep(1)
        ck(line_cell(host, gid) == nkey, f"the host's line of the guest names its new cell ({line_cell(host, gid)})")
        guest.call('coop.pool invulnfor 900')
        d = apart(host, guest, -dist, dist * 0.5, dist * 0.7)
        time.sleep(4)
        d = apart(host, guest, -dist, dist * 0.5, dist * 0.7)
        old = kin_keys(host)  # the guest called its mate at the host's place: kin of its cells may swim there (a mix)
        ck(d > dist * 0.7, f'the guest is far from the host again, in another fresh area ({d:.1f} m)')
        time.sleep(20)
        place_check(ck, host, guest, gid, hkey, nkey, r, "after the guest's editor", old=old, unit=unit)
        keyframe('after_editor', f"Cell, after the guest's own cell editor, the players apart: the guest's screen shows wild "
                 f"kin of its NEW cell {nkey} around it; the host's screen its own kin of {hkey}" + ' ' + screens(host, guest, unit))
        close_to_kin(host, guest, gid, hkey, nkey, unit, 'after_editor_kin_close', "Cell, after the guest's editor, each "
                     f"player next to its nearest wild kin: the guest's screen shows a kin of its NEW cell {nkey} beside it, the "
                     f"host's screen a kin of {hkey}")
    # 4. the host's save holds no key of the guest's cell (the kin of the guest's cell are in the host's pool now)
    out = host.call('coop.save')['out']
    print('  host:', out.strip(), flush=True)
    time.sleep(3)
    ck('saved' in out, f'the host saved ({out.strip()})')
    print(f"  the premade / first cell {gkey} of the guest in the host's save: {keys_in_save(lane.HOST_DIR, int(gkey, 16))}", flush=True)
    if nkey and nkey.startswith('7C'):
        found = keys_in_save(lane.HOST_DIR, int(nkey, 16))
        ck(not found, f"the host's new save holds no key of the guest's own cell {nkey} ({found})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
