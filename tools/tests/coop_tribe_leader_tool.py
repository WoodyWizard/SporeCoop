#!/usr/bin/env python3
"""Tribe: a guest's tribe leader never takes a weapon from the host's member (BUG-0583).

Lab (fix/w43-realign, realign_tribe_rejoin): 18 s after a rejoin the guest's game faulted 1151 times (every frame)
in GetAbilityIndexByType (cCreatureCitizen::Update): a citizen counted 21 abilities and its 21st was null, ECX = 1 at
the fault = the leader branch of the getter 0xC24E40. A leader (its tribe's +0x11C) with a tool of type 1..6 counts
one ability more than the getter serves (docs/re/tribe.md#tribe-leader-tool). The game never gives its leader such
a tool; the mirrors' tool field (tribe.tool_hold) copies the host's member's.

1. Tribe fixture. The host forms an NPC tribe (`coop.tribe form`: the guest's game makes its own copy of it,
   tribe.npc_tribes). --rejoin: the guest leaves and joins again.
2. Each game's tribe leaders (`coop.tribe`: `leader=1` from the game's own 0xC23430). A member that leads its tribe
   in the guest's game and not in the host's is the case (picked; none: SKIP).
3. In the host's game that member gets a spear (`coop.tribe settool <id> 1`: the game's SetSpecializedTool, as a
   rack's weapon taken).
--direct (the suite's case): when no member leads in the guest's game only, the guest's leader mirror gets the spear
through tribe.tool_hold's own apply (`coop.tribe applytool <id> 1`, the state of the lab's fault made on purpose).
PASS: the guest's game never faults, its leader keeps a tool outside 1..6 and the guest's game keeps drawing.

Not in the suite: the natural case (a guest-only leader) did not happen in 3 runs (with and without --rejoin), and
--direct did not fault with the fix off either (lab 20261105-002402: the host's next tool field gave the leader its
staff back at once). A probe for the next try.

  SPORE_LANE=2 python3 tools/tests/coop_tribe_leader_tool.py [--rejoin] [--direct]
"""
import re
import sys
import time

import lane
import twoinst
from coop_tribe_gameover import frames_go_on
from twoinst import Checks, start


def members(con):
    """{member id: (tribe index, tool, leader)} of every tribe"""
    out, ti = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            ti = int(m.group(1))
            continue
        m = re.match(r'\s+#(\d+) .* tool=(-?\d+) leader=(\d)', line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (ti, int(m.group(2)), m.group(3) == '1')
    return out


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    if '--no-form' not in sys.argv:
        print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
        time.sleep(10)
    if '--rejoin' in sys.argv:
        print('  guest: ' + guest.call('coop.leave')['out'].strip()[:80], flush=True)
        time.sleep(2)
        guest.call(lane.JOIN)
        ck(twoinst.wait_guest_in(host, guest, 120), 'the guest is back in the host\'s world')
        time.sleep(8)
    pick, t0 = None, time.time()
    while time.time() - t0 < 40 and pick is None:
        time.sleep(4)
        hm, gm = members(host), members(guest)
        cands = [i for i, (_, _, ld) in gm.items() if ld and i in hm and not hm[i][2]]
        pick = cands[0] if cands else None
    print(f'  host leaders {[i for i, v in hm.items() if v[2]]}, guest leaders {[i for i, v in gm.items() if v[2]]} '
          f'({len(hm)} / {len(gm)} members)', flush=True)
    direct = '--direct' in sys.argv
    if pick is None and direct:
        # the state of the lab's fault made on purpose: the guest's leader mirror gets the field's spear through
        # tribe.tool_hold's own apply (as if the host's member it mirrors held one)
        pick = next((i for i, v in gm.items() if v[2] and i in hm), None)
    if pick is None:
        print('  no member leads in the guest\'s game only: the scenario did not happen')
        print('RESULT: SKIP')
        return 0
    print(f'  member #{pick}: host {hm[pick]}, guest {gm[pick]}', flush=True)
    if direct:
        print('  guest: ' + guest.call(f'coop.tribe applytool {pick} 1')['out'].strip(), flush=True)
    else:
        print('  host: ' + host.call(f'coop.tribe settool {pick} 1')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 20 and faults(guest) == 0:
        time.sleep(2)
    gl = members(guest).get(pick)
    print(f'  after: host {members(host).get(pick)}, guest {gl}; guest: ' +
          guest.call('coop.tribe tools')['out'].strip().splitlines()[-1], flush=True)
    if not direct:
        ck(members(host).get(pick, (0, 0, 0))[1] == 1, f'member #{pick} holds the spear in the host\'s game')
    ck(faults(guest) == 0, 'the guest\'s game did not fault')
    ck(gl is not None and gl[2] and gl[1] not in range(1, 7), f'the guest\'s leader keeps its staff ({gl})')
    ck(frames_go_on(guest, 5), 'the guest\'s game keeps drawing')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
