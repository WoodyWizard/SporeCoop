#!/usr/bin/env python3
"""BUG-1527: a guest's kill while the host is away counts on its game and reaches the host's game when it is back.

Player report (0.7.15.5): "when I kill a creature (guest), the count to extinction goes up one, then back to zero".
The host was in its editor for minutes; the guest hunted its own game's animals; each kill turned its game's species
record from killed 0 to 1 and the guest wrote the host's last table over it within 0.5 s (social.cpp enforce_table).

1. Creature fixture. A species of the host's table (`coop.social species` on the host: a record with the Creature
   key's type and group, not allied, not extinct), its killed count k0 on both games.
2. The host counts as away on the guest (`coop.awaytest 1`: its editor, the away rules; `host_away=1`).
3. The guest's game applies one kill of that species (`coop.social event <species> 0`: the game's ApplyEvent 0 with
   no creature, the path a kill of the guest's own animal takes through the hook).
4. After 2 s the guest's game record still has killed k0 + 1 (the game's own record, read from its table).
5. The host is back (`coop.awaytest 0`); after 4 s the host's game record has killed k0 + 1 (the guest's kill
   replayed through the host's ApplyEvent, MSG_SOCIAL K_SOLO), and the guest's too (the host's next table agrees).
   Key frame: both games, the note says what the guest's hunt card of the species should show.

  SPORE_FIX_OFF=social.solo_table   FAILS (step 4: the guest's record back to k0 within 0.5 s; step 5: the host never
                                    counts the kill)
"""
import re
import time

from twoinst import Checks, keyframe, start

TYPE, GROUP = '2B978C46', '40626200'
REC = re.compile(r'species ([0-9A-F]{8})/([0-9A-F]{8})/([0-9A-F]{8}) relation=(-?\d+) x=\d+ impressed=(\d+) killed=(\d+)')


def table(con):
    """{species instance: (relation, impressed, killed)} of the game's relationship table"""
    out = con.call('coop.social species')['out']
    return {m.group(1): (int(m.group(4)), int(m.group(5)), int(m.group(6)))
            for m in REC.finditer(out) if m.group(2) == TYPE and m.group(3) == GROUP}


def wait_away(guest, want, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if f'host_away={want}' in guest.call('coop.awaytest')['out']:
            return True
        time.sleep(0.5)
    return False


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        return run(host, guest, ck)
    finally:
        guest.call('coop.awaytest 0')


def run(host, guest, ck):
    time.sleep(5)
    ht, gt = table(host), table(guest)
    cands = [k for k, v in ht.items() if v[0] not in (5, 6) and k in gt]
    print(f'  host species {len(ht)}, both {len(cands)}: {cands[:6]}', flush=True)
    ck(bool(cands), 'a species record on both games that is neither allied nor extinct')
    if not cands:
        ck.faults(host=host, guest=guest)
        return ck.result()
    sp = cands[0]
    k0 = ht[sp][2]
    print(f'  species {sp}: host {ht[sp]} guest {gt[sp]}', flush=True)
    print('  guest: ' + guest.call('coop.awaytest 1')['out'].strip(), flush=True)
    ck(wait_away(guest, 1), 'the guest counts the host as away')
    print('  guest: ' + guest.call(f'coop.social event {sp} 0')['out'].strip(), flush=True)
    time.sleep(2)
    g1 = table(guest).get(sp)
    print(f'  guest after its kill: {g1}; {guest.call("coop.social solo")["out"].strip().splitlines()[0]}', flush=True)
    ck(g1 is not None and g1[2] == k0 + 1,
       f"the guest's game still counts its kill of {sp} 2 s later while the host is away (killed {k0} -> {g1 and g1[2]})")
    print('  guest: ' + guest.call('coop.awaytest 0')['out'].strip(), flush=True)
    ck(wait_away(guest, 0), 'the host is back on the guest')
    time.sleep(4)
    h2, g2 = table(host).get(sp), table(guest).get(sp)
    print(f'  back: host {h2} guest {g2}; host {host.call("coop.social solo")["out"].strip().splitlines()[0]}', flush=True)
    ck(h2 is not None and h2[2] == k0 + 1, f"the host's game counts the guest's kill of {sp} (killed {k0} -> {h2 and h2[2]})")
    ck(g2 is not None and g2[2] == k0 + 1, f"the guest's game keeps the kill after the host's table came (killed {g2 and g2[2]})")
    keyframe('solo_kill', f"both games after the host's return: the hunt card of species {sp} (if shown) counts the "
             "guest's kill on both screens", settle=0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
