#!/usr/bin/env python3
"""BUG-0994 (Creature, guest, class BUG-0426): a guest's social click never names a copy of an animal its game has
just removed.

social.cpp's h_Post (the avatar posts the social stimulus to the NPC it clicks, inside the game's simulation) asks
npc::proxy_id_of_ai which proxy owns that AI block. It read +0xB4C of every creature key of the proxy table, also of
one the guest's game had removed and freed while the npc tick did not look (the settle second after an avatar change):
another creature the game built at that address matched, and the guest sent K_ENGAGE for the gone proxy (the host
started a social game with its NPC).

Two games on the creature fixture. On the guest, per proxy until --tries hits: `coop.npc settle 6` (the npc tick waits
as after an avatar change), `coop.npc reusehunt p<id> 150 x` (the game destroys the proxy's creature, then creatures
of its species are made until one takes the freed block); on a hit, `coop.social post <hit>` (the avatar's social
stimulus to the new creature through the game's BT_PostStimulus, the hooked call). PASS: the guest engaged no gone
proxy (`engaged ... -> <id>`, K_ENGAGE sent for it) and no game faulted. SPORE_FIX_OFF=npc.ai_lookup_live must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_npc_ai_lookup.py [--tries 1]"""
import argparse
import re
import time

from coop_proxy_identity import hunt, plist
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=1)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40 and sum(1 for v in plist(guest).values() if v[0] == 'spawned') < 3:
        time.sleep(1)
    ids = [i for i, v in sorted(plist(guest).items()) if v[0] == 'spawned']
    ck(len(ids) >= 2, f'the guest has spawned proxies of the host\'s animals ({len(ids)})')
    reused, posted, named = 0, 0, []
    for i in ids[:24]:
        if reused >= a.tries:
            break
        print('  ' + guest.call('coop.npc settle 6')['out'].strip(), flush=True)
        out = guest.call(f'coop.npc reusehunt p{i} 150 x')['out'].strip().splitlines()
        print(f'  proxy #{i}: {out[0] if out else "?"}', flush=True)
        h = hunt(guest)
        t1 = time.time()
        while time.time() - t1 < 5 and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit'] or not h['alive']:
            print(f'  proxy #{i}: no live creature at the freed address ({h["tries"]} tries)', flush=True)
            time.sleep(max(0.0, 6.5 - (time.time() - t1)))
            continue
        reused += 1
        out = guest.call(f'coop.social post {h["hit"]:08X}')['out'].strip()
        print(f'  proxy #{i}: new creature {h["hit"]:08X} as_proxy={h["as_proxy"]}: {out}', flush=True)
        m = re.search(r'engaged (-?\d+) -> (-?\d+), engages \+(\d+)', out)
        if m:
            posted += 1
            if int(m.group(2)) == i:
                named.append(f'#{i} (engages +{m.group(3)})')
        time.sleep(1.5)
        print('  host: ' + ' | '.join(host.call('coop.social games')['out'].strip().splitlines()[:3]), flush=True)
        time.sleep(max(0.0, 6.5 - (time.time() - t1)))
    ck(reused >= 1 and posted >= 1, f'a social click on a new creature at a freed proxy\'s address ({reused} reuses, {posted} posted)')
    ck(not named, f'the guest engaged no proxy whose creature its game had removed ({named})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
