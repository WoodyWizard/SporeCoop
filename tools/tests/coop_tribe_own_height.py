#!/usr/bin/env python3
"""Tribe: a guest's ground higher than the host's does not make the members walk in place (BUG-0329).

In the full campaign the Tribe village stood 5 m higher (or lower) on the guests, which load the
host's save, than on the host that made the village live (the members' positions differed only in
their distance from the planet's centre: 4.9-5.3 m radial, under 0.8 m sideways). The guest drove
each mirror at the host's absolute position: 5 m under its own ground it never got there, walked in
place at 5.5 m/s while the host's member stood, and was teleported again and again.

That terrain difference needs a whole campaign; the test makes its effect: `coop.tribe lift 5` on
the guest takes the host's members 5 m further from the planet's centre in the guest's copy of the
snapshots (as if the guest's ground were 5 m lower than the host's).

1. Two games on the tribe fixture, the guest's members follow the host's.
2. `coop.tribe lift 5` on the guest; 10 s to settle, then 20 s of samples (every 2 s): for every member
   that stands on the host (`host_speed` under 0.3 in the guest's `coop.tribe` line), the guest's own
   game speed of its mirror; and the mirrors' teleports (`drive ... teleports=`).
PASS: at most 10 % of those samples have the mirror moving faster than 1 m/s, and the teleports
grow by at most 2 per member. --nofix (`coop.fix drive.own_height off`): must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_own_height.py [--nofix] [--lift 5]
"""
import argparse
import re
import time

from twoinst import Checks, start


def members(con):
    """the guest's mirrors: id -> (own speed, host speed, teleports)"""
    out, cur = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ owner=.* speed=([\d.]+) host_speed=([\d.]+) dead=0', line)
        if m:
            cur = int(m.group(1))
            out[cur] = [float(m.group(2)), float(m.group(3)), 0]
            continue
        m = re.match(r'\s*drive err=\S+ .* teleports=(\d+)', line)
        if m and cur in out:
            out[cur][2] = int(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--lift', type=float, default=5)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        print(' ', guest.call('coop.fix drive.own_height off')['out'].strip(), flush=True)
    time.sleep(5)
    print('  guest:', guest.call(f'coop.tribe lift {a.lift}')['out'].strip(), flush=True)
    time.sleep(10)
    tp0 = {k: v[2] for k, v in members(guest).items()}
    standing, walking = 0, 0
    for _ in range(10):
        for k, (own, hs, tp) in members(guest).items():
            if hs < 0.3:
                standing += 1
                walking += own > 1.0
        time.sleep(2)
    end = members(guest)
    grew = [end[k][2] - tp0[k] for k in end if k in tp0]
    print(f'  {standing} samples of members standing on the host: the guest\'s mirror moving in {walking}; '
          f'teleports per member over 20 s: {sorted(grew)[-5:]}', flush=True)
    guest.call('coop.tribe lift 0')
    ck(standing >= 20, f'members standing on the host were sampled ({standing})')
    ck(walking <= 0.1 * standing, f'the guest\'s members stand where the host\'s stand ({walking} of {standing} samples moving)')
    ck(not grew or max(grew) <= 2, f'no mirror is teleported again and again (largest {max(grew) if grew else 0})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
