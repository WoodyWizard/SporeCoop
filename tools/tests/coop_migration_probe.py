#!/usr/bin/env python3
"""Probe (BUG-0640): where a migration starts and what each game does with it, without forcing a world share.

Creature fixture. The host earns DNA (coop.dna add), the shared DNA gives the guest the same total. Every 2 s for
the given time both games print: the migration property 0x514A219, the stage's DNA total, the cinematic, the
avatar's herd, CreatureGame_NestHerdFor(1) and its nest position, the world's player species, the avatar's species.

  coop_migration_probe.py [--dna N] [--secs S] [--editor]   (--editor: the evolution vote and both editors after)"""
import argparse
import time

from coop_migration import DONE, avatar_herd, cinematic, eax, migration, mode, new_nest, total_dna, wait
from coop_ui import Ui
from twoinst import Checks, start


def row(con):
    h, pos = new_nest(con)
    sp = con.call('coop.avatar')['out'].strip().splitlines()[0][:60]
    return (f'mig={migration(con)} dna={total_dna(con):.0f} cine={cinematic(con)} herd={avatar_herd(con) or 0:#x} '
            f'nestherd={h or 0:#x} nest={tuple(round(x) for x in pos) if pos else None} | {sp}')


def evolving(con):
    """herds with an evolution pending or done (BUG-0639)"""
    return [l for l in con.call('coop.nest evolve')['out'].splitlines()
            if 'should_evolve=1' in l or (' gen=' in l and ' gen=0 ' not in l)]


def watch(host, guest, secs, label):
    t0 = time.time()
    last = 0
    while time.time() - t0 < secs:
        if time.time() - last > 20:
            last = time.time()
            for who, con in (('host', host), ('guest', guest)):
                for l in evolving(con)[:8]:
                    print(f'  [{label} {time.time() - t0:5.0f}s] {who} evolve: {l}', flush=True)
        print(f'  [{label} {time.time() - t0:5.0f}s] host  {row(host)}', flush=True)
        print(f'  [{label} {time.time() - t0:5.0f}s] guest {row(guest)}', flush=True)
        for con in (host, guest):
            if cinematic(con):
                con.call('key enter')
        time.sleep(4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dna', type=float, default=250)
    ap.add_argument('--secs', type=float, default=40)
    ap.add_argument('--editor', action='store_true')
    a = ap.parse_args()
    host, guest = start()
    ck = Checks()
    watch(host, guest, 6, 'before')
    host.call(f'coop.dna add {a.dna}')
    watch(host, guest, a.secs, 'dna')
    if a.editor:
        m0 = mode(host)
        print('  ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
        time.sleep(2)
        gu = Ui(guest, 'guest')
        gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
        gu.click('ready')
        ck(wait(lambda: mode(host) != m0 and mode(guest) != m0, 60), 'both players are in their editors')
        host.call('wait.frames 200 60000')
        guest.call('wait.frames 200 60000')
        host.call('click %d %d' % DONE)
        guest.call('click %d %d' % DONE)
        watch(host, guest, a.secs, 'editor')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
