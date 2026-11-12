#!/usr/bin/env python3
"""BUG-0800: after a guest's Summon Flock, the host's own later Summon Flock follows the host, not the guest.

The host's game makes a guest's flock (BUG-0736, `creature.flock_relay`): it starts the Drama Manager's summon event
(kind 8) with the guest's puppet as the avatar and runs the event's update the same way. The event is one object the
Drama Manager builds once and starts again later; the mod remembered it for good, so the host's own next summon ran
its update with the guest's puppet as the avatar: the host's new flock went to the guest (or was let go at once, 100 m
from the puppet).

1. Two games on the creature fixture, the guest 150 m away from the host.
2. The guest plays its Summon Flock (`coop.rules roar 33`; the game's summon event when its creature has no such
   ability): the host's game makes the flock (its pack grows).
3. Wait until the host's summon event is over (its active byte +0xC is 0, `coop.drama`).
4. The host's game starts its own Summon Flock (`coop.drama start 8`, the game's StartKind(8) the ability calls).
PASS, in the host's game: its new flock members come to the host's creature (most within 25 m within 40 s). Printed:
what is left of the flock after the host's event ended (nothing, in every run: the end's "follow the avatar" and its
let-go find no member, so the stale swap of BUG-0800 changes nothing a player sees; the ticket is closed as not
reproduced and the test stays as the regression of the host's own flock after a guest's). 

  SPORE_LANE=7 python3 tools/tests/coop_creature_flock_forget.py
"""
import re
import time

from coop_party_defence import drama, pack
from twoinst import Checks, start


def summon(con):
    """(event pointer, active) of the summon kind in `coop.drama`"""
    m = re.search(r'summon\s+started=\d+ refused=\d+ event=(\S+) active=(-?\d+)', drama(con))
    return (m.group(1), int(m.group(2))) if m else (None, -1)


def avatar_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(v) for v in m.groups()) if m else None


def host_npcs(host):
    """[(ptr, distance to the host's avatar, position)] of the host's NPCs"""
    return [(m.group(1), float(m.group(2)), tuple(float(m.group(i)) for i in (3, 4, 5))) for m in re.finditer(
        r'#\d+ (\S+) species=\S+ hp=[\d.]+ dist=([\d.]+).*? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
        host.call('coop.npc list')['out'])]


def apart(host, guest, metres):
    hp = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    if not hp:
        return
    x, y, z = (float(v) for v in hp.groups())
    r = (x * x + y * y + z * z) ** 0.5
    ax = (-y, x, 0.0) if abs(z) < 0.9 * r else (0.0, -z, y)
    n = (ax[0] ** 2 + ax[1] ** 2 + ax[2] ** 2) ** 0.5
    g = (x + ax[0] / n * metres, y + ax[1] / n * metres, z + ax[2] / n * metres)
    print('  guest:', guest.call('coop.npc gopos %.1f %.1f %.1f' % g)['out'].strip()[:90], flush=True)


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    time.sleep(4)
    apart(host, guest, 150)
    time.sleep(4)
    # 2. the guest's flock, made by the host's game
    p0 = {m['ptr'] for m in pack(host) if not m['dead']}
    r = guest.call('coop.rules roar 33')['out'].strip()
    print('  guest:', r, flush=True)
    if 'no avatar' in r:
        print('  guest:', guest.call('coop.drama start 8')['out'].strip().splitlines()[0], flush=True)
    p1 = p0
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(1)
        p1 = {m['ptr'] for m in pack(host) if not m['dead']}
        if p1 - p0:
            break
    print(f'  host pack {len(p0)} -> {len(p1)}; host drama: ' + ' | '.join(ln.strip() for ln in drama(host).splitlines()),
          flush=True)
    ck(bool(p1 - p0), "the host's game made the guest's flock")
    if not p1 - p0:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # 3. the summon event ends by itself
    t0 = time.time()
    ev, act = summon(host)
    while time.time() - t0 < 240 and act == 1:
        time.sleep(2)
        ev, act = summon(host)
    print(f'  the host\'s summon event {ev} active={act} after {time.time() - t0:.0f} s', flush=True)
    ck(act == 0, 'the summon event of the guest\'s flock ended in the host\'s game')
    # 4. the host's own Summon Flock
    q0 = {m['ptr'] for m in pack(host) if not m['dead']}
    print('  host:', host.call('coop.drama start 8')['out'].strip().splitlines()[0], flush=True)
    ev2, act2 = summon(host)
    print(f'  host summon event {ev2} active={act2} (the guest\'s was {ev})', flush=True)
    new = []
    t0 = time.time()
    while time.time() - t0 < 15 and not new:
        time.sleep(1)
        new = [m for m in pack(host) if not m['dead'] and m['ptr'] not in q0]
    ck(bool(new), f"the host's game summoned a flock of its own ({len(new)} new pack members)")
    near = []
    t0 = time.time()
    ptrs = {m['ptr'] for m in new}
    rows = new
    while new and time.time() - t0 < 40:
        time.sleep(2)
        rows = [m for m in pack(host) if m['ptr'] in ptrs and not m['dead']]
        near = [m for m in rows if 0 <= m['dav'] < 25]
        if rows and len(near) * 2 >= len(ptrs):
            break
    print('  the host\'s new members (d_avatar, d_puppet):', [(m['dav'], m['dp']) for m in rows], flush=True)
    print('  host drama:', ' | '.join(ln.strip() for ln in drama(host).splitlines()), flush=True)
    # the event's end: its update (0xD512A0) then posts "follow the avatar" (stimulus 4) to each member and lets go
    # (+0x135 = 0) those over 100 m (50 m out of view) from the avatar; the members leave the pack with the event either
    # way. With the guest's puppet as the avatar they are sent after the guest's creature, 120-140 m away.
    t0 = time.time()
    ev3, act3 = summon(host)
    while new and time.time() - t0 < 240 and act3 == 1:
        time.sleep(2)
        ev3, act3 = summon(host)
    print(f'  the host\'s own summon event {ev3} active={act3} after {time.time() - t0:.0f} s', flush=True)
    start_d, end_d = {}, {}
    t0 = time.time()
    while new and time.time() - t0 < 40:
        gp = avatar_pos(guest)
        for ptr, d_av, pos in host_npcs(host):
            if ptr in ptrs and gp:
                dp = sum((a - b) ** 2 for a, b in zip(pos, gp)) ** 0.5
                start_d.setdefault(ptr, (d_av, dp))
                end_d[ptr] = (d_av, dp)
        time.sleep(2)
    print('  after the event, (d_avatar, d_puppet) first -> last:',
          {p: (start_d[p], end_d[p]) for p in end_d}, flush=True)
    if new:
        ck(len(near) * 2 >= len(ptrs),
           f"the host's new flock comes to the host's creature, not to the guest's ({len(near)} of {len(ptrs)} within 25 m)")
        # (printed, not checked: in every run, the fix on or off, the flock was gone from the game's NPCs after the
        # event's end, so the end's update with the guest's puppet as the avatar has nothing left to send away)
        print(f'  {len(end_d)} of {len(ptrs)} former members still among the host\'s NPCs after the event', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
