#!/usr/bin/env python3
"""Tribe: the duet, both players' members performing for one NPC tribe, fills its social mini-game (BUG-0748).

tribe.cpp duet_tick (host): when members of two players are in the social performance (BT node 0xB46E8803) for the
same village, the mini-game's progress (its UI object +0x68) rises by itself, 8 % a second, up to 97 % (the last
notes are the player's). No test had ever started a performance (docs/re/tribe.md#tribe-duet-test).

1. Two games on the tribe fixture (cold). The host's game forms an NPC tribe 40 m from the village and buys two
   instruments (types 4 and 5, `coop.tribe buy` + `buy_end`, the palette's placement); a member of the host's and one
   of the guest's take one each (`coop.tribe grab` on the host, a GrabTool order as a click on the tool gives).
2. Each player with real input: the view on its instrument member (the minimap), a box around it (the host's with its
   chief, put next to it), the social mode
   button, the view on the NPC tribe, a right-click on one of its members ("Right click on a tribe member to perform
   for them"): CitizenAction 26. A player whose clicks miss every member (its game's pick) gives the same order through
   its game's DoActionForObjects (`coop.tribe act 26 <id>`), what its click calls (printed).
3. In the host's game both performers are in node 0xB46E8803 for the same village (`coop.tribe duet`: the session's
   target) and the mini-game's progress rises with nobody playing a note: over 20 %.

SPORE_FIX_OFF=tribe.duet must FAIL (the progress stays where the players' notes leave it: 0).

  SPORE_LANE=7 python3 tools/tests/coop_tribe_duet.py
"""
import math
import random
import re
import time

import play_tribe as pt
from coop_tribe_tools import members
from twoinst import Checks, shot, start


def performers(host):
    out = host.call('coop.tribe duet')['out']
    ps = [dict(id=int(m.group(1)), owner=int(m.group(2)), target=m.group(3), state=int(m.group(4)), progress=float(m.group(5)))
          for m in re.finditer(r'performer #(\d+) owner=(\d+) target=(\S+) state=(-?\d+) ui=\S+ progress=([-\d.]+)', out)]
    m = re.search(r'ticks=(\d+)', out)
    return ps, int(m.group(1)) if m else 0


def held(host):
    return {int(m.group(1)): int(m.group(2)) for m in
            re.finditer(r'held (\d+) owner=\d+ tool=(\d+)', host.call('coop.tribe tools')['out'])}


def prepare(pl, ids, npc_at):
    """the view on the members, a box around them, social mode, the view on the NPC tribe"""
    pl.calibrate()
    st = pt.tribe_state(pl.con)
    if ids[-1] not in st:
        return False
    sel = []
    for _ in range(3):  # a box that misses (the member walked off the view's spot): look and box again
        pl.cur = None
        pl.look(pt.tribe_state(pl.con)[ids[-1]]['pos'])
        sel = pl.select(ids, pt.screen(pl.con), group=len(ids) > 1)
        if ids[-1] in sel:
            break
    if ids[-1] not in sel:  # the selection a click makes, through the game's SetSelected (printed)
        for i in ids:
            pl.con.call(f'coop.tribe select {i} 1')
        sel = [i for i, v in pt.tribe_state(pl.con).items() if v['sel'] and v['tribe'] == 'p']
        print(f'  {pl.name}: the boxes missed: selected by SetSelected {sel}', flush=True)
    pl.mode(0)
    pl.look(npc_at)
    pl.centre()
    print(f'  {pl.name}: selected {sel}', flush=True)
    return ids[-1] in sel


def perform_click(pl, npc):
    """right-click a member of the NPC tribe; the CitizenAction the click gave (or None)"""
    st = pt.tribe_state(pl.con)
    tm = [o for o in pl.on_screen(pt.screen(pl.con))
          if o[0][0] == 't' and o[0][1:].isdigit() and st.get(int(o[0][1:]), {}).get('tribe') == npc]
    tm.sort(key=lambda o: math.hypot(o[1] - pl.w / 2, o[2] - pl.h / 2))
    print(f'  {pl.name}: members of the village on screen {[o[0] for o in tm]}', flush=True)
    for key, x, y, _ in tm[:3]:
        for dy in (0, -8, -16, -26):
            before = pl.last_order()
            pl.do(f'move {x} {y + dy}')
            time.sleep(0.4)
            pl.do(f'click {x} {y + dy} right')
            time.sleep(0.8)
            last, n = pl.last_order()
            m = re.search(r'action=(\d+)', last)
            if n != before[1] and m:
                print(f'  {pl.name}: right-click on {key}: {last[:110]}', flush=True)
                if m.group(1) == '26':
                    return 26, int(key[1:])
                break  # another order (a walk): the next member
    return None, (int(tm[0][0][1:]) if tm else None)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mem = members(guest)
    gown = sorted(i for i, o in mem.items() if o != 0)
    hown = sorted(i for i, o in mem.items() if o == 0)
    if not ck(gown and hown, f'both players have members (host {hown}, guest {gown})'):
        return ck.result()
    hid, gid = hown[-1], gown[0]
    out = host.call('coop.tribe npc_tribe 3 40')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if not ck(m, 'the host formed an NPC tribe'):
        return ck.result()
    at = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(3)
    for t, deg in (('4', 60), ('5', 300)):
        print('  host: ' + host.call(f'coop.tribe buy {t} 9 {deg}')['out'].strip() + '; ' +
              host.call('coop.tribe buy_end')['out'].strip(), flush=True)
        time.sleep(1)
    tools = re.findall(r'tool (\d+) \S+ serial=\d+ type=(\d+)', host.call('coop.tribe tools')['out'])
    idx = {ty: int(i) for i, ty in tools}
    for mid, ty in ((hid, '4'), (gid, '5')):
        print(f'  host: grab #{mid} type {ty}: ' + host.call(f'coop.tribe grab {mid} {idx.get(ty, -1)}')['out'].strip()[:80],
              flush=True)
    t0, h = time.time(), {}
    while time.time() - t0 < 45:
        h = held(host)
        if h.get(hid) == 4 and h.get(gid) == 5:
            break
        time.sleep(2)
    if not ck(h.get(hid) == 4 and h.get(gid) == 5, f'the host\'s #{hid} holds instrument 4, the guest\'s #{gid} 5 ({h})'):
        return ck.result()
    centres = pt.tribe_centres(host)
    npc = min((k for k in centres if k != 'p'), key=lambda k: math.dist(centres[k], at))
    log = []
    hp = pt.Player('host', host, 0, random.Random(1), log, 1440, 1080)
    gp = pt.Player('guest', guest, 1, random.Random(2), log, 1440, 1080)
    # the host's chief goes with its band (with the instrument member alone selected the host's right-clicks on the
    # village's members gave no order; with the chief in the box they gave CitizenAction 26, lab 20261108-161904)
    chief = [i for i in hown if i != hid][0]
    p = pt.tribe_state(host)[hid]['pos']
    print('  host: ' + host.call('coop.tribe put %d %.2f %.2f %.2f' % ((chief,) + tuple(v + 1.5 for v in p)))['out'].strip(),
          flush=True)
    time.sleep(1)
    ok_h, ok_g = prepare(hp, [chief, hid], centres[npc]), prepare(gp, [gid], centres[npc])
    ck(ok_h and ok_g, f'each player selected its instrument member (host {ok_h}, guest {ok_g})')
    act_h, th = perform_click(hp, npc)
    act_g, tg = perform_click(gp, npc)
    for con, name, mid, act, target in ((host, 'host', hid, act_h, th), (guest, 'guest', gid, act_g, tg)):
        if act is None and target is not None:  # every pick missed: the call its click makes
            if name == 'guest':
                con.call(f'coop.tribe select {mid} 1')
            r = con.call(f'coop.tribe act 26 {target}')['out'].strip()
            print(f'  {name}: no click gave the order; DoActionForObjects: {r[:120]}', flush=True)
            if 'ordered' in r:
                act_h, act_g = (26, act_g) if name == 'host' else (act_h, 26)
    ck(act_h == 26, f'the host\'s right-click on a member of the village gave CitizenAction 26 ({act_h})')
    ck(act_g == 26, f'the guest\'s order on a member of the village is CitizenAction 26 ({act_g})')
    both, best, ticks, t0 = None, 0.0, 0, time.time()
    while time.time() - t0 < 25:
        ps, ticks = performers(host)
        owners = {}
        for p in ps:
            owners.setdefault(p['target'], set()).add(p['owner'])
        same = [t for t, o in owners.items() if {0, 1} <= o]
        if same:
            both = both or same[0]
            best = max([best] + [p['progress'] for p in ps if p['target'] == same[0]])
        print(f'  {time.time() - t0:4.1f} s: ' + '; '.join(f'#{p["id"]} o{p["owner"]} {p["target"]} s{p["state"]} '
                                                         f'{p["progress"]:.2f}' for p in ps) + f' ticks={ticks}', flush=True)
        if both and best > 0.5:
            break
        time.sleep(1)
    shot(host, 'duet_host.png')
    ck(both is not None, f'both players\' members perform for the same village in the host\'s game ({both})')
    ck(best > 0.2, f'the mini-game\'s progress rose with nobody playing ({best:.2f}, duet ticks {ticks})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
