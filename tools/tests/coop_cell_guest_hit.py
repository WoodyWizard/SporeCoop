#!/usr/bin/env python3
"""Cell: the host applies a guest's bite through the game's own Bite / DamageCell (BUG-0714, class BUG-0428).

The host applied a guest's bite (MSG_CELL_HIT) with a hand-made copy of DamageCell's gate and
`hp -= dmg`, and a guest's lethal bite (MSG_CELL_EAT ACT_BREAK) with `hp = 0` and BreakOrSwallow. What the
game does around a bite was lost: the bitten creature's hurt animation (PlayAnimation 0x10, its event 2
and the after-hurt reaction), Bite's own lethal rule, and the knockout gate of BUG-0509 on the lethal
bite (a knocked-out guest's bite in flight still killed).

1. Control: the host's own bite on the creature plays its hurt animation in the host's game (`anim=10`,
   the game's +0x1B0 of `coop.pool cell`), and the hp drops.
2. The guest bites the same creature three times (`coop.pool kill c<key> hit` and the game's Bite): each
   time the creature plays its hurt animation in the HOST's game and the host's hp drops.
3. The guest is knocked out (the host's bite on it) and a lethal bite of it reaches the host anyway
   (`coop.pool sendact c<key> break`: an old guest's, or one in flight): the creature lives on in the host's game.
4. After the knockout a lethal bite of the guest still kills it (the game's Bite on the host breaks it).

SPORE_FIX_OFF=cell.guest_hit_game must FAIL at 2 and 3.
"""
import re
import time

from coop_cell_ko_food import wait_downed, where
from twoinst import Checks, start


def num(out, key, cast=int, base=10):
    m = re.search(r'\b' + key + r'=(-?[\dA-Fa-f.]+)', out)
    if not m:
        return None
    return int(m.group(1), base) if cast is int else cast(m.group(1))


def host_cell(host, key):
    return host.call(f'coop.pool cell {key}')['out']


def hurt_seen(host, key, seconds=1.2):
    """polls the creature in the host's game: (hurt animation seen, lowest hp)"""
    t0, anim, low = time.time(), False, None
    while time.time() - t0 < seconds:
        st = host_cell(host, key)
        a, hp = num(st, 'anim', int, 16), num(st, 'hp')
        anim = anim or a == 0x10
        if hp is not None:
            low = hp if low is None else min(low, hp)
        if 'no such cell' in st or 'dying=1' in st:
            break
        time.sleep(0.05)
    return anim, low


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    key, r = None, ''
    for _ in range(15):
        r = guest.call('coop.pool kill - hit')['out']
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'the guest found a host creature ({r.strip()})')
    if not key:
        return ck.result()
    print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'].strip())
    time.sleep(2.5)

    # 1. control: the host's own bite
    host.call(f'coop.pool swimto {key} tp')
    time.sleep(2.5)
    h0 = num(host_cell(host, key), 'hp')
    print('  host bite: ' + host.call(f'coop.pool kill {key} hit')['out'].strip()[:160])
    anim, low = hurt_seen(host, key)
    ck(anim and low is not None and h0 is not None and low < h0,
       f'1. control: the host\'s own bite plays the hurt animation in the host\'s game (anim 0x10 {anim}, hp {h0} -> {low})')

    # 2. the guest's bites
    for i, how in enumerate(('hit', 'hit', '')):
        host.call(f'coop.pool swimto {key} tp')
        guest.call(f'coop.pool swimto {key} tp')
        time.sleep(2.5)  # the last bite's hurt animation and lock are over
        h0 = num(host_cell(host, key), 'hp')
        r = guest.call(f'coop.pool kill {key} {how}'.strip())['out'].strip()
        anim, low = hurt_seen(host, key)
        print(f'  guest bite {i + 1} ({how or "the game Bite"}): {r[:120]} | host hp {h0} -> {low}, hurt anim {anim}', flush=True)
        ck(low is not None and h0 is not None and low < h0, f'2.{i + 1} the host took the guest\'s bite ({h0} -> {low})')
        ck(anim, f'2.{i + 1} the creature plays its hurt animation in the host\'s game after the guest\'s bite')
    print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines() if x.startswith('cell(host)'))[-160:])

    # 3. a knocked-out guest's lethal bite in flight
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'
    for _ in range(4):  # 30 m from the host's cell (and the creature): no rescue
        guest.call('coop.pool tp 30 0')
        time.sleep(1.5)
        w = where(guest)
        if isinstance(w, tuple) and float(w[2]) > 15:
            break
    print('  ' + host.call(f'coop.pool hurt {gid} 50')['out'].strip(), flush=True)
    downed = wait_downed(guest, 1)
    ck(downed, '3. the guest is knocked out')
    st = host_cell(host, key)
    h0 = num(st, 'hp')
    print('  guest (knocked out): ' + guest.call(f'coop.pool sendact {key} break')['out'].strip(), flush=True)
    time.sleep(1.5)
    st = host_cell(host, key)
    alive = 'no such cell' not in st and 'dying=1' not in st and (num(st, 'hp') or 0) > 0
    print(f'  host: {st.strip()[:200]}', flush=True)
    ck(downed and alive, f'3. a knocked-out guest\'s lethal bite does not kill the creature in the host\'s game (hp {h0} -> {num(st, "hp")})')

    # 4. after the knockout, the guest's lethal bite kills it
    t0 = time.time()
    while time.time() - t0 < 16 and wait_downed(guest, 1, 0.3):
        time.sleep(0.5)
    time.sleep(3.5)
    killed = False
    if alive:
        for _ in range(3):
            guest.call(f'coop.pool sendact {key} break')
            time.sleep(1.5)
            st = host_cell(host, key)
            if 'no such cell' in st or 'dying=1' in st or (num(st, 'hp') or 0) <= 0:
                killed = True
                break
            time.sleep(10)  # kill_once (BUG-0632) holds a refused kill for 10 s
        print(f'  host after the lethal bite: {st.strip()[:200]}', flush=True)
        ck(killed, '4. after the knockout the guest\'s lethal bite kills the creature in the host\'s game')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
