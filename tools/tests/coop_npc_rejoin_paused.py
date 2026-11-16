#!/usr/bin/env python3
"""BUG-0125: a guest that leaves and joins again while the host's game stands in the pause menu
sees the animals around it once the host goes on.

The host keeps per guest id the NPCs that guest mirrors and forgot them only in its NPC tick, which
does not run while the host's simulation stands still. A guest that came back under the same id
during the pause was taken for one that already had them: no spawns, only states it drops.

Checked on the guest's game: creatures mirroring the host's NPCs exist near the guest
(`coop.npc list`, spawned proxies with their distance) within 15 s after the host closes the menu.

  coop_npc_rejoin_paused.py [--without-fix]   (--without-fix: coop.npc knownfix off, must FAIL)
"""
import re
import sys
import time

from harness import waiter
import lane
import uifind
from twoinst import Checks, shot, start
import twoinst


def near_proxies(con, r=100.0):
    n = 0
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#\d+ .*spawned .*dist=([\d.]+)', line)
        if m and float(m.group(1)) < r:
            n += 1
    return n


wait = waiter(step=1)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.npc knownfix off')['out'].strip())
    n = wait(lambda: near_proxies(guest), 40)
    ck(n, f'before: the guest mirrors {n} NPCs near it')

    host.call('key esc')
    paused = wait(lambda: uifind.shown(host, 'Resume Game'), 10)
    ck(paused, 'the host is in the pause menu')
    time.sleep(2)
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    guest.call(lane.JOIN)
    # the guest loads the host's world (shared while the host's menu is up); the menu stays open
    ok = twoinst.wait_guest_in(host, guest, close=lambda h: None)
    ck(ok, 'the guest is back in the host\'s world')
    ck('client id=1 ' in guest.call('coop.status')['out'], 'under the same player id (1)')
    time.sleep(8)  # the guest's own NPCs are gone (update filter) while the host still stands
    still = uifind.shown(host, 'Resume Game')
    ck(still, 'the host was paused through the whole rejoin')
    for _ in range(3):
        if not uifind.shown(host, 'Resume Game'):
            break
        host.call('key esc')
        time.sleep(1)
    n2 = wait(lambda: near_proxies(guest), 15)
    print(f'  after the host went on: {n2} mirrored NPCs near the guest')
    print('  host:', host.call('coop.npc')['out'].strip()[:200])
    ck(n2, 'the guest sees the host\'s NPCs near it again')
    shot(guest, 'npc_rejoin_paused_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
