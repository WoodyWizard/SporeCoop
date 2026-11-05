#!/usr/bin/env python3
"""Tribe: no village in the guest's game only (BUG-0483).

The detector found whole villages (tribe, hut, tools, food mat, members) in a guest's game only after the live
Creature -> Tribe change (campaign_ct). A guest's own NPC tribe controller formed tribes of its own (the host
forms its own and the guests make each of the host's: tribe.npc_tribes), and a guest's tribe the host no
longer had was never emptied.

1. Two games on the tribe fixture. The guest's game's NPC tribe controller forms its next tribe
   (`coop.tribe form`: the controller's own function, what it does during play; called again while it only
   asks for the next species' model to be baked, BUG-0587). PASS: the guest's game has
   no tribe more than the host's 10 s later, and none farther than 40 m from every tribe of the host's.
2. With the fix off for a moment the guest's game forms the tribe (a village the host has not, with members);
   with the fix on again its members are gone within 10 s (the game's own member vector).

SPORE_FIX_OFF=tribe.guest_tribes: step 1 has the guest's own tribe (FAIL), step 2 keeps its members.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_guest_tribes.py
"""
import math
import os
import re
import time

from twoinst import Checks, start


def tribes(con):
    """[(player, (x, y, z), members)] from the game's tribe list"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\): (\d+) members', line)
        if m:
            out.append((m.group(1) is not None, tuple(float(m.group(i)) for i in (2, 3, 4)), int(m.group(5))))
    return out


def guest_only(h, g):
    return [t for t in g if not t[0] and all(math.dist(t[1], x[1]) > 40 for x in h if not x[0])]


def show(ts):
    return [(tuple(round(v) for v in t[1]), t[2]) for t in ts]


def form(guest, label, secs=40):
    """`coop.tribe form` as the game's controller calls it during play: the first call after a load often only asks
    for the next species' model to be baked and forms nothing (0xCC6440, BUG-0587: "tribes 2 -> 2" in 13 runs of
    lab 20261104-21xxxx); the controller is called again until it forms. So call it again while the reply says
    the model is not baked yet (and the fix did not block it), for at most secs"""
    t0, n = time.time(), 0
    while True:
        out = guest.call('coop.tribe form')['out'].strip()
        n += 1
        print(f'  guest{label}: {out}', flush=True)
        m = re.match(r'tribes (\d+) -> (\d+)', out)
        if 'blocked' in out or 'not baked yet' not in out or (m and m.group(1) != m.group(2)) or \
                time.time() - t0 > secs:
            return n
        time.sleep(1)


def main():
    off = 'tribe.guest_tribes' in os.environ.get('SPORE_FIX_OFF', '')
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(8)
    # W2's re-alignment (realign.villages, BUG-0564) removes a village the host has not within ~4 s: with it on, the
    # twin run (SPORE_FIX_OFF=tribe.guest_tribes) passed step 1 although the guest's controller had formed three
    # tribes (lab 20261104-231126). This test is about the guest's controller: realign.villages stays off until
    # step 3
    guest.call('coop.fix realign.villages off')
    h0, g0 = tribes(host), tribes(guest)
    print(f'  host tribes {show(h0)}; guest tribes {show(g0)}', flush=True)
    form(guest, '')
    time.sleep(10)
    h1, g1 = tribes(host), tribes(guest)
    own = guest_only(h1, g1)
    print(f'  host tribes {show(h1)}; guest tribes {show(g1)}; the guest\'s own {show(own)}', flush=True)
    ck(len(g1) <= len(h1) and not own, f'the guest\'s game formed no tribe of its own ({show(own)})')
    # 2. a village the host has not: the fix off for the forming only
    if not off:
        guest.call('coop.fix tribe.guest_tribes off')
    form(guest, ' (fix off)')
    time.sleep(8)
    own = guest_only(tribes(host), tribes(guest))
    print(f'  the guest\'s own tribes {show(own)}', flush=True)
    ck(own and own[0][2] > 0, f'with the fix off the guest\'s game formed a tribe with members ({show(own)})')
    if not off:
        guest.call('coop.fix tribe.guest_tribes on')
        guest.call('coop.fix realign.villages on')  # (emptied by BUG-0483, or gone with the village: BUG-0564)
    t0 = time.time()
    while time.time() - t0 < 12:
        left = [t for t in guest_only(tribes(host), tribes(guest)) if t[2] > 0]
        if not left:
            break
        time.sleep(1)
    print(f'  the guest\'s own tribes now {show(guest_only(tribes(host), tribes(guest)))}', flush=True)
    st = [ln for ln in guest.call('coop.tribe')['out'].splitlines() if 'forms_blocked' in ln]
    print('  ' + (st[0][st[0].find('ours='):] if st else '-'), flush=True)
    ck(own and not left, f'a tribe the host has not is emptied in the guest\'s game ({show(left)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
