#!/usr/bin/env python3
"""Tribe: a guest's member eats on its own, and the guest sees it (BUG-0889, the report behind BUG-0573: "tribe
members do not eat on their own, they have to be ordered to eat").

A guest's mirrors run no behaviour tree (tribe.cpp h_BTTick): a member eats only in the host's game, by the host's
copy's own tree, and the guest sees it through the relays (the eat animation, tribe.mirror_anims; the hunger,
tribe.mirror_hunger). Two games on the tribe fixture:
1. a member the guest owns (`owner=` of the host's `coop.tribe` rows; members are dealt round-robin) is made hungry
   in the host's game (`coop.tribe hunger <id> 15`, the game's own setter) and given no order;
2. within 120 s the HOST's game feeds it by itself: its hunger (cCreatureBase +0xBBC) rises by 30 or more;
3. the guest's mirror shows it: its hunger in the guest's game rises the same way (within 5 of the host's 5 s after
   the meal), and the animation the host's member's tree started for the meal (`bhv=` of the host's
   `coop.tribe anims` while its hunger rose) is the mirror's current animation in the guest's game at least once.
SPORE_FIX_OFF=tribe.mirror_anims,tribe.mirror_hunger must FAIL at 3 (the guest sees no meal).

  SPORE_LANE=N python3 tools/tests/coop_tribe_guest_eat.py
"""
import re
import time

from twoinst import Checks, game_runs, start

ROW = re.compile(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+) .* hunger=([-\d.]+)/')
CUR = re.compile(r'^cur (\d+) anim=([0-9A-F]{8}) bhv=([0-9A-F]{8})', re.M)


def rows(con):
    """{id: (owner, hp, hunger)} of the player tribe"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        if line.startswith('tribe '):
            player = '(player)' in line
            continue
        m = ROW.match(line)
        if m and player and m.group(1) != '0':
            out[int(m.group(1))] = (int(m.group(2)), float(m.group(3)), float(m.group(4)))
    return out


def anims(con):
    return {int(m.group(1)): (m.group(2), m.group(3)) for m in CUR.finditer(con.call('coop.tribe anims')['out'])}


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(3)
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = int(gm.group(1)) if gm else 1
    h = rows(host)
    print(f'  host members: {h}', flush=True)
    print('  host food: ' + host.call('coop.wsync dump tribe_food')['out'].strip()[:200], flush=True)
    mine = [i for i, r in h.items() if r[0] == gid]
    ck(mine, f'a member of the player tribe is the guest\'s (owner {gid})')
    if not mine:
        return ck.result()
    mid = mine[0]
    print(f'  host #{mid}: ' + host.call(f'coop.tribe hunger {mid} 15')['out'].strip(), flush=True)
    t0 = time.time()
    base_h, meal_at, meal_anims, seen_g, last = None, None, set(), [], None
    while time.time() - t0 < 120:
        hr, gr = rows(host), rows(guest)
        ha, ga = anims(host), anims(guest)
        hh = hr.get(mid, (0, 0, None))[2]
        gh = gr.get(mid, (0, 0, None))[2]
        if base_h is None:
            base_h = hh
        if hh is not None and gh is not None:
            last = (time.time() - t0, hh, gh)
        if mid in ga:
            seen_g.append((time.time() - t0, ga[mid][0]))
        if hh is not None and base_h is not None and hh > base_h + 5 and mid in ha:
            meal_anims.add(ha[mid][1])  # the animation its tree started while the hunger rose
            if meal_at is None:
                meal_at = time.time() - t0
                print(f'  s {meal_at:.1f}: the host\'s member eats: hunger {base_h:.1f} -> {hh:.1f}, anim {ha[mid]}', flush=True)
        if meal_at is not None and time.time() - t0 > meal_at + 8:
            break
        time.sleep(0.4)
    hr, gr = rows(host), rows(guest)
    hh, gh = hr.get(mid, (0, 0, -1))[2], gr.get(mid, (0, 0, -1))[2]
    print(f'  end: host hunger {hh:.1f}, guest mirror {gh:.1f}; meal animations {sorted(meal_anims)}; '
          f'the mirror\'s animations {sorted({a for _, a in seen_g})}', flush=True)
    print('  host anims: ' + host.call('coop.tribe anims')['out'].strip().splitlines()[-1], flush=True)
    print('  guest anims: ' + guest.call('coop.tribe anims')['out'].strip().splitlines()[-1], flush=True)
    ck(meal_at is not None and hh >= (base_h or 0) + 30,
       f'2. the host\'s game fed the guest\'s member by itself, no order (hunger {base_h} -> {hh:.1f})')
    ck(gh >= 0 and abs(gh - hh) <= 5, f'3. the guest\'s mirror is as fed as the host\'s member ({gh:.1f} / {hh:.1f})')
    shown = meal_at is not None and any(a in meal_anims for t, a in seen_g if t >= meal_at - 4)
    ck(shown, f'3. the guest\'s mirror plays the meal\'s animation ({sorted(meal_anims)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
