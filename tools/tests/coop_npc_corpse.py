#!/usr/bin/env python3
"""Where the body of a killed NPC lies on the host and on the guest (player report 2026-11-01:
"bodies of other creatures are desynced after killing").

1. Two games on the creature fixture; both sample their NPCs every frame (`coop.npctrack`: the host its
   own NPCs, the guest its proxies; the object's position and the model's position, AnimatedCreature
   +0x04, where the body is drawn).
2. The host kills N NPCs near both players (`coop.npc hosthit`, a just-lethal hit: the game's own death
   and knockback on the host; the guest plays its own death on the proxy).
3. For SETTLE..END s after each death the host's and the guest's bodies are compared (the model
   position; the object's too). PASS: every body within LIMIT m of the host's (median over the frames).

  coop_npc_corpse.py [--kills N] [--secs S] [--by host|guest]"""
import argparse
import math
import os
import re
import time

from twoinst import Checks, shot, start

LIMIT = 1.5
SETTLE = 3.0


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.npctrack count')['out']).group(1))
    for i in range(0, n, 2000):
        for line in con.call(f'coop.npctrack dump {i} 2000')['out'].splitlines():
            f = line.split()
            if len(f) < 28:
                continue
            rows.append({'t': float(f[0]), 'id': int(f[1]), 'how': int(f[2]), 'flags': int(f[3]),
                         'd': tuple(map(float, f[5:8])), 'tgt': tuple(map(float, f[11:14])),
                         'anim': f[17], 'm': tuple(map(float, f[25:28]))})
    return rows


def host_npcs(con):
    return {int(m.group(1)): (float(m.group(2)), float(m.group(3)), 'DEAD' in m.group(0))
            for m in re.finditer(r'^\s*#(\d+) \S+ species=\S+ hp=([\d.]+) dist=([\d.]+)( DEAD)?', con.call('coop.npc list')['out'], re.M)}


def proxies(con):
    return {int(m.group(1)): (float(m.group(2)), bool(m.group(3)))
            for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned hp=\S+ dist=([\d.]+) .*?( DEAD)?(?= want=)', con.call('coop.npc list')['out'], re.M)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kills', type=int, default=3)
    ap.add_argument('--secs', type=float, default=15)
    ap.add_argument('--by', default='host', choices=('host', 'guest'), help='who kills (guest: coop.npc hit, its damage request)')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    for c in filter(None, os.environ.get('NPC_CORPSE_GUEST_CMDS', '').split(';')):
        print('  guest:', c, guest.call(c.strip())['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not proxies(guest):
        time.sleep(1)
    time.sleep(2)
    for c in (host, guest):
        c.call('coop.npctrack start 80')
    tt0 = time.time()
    kills = []
    for _ in range(a.kills):
        hs, gs = host_npcs(host), proxies(guest)
        cand = sorted((d, i) for i, (hp, d, dead) in hs.items() if not dead and hp > 0 and i in gs and d < 25 and i not in [k for k, _ in kills])
        if not cand:
            break
        nid = cand[0][1]
        hp = hs[nid][0]
        if a.by == 'guest':
            out = guest.call(f'coop.npc hit {nid} {hp + 0.5:.2f}')['out'].strip()
        else:
            out = host.call(f'coop.npc hosthit {nid} {hp + 0.5:.2f}')['out'].strip()
        kills.append((nid, time.time() - tt0))
        print(f'  kill #{nid} (hp {hp}, {cand[0][0]:.0f} m from the host): {out[:80]}', flush=True)
        time.sleep(a.secs / a.kills)
    time.sleep(SETTLE + 2)
    shot(guest, 'npc_corpse_guest')
    shot(host, 'npc_corpse_host')
    for c in (host, guest):
        c.call('coop.npctrack stop')
    hr, gr = dump(host), dump(guest)
    ck(len(kills) >= 1, f'the host killed NPCs the guest mirrors ({[k for k, _ in kills]})')
    for nid, tk in kills:
        h = [r for r in hr if r['id'] == nid and (r['flags'] & 1)]
        g = [r for r in gr if r['id'] == nid and (r['flags'] & 1)]
        if not h or not g:
            ck(False, f'#{nid}: dead on both ({len(h)} host rows, {len(g)} guest rows)')
            continue
        hd, gd = h[0]['t'], g[0]['t']
        hs_ = [r for r in h if r['t'] > hd + SETTLE]
        gs_ = [r for r in g if r['t'] > gd + SETTLE]
        if not hs_ or not gs_:
            ck(False, f'#{nid}: rows after the body settled ({len(hs_)} host, {len(gs_)} guest)')
            continue
        hm, gm = hs_[-1]['m'], gs_[-1]['m']
        hp_, gp = hs_[-1]['d'], gs_[-1]['d']
        dm, dp = math.dist(hm, gm), math.dist(hp_, gp)
        hr_ = math.sqrt(sum(x * x for x in hm))
        gr_ = math.sqrt(sum(x * x for x in gm))
        # the body over its own object (model vs object) on each machine
        hmo, gmo = math.dist(hm, hp_), math.dist(gm, gp)
        print(f'  #{nid}: body (model) host {tuple(round(x, 2) for x in hm)} guest {tuple(round(x, 2) for x in gm)}: '
              f'{dm:.2f} m apart (radius host {hr_:.2f} guest {gr_:.2f}); objects {dp:.2f} m apart; model-object '
              f'host {hmo:.2f} guest {gmo:.2f}; anim host {hs_[-1]["anim"]} guest {gs_[-1]["anim"]}', flush=True)
        ck(dm <= LIMIT, f'#{nid}: the guest shows the body where the host has it ({dm:.2f} m <= {LIMIT} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
