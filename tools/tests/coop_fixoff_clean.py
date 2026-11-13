#!/usr/bin/env python3
"""Every fix off (`coop.fix all off`) leaves a clean game: no fault, no crash (BUG-0977).

The lab's twin runs (`--fixoff`, SPORE_FIX_OFF) only mean something when the game with the old code paths does not
fault on its own. The rel/0.7.13 final check: walk_start_walkin (host alone after the guest left, then `all off`) had
caught faults in ucrtbase on the main thread right after the switch and then a crash in DXVK's CS thread.

What the walk test did is done here without the walks, several times: the guest leaves (the host plays alone with the
session still up), the host's fixes go off, then the commands the walk test sends (the pause and stance reads, the NPC
list, `coop.sync` every 0.25 s for 6 s), then all on again; N rounds. Checked: the host's fault count stays 0 and the
game lives.

  SPORE_PROBE_OFF=<prefix>[,<prefix>...]  only the switches whose names start so go off (bisecting; `all` = all off)
  SPORE_PROBE_ON=<name>[,<name>...]       these stay on (after `all off`)

  SPORE_LANE=4 python3 tools/tests/coop_fixoff_clean.py [--rounds 4]
"""
import argparse
import os
import re
import time

from twoinst import Checks, ability_mode, start


def faults(con):
    m = re.search(r'total faults: (\d+)', (con.call('exc')['out'].splitlines() or [''])[0])
    return int(m.group(1)) if m else -1


def switch_off(con, names, keep):
    if names is None:
        out = con.call('coop.fix all off')['out'].strip()
    else:
        for n in names:
            con.call(f'coop.fix {n} off')
        out = f'{len(names)} fixes off one by one'
    for n in keep:
        con.call(f'coop.fix {n} on')
    return out + (f', {len(keep)} kept on' if keep else '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=4)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    guest.call('coop.leave')
    time.sleep(3)
    pre = [p.strip() for p in os.environ.get('SPORE_PROBE_OFF', 'all').split(',') if p.strip()]
    keep = [p.strip() for p in os.environ.get('SPORE_PROBE_ON', '').split(',') if p.strip()]
    names = None
    if pre != ['all']:
        lst = host.call('coop.fix list')['out'].splitlines()
        allnames = [m.group(1) for m in (re.match(r'\s*(\S+\.\S+) (?:on|off)\b', x) for x in lst) if m]
        names = [n for n in allnames if any(n.startswith(p) for p in pre)]
        print(f'  probe: {len(names)} of {len(allnames)} switches go off', flush=True)
    f0 = faults(host)
    print(f'  host faults before: {f0}', flush=True)
    for r in range(a.rounds):
        print(f'  round {r + 1}: ' + switch_off(host, names, keep), flush=True)
        host.call('coop.pauses')
        ability_mode(host)
        host.call('coop.npc list')
        t0 = time.time()
        while time.time() - t0 < 6:
            host.call('coop.sync')
            time.sleep(0.25)
        n = faults(host)
        print(f'  round {r + 1}: host faults {n}', flush=True)
        host.call('coop.fix all on')
        time.sleep(1.0)
        if n != f0:
            break
    ck(faults(host) == f0, f'no fault on the host while its fixes were off ({faults(host)}, {f0} before)')
    ck.faults(host=host)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
