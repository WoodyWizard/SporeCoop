#!/usr/bin/env python3
"""Tribe: a host member's tool 0 and hunger 0 reach the guest's mirror (BUG-1043); a guest's leave keeps the host's
field statistics (BUG-1044).

The replicated fields (mods/coop/fields.cpp) dropped every received value under 0.001: a host member that put its
tool down (+0x102C mSpecializedTool 0) kept the weapon on the guest's mirror, and a starving member (hunger +0xBBC 0)
kept its last hunger there. And a guest's leave erased the host's statistics of the field kind whose id was the
guest's player id (1 = npc), counted as state of that player.

1. Two games on the tribe fixture; a member of the host's (owner 0, not the leader) in the player tribe.
2. The host gives it a weapon (`coop.tribe settool <id> 1`, the game's SetSpecializedTool): the guest's mirror has
   tool 1 within 5 s (the guest game's +0x102C).
3. The host puts the tool down (`settool <id> 0`): the guest's mirror has tool 0 within 5 s.
4. The host's member starves (`coop.tribe hunger <id> 0`, the game's setter): the guest's mirror reads hunger 0
   (within 0.5) within 5 s; then 80 again.
5. BUG-1044: the host's `coop.fields` statistics of kind npc (id 1) before and after the guest (id 1) leaves: kept.
SPORE_FIX_OFF=fields.zero_values: 3 and 4 FAIL. SPORE_FIX_OFF=fields.player_state: 5 FAILs.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_zero_fields.py
"""
import re
import time

from coop_tribe import members
from twoinst import Checks, start

ROW = r'\n\s+#{id} \S+ owner=-?\d+ .*? tool=(-?\d+) leader=(\d+) .*? hunger=([-\d.]+)/'


def row(con, mid):
    """(tool, leader, hunger) of member mid on this game"""
    m = re.search(ROW.format(id=mid), con.call('coop.tribe')['out'])
    return (int(m.group(1)), int(m.group(2)), float(m.group(3))) if m else None


def wait(cond, secs=5.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(0.25)
    return cond()


def npc_sent(host):
    m = re.search(r'fields npc \(kind 1, [^)]*\): on=\d active=\d sent=(\d+)', host.call('coop.fields')['out'])
    return int(m.group(1)) if m else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    hm = {}
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        hm = members(host)
        if any(v[0] == 1 for v in hm.values()):
            break
        time.sleep(3)
    time.sleep(3)
    cand = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player' and (row(host, i) or (0, 1))[1] == 0]
    print(f'  host members that do not lead: {cand}', flush=True)
    if not ck(cand, 'a member of the host\'s that does not lead the tribe'):
        return ck.result()
    mid = cand[-1]
    # 2. a weapon
    print('  host:', host.call(f'coop.tribe settool {mid} 1')['out'].strip(), flush=True)
    g = wait(lambda: (row(guest, mid) or (None,))[0] == 1)
    ck(g, f"the guest's mirror #{mid} has the host's weapon (tool {row(guest, mid)})")
    # 3. down
    print('  host:', host.call(f'coop.tribe settool {mid} 0')['out'].strip(), flush=True)
    h = row(host, mid)
    g = wait(lambda: (row(guest, mid) or (None,))[0] == 0)
    ck(h and h[0] == 0 and g, f"the host's member put the tool down and the guest's mirror has no tool either "
                              f"(host {h}, guest {row(guest, mid)})")
    # 4. hunger 0
    print('  host:', host.call(f'coop.tribe hunger {mid} 0')['out'].strip(), flush=True)
    g = wait(lambda: (lambda r: r is not None and abs(r[2]) <= 0.5)(row(guest, mid)))
    ck(g, f"the guest's mirror is as starving as the host's member (host {row(host, mid)}, guest {row(guest, mid)})")
    host.call(f'coop.tribe hunger {mid} 80')
    # 5. BUG-1044
    time.sleep(2)
    s0 = npc_sent(host)
    print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(4)
    s1 = npc_sent(host)
    print(f'  host fields npc sent: {s0} -> {s1}; held of player 1: '
          f'{host.call("coop.players held 1")["out"].strip()!r}', flush=True)
    if s0:
        ck(s1 is not None and s1 >= s0, f"the guest's leave kept the host's statistics of kind npc ({s0} -> {s1})")
    else:
        print('  (no npc fields sent in this world: the statistics check says nothing)', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
