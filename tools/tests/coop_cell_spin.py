#!/usr/bin/env python3
"""Cell: an NPC creature that both players go for faces the same way on the guest as on the host,
and both players' bites land, the lethal one too (the player report of 0.7.x: "enemies spun
uncontrollably for the guest, switching between facing the host and the guest, so they were almost
impossible to kill even with both attacking").

Both players swim at the nearest NPC creature for a while (the game's own steering towards the
creature, as a click does); the creature's facing (its rotation +0x5C, `coop.sync`) is sampled on
both machines. Before the fix the guest's copy (a soft body whose particles kept their own
orientation) turned 5x as much as the host's cell and was 150 degrees off in 1 of 10 samples.

Then each player bites a creature to death through the game's own Bite 0xE7B0A0 (`coop.pool
realbite`: what a bite event runs, DamageCell or, for a lethal bite, BreakOrSwallow). Before the
fix the guest's lethal bite broke the mirror on the guest only and never reached the host: a
creature of the players' size (6 hp, 3 per bite) could not be killed by the guest at all.

  coop_cell_spin.py [--seconds 25] [--rounds 2] [--without-fix]
"""
import argparse
import math
import re
import time

from coop_sync import angle, pct
from twoinst import Checks, shot, start


def sync(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8:
            try:
                d[f[0]] = (tuple(map(float, f[1:4])), tuple(map(float, f[4:8])))
            except ValueError:
                pass
    return d


def me(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def yaw(q):
    x, y, z, w = q
    return math.degrees(math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)))


def stat(con, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def host_hp(host, key):
    """the creature's hp on the host, -1 when the host removed it"""
    out = host.call(f'coop.pool cell c{key}')['out']
    m = re.search(r'hp=(-?\d+)', out)
    return int(m.group(1)) if m else -1


def bite_to_death(ck, who, con, host, guest, key):
    """<who> bites creature <key> (host index) until the host has it dead"""
    hits0, own0, breaks0 = stat(host, 'hits_in'), stat(host, 'own_hits'), stat(guest, 'breaks_sent')
    hps = [host_hp(host, key)]
    for _ in range(5):
        print('   ', who, con.call(f'coop.pool realbite c{key}')['out'], flush=True)
        # BUG-1383: the host's answer to the bite (its hp, or the cell gone after a lethal one), up to 5 s: the host
        # removed a creature of the guest's lethal bite more than 1 s later (lab 20261120-133703: hp stayed 3 after
        # "kill applied"), and a bite on the guest's broken mirror sends nothing; the old fixed 1 s let the test bite
        # a third time and count that bite as lost
        t0 = time.time()
        while True:
            time.sleep(0.25)
            hp = host_hp(host, key)
            if hp != hps[-1] or time.time() - t0 > 5:
                break
        hps.append(hp)
        if hps[-1] <= 0:
            break
    print(f'    {who}: the creature\'s hp on the host {hps}', flush=True)
    ck(len(hps) > 2 and hps[1] < hps[0], f'the {who}\'s first bite takes hp on the host ({hps[0]} -> {hps[1]})')
    ck(hps[-1] <= 0, f'the {who}\'s bites kill it on the host (hp {hps})')
    if who == 'guest':
        ck(stat(host, 'hits_in') - hits0 == len(hps) - 1, f'every guest bite reached the host (hits_in +{stat(host, "hits_in") - hits0})')
        ck(stat(guest, 'breaks_sent') > breaks0, 'the lethal bite (BreakOrSwallow on the guest) went to the host')
    else:
        ck(stat(host, 'own_hits') > own0, f'the host\'s bites took hp (own_hits +{stat(host, "own_hits") - own0})')


def turned(v):
    return sum(abs(((b - a + 180) % 360) - 180) for a, b in zip(v, v[1:]))


def nearest_creature(host, guest, skip):
    """the nearest NPC creature of the host (key c<host index>) that the guest mirrors"""
    gs = sync(guest)
    best = None
    for line in host.call('coop.pool prey 60')['out'].splitlines():
        m = re.match(r'npc (\d+) .*\| me d=([\d.]+)', line)
        if m and 'c' + m.group(1) in gs and m.group(1) not in skip and (best is None or float(m.group(2)) < best[0]):
            best = (float(m.group(2)), m.group(1))
    return best[1] if best else None


def chase(host, guest, key, seconds, through=2.0):
    """both players swim at creature <key> for <seconds>, aiming <through> m past its centre (they
    ram it as players do, rather than stop on it); returns the facing samples"""
    diffs, hy, gy, near = [], [], [], 0
    t0 = time.time()
    while time.time() - t0 < seconds:
        hs, gs = sync(host), sync(guest)
        if 'c' + key not in hs or 'c' + key not in gs:
            break
        (hp, hq), (gp, gq) = hs['c' + key], gs['c' + key]
        for con, p in ((host, hp), (guest, gp)):
            m = me(con)
            if m:  # swim at it (the game steers the avatar there, as after a click on it)
                dx, dy = p[0] - m[0], p[1] - m[1]
                k = 1 + through / max(0.5, math.hypot(dx, dy))
                con.call(f'coop.pool goto {dx * k:.2f} {dy * k:.2f}')
        hm, gm = me(host), me(guest)
        if hm and gm and math.dist(hm, hp[:2]) < 6 and math.dist(gm, gp[:2]) < 6:
            near += 1
        diffs.append(angle(hq, gq))
        hy.append(yaw(hq))
        gy.append(yaw(gq))
        time.sleep(0.15)
    return diffs, hy, gy, near


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=25)
    ap.add_argument('--rounds', type=int, default=2)
    ap.add_argument('--without-fix', action='store_true', help='the guest as in 0.7.2 (coop.pool mirrorfix 0): must fail')
    args = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    print(' ', guest.call(f'coop.pool mirrorfix {0 if args.without_fix else 1}')['out'])
    time.sleep(3)
    diffs, th, tg, near, done = [], 0.0, 0.0, 0, []
    for r in range(args.rounds):
        key = nearest_creature(host, guest, done)
        if not key:
            print('  no NPC creature near the host')
            break
        done.append(key)
        d, hy, gy, n = chase(host, guest, key, args.seconds)
        diffs += d
        th += turned(hy)
        tg += turned(gy)
        near += n
        print(f'  round {r + 1}: creature c{key}, {len(d)} samples ({n} with both players within 6 m): facing difference '
              f'median {pct(d, 0.5):.1f} p90 {pct(d, 0.9):.1f} deg; turned on the host {turned(hy):.0f} deg, on the guest '
              f'{turned(gy):.0f} deg', flush=True)
        if r == 0:
            shot(host, 'cell_spin_host.png')
            shot(guest, 'cell_spin_guest.png')
    gst = guest.call('coop.pool')['out']
    print(' ', next((l for l in gst.splitlines() if l.startswith('cell(guest)')), gst).strip())
    ck(len(diffs) >= 40, f'facing samples taken ({len(diffs)})')
    ck(near >= 10, f'both players close to the creature ({near} samples within 6 m of both)')
    # the guest shows the host's state of 0.1-0.2 s ago (snapshots at 10 Hz): a quick turn on the
    # host shows up there a little later, so a few samples are caught mid-turn
    ck(pct(diffs, 0.9) < 30, f'the creature faces the same way on both machines (90% of samples within {pct(diffs, 0.9):.1f} deg)')
    ck(tg < th * 1.6 + 90, f'and does not spin on the guest (turned {tg:.0f} deg there, {th:.0f} deg on the host)')
    for name, con in (('host', host), ('guest', guest)):  # how often the chase got to a bite (information)
        print(f'  {name}: collision pairs with creatures {stat(con, "creature_pairs")}, bite events {stat(con, "bite_events")}')

    # both players bite a creature to death (each its own one)
    for who, con in (('guest', guest), ('host', host)):
        key = nearest_creature(host, guest, done)
        if not key:
            ck(False, f'a creature for the {who} to bite')
            continue
        done.append(key)
        bite_to_death(ck, who, con, host, guest, key)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
