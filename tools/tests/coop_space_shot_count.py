#!/usr/bin/env python3
"""Space: a host NPC ship that fires again after a short pause shows one shot on the guest (BUG-1046).

The host forgot a shooter 1 s after its last shot and a new shot restarted its count at 1; the guest kept the old
entry (with the shots it already fired) until about 2.3 s after the last shot. A shot in that window made the guest's
game fire 4 copies (old count >= 2) or none (old count 1).

Fixture space_empire, both on its colony planet 01460035 (weapons fire on planets; in a star system FireTool refused
every shot, lab 20261113-012916-2). The host spawns a ship of its own empire (no enemy near: it does not fight on its
own); `coop.fleet fire <id>` makes the host's game fire that ship's weapon at the
host's ship once (the game's FireTool through the hook, as cCombatSimulator does). The guest's game fires its mirror's
weapon for each shot the host relays; `fired=` of `coop.fleet list` counts the guest game's FireTool calls that
fired (the game's own answer).
The count is per ship, the reload per weapon (one weapon took no second shot within 2.1 s, lab 20261113-013314), so the
shots alternate the ship's weapon slots (`coop.fleet fire <id> <slot>`):
  A. one shot, 1.5 s later one of another weapon: the guest's game fires 1 for it (0.7.13: 0)
  B. two shots under 1 s apart, 1.5 s later one of a third weapon: 1 (0.7.13: 4)
SPORE_FIX_OFF=space.shot_count: 0 / 4 (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_shot_count.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, start

PLANET = '01460035'  # space_empire's colony in the home system


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=(\w+) hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d['me' if m.group(6) else int(m.group(1))] = (int(m.group(2)), m.group(3), float(m.group(4)))
    return d


def shots(con, key='fired'):
    line = next((ln for ln in con.call('coop.fleet list')['out'].splitlines() if ln.startswith('space shots:')), '')
    m = re.search(r'\b' + key + r'=(\d+)', line)
    return int(m.group(1)) if m else -1


def fire(host, sid):
    out = host.call(f'coop.fleet fire {sid}')['out'].strip()
    return out


def main():
    host, guest = start('space_empire')
    ck = Checks()
    ph = Player('host', lane.HOST, 0, 1, time.time() + 900, {})
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 900, {})
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.land_retry(PLANET)
    ck(ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet', f'both on the colony planet ({ph.place()} {pg.place()})')
    time.sleep(3)
    before = set(ships(host))
    print(' ', host.call('coop.fleet spawn 2 own')['out'].strip(), flush=True)
    time.sleep(2)
    hs = ships(host)
    new = [k for k in hs if k != 'me' and k not in before and k >= 0]
    ck(new, f'the host spawned a ship ({new})')
    if not new:
        ck.faults(host=host, guest=guest)
        return ck.result()
    sid = new[0]
    ok = False
    for _ in range(20):
        if sid in ships(guest):
            ok = True
            break
        time.sleep(0.5)
    ck(ok, f'the guest mirrors ship #{sid}')
    # warm-up: one shot, the guest fires it (the weapon and the path work at all)
    f0 = shots(guest)
    print('  warm-up:', fire(host, sid), flush=True)
    time.sleep(3)
    f1 = shots(guest)
    print(f'  warm-up: guest fired {f0} -> {f1}', flush=True)
    ck(f1 - f0 == 1, f"one shot of the host's ship is one shot in the guest's game ({f1 - f0})")
    time.sleep(3)  # both games forget the shooter

    m = re.search(r'slots[= ]([\d,]+)', fire(host, f'{sid} 99'))
    slots = [int(x) for x in m.group(1).split(',')] if m else []
    print(f'  the ship\'s weapon slots: {slots}', flush=True)

    def shoot(slot, until):
        """fire `slot` until the game takes the shot (its reload) or `until`; the time it left, else None"""
        while time.time() < until:
            if 'fired=1' in fire(host, f'{sid} {slot}'):
                return time.time()
            time.sleep(0.05)
        return None

    def case(first, last, what):
        """shots of the slots `first` (each as soon as the game takes it, under 1 s apart), then `last` 1.5 s after
        the last of them: the host forgot the shooter at 1.0 s, the guest keeps it until about 2.3 s"""
        t = None
        for sl in first:
            t = shoot(sl, time.time() + (8 if t is None else 0.9))
            if t is None:
                print(f'  {what}: slot {sl} did not fire in time', flush=True)
                time.sleep(4)
                return None
        n = len(first)
        time.sleep(1.2)
        g1 = shots(guest)
        while time.time() - t < 1.5:
            time.sleep(0.02)
        t2 = shoot(last, t + 2.0)
        if t2 is None:
            print(f'  {what}: slot {last} did not fire 1.5-2.0 s after the last', flush=True)
            time.sleep(4)
            return None
        time.sleep(2.0)
        g2 = shots(guest)
        print(f'  {what}: {n} shot(s), then one {t2 - t:.2f} s later: the guest\'s game fired {g2 - g1} for it', flush=True)
        time.sleep(4)  # both forget the shooter; the weapons reload
        return g2 - g1

    res = {}
    if len(slots) >= 2:
        res['A'] = case([slots[0]], slots[1], 'A (one shot, a pause)')
    if len(slots) >= 3:
        res['B'] = case([slots[0], slots[1]], slots[2], 'B (two shots, a pause)')
    got = {k: v for k, v in res.items() if v is not None}
    print(f'  shots after a pause, fired by the guest\'s game: {got}', flush=True)
    if not got:
        print('RESULT: SKIP (the ship has no two weapons that fired in time)', flush=True)
        ck.faults(host=host, guest=guest)
        return 0
    for k, v in got.items():
        ck(v == 1, f"{k}: the shot after the pause is one shot in the guest's game ({v}; 0.7.13: 0 for A, 4 for B)")
    print('  host ', next((ln for ln in host.call('coop.fleet list')['out'].splitlines() if ln.startswith('space shots:')), ''))
    print('  guest', next((ln for ln in guest.call('coop.fleet list')['out'].splitlines() if ln.startswith('space shots:')), ''))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
