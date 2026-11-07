#!/usr/bin/env python3
"""Cell stage: a guest's kill and a guest's meal happen once in the host's world (class BUG-0420).

--case kill (BUG-0632, player report 2026-11-06 "a giant piece of meat"): the guest bites an NPC creature
of the host's pool to death with the game's own bite (Bite 0xE7B0A0: BreakOrSwallow on the lethal one,
`coop.pool kill`) and keeps biting it for 1.5 s, as a player whose cell stays on the body does. The
host's break plays its animation before the death flag and the loot (event 5), so the guest's mirror
still looks alive and its game breaks it again. Checked in the host's game, per kill: BreakOrSwallow ran
once (`npc_deaths`: the game's breaks and kills of NPCs of its pool, each one a death with its own loot drop;
the SpawnLoot calls are printed, a break makes several through its loot table).

--case eat (BUG-0636, "I can sometimes eat the same big plant's part twice"): the guest eats a plant or
meat cell of the host's pool (the game's eat handler EatFood, `coop.pool eat`) and its mouth stays on the
spot (the eat repeated every 40 ms for 1.5 s). Over a delayed link (SPORE_NET delay) the state chunks
already on the way still list the cell: checked in the guest's game, the eaten cell does not come back
there (no cell of that host index for 1.5 s, the repeated eats find nothing), and the shared food rose
once on both games.

  coop_cell_once.py --case kill|eat [--rounds N]   (SPORE_FIX_OFF=cell.kill_once / cell.eat_once: fails)
"""
import argparse
import re
import time

from twoinst import Checks, start


def stat(con):
    out = con.call('coop.pool')['out']
    d = {}
    for k in ('npc_deaths', 'loot_calls', 'food', 'kills_in', 'kills_sent', 'eats_sent', 'kills_dup_sent',
              'kills_dup_refused', 'eaten_skipped', 'gone_again'):
        m = re.search(r'\b' + k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else 0
    return d


def kill_case(host, guest, ck, rounds):
    done = 0
    for i in range(rounds * 3):
        if done >= rounds:
            break
        k = guest.call('coop.pool kill')['out']
        m = re.match(r'bit (c\d+) ', k)
        if not m:
            print(f'  (guest: {k})')
            guest.call('coop.pool tp 30 0')
            time.sleep(2)
            continue
        key = m.group(1)
        guest.call(f'coop.pool swimto {key} tp')
        host.call('coop.pool tp 2 1')  # the host watches from next to the guest
        time.sleep(0.5)
        h0 = stat(host)
        lethal = False
        for _ in range(12):
            k = guest.call(f'coop.pool kill {key}')['out']
            if k == 'gone':
                break
            g = stat(guest)
            if g['kills_sent'] + g['kills_dup_sent'] > 0 and stat(host)['kills_in'] > h0['kills_in']:
                lethal = True
                break
            time.sleep(0.6)
        if not lethal:
            print(f'  guest could not kill {key}: {k}')
            continue
        bites = 0
        t0 = time.time()
        while time.time() - t0 < 1.5:  # the guest's cell stays on the body and bites on
            r = guest.call(f'coop.pool kill {key}')['out']
            bites += r.startswith('bit ')
            if r == 'gone':
                break
            time.sleep(0.3)
        time.sleep(3.0)  # the break's animation, its loot (event 5) and DestroyCell
        h1 = stat(host)
        deaths, loots = h1['npc_deaths'] - h0['npc_deaths'], h1['loot_calls'] - h0['loot_calls']
        done += 1
        print(f'  kill {done} of {key}: guest bit it {bites} more times; host: kills_in {h1["kills_in"] - h0["kills_in"]}, '
              f'breaks/deaths {deaths}, SpawnLoot calls {loots}, refused again {h1["kills_dup_refused"] - h0["kills_dup_refused"]}')
        ck(deaths == 1, f'kill {done}: the host\'s game broke the creature once ({deaths})')
        guest.call('coop.pool tp 25 0')
        time.sleep(1.0)
    ck(done >= rounds, f'the guest killed {done} of {rounds} creatures')


def eat_case(host, guest, ck, rounds):
    done = 0
    tries = 0
    while done < rounds and tries < rounds * 4:
        tries += 1
        # the nearest plant / meat cell of the host's pool, mirrored on the guest
        out = guest.call('coop.pool food_near 30')['out']
        m = re.search(r'food (c\d+) (\S+) dist=', out)
        if not m:
            print(f'  (guest: {out.strip()[:120]})')
            guest.call('coop.pool tp 20 0')
            time.sleep(2)
            continue
        key, kind = m.group(1), m.group(2)
        guest.call(f'coop.pool swimto {key} tp')
        time.sleep(0.6)
        h0, g0 = stat(host), stat(guest)
        r = guest.call(f'coop.pool eat {key}')['out']
        pm = re.search(r'points=(\d+)', r)
        pts = int(pm.group(1)) if pm else 0
        if pts <= 0:
            print(f'  guest could not eat {key}: {r}')
            continue
        again, back = 0, 0
        t0 = time.time()
        while time.time() - t0 < 1.5:  # the mouth stays on the spot
            r2 = guest.call(f'coop.pool eat {key}')['out']
            if r2.startswith('ate '):  # its mirror was made again and the guest's game ate it once more
                again += 1
            elif 'no such' not in r2:
                back += 1
            time.sleep(0.04)
        time.sleep(3.0)  # totals settle (a refused meal's points are taken back)
        h1, g1 = stat(host), stat(guest)
        done += 1
        print(f'  meal {done}: guest ate {kind} {key} (+{pts}); came back {back} times, eaten again {again} times; '
              f'food host {h0["food"]} -> {h1["food"]}, guest {g0["food"]} -> {g1["food"]}; skipped {g1["eaten_skipped"] - g0["eaten_skipped"]}')
        ck(again == 0, f'meal {done}: the eaten {kind} did not come back on the guest, it could not be eaten again ({again} times)')
        ck(h1['food'] == g1['food'], f'  both games have one food total ({h1["food"]} / {g1["food"]})')
    ck(done >= rounds, f'the guest ate {done} of {rounds} cells')


def locks(con):
    m = re.search(r'own_locks=(\d+) anim=(-?\d+)', con.call('coop.pool tokens')['out'])
    return (m.group(1), int(m.group(2))) if m else ('?', -1)


def token_case(host, guest, ck, rounds):
    """probe (BUG-0633): the host's own collect locks after a guest's kill next to it"""
    for i in range(rounds):
        k = guest.call('coop.pool kill')['out']
        m = re.match(r'bit (c\d+) ', k)
        if not m:
            guest.call('coop.pool tp 30 0')
            host.call('coop.pool tp 2 1')
            time.sleep(2)
            continue
        key = m.group(1)
        guest.call(f'coop.pool swimto {key} tp')
        host.call('coop.pool tp 2 1')
        time.sleep(0.5)
        print(f'  before kill {i}: host locks {locks(host)}', flush=True)
        for _ in range(12):
            r = guest.call(f'coop.pool kill {key}')['out']
            if 'dying' in r or r == 'gone' or stat(guest)['kills_sent'] > 0 and 'hp=0' in r:
                break
            time.sleep(0.6)
        for t in range(8):
            print(f'    +{t * 0.5:.1f} s host locks {locks(host)} guest locks {locks(guest)}', flush=True)
            time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('kill', 'eat', 'token'), required=True)
    ap.add_argument('--rounds', type=int, default=4)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    {'kill': kill_case, 'eat': eat_case, 'token': token_case}[a.case](host, guest, ck, a.rounds)
    g = stat(guest)
    h = stat(host)
    print(f'  guest: kills_sent={g["kills_sent"]} dup_sent={g["kills_dup_sent"]} eats_sent={g["eats_sent"]} gone_again={g["gone_again"]} '
          f'eaten_skipped={g["eaten_skipped"]}; host: kills_in={h["kills_in"]} dup_refused={h["kills_dup_refused"]}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
