#!/usr/bin/env python3
"""BUG-0401: the creature parts a player finds are the party's (shared progress, like DNA): a part the
host finds is unlocked in every guest's game, a part a guest finds in the host's (and so in every
other guest's), and a rejoin keeps them all. Nothing is ever locked.

Checked in the games' own collected parts (cPlayer +0x10E8, `coop.parts`: the count of unlocked
unlockable parts and an order-free hash of their keys, the trace's parts= field) and in the guest's
own editor palette (`coop.editor`: the parts its palette shows locked, BUG-0368's machinery):
  1. the host finds a part the game's way (`coop.parts find <level>`: the creature stage's "a part was
     found" message 0x5371F11, a skeleton's reward; `unlock 2` if no level gives one): the guest's
     collected parts become the host's (count and hash) within 20 s;
  2. the guest finds one: the host gets it (both the same again);
  3. the guest's own editor (the host's vote) shows exactly the party's parts unlocked, and still
     locks the rest (BUG-0368: not every part);
  4. the guest leaves and joins again (the host's world is saved and loaded again): the guest still
     has every part of the party.

--from-cell (BUG-0441, suite `parts_transition`): the two players start in the `cell` fixture and the host
changes Cell -> Creature the players' way (coop_campaign.py Campaign.advance: the food bar, History and
its check, the guest's real "I'm ready", the editors left with "done"); first
  0. within 10 s of the guest playing Creature its collected parts are the host's (count and hash); the
     live start from Cell added 12 creature twins of Cell parts to the host's unlockable map
     (docs/re/creations.md#creations-cell-twins), the guest's loaded world has them only in its item
     states;
then 1-4 as above, and in 3 the guest's own editor offers exactly the host's parts (the palette's check,
the same hash).

Runs that must FAIL: SPORE_FIX_OFF=parts.shared (each game keeps its own parts: 1 and 2 time out, the
rejoin loses the guest's find); --from-cell with SPORE_FIX_OFF=parts.census_states (0: the guest counts 6
parts fewer, the host's twins)."""
import argparse
import random
import re
import time
from types import SimpleNamespace

import lane
from coop_ui import Ui
from twoinst import Checks, shot, start, wait_guest_in

DONE = (1416, 1062)    # the green "done" check of the creature editor (1440x1080 test window)


def parts(con):
    out = con.call('coop.parts')['out']
    m = re.search(r'parts=(\d+):([0-9A-F]{8}) unlockable=(\d+)', out)
    return (int(m.group(1)), m.group(2), int(m.group(3))) if m else (None, None, None)


def sync_line(con):
    m = re.search(r'^sync: (.*)$', con.call('coop.parts')['out'], re.M)
    return m.group(1) if m else '?'


def find(con, who):
    """the game's own 'a part was found'; falls back to UnlockPart when no level gives a part"""
    for level in (1, 2, 3, 4, 5, -1):
        out = con.call(f'coop.parts find {level}')['out']
        m = re.search(r'unlocked_now=(-?\d+)', out)
        if m and int(m.group(1)) > 0:
            print(f'  {who} found {m.group(1)} part(s) the game\'s way (level {level}): {out.splitlines()[0]}', flush=True)
            return 'find'
    out = con.call('coop.parts unlock 2')['out']
    print(f'  {who}: no level gave a part, UnlockPart instead: {out.splitlines()[0]}', flush=True)
    return 'unlock'


def wait_same(a, b, t=20):
    t0 = time.time()
    pa = pb = None
    while time.time() - t0 < t:
        pa, pb = parts(a), parts(b)
        if pa[0] is not None and pa[:2] == pb[:2]:
            return True, pa, pb, time.time() - t0
        time.sleep(1)
    return False, pa, pb, time.time() - t0


def mode(con):
    return con.call('game.mode')['out']


def editor_locked(con):
    out = con.call('coop.editor')['out'].strip()
    m = re.search(r'unlockable (\d+) locked (\d+)', out)
    return out, (int(m.group(1)), int(m.group(2))) if m else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


CAPTION_OK = (1410, 1048)  # the green check of a caption that waits for the player (coop_campaign.py)


def wait_no_cinematic(host, guest, t=90):
    """a vote while the host's game plays a cinematic opens an editor that never draws (BUG-0404, fixed on
    fix/w34-vote-cinematic). After Cell -> Creature the host's game shows the stage's intro (cinematic
    0A34E41F, "It is the beginning of a new day...") and waits for its green check; a player reads it and
    clicks it (after 6 s, then every 10 s, as coop_campaign.py does) before starting the next vote"""
    t0 = time.time()
    last = 0.0
    while time.time() - t0 < t:
        playing = [(con, c) for con, c in ((host, cinematic(host)), (guest, cinematic(guest))) if c]
        if not playing:
            return True
        if time.time() - t0 >= 6 and time.time() - last >= 10:
            last = time.time()
            for con, c in playing:
                print(f'  cinematic {c} plays: its check clicked like a player', flush=True)
                con.call(f'move {CAPTION_OK[0]} {CAPTION_OK[1]}')
                time.sleep(0.4)
                con.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')
        time.sleep(2)
    print(f'  (a cinematic still plays after {t} s: {cinematic(host)} / {cinematic(guest)})', flush=True)
    return False


def editor_offers(out):
    """the parts the open editor's palette offers (its check 0x5C6820 over every item): count, hash"""
    m = re.search(r'available=(\d+):([0-9A-F]{8})', out)
    return (int(m.group(1)), m.group(2)) if m else (None, None)


# BUG-0446: Cell part index -> its creature twin's instance (tables 0x1483DD8 {part, cell item} and
# 0x1485FE8 {cell item, creature twin}, docs/re/creations.md#creations-cell-twins; part 11 has three)
CELL_TWINS = {2: ['FE534766'], 3: ['3A3C49A5'], 4: ['32AC9620'], 5: ['B70894B6'], 6: ['4908E576'],
              7: ['C5744537'], 8: ['1610AD94'], 9: ['DEF1C7FF'], 10: ['30E66572'],
              11: ['02B3EDDB', '7E262CF0', 'BE830BDE']}


def cell_parts(con):
    m = re.search(r'cell parts=(\d+)', con.call('coop.parts cell')['out'])
    return [int(c) for c in m.group(1)] if m else None


def twin_unlocked(con, inst):
    """the creature twin is unlocked in the game's own item states (IsUnlocked: the 'u')"""
    return re.search(r'[0-9A-F]{8}:' + inst + r'=[0-9A-F]+u', con.call('coop.parts states')['out']) is not None or \
        re.search(inst + r':[0-9A-F]{8}=[0-9A-F]+u', con.call('coop.parts states')['out']) is not None


def guest_cell_part(ck, host, guest):
    """BUG-0446: the guest collects a Cell part (with a creature twin) the host's cell has not; the
    host's game gets it (parts.shared_cell). Returns the part index."""
    t0 = time.time()
    hp = gp = None
    while time.time() - t0 < 30 and not (hp and gp):
        hp, gp = cell_parts(host), cell_parts(guest)
        time.sleep(1)
    print(f'  Cell parts: host {hp}, guest {gp}', flush=True)
    free = [i for i in sorted(CELL_TWINS) if hp and gp and hp[i] == 0 and gp[i] == 0]
    ck(free, 'a Cell part with a creature twin neither game has')
    if not free:
        return None
    i = free[0]
    print(f'  guest: {guest.call(f"coop.parts cell collect {i}")["out"].strip()}', flush=True)
    t0 = time.time()
    while time.time() - t0 < 20 and not (cell_parts(host) or [0] * 13)[i]:
        time.sleep(1)
    print(f'  Cell parts after the guest\'s collect ({time.time() - t0:.0f} s): host {cell_parts(host)}, guest {cell_parts(guest)}', flush=True)
    return i


def from_cell(ck, cell_part=False):
    """BUG-0441: Cell -> Creature the players' way (as transition_net), then the parts of both games"""
    from coop_campaign import Campaign, Player
    host, guest = start('cell')
    part = guest_cell_part(ck, host, guest) if cell_part else None
    players = [Player('Alice', lane.HOST), Player('Bob', lane.GUEST, lane.JOIN)]
    seed = random.randrange(1 << 30)
    ca = SimpleNamespace(start='cell', end='creature', minutes=0, advance_minutes=None, players=2, noise=0.0,
                         rejoins=False, finale=False, finale_from_galaxy=False, jump=False, seed=seed)
    camp = Campaign(ca, players, lane.out_dir('parts_transition', time.strftime('%Y%m%d-%H%M%S')), seed)
    camp.ids()
    t0 = time.time()
    ok = camp.advance('cell')
    ck(ok, f"the host changed Cell -> Creature the players' way and the guest followed ({time.time() - t0:.0f} s)")
    ck('Creature' in mode(guest) and guest.call('coop.avatar')['ok'], f"the guest plays Creature with an avatar ({mode(guest)})")
    ok, ph, pg, dt = wait_same(host, guest, 10)
    print(f'  after the transition ({dt:.0f} s): host {ph}, guest {pg} | guest sync: {sync_line(guest)}', flush=True)
    ck(ok, f"within 10 s of the transition the guest's collected parts are the host's (guest {pg[0]}:{pg[1]} of {pg[2]}, "
           f"host {ph[0]}:{ph[1]} of {ph[2]})")
    for it in camp.F.items:
        if it['kind'] != 'info':
            print(f'    finding [{it["kind"]}] {it["stage"]}: {it["text"]}', flush=True)
    ck(not [it for it in camp.F.items if it['kind'] != 'info'], 'no campaign finding (dead game, crash, freeze, fault)')
    if part is not None:  # BUG-0446: the guest's Cell part gave its creature twin, in both games
        for inst in CELL_TWINS[part]:
            th, tg = twin_unlocked(host, inst), twin_unlocked(guest, inst)
            print(f'  twin {inst} of the guest\'s Cell part {part}: host {th}, guest {tg}', flush=True)
            ck(th, f"the host's Creature game unlocked the twin {inst} of the Cell part the guest collected")
            ck(tg, f"the guest's Creature game unlocked the twin {inst} of the Cell part it collected")
    return host, guest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--from-cell', action='store_true', help='start in Cell, change to Creature the players\' way (BUG-0441)')
    ap.add_argument('--guest-cell-part', action='store_true',
                    help='with --from-cell: the guest collects a Cell part first, its twin is checked, then stop (BUG-0446)')
    a = ap.parse_args()
    ck = Checks()
    if a.from_cell:
        host, guest = from_cell(ck, a.guest_cell_part)
        if a.guest_cell_part:
            ck.faults(host=host, guest=guest)
            return ck.result()
    else:
        host, guest = start('creature')
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host world')
    h0, g0 = parts(host), parts(guest)
    print(f'  start: host {h0}, guest {g0}', flush=True)
    ck(h0[0] is not None and g0[0] is not None, 'both games have collected parts')
    ok, _, _, _ = wait_same(host, guest, 15)
    ck(ok, f'both start with the same parts (host {parts(host)}, guest {parts(guest)})')

    # 1. the host finds a part
    find(host, 'host')
    h1 = parts(host)
    ck(h1[0] > h0[0], f'the host has more parts now ({h0[0]} -> {h1[0]})')
    ok, ph, pg, dt = wait_same(host, guest)
    print(f'  after {dt:.0f} s: host {ph}, guest {pg} | guest sync: {sync_line(guest)}', flush=True)
    ck(ok, f"the guest's game has the parts the host found (guest {pg[0]}:{pg[1]}, host {ph[0]}:{ph[1]})")

    # 2. the guest finds a part
    g_before = parts(guest)
    find(guest, 'guest')
    g2 = parts(guest)
    ck(g2[0] > g_before[0], f'the guest has more parts now ({g_before[0]} -> {g2[0]})')
    ok, ph, pg, dt = wait_same(host, guest)
    print(f'  after {dt:.0f} s: host {ph}, guest {pg} | host sync: {sync_line(host)}', flush=True)
    ck(ok and ph[0] >= g2[0], f"the host's game has the parts the guest found (host {ph[0]}:{ph[1]}, guest {pg[0]}:{pg[1]})")
    # the party's parts: the host's after its find plus the guest's find (a random locked part: the same
    # part as the host's is a 1 in ~250 chance)
    union = h1[0] + (g2[0] - g_before[0])
    print(f'  the party has {union} parts', flush=True)

    # 3. the guest's own editor offers the party's parts, and locks the rest (BUG-0368)
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    party = parts(host)  # the party's set (the host's game holds it) before the editors
    wait_no_cinematic(host, guest)
    print(' ', host.call('coop.vote editor')['out'].strip())
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and (mode(host) == m0 or mode(guest) == m0):
        time.sleep(1)
    ck(mode(guest) != m0, f"the guest's own editor opened ({mode(guest)})")
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    shot(guest, 'parts_shared_guest_editor.png')
    out, lk = editor_locked(guest)
    print('  guest editor:', out, flush=True)
    ck(lk is not None, f"the guest's open editor is found ({out[:100]})")
    if lk:
        unlockable, locked = lk
        ck(locked == unlockable - union,
           f"the guest's palette offers the party's {union} parts: {unlockable - locked} of {unlockable} available")
        ck(locked > 0, f'the guest\'s palette still locks the parts nobody found ({locked}, BUG-0368)')
        off = editor_offers(out)
        ck(off == party[:2], f"the guest's palette offers the host's parts, the same ones ({off[0]}:{off[1]}, host "
                             f"{party[0]}:{party[1]})")
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != m0:
        time.sleep(1)
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != m0:
        time.sleep(1)
    ck(mode(guest) == m0 and mode(host) == m0, f'both left the editor ({mode(host)} / {mode(guest)})')
    time.sleep(3)

    # 4. a rejoin: the host's world is saved and loaded by the guest again
    print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), "the guest is back in the host's world")
    time.sleep(20)
    wait_guest_in(host, guest)
    ok, ph, pg, dt = wait_same(host, guest, 40)
    print(f'  after the rejoin: host {ph}, guest {pg} | guest sync: {sync_line(guest)}', flush=True)
    ck(ok and pg[0] is not None and pg[0] >= union,
       f"after the rejoin the guest has every part of the party ({pg[0]} of {union}, host {ph[0]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
