#!/usr/bin/env python3
"""A running NPC's health and death reach the guest (BUG-0334).

npc.kinematic (BUG-0281) places the guest's proxy of a moving host NPC on the host's path. Its first
version went on to the next proxy right after placing it, so the health, the death and the impressed
flag of a running NPC were not applied on the guest until it stood still: an NPC that fled after a
joint hit kept its old health on the guest (`team`: "the guest's mirror shows the health after the
joint hit (4.2 / host 1.95)", smoke 20261030-235253).

1. Two games on the creature fixture; the host picks an NPC the guest mirrors and makes it run back
   and forth (`coop.npc dash`, 4 m/s) for the whole test.
2. While it runs the host hits it (`coop.npc hosthit`, a third of its health): within 3 s the guest's
   game shows the same health on its proxy (the game's health of the creature, `coop.npc list`).
3. Still running, the host kills it: within 3 s the guest's proxy is dead in the guest's game.

  coop_npc_run_hp.py   (SPORE_FIX_OFF=npc.kinematic_sync must FAIL)"""
import re
import time

from twoinst import Checks, start


def host_npcs(con):
    return {int(m.group(1)): (float(m.group(2)), 'DEAD' in m.group(0))
            for m in re.finditer(r'^\s*#(\d+) \S+ species=\S+ hp=([\d.]+) dist=[\d.]+( DEAD)?', con.call('coop.npc list')['out'], re.M)}


def proxies(con):
    return {int(m.group(1)): (float(m.group(2)), bool(m.group(3)))
            for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned hp=([\d.]+) dist=\S+ err=\S+ age=\S+( DEAD)?', con.call('coop.npc list')['out'], re.M)}


def main():
    host, guest = start('creature')
    ck = Checks()
    nid = None
    for _ in range(20):
        hs, gs = host_npcs(host), proxies(guest)
        both = [i for i, (hp, dead) in hs.items() if i in gs and hp > 4 and not dead]
        if both:
            nid = both[0]
            break
        time.sleep(1)
    ck(nid is not None, f'an NPC of the host that the guest mirrors (#{nid})')
    if nid is None:
        return ck.result()
    # long legs (40 m: 10 s of running between the 0.6 s stops) so the hits land while it runs
    print(' ', host.call(f'coop.npc dash {nid} 40 4 60 0')['out'].strip(), flush=True)
    time.sleep(1.5)

    def running():  # the guest's latest state of it says it moves (its proxy is placed on the path)
        for l in guest.call('coop.npc list')['out'].splitlines():
            m = re.match(rf'\s*#{nid} .* spd=([\d.]+) ', l)
            if m:
                return float(m.group(1)) > 0.3
        return False
    t0 = time.time()
    while time.time() - t0 < 10 and not running():
        time.sleep(0.2)
    h0 = host_npcs(host)[nid][0]
    d = round(h0 / 3, 2)
    host.call(f'coop.npc hosthit {nid} {d}')
    time.sleep(0.5)
    h1 = host_npcs(host).get(nid, (None,))[0]
    g1 = None
    t0 = time.time()
    while time.time() - t0 < 3:
        g1 = proxies(guest).get(nid, (None,))[0]
        if g1 is not None and h1 is not None and abs(g1 - h1) <= 0.1:
            break
        time.sleep(0.25)
    gl = [l.strip()[:200] for l in guest.call('coop.npc list')['out'].splitlines() if l.strip().startswith(f'#{nid} ')]
    print(f'  hit {d} while it runs: host {h0} -> {h1}, guest proxy {g1} ({gl[0] if gl else "-"})', flush=True)
    ck(h1 is not None and h1 < h0, f'the host\'s NPC lost health ({h0} -> {h1})')
    ck(g1 is not None and h1 is not None and abs(g1 - h1) <= 0.1,
       f'the guest\'s game shows the running NPC\'s new health ({g1} / host {h1})')
    t0 = time.time()
    while time.time() - t0 < 10 and not running():
        time.sleep(0.2)
    ck(running(), 'it still runs on the guest when the host kills it')
    host.call(f'coop.npc hosthit {nid} 1000')
    dead = False
    t0 = time.time()
    while time.time() - t0 < 3 and not dead:
        dead = proxies(guest).get(nid, (0, False))[1]
        time.sleep(0.25)
    hd = host_npcs(host).get(nid, (0, False))[1]
    ck(hd, 'the host\'s NPC is dead')
    ck(dead, 'the guest\'s proxy of it is dead in the guest\'s game within 3 s')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
