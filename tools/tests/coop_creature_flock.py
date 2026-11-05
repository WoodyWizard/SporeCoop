#!/usr/bin/env python3
"""BUG-0556: the creatures the host summons with Summon Flock are the guest's allies too (player report 0.7.5:
"the flock the host calls is not friendly (green) to the guest"; the host's screen shows them with hearts).

Summon Flock is the drama event cSummonCreatureEvent (kind 8, started by the ability's handler 0xD8E840): a herd
of a game species next to the avatar, each member in the host's pack (+0xB58 bit 0x100) and wearing the event's
hearts effect 0x6A17B05A. The game's "friendly" predicate (creature vt 0xD4, read by the rollover and the cursor)
does not read the pack bit; the guest's proxies were strangers of another herd, without hearts
(docs/re/creature.md#creature-player-rules).

1. Two games on the creature fixture, the guest next to the host.
2. The host's game starts its Summon Flock event (`coop.drama start 8`, the game's own StartKind(8) the ability
   calls): the host's pack fills with the flock (`coop.npc list` ` pack`).
3. In the guest's own game every proxy of a flock member answers the game's vt 0xD4 "friendly" and wears the
   hearts effect (GetEffectFromPools 0x6A17B05A), as the host's members wear it.

  coop_creature_flock.py   (SPORE_FIX_OFF=creature.party_allies,npc.flock_hearts: strangers without hearts, FAILS)"""
import re
import time

from twoinst import Checks, start


def friendly(con, nid):
    m = re.search(r'friendly=(\d) pack=(\d) hearts=(\d)', con.call(f'coop.rules friendly {nid}')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (float(p.group(1)) + 3, float(p.group(2)), float(p.group(3))))
    time.sleep(2)
    print('  ' + host.call('coop.drama start 8')['out'].strip().replace('\n', ' | '), flush=True)
    flock = []
    t0 = time.time()
    while time.time() - t0 < 15 and not flock:
        time.sleep(1)
        flock = re.findall(r'#(\d+) \S+ species=\S+ hp=[\d.]+ dist=[\d.]+ .* pack', host.call('coop.npc list')['out'])
    ck(bool(flock), f'the host\'s game summoned a flock into its pack ({flock})')
    if not flock:
        return ck.result()
    time.sleep(3)  # the guest mirrors them (spawn + states)
    host_rows = {n: friendly(host, n) for n in flock}
    rows = {}
    t0 = time.time()
    while time.time() - t0 < 10:
        rows = {n: friendly(guest, n) for n in flock}
        if all(r and r[0] and r[2] for r in rows.values()):
            break
        time.sleep(1)
    print(f'  host (friendly, pack, hearts): {host_rows}', flush=True)
    print(f'  guest (friendly, pack, hearts): {rows}', flush=True)
    print('  guest ' + guest.call('coop.rules')['out'].strip().splitlines()[-1], flush=True)
    mirrored = [n for n, r in rows.items() if r]
    ck(len(mirrored) == len(flock), f'the guest mirrors every flock member ({len(mirrored)} of {len(flock)})')
    ck(all(rows[n][0] for n in mirrored) and mirrored,
       f'every flock member is friendly in the guest\'s own game (vt 0xD4): {[n for n in mirrored if not rows[n][0]]} are not')
    ck(any(host_rows[n] and host_rows[n][2] for n in flock), 'the host\'s flock wears the hearts (the game\'s own effect)')
    ck(all(rows[n][2] for n in mirrored) and mirrored,
       f'every flock member wears the hearts on the guest\'s screen: {[n for n in mirrored if not rows[n][2]]} do not')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
