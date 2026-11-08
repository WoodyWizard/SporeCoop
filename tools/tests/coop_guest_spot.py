#!/usr/bin/env python3
"""BUG-0333: the guest's avatar stays where it joined while nothing moves it.

In 2 of ~60 `team` runs of 0.7.3.1 (lab 20261030-205830, fix/w17-creature 20261030-230659 team#5) the
guest's avatar was found ~14 m from where it joined, both times at the same spot (-159 -471 -86), a few
seconds after the mod had put the guest's own creature on it (SetSpecies) and while the test hit NPCs
through the guest. This test repeats what `team` did then, on purpose and with nothing else moving the
guest: it waits for the own creature swap (when there is one), hits the nearest mirrored NPCs through
the guest (`coop.npc hit`, the game's damage with the guest's avatar as the attacker), the host hits
them too, and samples the guest's position in its own game every 0.25 s for the whole time. Checked:
the guest's avatar never gets more than 3 m from where it stood after the join.

  coop_guest_spot.py [--hits N]
  SPORE_FIX_OFF=creations.guest_world_species: the guest plays its newest own creation (the swap)
"""
import math
import re
import sys
import time

from coop_creature_desync import npcs
from twoinst import Checks, shot, start


def pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


def main():
    hits = int(sys.argv[sys.argv.index('--hits') + 1]) if '--hits' in sys.argv else 4
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')  # no NPC bite kills or throws anyone
    p0 = pos(guest)
    ck(p0 is not None, f'the guest is in the host\'s world ({p0})')
    far = [0.0, None]
    t0 = time.time()

    def sample(secs):
        end = time.time() + secs
        while time.time() < end:
            p = pos(guest)
            d = dist(p, p0)
            if d is not None and d > far[0]:
                far[0], far[1] = d, (round(time.time() - t0, 1), p)
            time.sleep(0.25)

    sample(6)  # the own creature swap (3 s after the avatar is seen) and the first seconds after it
    body = re.search(r'body: .*', guest.call('coop.status')['out'])
    print(f'  guest after the join: {guest.call("coop.avatar")["out"].strip()[:160]}', flush=True)
    done = 0
    # wild NPCs only, not the host's family (its species): a hit on the family makes it bite the host back
    # (the lab's posse_damage gate; BUG-0437 in coop_team.py)
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    family = {int(m.group(1)) for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) .*', host.call('coop.npc list')['out'], re.M)
              if m.group(2) == hsp or 'family' in m.group(0)}
    cands = []
    for _ in range(20):  # the wild NPCs the guest mirrors, nearest first
        lst = guest.call('coop.npc list')['out']
        cands = sorted((float(m.group(4)), int(m.group(1))) for m in
                       re.finditer(r'#(\d+) species=(\S+) spawned hp=([\d.]+) dist=([\d.]+)', lst)
                       if int(m.group(1)) not in family and m.group(2) != hsp and float(m.group(4)) < 60)
        if cands:
            break
        time.sleep(1)
    for dd, nid in cands:
        if done >= hits:
            break
        before = pos(guest)
        guest.call(f'coop.npc hit {nid} 0.3')
        sample(1.0)
        host.call(f'coop.npc hosthit {nid} 0.3')
        sample(2.5)
        print(f'  hit n{nid} at {dd:.0f} m: the guest moved {dist(pos(guest), before):.1f} m', flush=True)
        done += 1
    sample(10)
    print(f'  the guest joined at {p0}; farthest {far[0]:.1f} m at {far[1]}', flush=True)
    if body:
        print(f'  {body.group(0)[:200]}')
    ck(done > 0, f'the guest hit NPCs ({done})')
    ck(far[0] <= 3.0, f'the guest\'s avatar stays where it joined (farthest {far[0]:.1f} m in {time.time() - t0:.0f} s)')
    shot(guest, 'guest_spot_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
