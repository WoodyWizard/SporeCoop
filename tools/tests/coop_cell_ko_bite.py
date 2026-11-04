#!/usr/bin/env python3
"""Cell: a knocked-out cell cannot bite anybody (BUG-0509).

Player reports (0.7.3.6): "after KO I could still damage with my corpse's mouth and kill", "I got knocked out but
I could still attack". The knockout holds the cell at hp 1 (invulnerable, translucent), but the game's attack code
still ran for the avatar: its bites hurt and killed (a guest's through MSG_CELL_HIT on the host).

1. The guest swims 30 m away from the host; the host bites it lethally (`coop.pool hurt`): knocked out.
2. While it is out it bites the nearest creature (the first bite names it) and BITES times more through the game's own Bite (`coop.pool realbite`,
   what the bite event runs). That creature's health in the HOST's game (`coop.pool cell c<key>` on the host,
   the owner of the NPCs) must not drop, and it must not die.
3. The same with the host knocked out (`coop.pool selfhit 50`, the guest far away): the host's bites change
   nothing in its own game.

  coop_cell_ko_bite.py   (SPORE_FIX_OFF=cell.ko_no_bite: the knocked-out cell's bites land, FAILS)"""
import re
import time

from coop_cell_ko_food import pool, wait_downed, where
from twoinst import Checks, start

BITES = 4


def bite(con, key=None):
    """the knocked-out cell bites (the nearest creature within 200 m, or c<key>): (host key, text)"""
    out = con.call(f'coop.pool realbite {"c" + key if key else 200}')['out'].strip()
    m = re.search(r'bit c(\d+)', out)
    return (m.group(1) if m else None), out[:60]


def host_hp(host, key):
    m = re.search(r'hp=(-?\d+)', host.call(f'coop.pool cell c{key}')['out'])
    return int(m.group(1)) if m else None  # None: gone (killed and removed)


def knocked_out_bites(ck, name, biter, host, knock):
    knock()
    ck(wait_downed(biter, 1), f'{name}: knocked out (its own game says downed=1, hp {pool(biter)["hp"]})')
    # the first bite names the creature (the nearest one); its health on the host is read after it
    key, out = bite(biter)
    ck(key is not None, f'{name}: a creature to bite near it (c{key}: {out})')
    if key is None:
        return
    time.sleep(0.5)
    hp0 = host_hp(host, key)
    low = hp0
    outs = [out]
    for _ in range(BITES):
        if pool(biter)['downed'] != 1:
            break
        outs.append(bite(biter, key)[1])
        time.sleep(0.4)
        h = host_hp(host, key)
        low = h if h is None or low is None else min(low, h)
    time.sleep(1.0)
    h = host_hp(host, key)
    low = h if h is None or low is None else min(low, h)
    st = re.search(r'ko_bites_blocked=\d+ ko_hits_refused=\d+', biter.call('coop.pool')['out'])
    print(f'  {name}: bites {outs}; c{key} on the host {hp0} -> low {low}; {st.group(0) if st else ""}', flush=True)
    ck(hp0 is not None and low is not None and low >= hp0,
       f'{name}: the knocked-out cell\'s bites did not hurt c{key} in the host\'s game ({hp0} -> {low})')
    t0 = time.time()  # the knockout ends (respawn after 10 s), then 3 s invulnerable
    while time.time() - t0 < 14 and pool(biter)['downed'] == 1:
        time.sleep(0.5)
    time.sleep(3.5)


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

    def away(con):
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

    knocked_out_bites(ck, 'guest', guest, host, knock_guest)

    def knock_host():
        away(guest)
        time.sleep(3)  # the guest's respawn reaches the host (a knocked-out partner cannot rescue)
        for _ in range(3):
            print(' ', host.call('coop.pool selfhit 50')['out'].strip(), flush=True)
            if wait_downed(host, 1, 3):
                return
            time.sleep(4)  # died the vanilla way: the respawn, then again

    knocked_out_bites(ck, 'host', host, host, knock_host)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
