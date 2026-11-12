#!/usr/bin/env python3
"""Cell: a guest's kill the host did not make does not block that creature's next kill (BUG-0821).

A guest's lethal bite on a host creature (the game's Bite on the mirror -> BreakOrSwallow -> MSG_CELL_EAT
ACT_BREAK) is replayed by the host through the game's Bite with the host's hp (BUG-0714): when the host's
creature has more hp than the guest's mirror showed, the game spares it (a hit). The host still wrote the
kill into its 10 s ledger (BUG-0632, kill_once) and the guest its "kill sent": for 10 s every later lethal
bite of that creature was dropped on the guest ("dup sent") and refused on the host ("killed here already").

1. The guest finds a host creature; the host gives it hp 1, the guest's mirror shows it (2 s).
2. The guest's game holds the host's state for 3 s (`coop.pool statehold`: its mirror keeps hp 1), the
   host gives the creature hp 60, the guest bites it with the game's Bite (`coop.pool kill c<key>`): lethal
   on the guest's mirror, sent; the host's game spares it (alive in the host's game, hp under 60).
3. 3 s later (the state flows again) the host gives it hp 1 and the guest bites it again (lethal on both):
   within 3 s the creature is dying or gone in the HOST's game.
SPORE_FIX_OFF=cell.kill_refused must FAIL at 3 (alive for the 10 s).

  SPORE_LANE=3 python3 tools/tests/coop_cell_kill_refused.py
"""
import re
import time

from coop_cell_guest_hit import host_cell, num
from twoinst import Checks, start


def dead(st):
    return 'no such cell' in st or 'dying=1' in st or (num(st, 'hp') or 0) <= 0


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    key, r = None, ''
    for _ in range(15):
        r = guest.call('coop.pool kill - hit')['out']
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'the guest found a host creature ({r.strip()[:100]})')
    if not key:
        return ck.result()
    host.call(f'coop.pool swimto {key} tp')
    guest.call(f'coop.pool swimto {key} tp')
    print('  ' + host.call(f'coop.pool npcflag {key} hp 1')['out'].strip(), flush=True)
    time.sleep(2.5)
    # 2. the spared kill
    print('  guest: ' + guest.call('coop.pool statehold 3')['out'].strip(), flush=True)
    print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'].strip(), flush=True)
    r = guest.call(f'coop.pool kill {key}')['out'].strip()
    print(f'  guest bite 1 (lethal on its mirror): {r[:160]}', flush=True)
    time.sleep(1.5)
    st = host_cell(host, key)
    print(f'  host: {st.strip()[:200]}', flush=True)
    ck(not dead(st), f'2. the host\'s game spares the creature (hp {num(st, "hp")} of 60)')
    if dead(st):
        return ck.result()
    time.sleep(2.0)  # the state flows again
    # 3. the next lethal bite kills it
    print('  ' + host.call(f'coop.pool npcflag {key} hp 1')['out'].strip(), flush=True)
    time.sleep(0.8)  # the mirror has hp 1 again
    killed, t0 = False, time.time()
    while time.time() - t0 < 6 and not killed:
        r = guest.call(f'coop.pool kill {key}')['out'].strip()
        print(f'  guest bite (t+{time.time() - t0:.1f}): {r[:120]}', flush=True)
        for _ in range(6):
            time.sleep(0.25)
            if dead(host_cell(host, key)):
                killed = True
                break
        if 'gone' in r:
            break
    st = host_cell(host, key)
    print(f'  host after: {st.strip()[:200]}', flush=True)
    print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines() if x.startswith('cell_once')), flush=True)
    print('  guest: ' + ' '.join(x for x in guest.call('coop.pool')['out'].splitlines()
                                 if x.startswith('cell_once') or x.startswith('cell_c1')), flush=True)
    ck(killed, f'3. the guest\'s next lethal bite kills the creature in the host\'s game within {time.time() - t0:.0f} s')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
