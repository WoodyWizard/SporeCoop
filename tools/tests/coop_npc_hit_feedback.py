#!/usr/bin/env python3
"""Creature: what a guest's game shows of its own hit on an animal (BUG-0717, class BUG-0428).

On a guest a hit on an NPC proxy goes to the host (MSG_DAMAGE_REQ) and npc::damage_filter returns false: the
game's cCreatureAnimal::TakeDamage does not run on the proxy (the host's game is the one that applies it). The
question of the ticket: does the guest lose the hit's feedback that way? This test hits like the players do
(the guest's TakeDamage call with its avatar as the attacker, `coop.npc hit`) and reads the guest's own game:

1. the proxy's health in the guest's game (the game's combatant health) drops, and how long after the hit;
2. the animal reacts in the guest's game as in the host's: after the hit the proxy plays the animation the host's
   NPC plays (`coop.npc cur`: GetCurrentAnimation of both, sampled for 4 s) at least once;
3. the guest's TakeDamage on the proxy was filtered (`coop.hp dmglog`): no second health change, no second throw,
   no stimulus on the proxy (no AI);
for three NPCs. Reported: the delay of 1 per hit (the host's state interval 0.1 s plus the round trip).
No switch: it records what the guest sees (the run is the ticket's evidence).
"""
import re
import time

from coop_creature_kills import family, npcs, proxies
from twoinst import Checks, start

CUR = re.compile(r'^cur (\d+) anim=([0-9A-F]{8})', re.M)


def cur(con):
    return {m.group(1): m.group(2) for m in CUR.finditer(con.call('coop.npc cur')['out'])}


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(4)
    fam = family(host)
    done = 0
    delays = []
    for _ in range(12):
        h, g = npcs(host), proxies(guest)
        cand = sorted((n[3], i) for i, n in h.items() if i in g and not n[4] and n[2] > 2 and n[1] != fam)
        if not cand:
            time.sleep(2)
            continue
        nid = next((i for _, i in cand if i not in [d[0] for d in delays]), None)
        if nid is None:
            break
        guest.call(f'coop.social goto {nid}')
        time.sleep(1.5)
        hp0 = proxies(guest).get(nid, (0, 0, 0))[2]
        hh0 = npcs(host).get(nid, (0, 0, 0))[2]
        guest.call('coop.hp dmglog clear')
        t0 = time.time()
        print(f'  #{nid}: guest hits ({guest.call(f"coop.npc hit {nid} 1.0")["out"].strip()[:80]}), hp {hp0} (host {hh0})', flush=True)
        seen = None
        while time.time() - t0 < 2.0:
            px = proxies(guest).get(nid)
            if px and px[2] < hp0 - 0.5:
                seen = time.time() - t0
                break
            time.sleep(0.03)
        same, samples = 0, 0
        t1 = time.time()
        while time.time() - t1 < 4.0:
            ha, ga = cur(host).get(nid), cur(guest).get(nid)
            samples += 1
            same += ha is not None and ha == ga
            time.sleep(0.25)
        log = guest.call('coop.hp dmglog')['out']
        filtered = 'filtered' in log
        print(f'  #{nid}: proxy hp {hp0} -> {proxies(guest).get(nid, (0, 0, "?"))[2]} after {seen if seen is None else round(seen, 2)} s, '
              f'host {npcs(host).get(nid, (0, 0, "?"))[2]}; same animation {same}/{samples}; dmglog filtered={filtered}', flush=True)
        ck(seen is not None, f'1. #{nid}: the hit shows in the guest\'s game: the proxy\'s health drops ({seen} s)')
        ck(same > 0, f'2. #{nid}: the animal plays the host\'s animation in the guest\'s game after the hit ({same}/{samples})')
        ck(filtered, f'3. #{nid}: the guest\'s game did not apply the hit a second time (TakeDamage filtered)')
        delays.append((nid, seen))
        done += 1
        if done >= 3:
            break
    ck(done >= 3, f'three animals hit ({done})')
    print(f'  delays: {delays}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
