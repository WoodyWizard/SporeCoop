#!/usr/bin/env python3
"""BUG-0561: an allied species' nest shows the ally face on the guest's minimap (player report 0.7.5: "an allied
nest shows only the grey discovered mark on my minimap").

The Creature minimap (0xD425A0) gives a nest the ally icon (flag 0x200000, picture 0xB3B6763E in 0xE10560) when its
herd has mbBestFriends (+0x191) set; it never reads the relationship table. The game sets +0x191 on every herd of a
species in ApplyEvent when the relation becomes 6; a guest gets the host's table written into its own and no
ApplyEvent runs in its game (docs/re/creature.md#creature-player-rules).

1. Two games on the creature fixture. The host's relation to a wild species with nest herds becomes "allied"
   (`coop.social record <species> 6 3`: the record the host's game has after "Impress 3 ... to ally").
2. Within 15 s the guest's own table has relation 6 for it (`coop.social species`) and every herd of that species in
   the guest's game has +0x191 set (`coop.rules nests`: the field the minimap's icon reads).
3. The relation back to 0: the guest's herds lose the flag again (warm runs reuse these games).
Not checked: the drawn icon itself (the minimap's icon cache 0xE0DF10 is not read by the test).

  coop_creature_nest_ally.py   (SPORE_FIX_OFF=social.nest_ally: +0x191 stays 0 on the guest, FAILS)"""
import re
import time

from twoinst import Checks, start

T, G = '2B978C46', '40626200'


def flags(con, sp):
    return [int(x) for x in re.findall(r'best_friends=(\d)', con.call(f'coop.rules nests {sp} {T} {G}')['out'])]


def relation(con, sp):
    m = re.search(rf'species {sp}/\S+ relation=(-?\d+)', con.call('coop.social species')['out'], re.I)
    return int(m.group(1)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    sp = None
    for s in dict.fromkeys(re.findall(r'species=([0-9A-F]{8})', host.call('coop.npc list')['out'])):
        if s != hsp and flags(guest, s) and flags(host, s):
            sp = s
            break
    ck(sp is not None, f'a wild species with nest herds in both games ({sp})')
    if not sp:
        return ck.result()
    print('  guest before: ' + str(flags(guest, sp)), flush=True)
    print('  ' + host.call(f'coop.social record {sp} 6 3')['out'].strip(), flush=True)
    rel = got = None
    t0 = time.time()
    while time.time() - t0 < 15:
        rel, got = relation(guest, sp), flags(guest, sp)
        if rel == 6 and got and all(got):
            break
        time.sleep(1)
    print(f'  guest after {time.time() - t0:.1f} s: relation {rel}, herds +0x191 {got}', flush=True)
    ck(rel == 6, f'the guest\'s own table has the alliance ({rel})')
    ck(bool(got) and all(got), f'every herd of {sp} in the guest\'s game is the player\'s best friend (+0x191 {got})')
    host.call(f'coop.social record {sp} 0 0')
    t0 = time.time()
    while time.time() - t0 < 15:
        back = flags(guest, sp)
        if back and not any(back):
            break
        time.sleep(1)
    ck(bool(back) and not any(back), f'the alliance gone: the guest\'s herds lose the flag ({back})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
