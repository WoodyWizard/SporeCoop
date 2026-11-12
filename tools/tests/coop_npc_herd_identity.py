#!/usr/bin/env python3
"""BUG-0843: a guest's copy of a host animal is never put into another herd that the game built where the species'
nest herd was (Creature, guest).

The guest caches the nest herd of each species (learnt from its own NPCs) and reused it while `object_alive` said
so: that reads the herd's own memory. A herd the game destroyed and freed, whose block a new herd took (CreateHerd
gives it back within frames), passed, and the next proxy of the species was made into that herd of another species.

Two games on the creature fixture. On the guest, for up to --tries proxies whose herd is a cached nest herd:
  1. `coop.npc reusehunt <herd> 150 xH`: the game destroys the nest herd (DestroyInstance), then once a frame a
     herd of the guest's own species is made until the game gives it the freed block;
  2. `coop.npc reusehunt p<id> 1 x`: the game destroys the proxy's creature, so the guest makes it again (in its
     species' herd: proxy_herd);
  3. within 9 s the proxy must be a creature of the guest's game again, in a herd of its own species (the game's
     +0x1674: `herd_sp=1` in `coop.npc list`), never in the new herd of another species (the game's Create refuses
     that one: the host's animal was then gone from the guest's game for good).
PASS: every such proxy is made again in a herd of its species, and no game faulted.
SPORE_FIX_OFF=npc.herd_identity must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_npc_herd_identity.py [--tries 2]"""
import argparse
import re
import time

from twoinst import Checks, start


def plist(con):
    out = con.call('coop.npc list')['out']
    nests = set(int(m.group(1), 16) for m in re.finditer(r'nest \S+ herd=([0-9A-Fa-f]+) alive=1', out))
    prox = {}
    for line in out.splitlines():
        m = re.match(r'\s*#(\d+) species=(\S+) spawned .*?ptr=(\S+) herd=([0-9A-Fa-f]+).*?herd_sp=(-?\d)', line)
        if m:
            prox[int(m.group(1))] = (m.group(2), int(m.group(3), 16), int(m.group(4), 16), int(m.group(5)))
    return nests, prox


def hunt(con):
    out = con.call('coop.npc reusehunt')['out']
    m = re.search(r'left=(-?\d+) tries=(\d+) .*?hit=([0-9A-Fa-f]+)', out)
    return {'left': int(m.group(1)), 'tries': int(m.group(2)), 'hit': int(m.group(3), 16)} if m else \
        {'left': 0, 'tries': 0, 'hit': 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=2)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40:
        nests, prox = plist(guest)
        if any(p[2] in nests for p in prox.values()):
            break
        time.sleep(1)
    nests, prox = plist(guest)
    # first the herds the guest made for species without a nest in its world (g_herds: nothing else refreshes them),
    # then a species whose proxies all live in its one cached nest herd (another nest of it would take the cache's
    # place, and the stale entry would never be asked)
    by_sp = {}
    for i, p in prox.items():
        by_sp.setdefault(p[0], set()).add(p[2])
    family = set(int(m.group(1), 16) for m in re.finditer(r'herd=([0-9A-Fa-f]+) .*?fam=1', guest.call('coop.npc list')['out']))
    cand = [i for i, p in sorted(prox.items()) if p[2] not in nests and p[2] not in family and len(by_sp[p[0]]) == 1]
    cand += [i for i, p in sorted(prox.items()) if p[2] in nests and by_sp[p[0]] == {p[2]} and i not in cand]
    cand += [i for i, p in sorted(prox.items()) if p[2] in nests and i not in cand]
    print(f'  candidates {cand}: herds by species {dict((k, sorted(hex(h) for h in v)) for k, v in by_sp.items())}, '
          f'nest herds {sorted(hex(h) for h in nests)}', flush=True)
    ck(cand, f'the guest has proxies in herds it cached ({len(cand)} of {len(prox)}, {len(nests)} nest herds)')
    reused, wrong, used = 0, [], set()
    for i in cand:
        if reused >= a.tries:
            break
        _, prox = plist(guest)
        if i not in prox or prox[i][2] in used:
            continue
        herd = prox[i][2]
        used.add(herd)
        print(f'  proxy #{i} (species {prox[i][0]}) in herd {herd:08X} (nest herd: {herd in nests}): ' +
              guest.call(f'coop.npc reusehunt {herd:X} 150 xH')['out'].strip().splitlines()[0], flush=True)
        t1 = time.time()
        h = hunt(guest)
        while time.time() - t1 < 6 and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit']:
            print(f'  herd {herd:08X}: no herd got its block in {h["tries"]} tries', flush=True)
            continue
        reused += 1
        print(f'  herd {herd:08X}: a herd of another species has its block now (try {h["tries"]})', flush=True)
        # (a herd's destroy takes its members with it: the proxy is made again by itself; else the game destroys it)
        print('  ' + guest.call(f'coop.npc reusehunt p{i} 1 x')['out'].strip().splitlines()[0], flush=True)
        t2, p = time.time(), None
        while time.time() - t2 < 8 and not p:
            time.sleep(1.0)
            _, prox = plist(guest)
            p = prox.get(i)
        time.sleep(1.0)
        _, prox = plist(guest)
        p = prox.get(i) or p
        print(f'  proxy #{i} now: {p and hex(p[1])} in herd {p and hex(p[2])}, herd of its species: {p and p[3]}', flush=True)
        # (made into the other species' herd, or never made again: the game's Create refuses that herd, so the host's
        # animal is gone from the guest's game for good)
        if not p or p[3] == 0 or p[2] == herd:
            wrong.append(i)
    ck(reused >= 1, f'the game gave a freed nest herd\'s block to a new herd ({reused} times)')
    ck(not wrong, f'every proxy was made again in a herd of its own species (not made, or in the other herd: {wrong})')
    print('  guest ' + guest.call('coop.npc')['out'].splitlines()[0][:200], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
