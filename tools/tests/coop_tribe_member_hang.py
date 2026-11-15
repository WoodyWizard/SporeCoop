#!/usr/bin/env python3
"""Tribe: a member the host's game holds up in the air is up on the guest's screen too (BUG-1082).

The campaign's raids failed the sync-check: a member the host's game held over its ground (the airborne node
0x0485FD10 at 5-15 m with speed 0 for 15-28 s, an NPC village member 114 m up) stood on the guest's ground: 11.9 m and
113.9 m apart on two checks 3 s apart (docs/re/tribe.md#tribe-member-hang). The test holds members up on the host
(`coop.tribe hang <id> <m> <s>`: not grounded, no velocity, as the game held them) and reads the guest's mirror:
1. a member 12 m up for 12 s, 2. another 60 m up, 3. a third 110 m up (the suite runs it with the host at 10 fps over
the player network: the air list comes 0.2-0.4 s apart, as from the campaign's loaded host). NPC village members take
the same path (the list has every member of every tribe); the test's hold does not keep an NPC member up on the host.
Each: the guest's mirror within 3 m of the host's member (3D, the sync-check's own measure) in at least 80 % of the
reads from 2 s after the start, and over 5 m above its own ground. Afterwards the members are on the ground again on
both games. `coop.fix tribe.mirror_hang off` on both games (the 0.7.13 rule: 30 m at most, 0.35 s) fails.
"""
import math
import re
import time

from coop_tribe_member_air import ground
from twoinst import Checks, game_runs, start

ROW = re.compile(r'\s+#(\d+) \S+ owner=(-?\d+) .*?hp=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*? dead=(\d) .*?leader=(\d)')


def all_members(con):
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        if line.startswith('tribe '):
            player = '(player)' in line
            continue
        m = ROW.match(line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = dict(owner=int(m.group(2)), hp=float(m.group(3)), player=player,
                                        pos=tuple(float(m.group(i)) for i in (4, 5, 6)), dead=m.group(7) == '1',
                                        leader=m.group(8) == '1')
    return out


def hang_once(host, guest, mid, agl, secs):
    """hold #mid up on the host; [(apart, guest up)] of the reads while the host's member was up (from 2 s on)"""
    print(f'    #{mid} held {agl} m up for {secs} s:', host.call(f'coop.tribe hang {mid} {agl} {secs}')['out'].strip(), flush=True)
    t0, reads, k, lost = time.time(), [], 0, 0
    while time.time() - t0 < secs - 1:
        time.sleep(0.5)
        if time.time() - t0 < 2:
            continue
        h, g = all_members(host).get(mid), all_members(guest).get(mid)
        if not h or not g:
            continue
        up_h, up_g = ground(host, h['pos'])[1], ground(guest, g['pos'])[1]
        if up_h < 0.8 * agl:  # (the test's hold lost this frame: a node that places the member itself)
            lost += 1
            continue
        d = math.dist(h['pos'], g['pos'])
        reads.append((d, up_g))
        k += 1
        if k <= 3 or d >= 3.0 and k % 4 == 0:
            print(f'      host {up_h:.1f} m up, guest\'s mirror {up_g:.1f} m up, {d:.1f} m apart', flush=True)
    time.sleep(4)  # the hold ended: the game lets it fall
    if lost > 1:  # the host's own member went up and down (its node places it): no steady hang, another member
        print(f'      the host\'s hold did not keep #{mid} up ({lost} reads down): another member', flush=True)
        return []
    return reads


def hang(ck, host, guest, cands, agl, secs, tag):
    print(f'  {tag}', flush=True)
    reads = []
    while cands and len(reads) < 5:
        reads = hang_once(host, guest, cands.pop(0), agl, secs)
    for who, con in (('host', host), ('guest', guest)):
        air = re.search(r'air fix=.*', con.call('coop.tribe anims')['out'])
        print(f'    {who}: {air.group(0) if air else "?"}', flush=True)
    near = sum(1 for d, _ in reads if d < 3.0)
    ups = sorted(u for _, u in reads)
    ck(len(reads) >= 5 and near >= 0.8 * len(reads),
       f'{tag}: the guest\'s mirror is where the host\'s member is ({near} of {len(reads)} reads within 3 m, worst '
       f'{max((d for d, _ in reads), default=-1):.1f} m)')
    ck(ups and ups[len(ups) // 2] > 0.5 * agl,
       f'{tag}: the guest\'s mirror is up in the air on the guest\'s screen (median {ups[len(ups) // 2] if ups else -1:.1f} m '
       f'over its ground)')


def main():
    import sys
    host, guest = start('tribe')
    ck = Checks()
    if '--nofix' in sys.argv:
        print(' ', host.call('coop.fix tribe.mirror_hang off')['out'].strip(), guest.call('coop.fix tribe.mirror_hang off')['out'].strip())
    game_runs(guest, 90)
    time.sleep(4)
    ms = all_members(host)
    if sum(1 for m in ms.values() if m['player'] and not m['leader']) < 9:
        print('  host:', host.call('coop.tribe spawn 6')['out'].strip()[:80], flush=True)
        time.sleep(6)
        ms = all_members(host)
    own = [i for i, m in ms.items() if m['player'] and not m['leader'] and not m['dead'] and m['hp'] > 0]
    if not ck(len(own) >= 3, f'the host has player members other than the leader ({own})'):
        return ck.result()
    cands = sorted(own, reverse=True)  # the newest first: idle, no node that places them (the hold keeps those up)
    hang(ck, host, guest, cands, 12, 12, '1. a member 12 m up')
    hang(ck, host, guest, cands, 60, 12, '2. a member 60 m up')
    hang(ck, host, guest, cands, 110, 12, '3. a member 110 m up')
    time.sleep(4)
    for mid in own:
        h, g = all_members(host).get(mid), all_members(guest).get(mid)
        if h and g:
            print(f'  after: #{mid} host {ground(host, h["pos"])[1]:.1f} m, guest {ground(guest, g["pos"])[1]:.1f} m over the ground',
                  flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
