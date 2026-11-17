#!/usr/bin/env python3
"""Civilization: the guest never draws the host's wild creatures flying through the air when the game moves them
elsewhere (BUG-1216).

The Civ game takes wild creatures back into its pool when no camera is near and gives the objects out again for the
herds near a camera, several first at an unplaced point (z = 0) and a moment later at the new herd
(docs/re/civ.md#civ-wild-reused-frozen): the host's states of one creature jump hundreds of metres. A guest blended
across that step: its copy was walked after a target sliding along the chord through the planet and flew in a straight
line 10-80 m over the guest's ground (27 such copies in a minute of civ_wild_sync, lab 20261117-124343); in the
4-player campaign's Civ the sync-check found herds 80-205 m from the host's (`n apart`, lab 20261117-122701).

1. Two games on civ_npc (a herd of 7 and three epics). From the guest's join on, every 0.2 s: every copy on the guest
   and its creature on the host (`coop.npc list`: where each game draws it, its height over that game's own terrain).
2. Both cameras go to the far side of the planet and back to the herd twice (W/A/S/D as a player pans): the game pools
   the herds behind the cameras and puts herds out where they look.
3. Checks over all samples:
   * a copy is more than 3 m over the guest's ground in two samples in a row while its creature stands or walks on the
     host's ground (under 0.5 m over it): FAIL (BUG-1216: 10-80 m up for a second and more);
   * at least 10 new copies were made (else INCONCLUSIVE: nothing was put out).
SPORE_FIX_OFF=npc.civ_jump: the copies fly to their new place: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_proxy_air.py
"""
import re
import time

from coop_civ_wild_sync import wild
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start

AIR_MAX = 3.0      # m over the guest's ground
HOST_GROUND = 0.5  # m: the host's creature on its ground


def host_rows(con):
    """{id: agl} of the host's NPCs (coop.npc list)"""
    out = {}
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ species=\S+ .* agl=([-\d.]+)', ln)
        if m:
            out[int(m.group(1))] = float(m.group(2))
    return out


def guest_rows(con):
    """{id: (drawn pos, agl)} of the guest's spawned copies (coop.npc list)"""
    out = {}
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ spawned .*? at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) agl=([-\d.]+)', ln)
        if m:
            out[int(m.group(1))] = ((float(m.group(2)), float(m.group(3)), float(m.group(4))), float(m.group(5)))
    return out


class Watch:
    def __init__(self, host, guest):
        self.host, self.guest = host, guest
        self.seen, self.up, self.flights, self.worst = set(), {}, {}, {}

    def sample(self, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            g, h = guest_rows(self.guest), host_rows(self.host)
            for i, (p, agl) in g.items():
                self.seen.add(i)
                if agl > AIR_MAX and i in h and abs(h[i]) < HOST_GROUND:
                    self.up[i] = self.up.get(i, 0) + 1
                    if self.up[i] >= 2:
                        self.flights[i] = self.flights.get(i, 0) + 1
                    self.worst[i] = max(self.worst.get(i, 0.0), agl)
                else:
                    self.up[i] = 0
            time.sleep(0.2)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    w = Watch(host, guest)
    w.sample(10)  # the herds the guest's join brings out
    herd = [x[0] for x in wild(host) if x[1]]
    if not ck(len(herd) >= 3, f'wild creatures in the host\'s game ({len(herd)})'):
        return ck.result()
    c0 = max(herd, key=lambda p: sum(1 for q in herd if dist(p, q) < 30))
    far = tuple(-v for v in c0)
    for k in range(2):
        frame_city({'host': host, 'guest': guest}, far, answer=False)
        w.sample(12)
        frame_city({'host': host, 'guest': guest}, c0, answer=False)
        w.sample(12)
    keyframe('herd', 'the herd on the ground on both screens', players={'host': host, 'guest': guest})
    print(f'  copies made on the guest: {len(w.seen)}; copies over {AIR_MAX} m in the air in two samples in a row: '
          f'{len(w.flights)} ' + ' '.join(f'#{i} {w.worst[i]:.1f} m' for i in sorted(w.flights)), flush=True)
    print('  guest', guest.call('coop.npc status')['out'].strip().splitlines()[1][-60:], flush=True)
    if len(w.seen) < 10:
        return ck.inconclusive(f'only {len(w.seen)} copies made on the guest: no herd was put out')
    ck(not w.flights, f'no copy flies over the guest\'s ground while its creature is on the host\'s ({len(w.flights)} did, '
       f'highest {max(w.worst.values(), default=0):.1f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
