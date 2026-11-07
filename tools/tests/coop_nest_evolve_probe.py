#!/usr/bin/env python3
"""Probe (BUG-0639: "for the host the nest is evolving, the guest can attack them"): what an NPC nest does on the host
after its species dies out and after the pack's brain upgrade, and what the guest's game shows and may hit there.

Creature fixture. 1. the nearest nest of another species: the host's avatar kills its members (coop.nest kill) until
the species is extinct; 2. the pack's DNA crosses a brain threshold (coop.dna add); then for --secs both games print
every 15 s: the herds with an evolution pending or done (coop.nest evolve: +0x85, generation, evolved keys, extinct,
members), the host's members of that herd, the guest's mirrors within 25 m of its home; at the end the guest hits a
mirror there (the damage filter's way) and the host's log says whether its game took the hit.

  coop_nest_evolve_probe.py [--secs S] [--no-dna]"""
import argparse
import re
import time

from coop_nest_eggs import nest_herds
from twoinst import Checks, start


def near_mirrors(guest, home, r=25.0):
    out = []
    for line in guest.call('coop.npc list')['out'].splitlines():
        # the guest's rows: '#id species=X spawned hp=H dist=D ... at=(x y z)' (the host's have pos=)
        m = re.match(r'\s*#(\d+) species=(\S+) spawned hp=([\d.]+) .*? at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and sum((float(m.group(4 + i)) - home[i]) ** 2 for i in range(3)) < r * r:
            out.append((m.group(1), m.group(2), m.group(3), 'DEAD' in line))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=180)
    ap.add_argument('--no-dna', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    time.sleep(3)
    ns = nest_herds(host)
    print('  nests of other species (dist, herd, members, home):', ns[:4], flush=True)
    if not ck(ns, 'a nest of another species with members on the host'):
        return ck.result()
    _, h, _, home = ns[0]
    host.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 12, home[1], home[2] + 2))
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 14, home[1], home[2] + 2))
    time.sleep(5)
    print('  before: guest mirrors near the nest', near_mirrors(guest, home), flush=True)
    for _ in range(4):
        print('  host:', host.call(f'coop.nest kill {h} 20')['out'].strip(), flush=True)
        time.sleep(4)
        if any(f'herd {h}' in l and 'extinct=1' in l for l in host.call('coop.nest evolve')['out'].splitlines()):
            break
    if not a.no_dna:
        # the guest waits at the next nest: right after the DNA its herd evolves on the host; the guest hits a live
        # mirror of it at once and the host's game says whether its member took the hit (an "evolving" member)
        h2, home2 = (ns[1][1], ns[1][3]) if len(ns) > 1 else (None, None)
        if home2:
            guest.call('coop.npc gopos %.2f %.2f %.2f' % (home2[0] + 8, home2[1], home2[2] + 2))
            time.sleep(4)
        print('  host:', host.call('coop.dna add 250')['out'].strip().splitlines()[0], flush=True)
        if home2:
            for _ in range(3):
                ms = [m for m in near_mirrors(guest, home2, 30) if not m[3]]
                rows = [l for l in host.call('coop.nest evolve')['out'].splitlines() if h2 in l]
                print(f'  next nest {h2}: host {rows[:1]}; guest live mirrors {ms[:3]}', flush=True)
                if ms:
                    hp0 = re.search(rf'#{ms[0][0]} \S+ species=\S+ hp=([\d.]+)', host.call('coop.npc list')['out'])
                    print('  guest hits', ms[0], ':', guest.call(f'coop.npc hit {ms[0][0]} 3')['out'].strip(), flush=True)
                    time.sleep(2)
                    hp1 = re.search(rf'#{ms[0][0]} \S+ species=\S+ hp=([\d.]+)', host.call('coop.npc list')['out'])
                    print(f'  the host\'s member #{ms[0][0]} hp {hp0 and hp0.group(1)} -> {hp1 and hp1.group(1)}', flush=True)
                time.sleep(4)
    t0, last = time.time(), 0
    while time.time() - t0 < a.secs:
        if time.time() - last >= 15:
            last = time.time()
            s = f'{time.time() - t0:4.0f}s'
            for who, con in (('host', host), ('guest', guest)):
                rows = [l for l in con.call('coop.nest evolve')['out'].splitlines()
                        if ' gen=0 ' not in l or 'extinct=1' in l or h in l]  # (+0x85 is 1 from the herd's constructor 0xC6C2B0)
                for l in rows[:10]:
                    print(f'  [{s}] {who} {l}', flush=True)
            print(f'  [{s}] host herd: ' + ' | '.join(host.call(f'coop.nest herd {h}')['out'].strip().splitlines()[:6]), flush=True)
            print(f'  [{s}] guest mirrors near the nest: {near_mirrors(guest, home)}', flush=True)
        for con in (host, guest):
            if 'cinematic' in con.call('coop.status')['out'] and 'playing' in con.call('coop.status')['out']:
                con.call('key enter')
        time.sleep(3)
    ms = [m for m in near_mirrors(guest, home) if not m[3]]
    if ms:
        print('  guest hits mirror', ms[0], ':', guest.call(f'coop.npc hit {ms[0][0]} 5')['out'].strip(), flush=True)
        time.sleep(3)
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
