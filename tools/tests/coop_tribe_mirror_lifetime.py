#!/usr/bin/env python3
"""BUG-0844: a Tribe guest never moves (or removes) a member of another tribe that its game built where a removed copy
of a host's member was.

A mirror's citizen was kept across frames and checked by its own memory (object_alive). The game frees a removed
citizen later and gives the block to the next citizen it makes, in any tribe. While the host's states paused (its
editor at graduation: the fork's report) the mirror stayed bound to that address; when they came again the guest drove
the new citizen as the host's member, and the snapshot's end called RemoveMember through the owner field of the
freed block (the fork's crash in remove_member).

Two games on the tribe fixture. On the guest, up to --tries times:
  1. `coop.tribe statehold 1`: the host's tribe states are not taken (as while the host is in an editor);
  2. `coop.tribe reusehunt npc 150`: the game removes the citizen of a mirror of another tribe than the guest's
     player tribe (RemoveMember with destroy on its tribe, no bookkeeping of ours), then once a frame a member is
     made in the guest's player tribe until the game gives it the freed block;
  3. `coop.tribe statehold 0`, 3 s of the host's states: where is the guest's new member? Taken for the mirror, it
     stands where the host's member is; left alone, it stays a member of the guest's tribe where it was made.
PASS: the guest's new member at a freed mirror's address is never moved onto the host's member of the other tribe
(the guest's own rules may remove it: a member the host has not), and no game faulted. SPORE_FIX_OFF=tribe.mirror_member_lifetime must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_mirror_lifetime.py [--tries 2]"""
import argparse
import math
import re
import time

from twoinst import Checks, start


def hunt(con):
    out = con.call('coop.tribe reusehunt')['out']
    m = re.search(r'mirror=(\d+) left=(-?\d+) tries=(\d+) hit=([0-9A-Fa-f]+|\(nil\)|0+) alive=(\d) own_tribe=(\d) '
                  r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) host_member=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) bound_as=(-?\d+)', out)
    if not m:
        return None
    hit = m.group(4)
    return {'id': int(m.group(1)), 'left': int(m.group(2)), 'tries': int(m.group(3)),
            'hit': int(hit, 16) if re.fullmatch(r'[0-9A-Fa-f]+', hit) else 0, 'alive': m.group(5) == '1',
            'own': m.group(6) == '1', 'pos': tuple(float(x) for x in m.group(7, 8, 9)),
            'want': tuple(float(x) for x in m.group(10, 11, 12)), 'bound': int(m.group(13)), 'line': out.strip()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=1)
    ap.add_argument('--rounds', type=int, default=16)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(10)
    reused, wrong = 0, []
    for _ in range(a.rounds):
        if reused >= a.tries:
            break
        guest.call('coop.tribe statehold 1')
        out = guest.call('coop.tribe reusehunt npc 150')['out'].strip()
        print('  ' + out, flush=True)
        if 'removed by the game' not in out:
            guest.call('coop.tribe statehold 0')
            time.sleep(3)
            continue
        t1 = time.time()
        h = hunt(guest)
        while time.time() - t1 < 6 and h and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h or not h['hit']:
            print(f'  no member got the freed block ({h and h["tries"]} tries)', flush=True)
            guest.call('coop.tribe statehold 0')
            time.sleep(3)
            continue
        reused += 1
        made = h['pos']
        print(f'  a new member of the guest\'s tribe at the freed block of mirror #{h["id"]}: {h["line"]}', flush=True)
        guest.call('coop.tribe statehold 0')
        time.sleep(3)
        h = hunt(guest)
        d_want = math.dist(h['pos'], h['want']) if h['alive'] else -1
        moved = math.dist(h['pos'], made) if h['alive'] else -1
        print(f'  3 s later: {h["line"]} (moved {moved:.1f} m, {d_want:.1f} m from the host\'s member)', flush=True)
        if h['alive'] and 0 <= d_want < 1.5 and moved > 0.5:
            wrong.append(h['id'])
    ck(reused >= 1, f'the game gave a removed mirror\'s block to a new member of the guest\'s tribe ({reused} times)')
    ck(not wrong, f'that member of another tribe was not driven as the host\'s member (taken for it: {wrong})')
    guest.call('coop.tribe statehold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
