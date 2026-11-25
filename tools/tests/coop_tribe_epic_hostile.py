#!/usr/bin/env python3
"""BUG-1542: Tribe co-op epic creature skips domesticated/friendly species near the village and is hostile.

1. Tribe fixture: domesticate a wild animal right next to the player village (`coop.tribe epic tame`: sets
   its species relation to 6 = allied, places it 5 m from the village, and calls the game's DomesticateAnimal).
2. Trigger the co-op epic (`coop.tribe epic`).
3. Verify:
   - the epic spawned (`is_epic=1`);
   - the epic's species is NOT the domesticated/allied pet's species;
   - the game's own relation check (`cCreature_RelationIs2or5` / `GetRelationship`) sees the epic as hostile
     (`hostile=1`, `rel in (2, 5)`);
   - placed next to a player tribe member (`coop.tribe epic near <id>`), the epic attacks and deals combat
     blows (`blows >= 1`) to the player tribe;
   - 0 faults on host and guest.
SPORE_FIX_OFF=tribe.epic_hostile fails (picks the domesticated pet's species, hostile=0, 0 blows).

  SPORE_LANE=3 python3 tools/tests/coop_tribe_epic_hostile.py
"""
import re
import sys
import time

from twoinst import Checks, start


def epics(con):
    out = con.call('coop.tribe epic stats')['out']
    rows = []
    for line in out.splitlines():
        m = re.match(
            r'epic (\S+) ours=(\d) hp=([-\d.]+)/([-\d.]+) scale=([-\d.]+) herd=\S+ arch=(\w+) gen=(-?\d+) '
            r'scale_mult=([-\d.]+) hp_override=([-\d.]+) dmg_mult=([-\d.]+) blows=(\d+) last=([-\d.]+) max=([-\d.]+) '
            r'species=(\w+) rel=(-?\d+) hostile=(-?\d+)',
            line,
        )
        if m:
            rows.append(dict(
                ptr=m.group(1), ours=m.group(2) == '1', hp=float(m.group(3)), maxhp=float(m.group(4)),
                scale=float(m.group(5)), arch=m.group(6), gen=int(m.group(7)),
                blows=int(m.group(11)), last=float(m.group(12)), max=float(m.group(13)),
                species=m.group(14).upper(), rel=int(m.group(15)), hostile=int(m.group(16)),
            ))
    return rows, out


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(4)

    tame_out = host.call('coop.tribe epic tame')['out'].strip()
    print('  host tame:', tame_out, flush=True)
    tm = re.search(r'ok=(\d) species=([0-9A-Fa-f]+) dom=(\d) rel=(-?\d+) dist=([\d.]+)', tame_out)
    ck(bool(tm and tm.group(1) == '1' and tm.group(3) == '1'),
       f'control: domesticated a wild animal near the village ({tame_out})')
    if not tm:
        return ck.result()
    pet_species = tm.group(2).upper()
    time.sleep(1)

    spawn_out = host.call('coop.tribe epic')['out'].strip()
    print('  host epic:', spawn_out, flush=True)
    sm = re.search(
        r'epic spawned (\S+) is_epic=(\d) hp=([-\d.]+) species=([0-9A-Fa-f]+) player_species=(\d+) skipped=(\d+) '
        r'rel=(-?\d+) hostile=(-?\d+)',
        spawn_out,
    )
    ck(bool(sm and sm.group(2) == '1'), f'co-op epic spawned ({spawn_out})')
    if not sm:
        ck.faults(host=host, guest=guest)
        return ck.result()

    epic_species = sm.group(4).upper()
    epic_rel = int(sm.group(7))
    epic_hostile = int(sm.group(8))
    ck(epic_species != pet_species,
       f'co-op epic skipped domesticated/allied species {pet_species} (picked {epic_species})')
    ck(epic_hostile == 1 and epic_rel in (2, 5),
       f'co-op epic is hostile to the player tribe (hostile={epic_hostile}, rel={epic_rel})')

    # Verify the epic actually attacks a player tribe member when placed nearby
    own = None
    for line in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(\d+) age=1 sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0' and float(m.group(3)) > 0:
            own = m.group(1)
            break
    ck(own is not None, f'control: found live adult player tribe member (#{own})')
    blows_row = None
    if own is not None:
        t0 = time.time()
        while time.time() - t0 < 45:
            host.call(f'coop.tribe epic near {own}')
            time.sleep(5)
            rows, _ = epics(host)
            ours = [r for r in rows if r['ours']]
            if ours and ours[0]['blows'] >= 1:
                blows_row = ours[0]
                break
        rows, stats_out = epics(host)
        print('  stats:\n  ' + stats_out.replace('\n', '\n  '), flush=True)
        ck(blows_row is not None and blows_row['blows'] >= 1,
           f'co-op epic attacks player tribe members (blows={blows_row["blows"] if blows_row else 0}, '
           f'max={blows_row["max"] if blows_row else 0:.1f})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
