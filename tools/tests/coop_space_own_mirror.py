#!/usr/bin/env python3
"""Space: a partner's colony in the host's own star system is not the host's colony for raids (BUG-1487, audit SPC-13)
nor for an armada contract, and neither is an NPC empire's colony (BUG-1486, audit SPC-11).

The guest's new colony in the home system both players own shows in the host's game as a colony of the host's empire
(the own-star mirror of BUG-0543: tech 5, the host's political id). 0.7.16.1 took it for the host's own:
* a raid on it (the helper raid space_allies starts while the host is on that planet, or any raid there) told the guest
  "<host>'s colony is raided" for the guest's own colony (and with 3+ players paid a bonus twice);
* an armada contract (the auto offer every 10 minutes, or an explicit planet) could target it, or any NPC empire's
  colony (tech level 5 is every empire's colony).

1. The guest founds a colony on a free planet of the home system (`coop.space allies colonize`); the host's game shows it
   as a colony of the host's empire within 30 s (BUG-0543).
2. A raid starts on that planet in the host's game (`allies raid <PlanetID>`: the game's StartRaid, as the helper raid).
   Over 8 s the guest's game must get no raid notice of the host (`raids_told` unchanged, no "colony is raided") and the
   host's game must not count it as a raid on its colony. SPORE_FIX_OFF=space.own_mirror_raids: FAIL.
3. The host offers an armada contract against that planet and against an NPC empire's colony near it (`contracts offer
   armada <PlanetID>`): both refused ("no colony of the host"); one against the host's own colonies is offered.
   SPORE_FIX_OFF=contracts.host_colony: the guest's planet is taken as the target (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_own_mirror.py
"""
import re
import time

from coop_space_allies_own import planet_line, tech
from harness import sp, waiter
from twoinst import Checks, keyframe, start

wait = waiter(step=1.0, final=True)


def num(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con))
    return int(m.group(1)) if m else -1


def offer(host, arg):
    out = sp(host, 'contracts offer armada', arg).strip()
    if out.startswith('offered'):
        print(f'  armada against {arg}: {out} | ' + sp(host, 'contracts').strip()[:200], flush=True)
        sp(host, 'contracts cancel')
    else:
        print(f'  armada against {arg}: {out}', flush=True)
    return out


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    star = re.search(r'star (\d+)', sp(guest, 'star')).group(1)
    hs = sp(host, 'allies star', star)
    host_active = re.search(r'active ([0-9A-F]+)', sp(host, 'star')).group(1)
    guest_active = re.search(r'active ([0-9A-F]+)', sp(guest, 'star')).group(1)
    free = [m.group(1) for m in re.finditer(r"planet ([0-9A-F]{8}) '[^']*' tech=([0-3])\b", hs)
            if m.group(1) not in (host_active, guest_active)]
    ck(free, f'a free planet in the home system {star}')
    if not free:
        return ck.result()
    pid = free[0]

    # 1. the guest's colony in the home system: an own-star mirror colony in the host's game
    print('  guest colonizes:', sp(guest, 'allies colonize', pid).replace('\n', ' | '), flush=True)
    ok = wait(lambda: tech(planet_line(host, pid)) == 5, 30)
    print('  host planet:', planet_line(host, pid), '| host allies:', sp(host, 'allies list').replace('\n', ' | ')[:300], flush=True)
    ck(ok and ' OWN' in sp(host, 'allies list'), f'the host\'s game shows {pid} as the guest\'s colony in its own system')
    if not ok:
        return ck.result()

    # 2. a raid on it in the host's game
    raids0, told0 = num(host, 'raids'), num(guest, 'raids_told')
    print('  host raid:', sp(host, 'allies raid', pid, 3).strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 8:
        time.sleep(1)
    raids1, told1 = num(host, 'raids'), num(guest, 'raids_told')
    print(f'  host raids {raids0} -> {raids1} (own_mirror_raids={num(host, "own_mirror_raids")}); '
          f'guest raids_told {told0} -> {told1}', flush=True)
    ck(told1 == told0, f'the guest got no "the host\'s colony is raided" for its own colony ({told0} -> {told1})')
    ck(raids1 == raids0, f'the host\'s game did not count a raid on its own colony ({raids0} -> {raids1})')
    keyframe('own_mirror_raid', 'the guest after a raid started on its colony in the host\'s game: no "colony is raided" '
             'notice of the host on its screen', players={'host': host, 'guest': guest})

    # 3. armada targets
    out = offer(host, '0x' + pid)
    ck(not out.startswith('offered'), f'no armada against the guest\'s colony {pid} ({out})')
    npc = re.search(r'npccolony ([0-9A-F]{8}) (\d+) ([0-9A-F]+)', sp(host, 'allies npccolony'))
    if npc:
        out = offer(host, '0x' + npc.group(1))
        ck(not out.startswith('offered'), f'no armada against an NPC empire\'s colony {npc.group(1)} (owner {npc.group(3)}): {out}')
    else:
        print('  (no NPC colony near the host: that case not checked)', flush=True)
    out = offer(host, '')
    ck(out.startswith('offered'), f'an armada against a colony of the host\'s own empire is offered ({out})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
