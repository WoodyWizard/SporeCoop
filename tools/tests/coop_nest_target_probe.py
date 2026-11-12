#!/usr/bin/env python3
"""BUG-0881 probe: while the host's species migrates, does the guest's game pick another next nest than the host's?

The wsync row of the nest (BUG-0640) starts the guest's migration with the guest's own `CreatureGame_NestHerdFor(1)`
(the nearest player-species nest whose brain range holds the level) as the target; the ticket suspects that a guest
standing far from the host gets another nest than the host's and claims it for good.

1. Two games on the creature fixture; the pack earns DNA for a bigger brain and the host's creature dies and respawns
   (as coop_nest_follow.py): the host's game migrates (state 2, the goal).
2. The guest's creature is put at the host, then 300 and 600 m away from it in four directions; at each place the
   guest's game's own next nest (`CreatureGame_NestHerdFor(1)` and its Herd_NestPosition) is read, and the host's.
   Last, the guest's creature goes to the host's next nest.
3. On the guest's game: the herds of the player species with a nest and their brain range (+0x120..+0x124) against
   the level 0x169E370. `CreatureGame_NestHerdFor(1)` 0xD40AF0 (Ghidra) returns the nearest of those herds that holds
   the level, in the avatar's region, else the avatar's own herd; row 11 starts a migration only when that is another
   nest than the guest's home.
PASS (not reproduced): wherever the guest stands, its game starts no migration to a nest other than the host's (state
0, or the target within 30 m of the host's next nest). FAIL: a migration to another nest.

  SPORE_LANE=7 python3 tools/tests/coop_nest_target_probe.py
"""
import math
import time

from coop_migration import avatar_herd, cinematic, confirm_captions, in_world, migration, new_nest, total_dna, u32, wait
from coop_nest_follow import host_respawn
from twoinst import Checks, start


def avatar_pos(con):
    import re
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(v) for v in m.groups()) if m else None


def around(p, metres, k):
    """a point `metres` along the surface from p, in direction k of 4"""
    r = math.dist(p, (0, 0, 0))
    up = [v / r for v in p]
    a = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    e1 = [up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]]
    n1 = math.dist(e1, (0, 0, 0))
    e1 = [v / n1 for v in e1]
    e2 = [up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0]]
    ang = metres / r
    c, s = math.cos(k * math.pi / 2), math.sin(k * math.pi / 2)
    d = [c * e1[i] + s * e2[i] for i in range(3)]
    q = [r * (math.cos(ang) * up[i] + math.sin(ang) * d[i]) for i in range(3)]
    return q


def s32(v):
    return v - (1 << 32) if v is not None and v >= 1 << 31 else v


def species_nests(con):
    """(herd, home, brain range lo, hi) of the herds of the avatar's species with a nest, and the game's level"""
    import re
    ah = avatar_herd(con)
    sp = ah and u32(con, ah + 0xA4)
    level = s32(u32(con, 0x169E370))
    out = []
    for m in re.finditer(r'herd (\S+) arch=\S+ species=(\S+) nest=(\S+) .*?home=\((\S+) (\S+) (\S+)\)',
                         con.call('coop.npc archless all')['out']):
        if m.group(3) in ('00000000', '(nil)') or not sp or int(m.group(2).split('/')[0], 16) != sp:
            continue
        h = int(m.group(1), 16)
        out.append((hex(h), tuple(round(float(v)) for v in m.group(4, 5, 6)), s32(u32(con, h + 0x120)),
                    s32(u32(con, h + 0x124))))
    return level, out


def main():
    host, guest = start('creature')
    ck = Checks()
    d0 = total_dna(guest)
    host.call('coop.dna add 250')
    ck(wait(lambda: total_dna(guest) >= d0 + 250, 15), 'the guest has the DNA total too')
    time.sleep(3)
    for con in (host, guest):
        c = cinematic(con)
        if c:
            confirm_captions(con, c)
    host_respawn(host, ck)
    ck(wait(lambda: migration(host) == 2, 40), f'the host has the goal "Migrate to a new nest" (state {migration(host)})')
    ck(wait(lambda: in_world(guest), 150, 2), "the guest plays in the host's world")
    hh, hpos = new_nest(host)
    print(f"  the host's next nest: herd {hh and hex(hh)} at {hpos}", flush=True)
    ck(hpos, "the host's game has its next nest")
    hp = avatar_pos(host)
    seen = []
    if hpos and hp:
        level, ns = species_nests(guest)
        print(f"  guest's game: level {level}, its species' nests (herd, home, brain range): {ns}", flush=True)
        level, ns = species_nests(host)
        print(f"  host's game: level {level}, its species' nests (herd, home, brain range): {ns}", flush=True)
        places = [(0, 0)] + [(m, k) for m in (300, 600) for k in range(4)] + [('next', 0)]
        for metres, k in places:
            if metres == 'next':
                q = [hpos[0] - 2, hpos[1], hpos[2] + 2]
            else:
                q = around(hp, metres, k) if metres else [hp[0] + 3, hp[1], hp[2]]
            guest.call('coop.npc gopos %.1f %.1f %.1f' % tuple(q))
            time.sleep(5)
            gh, gpos = new_nest(guest)
            gp = avatar_pos(guest)
            off = math.dist(gpos, hpos) if gpos else None
            seen.append((metres, k, off, migration(guest)))
            print(f'  guest {metres} m (dir {k}) at {gp}: its next nest {gpos} ({off if off is None else round(off)} m from the '
                  f"host's), migration state {migration(guest)}", flush=True)
    ck(seen and all(st == 0 or (o is not None and o < 30) for _, _, o, st in seen),
       "wherever the guest stands its game starts no migration to another nest than the host's next one "
       f"({[(m, k, o and round(o), st) for m, k, o, st in seen]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
