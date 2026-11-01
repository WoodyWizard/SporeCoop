#!/usr/bin/env python3
"""Cell: a knocked-out cell eats nothing, on either machine (BUG-0364).

Seen (lab 20261101-023131 cell_coop, guest): knocked out at s 46.46, then "own cell ate +12 plant"
twice while still out; the respawn came with food 56 instead of 32, so a bigger "small again" body.
The knockout holds the cell in place at hp 1, but the game's contact events still ran: food that
touched it was eaten (event 0x12, EatFood 0xE7DF40 -> AvatarEat -> AddFoodPoints, shared food).

1. The guest swims away from the host; the host bites it lethally (`coop.pool hurt`): it is knocked
   out (its own game: downed=1, hp 1).
2. While it is out, its mouth touches food: the game's eat event for the nearest plants
   (`coop.pool eat <key>` calls EatFood, the function the contact event calls) several times.
3. The shared food total on BOTH games did not grow by the knocked-out cell's meals (only by what
   the other player ate itself in that time, its own_points).
4. The same with the host knocked out (`coop.pool selfhit 50` with the guest far away): the host's
   meals while out add nothing on either game.
SPORE_FIX_OFF=cell.ko_no_eat must FAIL (the knocked-out cell eats: +12 per plant).

  SPORE_LANE=7 python3 tools/tests/coop_cell_ko_food.py
"""
import re
import time

from twoinst import Checks, start


def pool(con):
    out = con.call('coop.pool')['out']
    d = {}
    for k in ('hp', 'food', 'downed', 'own_points', 'ko_eats_blocked', 'respawns'):
        m = re.search(r'\b' + k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else None
    return d


def plants(con, n=4, radius=120):
    """the nearest plants this game shows (its screen; a few tries: the pool around a new spot fills)"""
    keys = []
    for _ in range(8):
        out = con.call(f'coop.pool see {radius}')['out']
        keys = [m.group(1) for m in re.finditer(r'see (c\d+) plant ', out)][:n]
        if len(keys) >= 2:
            break
        time.sleep(1)
    return keys


def where(con):
    out = con.call('coop.pool')['out']
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\).*partner_dist=(-?[\d.]+) revive_dist=(-?[\d.]+)', out)
    return m.groups() if m else out.splitlines()[0][:200]


def wait_downed(con, want, timeout=6.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if pool(con)['downed'] == want:
            return True
        time.sleep(0.25)
    return False


def knocked_out_meals(ck, name, eater, other, knock):
    """`eater` is knocked out by `knock()`, then touches plants; the totals of both games are checked."""
    knock()
    print(f'  {name} at the knockout: pos, partner_dist, revive_dist = {where(eater)}', flush=True)
    ck(wait_downed(eater, 1), f'{name}: knocked out (its own game says downed=1, hp {pool(eater)["hp"]})')
    e0, o0 = pool(eater), pool(other)
    keys = plants(eater)
    ate = []
    for k in keys:
        r = eater.call(f'coop.pool eat {k}')['out'].strip()
        ate.append(f'{k}: {r}')
        time.sleep(0.6)
    time.sleep(2.0)  # the host's count reaches the guest (MSG_CELL_FOOD)
    e1, o1 = pool(eater), pool(other)
    print(f'  {name} knocked out, touched {len(keys)} plants: ' + '; '.join(ate), flush=True)
    print(f'  {name}: food {e0["food"]} -> {e1["food"]}, other game {o0["food"]} -> {o1["food"]}, '
          f'other ate itself +{(o1["own_points"] or 0) - (o0["own_points"] or 0)}, '
          f'blocked {e0["ko_eats_blocked"]} -> {e1["ko_eats_blocked"]}', flush=True)
    still = e1['downed'] == 1
    ck(len(keys) >= 2, f'{name}: plants to touch ({len(keys)})')
    ck(still, f'{name}: still knocked out after its meals (downed {e1["downed"]})')
    other_ate = (o1['own_points'] or 0) - (o0['own_points'] or 0)
    for who, a, b in ((f'{name} (knocked out)', e0, e1), ('the other game', o0, o1)):
        grew = (b['food'] or 0) - (a['food'] or 0)
        ck(a['food'] is not None and grew <= other_ate,
           f'{name}: no food for a knocked-out cell on {who} ({a["food"]} -> {b["food"]}, '
           f'the other player ate +{other_ate} itself)')
    # the plants it touched on its screen afterwards (information only: plants drift off the screen and
    # the pool's cells eat them too; with the fix off all of them were gone)
    left = set(re.findall(r'see (c\d+) ', eater.call('coop.pool see 120')['out']))
    print(f'  {name}: plants it touched still on its screen: {len([k for k in keys if k in left])} of {len(keys)}', flush=True)
    # let the knockout end (respawn after 10 s)
    t0 = time.time()
    while time.time() - t0 < 14 and pool(eater)['downed'] == 1:
        time.sleep(0.5)
    time.sleep(3.5)  # invulnerable for 3 s after a respawn


def main():
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30:
        m = re.search(r'mirrors=(\d+)', guest.call('coop.pool')['out'])
        if m and int(m.group(1)) > 10:
            break
        time.sleep(1)
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'

    # 1-3: the guest knocked out, far enough from the host that nobody revives it
    def away(con):
        """30 m from the partner (a cell that has just hatched is put back for a moment: tried again)"""
        for _ in range(4):
            con.call('coop.pool tp 30 0')
            time.sleep(1.5)
            w = where(con)
            if isinstance(w, tuple) and float(w[2]) > 15:
                return
        print(f'  could not move 30 m away: {where(con)}', flush=True)

    def knock_guest():
        away(guest)
        print(' ', host.call(f'coop.pool hurt {gid} 50')['out'].strip(), flush=True)

    knocked_out_meals(ck, 'guest', guest, host, knock_guest)

    # 4: the host knocked out
    def knock_host():
        away(guest)
        for i in range(4):  # plants on the host's screen to touch
            if len(plants(host)) >= 2:
                break
            host.call('coop.pool goto %d 0' % (20 if i % 2 == 0 else -20))
            time.sleep(4)
        print(' ', host.call('coop.pool selfhit 50')['out'].strip(), flush=True)

    knocked_out_meals(ck, 'host', host, guest, knock_host)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
