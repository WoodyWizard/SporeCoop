#!/usr/bin/env python3
"""Tribe: every tribe's tools stand where the host's stand, in the guest's game too (BUG-0523).

Each game made the tools its tribes buy at its own spots (an NPC tribe's planner, a tribe the guest's game
spawned for the host's with its own starting tools): the detector's `unmatched:tribe_tool`. With
wobj.tribe_tools_all the tools of every tribe are the host's world objects: the guest makes the host's in its
mapped tribe and removes its own the host has not.

1. Tribe fixture. The host forms an NPC tribe 70 m off (`coop.tribe npc_tribe 3 70`, what its tribe controller
   does in play); the guest's game makes the tribe for it (tribe.npc_tribes), with tools of its own.
2. The host places a rack for that NPC tribe (`coop.tribe place <tribe> 1 9 45`: CreateTool as a tribe's
   purchase); the guest places a tool of its own for the same tribe at another spot (type 2, `place` on the
   guest: what its own planner would buy).
3. Within 40 s, in the guest's game (`coop.tribe tools all`: the tribes' own tool lists): every host tool within
   100 m of the village has a tool of the same type within 3 m; the guest's own type-2 tool is gone; the host's
   rack is there.

SPORE_FIX_OFF=wobj.tribe_tools_all: must FAIL (no rack on the guest, its own tool stays).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_tools_all.py
"""
import re
import time

from twoinst import Checks, shot, start


def tools(con):
    """[(tribe, player, type, (x, y, z))] of every tribe of this game"""
    out = con.call('coop.tribe tools all')['out']
    r = []
    for line in out.splitlines():
        m = re.match(r'atool \S+ serial=\d+ tribe=(-?\d+) player=(\d) type=(-?\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            r.append((int(m.group(1)), m.group(2) == '1', int(m.group(3)), tuple(float(m.group(i)) for i in (4, 5, 6))))
    return r


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def centre(con):
    """the player tribe's fire pit (type 10): the village"""
    return next((p for t, pl, ty, p in tools(con) if pl and ty == 10), None)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    out = host.call('coop.tribe npc_tribe 3 70')['out'].strip()
    print('  host:', out)
    ck(out.startswith('npc tribe'), 'the host formed an NPC tribe')
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    made = tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None
    time.sleep(12)
    ht = tools(host)
    print(f'  host tools: {ht}')
    # the new tribe: its fire pit (the tool SpawnNpcTribe makes) at the tribe's place (its index in the host's table
    # can change when a tribe is added: looked up now)
    npc = [t for t, p, ty, q in ht if not p and t >= 0 and made and dist(q, made) < 6]
    ck(bool(npc), f'the host\'s new NPC tribe has an index ({npc})')
    if not npc:
        ck.faults(host=host, guest=guest)
        return ck.result()
    ix = npc[0]
    out = host.call(f'coop.tribe place {ix} 1 9 45')['out'].strip()
    print('  host:', out)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    ck(bool(m), f'the host placed a rack for NPC tribe {ix}')
    if not m:
        ck.faults(host=host, guest=guest)
        return ck.result()
    rack = tuple(float(m.group(i)) for i in (1, 2, 3))
    out = guest.call(f'coop.tribe place {ix} 2 10 225')['out'].strip()
    print('  guest:', out)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    own = tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None
    ck(own is not None, f'the guest placed a tool of its own for tribe {ix} (as its planner would)')
    c = centre(host) or rack
    t0 = time.time()
    missing, extra, has_rack, own_left = ['?'], ['?'], False, True
    while time.time() - t0 < 40:
        time.sleep(3)
        ht, gt = tools(host), tools(guest)
        near = [x for x in ht if dist(x[3], c) < 100]
        missing = [x for x in near if not any(g[2] == x[2] and dist(g[3], x[3]) < 3 for g in gt)]
        extra = [g for g in gt if dist(g[3], c) < 90 and not any(x[2] == g[2] and dist(g[3], x[3]) < 3 for x in ht)]
        has_rack = any(g[2] == 1 and dist(g[3], rack) < 3 for g in gt)
        own_left = own is not None and any(g[2] == 2 and dist(g[3], own) < 1 for g in gt)
        if not missing and not extra and has_rack and not own_left:
            break
    print(f'  {time.time() - t0:.0f} s; host {len(near)} tools near the village')
    print('  guest', guest.call('coop.wobj')['out'].strip().splitlines()[-1])
    ck(has_rack, f'the guest\'s game has the host\'s rack of NPC tribe {ix} at its place {rack}')
    ck(not own_left, 'the guest\'s own tool the host has not is gone from its game')
    ck(not missing, f'every host tool near the village has a guest tool of its type within 3 m (missing {missing})')
    ck(not extra, f'the guest has no tool near the village the host has not (extra {extra})')
    print('  shots:', shot(host, 'tribe_tools_all_host.png'), shot(guest, 'tribe_tools_all_guest.png'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
