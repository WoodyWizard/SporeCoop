#!/usr/bin/env python3
"""Creature stage: the mating call leads to the creature editor through the co-op vote, with real input (BUG-0177,
docs/re/creature.md#creature-mating-call).

The game's drama manager picks the mate itself near the nest (every 5 s: a family member within 15 m of the nest,
marked +0xB58 & 0x10, `coop.drama mate`); the mating call (Backspace) only shows hearts. A left click on the mate
(cursor kind 9) makes the avatar walk to it and mate (BT node 02D852F0, the mate 91BFFC22), the CRG_LaidEgg
cinematic plays, and the game's mating editor request (0xD3C650, tag 0x12191CA) goes to the co-op vote.

1. Two games on the creature fixture (the host at its nest). The host's game names a mate (`coop.drama mate`).
2. The host presses Backspace (the call), then left-clicks the mate where its game draws it (`coop.targets all`):
   the avatar goes to mate (its node 02D852F0).
3. The host's game asks for the editor: the vote "wants to evolve (Creature)" on the host and on the guest's
   screen; the guest is ready; the host's editor opens (game.mode leaves the Creature stage), the guest's own too.
4. Both leave their editors with "done": both games are back in the Creature stage.

  coop_creature_mating.py   (MATING_CLICK=other: a click on another member of the family instead: must FAIL)"""
import os
import re
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

W, H = 1440, 1080
MATE_NODE = '02D852F0'


def mate(con):
    m = re.search(r'mate ([0-9A-Fa-fx]+) flags=([0-9A-F]+) node=([0-9A-F]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) \| '
                  r'avatar node=([0-9A-F]+) eggs=(-?\d+)', con.call('coop.drama mate')['out'])
    if not m:
        return None
    p = int(m.group(1), 16)
    return dict(ptr=p, flags=int(m.group(2), 16), node=m.group(3), av_node=m.group(7), eggs=int(m.group(8)))


def npc_ids(con):
    """host: {ptr: (id, species, dist)}"""
    return {int(m.group(2), 16): (m.group(1), m.group(3), float(m.group(4))) for m in
            re.finditer(r'#(\d+) ([0-9A-F]{8}) species=([0-9A-F]+) hp=[\d.]+ dist=([\d.]+)', con.call('coop.npc list')['out'])}


def screen_of(con, nid):
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + nid and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
            return int(f[3]), int(f[4])
    return None


def mode(con):
    return con.call('game.mode')['out'].strip()


def main():
    host, guest = start('creature')
    ck = Checks()
    hu, gu = Ui(host, 'host'), Ui(guest, 'guest')
    other = os.environ.get('MATING_CLICK') == 'other'
    m0 = mode(host)

    # 1. the game picks the mate near the nest (every 5 s)
    mt = None
    t0 = time.time()
    while time.time() - t0 < 30:
        mt = mate(host)
        if mt and mt['ptr'] and mt['flags'] & 0x10:
            break
        time.sleep(1)
    print(f'  mate: {host.call("coop.drama mate")["out"].strip()}', flush=True)
    ck(mt and mt['ptr'] and mt['flags'] & 0x10, 'the host\'s game picked a mate near the nest')
    ids = npc_ids(host)
    me = ids.get(mt['ptr']) if mt else None
    ck(me is not None, f'the mate is one of the host\'s NPCs ({me})')
    if not me:
        ck.faults(host=host, guest=guest)
        return ck.result()
    nid = me[0]
    if other:  # the twin: another adult of the family
        fam = [v for p, v in ids.items() if v[1] == me[1] and p != mt['ptr']]
        print(f'  MATING_CLICK=other: clicking #{fam[0][0] if fam else None} instead of the mate #{nid}', flush=True)
        nid = fam[0][0] if fam else nid

    # 2. the call, then a click on the mate
    host.call('coop.npc gonpc %s 5' % nid)
    time.sleep(1.5)
    host.call('key 0x08')  # Backspace: the mating call (hearts only)
    time.sleep(3)
    going = False
    for i in range(6):
        xy = screen_of(host, nid)
        if not xy:
            host.call(f'drag {W // 2} {H // 2} {W // 2 + 350} {H // 2} right')
            time.sleep(0.5)
            continue
        host.call(f'click {xy[0]} {xy[1]}')
        t1 = time.time()
        while time.time() - t1 < 3 and not going:
            m = mate(host)
            going = bool(m and m['av_node'] == MATE_NODE)
            time.sleep(0.3)
        print(f'  click {i + 1} at {xy}: avatar node {mate(host)["av_node"] if mate(host) else None}', flush=True)
        if going:
            break
    ck(going, f'a left click on the mate: the host\'s creature goes to mate (node {MATE_NODE})')

    # 3. the game's mating editor request: the vote
    st = None
    t0 = time.time()
    while time.time() - t0 < 120:
        v = host.call('coop.vote')['out']
        if 'vote active' in v or mode(host) != m0:
            break
        time.sleep(1)
    print(f'  host vote: {host.call("coop.vote")["out"].strip()[:160]} ({time.time() - t0:.0f} s after the click)', flush=True)
    st = gu.wait(lambda s: s['open'] == '1' and 'wants to evolve' in s.get('world', ''), 20, 'vote on the guest')
    ck(st is not None, f'the guest sees the vote of the mating editor: {st.get("world") if st else None}')
    shot(host, 'mating_vote_host.png')
    if st:
        gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
        gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) == m0:
        time.sleep(1)
    ck(mode(host) != m0, f'the host\'s creature editor opened after the vote ({mode(host)})')
    t0 = time.time()
    while time.time() - t0 < 30 and mode(guest) == m0:
        time.sleep(1)
    ck(mode(guest) != m0, f'the guest\'s own editor opened too ({mode(guest)})')

    # 4. both leave with "done"
    if mode(host) != m0 or mode(guest) != m0:
        host.call('wait.frames 200 60000')
        guest.call('wait.frames 200 60000')
        shot(host, 'mating_editor_host.png')
        for c in (guest, host):
            if mode(c) != m0:
                c.call('click 1416 1062')  # the green "done" check of the editor
        t0 = time.time()
        while time.time() - t0 < 90 and (mode(host) != m0 or mode(guest) != m0):
            time.sleep(2)
        ck(mode(host) == m0 and mode(guest) == m0, f'both games are back in the Creature stage ({mode(host)}, {mode(guest)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
