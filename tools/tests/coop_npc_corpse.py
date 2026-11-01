#!/usr/bin/env python3
"""Where the bodies of killed NPCs lie on the host and on the guest (BUG-0383, player report 2026-11-01:
"bodies of other creatures are desynced after killing").

Both games sample their NPCs for 0.6 s at each check (`coop.npctrack`: the host its own NPCs, the guest
its proxies; the model's position, AnimatedCreature +0x04, where the body is drawn). Kinds of deaths:

  stand     a standing NPC, the host's just-lethal hit (`coop.npc hosthit`)
  guest     a standing NPC, the guest's lethal hit request (`coop.npc hit`, applied by the host)
  run       an NPC the host's game walks at 6 m/s (`coop.npctrack walk`), killed while it runs
  thrown    a hit with a push (`coop.npc hosthit <id> <dmg> <push>`, as a charge throws): it flies
  slope     an NPC walked onto the fixture's slope (0.3-0.5) and killed there
  npc       an NPC killed by another NPC (a spawned epic, `coop.npc epic`), no player involved
  later     an NPC killed while the guest is 150 m away (outside the 110 m the host sends it); the
            guest comes back afterwards: a player who walks to the place of a fight sees the bodies
  eaten     the guest eats a body (its own game: a local act); the host's body stays, the guest's
            body is where the host's is until it is eaten (printed: what each game shows)

Checks per body, 3 s and 30 s after the death (a corpse lies 60 s): the guest shows a body (a proxy the
game has dead) within LIMIT m of the host's body, and both bodies lie on their game's ground (the model's
height over the terrain within GROUND m).

  coop_npc_corpse.py [--only stand,run,...] [--skip later,...]
  (SPORE_FIX_OFF=npc.play_death fails: the guest's proxies stand; npc.dead_proxy fails `later`)"""
import argparse
import math
import os
import re
import time

from coop_npc_pooled import SEA, away, ground, land_path
from twoinst import Checks, shot, start

LIMIT = 1.5      # m between the host's and the guest's body
GROUND = (-0.6, 1.2)  # m over the terrain: a body on the ground (a lying model's origin is ~0-0.6 m up)
SLOPE = (-195.0, -465.0, -45.0)  # creature fixture: ground with a slope of 0.3-0.5 (docs/06-testing.md#testing-puppet-ground)
KINDS = ('stand', 'guest', 'run', 'thrown', 'slope', 'npc', 'later', 'eaten')


def snap(con, radius=80):
    """{id: the last row} of 0.6 s of coop.npctrack (host: its NPCs, how 4; guest: its proxies)"""
    con.call(f'coop.npctrack start {radius}')
    time.sleep(0.6)
    con.call('coop.npctrack stop')
    rows = {}
    n = int(re.search(r'samples=(\d+)', con.call('coop.npctrack count')['out']).group(1))
    for i in range(0, n, 2000):
        for line in con.call(f'coop.npctrack dump {i} 2000')['out'].splitlines():
            f = line.split()
            if len(f) < 28:
                continue
            rows[int(f[1])] = {'t': float(f[0]), 'id': int(f[1]), 'how': int(f[2]), 'flags': int(f[3]),
                               'd': tuple(map(float, f[5:8])), 'anim': f[17], 'm': tuple(map(float, f[25:28])),
                               'vis': int(f[22]), 'sx': float(f[23]), 'sy': float(f[24])}
    return rows


def host_npcs(con):
    """{id: (hp, dist, dead, pos, species)} of the host's NPCs"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=([\d.]+)( DEAD)? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) spd=([\d.]+)',
                         con.call('coop.npc list')['out'], re.M):
        out[int(m.group(1))] = (float(m.group(3)), float(m.group(4)), bool(m.group(5)),
                                tuple(float(m.group(i)) for i in (6, 7, 8)), m.group(2), float(m.group(9)))
    return out


def proxies(con):
    """{id: (spawned, dead)} of the guest's proxies (dead: the game's own dead flag of the proxy, mbDead +0xB5E)"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) species=\S+ (spawned|pending) hp=\S+ dist=\S+ (.*)$', con.call('coop.npc list')['out'], re.M):
        g = re.search(r'game_dead=(-?\d)', m.group(3))  # the game's own dead flag of the proxy (not the mod's)
        out[int(m.group(1))] = (m.group(2) == 'spawned', bool(g and g.group(1) == '1'))
    return out


def pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def species_of_avatar(con):
    m = re.search(r'key=0x([0-9A-F]+)', con.call('coop.avatar')['out'])
    return m.group(1).zfill(8) if m else ''


def over_ground(con, p):
    return math.sqrt(sum(x * x for x in p)) - ground(con, p)


class Corpses:
    def __init__(self, host, guest, ck):
        self.host, self.guest, self.ck = host, guest, ck
        self.kills = []  # (nid, kind, time)
        self.fam = species_of_avatar(host)

    def candidates(self, near=None, maxd=25.0):
        hs, gs = host_npcs(self.host), proxies(self.guest)
        done = {k for k, _, _ in self.kills}
        out = []
        for i, (hp, d, dead, p, sp, spd) in hs.items():
            if dead or hp <= 0 or i in done or sp == self.fam or not gs.get(i, (False,))[0]:
                continue
            dist = math.dist(p, near) if near else d
            if dist < maxd:
                out.append((dist, i, hp, p))
        return sorted(out)

    def kill(self, nid, kind, hp, push=0.0, by_guest=False):
        if by_guest:
            out = self.guest.call(f'coop.npc hit {nid} {hp + 0.5:.2f}')['out'].strip()
        else:
            out = self.host.call(f'coop.npc hosthit {nid} {hp + 0.5:.2f}' + (f' {push:.0f}' if push else ''))['out'].strip()
        self.kills.append((nid, kind, time.time()))
        print(f'  {kind}: kill #{nid} (hp {hp}): {out[:60]}', flush=True)

    def dead_on_host(self, nid, secs=4.0):
        t0 = time.time()
        while time.time() - t0 < secs:
            h = host_npcs(self.host).get(nid)
            if h and h[2]:
                return True
            time.sleep(0.3)
        return False

    def check(self, label, ids=None):
        """the bodies of the kills (or of `ids`) now, on both games"""
        hr, gr = snap(self.host), snap(self.guest)
        gl = proxies(self.guest)
        for nid, kind, tk in self.kills:
            if ids is not None and nid not in ids:
                continue
            age = time.time() - tk
            h, g = hr.get(nid), gr.get(nid)
            name = f'{kind} #{nid} at {age:.0f} s ({label})'
            if not h or not (h['flags'] & 1):
                print(f'  {name}: no body on the host ({h and h["flags"]}): not judged', flush=True)
                continue
            hg = over_ground(self.host, h['m'])
            gspawned, gdead = gl.get(nid, (False, False))
            if not g:
                self.ck(False, f'{name}: the guest shows the host\'s body (none: proxy {"spawned" if gspawned else "not spawned"})')
                continue
            dm = math.dist(h['m'], g['m'])
            gg = over_ground(self.guest, g['m'])
            print(f'  {name}: host body {tuple(round(x, 2) for x in h["m"])} anim {h["anim"]} {hg:+.2f} m over its ground; '
                  f'guest {tuple(round(x, 2) for x in g["m"])} anim {g["anim"]} {gg:+.2f} m, game dead={gdead}: {dm:.2f} m apart',
                  flush=True)
            self.ck(gdead, f'{name}: the guest\'s game has the body dead (not a standing creature)')
            self.ck(dm <= LIMIT, f'{name}: the guest shows the body where the host has it ({dm:.2f} m <= {LIMIT} m)')
            self.ck(GROUND[0] <= hg <= GROUND[1] and GROUND[0] <= gg <= GROUND[1],
                    f'{name}: on the ground on both (host {hg:+.2f} m, guest {gg:+.2f} m)')
        return hr, gr


def go_nest(C, host, guest, avoid=()):
    """both players 8 m from the nearest living NPC of another species the guest mirrors"""
    cand = [c for c in C.candidates(maxd=110) if host_npcs(host)[c[1]][4] not in avoid]
    if not cand:
        return None
    nid = cand[0][1]
    host.call(f'coop.npc gonpc {nid} 8')
    time.sleep(1.0)
    p = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (p[0] + 2, p[1], p[2]))
    time.sleep(3.0)
    print(f'  both players 8 m from #{nid} ({host_npcs(host)[nid][4]})', flush=True)
    return host_npcs(host)[nid][4]


def wait_until(t):
    d = t - time.time()
    if d > 0:
        time.sleep(d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='')
    ap.add_argument('--skip', default='')
    a = ap.parse_args()
    kinds = [k for k in (a.only.split(',') if a.only else KINDS) if k and k not in a.skip.split(',')]
    host, guest = start('creature')
    ck = Checks()
    for c in filter(None, os.environ.get('NPC_CORPSE_GUEST_CMDS', '').split(';')):
        print('  guest:', c, guest.call(c.strip())['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not any(s for s, _ in proxies(guest).values()):
        time.sleep(1)
    for c in (host, guest):
        c.call('coop.team hp 1000')  # an epic or a fight back must not kill a player
    hp0 = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp0[0] + 2, hp0[1], hp0[2]))
    time.sleep(2)
    C = Corpses(host, guest, ck)

    # ---- A: standing, the guest's, running and thrown bodies near both players (at the nearest nest of another
    # species: the NPCs at the start are the players' own family)
    batch = []
    if any(k in kinds for k in ('stand', 'guest', 'run', 'thrown')):
        go_nest(C, host, guest)
    for kind in ('stand', 'guest', 'run', 'thrown'):
        if kind not in kinds:
            continue
        cand = C.candidates()
        if not cand:
            ck(False, f'{kind}: a living NPC the guest mirrors within 25 m of the host')
            continue
        _, nid, hp, p = cand[0]
        if kind == 'run':
            to = away(p, 18, 1.0)
            print('  ' + host.call('coop.npctrack walk %d %.2f %.2f %.2f 6' % (nid, *to))['out'].strip(), flush=True)
            time.sleep(1.5)
            spd = host_npcs(host).get(nid, (0,) * 6)[5]
            print(f'  run: #{nid} at {spd:.1f} m/s when it is hit', flush=True)
        C.kill(nid, kind, hp, push=30.0 if kind == 'thrown' else 0.0, by_guest=kind == 'guest')
        ck(C.dead_on_host(nid), f'{kind}: #{nid} died in the host\'s game')
        batch.append(nid)
        time.sleep(1.0)
    if batch:
        time.sleep(3.0)
        C.check('3 s', batch)
        shot(guest, 'corpse_a_guest')
        shot(host, 'corpse_a_host')
        wait_until(C.kills[-1][2] + 30)
        C.check('30 s', batch)

    # ---- B: on the slope
    if 'slope' in kinds:
        print('  host: ' + host.call('coop.abilities goto %.1f %.1f %.1f' % (SLOPE[0] - 4, SLOPE[1], SLOPE[2] + 3))['out'].strip())
        print('  guest: ' + guest.call('coop.abilities goto %.1f %.1f %.1f' % (SLOPE[0] - 6, SLOPE[1], SLOPE[2] + 5))['out'].strip())
        time.sleep(4)
        cand = C.candidates(near=SLOPE, maxd=45)
        if cand:
            _, nid, hp, p = cand[0]
            print('  ' + host.call('coop.npctrack walk %d %.2f %.2f %.2f 5' % (nid, *SLOPE))['out'].strip(), flush=True)
            t1 = time.time()
            while time.time() - t1 < 20:  # its AI takes over now and then (back to the nest): asked again
                h = host_npcs(host).get(nid)
                if h and math.dist(h[3], SLOPE) < 2.0:
                    break
                host.call('coop.npctrack walk %d %.2f %.2f %.2f 5' % (nid, *SLOPE))
                time.sleep(0.5)
            h = host_npcs(host).get(nid)
            hp = h[0] if h else hp
            d = math.dist(h[3], SLOPE) if h else -1
            # the slope where it stands: the ground 1.5 m around it
            gs = [ground(host, away(h[3], 1.5, k * math.pi / 2)) for k in range(4)] if h else [0] * 4
            print(f'  slope: #{nid} {d:.1f} m from the slope point, ground slope {max(abs(gs[0] - gs[2]), abs(gs[1] - gs[3])) / 3:.2f}',
                  flush=True)
            C.kill(nid, 'slope', hp)
            ck(C.dead_on_host(nid), f'slope: #{nid} died in the host\'s game')
            time.sleep(3.0)
            C.check('3 s', [nid])
            shot(guest, 'corpse_slope_guest')
            wait_until(C.kills[-1][2] + 30)
            C.check('30 s', [nid])
        else:
            ck(False, 'slope: an NPC the guest mirrors within 45 m of the slope')

    # ---- C: an NPC killed by an NPC (a spawned epic attacks the creatures around it)
    if 'npc' in kinds:
        hp0 = pos(host)
        cand = C.candidates(maxd=60)
        if cand:  # both players 8 m from a living NPC; the epic comes there
            _, nid0, _, p = cand[0]
            host.call('coop.npc gonpc %d 8' % nid0)
            time.sleep(1)
            h1 = pos(host)
            guest.call('coop.npc gopos %.2f %.2f %.2f' % (h1[0] + 2, h1[1], h1[2]))
            time.sleep(2)
        dead0 = {i for i, v in host_npcs(host).items() if v[2]}
        print('  ' + host.call('coop.npc epic 0 6')['out'].strip(), flush=True)
        t1 = time.time()
        got = []
        while time.time() - t1 < 60 and not got:
            time.sleep(1)
            for c in (host, guest):
                c.call('coop.team hp 1000')
            hs = host_npcs(host)
            got = [i for i, v in hs.items() if v[2] and i not in dead0 and i not in {k for k, _, _ in C.kills}
                   and v[1] < 60]
        if got:
            nid = got[0]
            C.kills.append((nid, 'npc', time.time()))
            print(f'  npc: #{nid} killed by the epic after {time.time() - t1:.0f} s', flush=True)
            time.sleep(3.0)
            C.check('3 s', [nid])
            shot(guest, 'corpse_npc_guest')
            wait_until(C.kills[-1][2] + 30)
            for c in (host, guest):
                c.call('coop.team hp 1000')
            C.check('30 s', [nid])
        else:
            print('  npc: the epic killed nothing within 60 s (not judged)', flush=True)
        # the epic goes (the next kinds must not be its prey)
        for i, v in host_npcs(host).items():
            if not v[2] and v[0] > 50:
                host.call(f'coop.npc hosthit {i} 100000')

    # ---- D: killed while the guest is away; the guest comes back
    if 'later' in kinds:
        hp0 = pos(host)
        cand = C.candidates(maxd=40)
        if cand:
            _, nid, hp, p = cand[0]
            far, over = land_path(guest, list(hp0), 150)
            print(f'  later: the guest walks 150 m away ({over:.1f} m over the water there)', flush=True)
            guest.call('coop.abilities goto %.2f %.2f %.2f' % tuple(far))
            t1 = time.time()
            while time.time() - t1 < 10 and proxies(guest).get(nid, (False,))[0]:
                time.sleep(0.5)
            gone = not proxies(guest).get(nid, (False,))[0]
            print(f'  later: the guest\'s proxy of #{nid} {"is gone" if gone else "is still there"} 150 m away', flush=True)
            C.kill(nid, 'later', hp)
            ck(C.dead_on_host(nid), f'later: #{nid} died in the host\'s game')
            time.sleep(3)
            hp1 = pos(host)
            guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp1[0] + 2, hp1[1], hp1[2]))
            time.sleep(5)
            C.check('back', [nid])
            shot(guest, 'corpse_later_guest')
            shot(host, 'corpse_later_host')
            wait_until(C.kills[-1][2] + 30)
            C.check('30 s', [nid])
        else:
            ck(False, 'later: an NPC the guest mirrors within 40 m of the host')

    # ---- E: a body eaten on one machine
    if 'eaten' in kinds:
        eat(C, host, guest, ck)

    ck(len(C.kills) >= 1, f'bodies judged: {[(k, n) for n, k, _ in C.kills]}')
    ck.faults(host=host, guest=guest)
    return ck.result()


def eat(C, host, guest, ck):
    """the guest kills an NPC, faces the body 3 m away and clicks on it (the game's eat: its creature walks
    there and eats); what each game shows of the body afterwards is compared (the host's stays: eating is
    each game's own act)"""
    cand = C.candidates(maxd=40)
    if not cand:
        print('  eaten: no NPC near (not judged)', flush=True)
        return
    _, nid, hp, p = cand[0]
    C.kill(nid, 'eaten', hp, by_guest=True)
    if not C.dead_on_host(nid):
        ck(False, f'eaten: #{nid} died in the host\'s game')
        return
    time.sleep(3)
    hb = snap(host).get(nid)
    if not hb:
        print('  eaten: no body on the host (not judged)', flush=True)
        return
    guest.call('coop.nest look %.2f %.2f %.2f 4' % hb['m'])
    time.sleep(1.5)
    W, H = 1440, 1080
    clicks = []
    for _ in range(4):
        g = snap(guest).get(nid)
        if g and g['vis'] and 0 < g['sx'] < W and 0 < g['sy'] < H:
            guest.call(f'click {int(g["sx"])} {int(g["sy"])}')
            clicks.append((int(g['sx']), int(g['sy'])))
        time.sleep(3)
    hp_g = re.search(r'hp=([\d.]+)', guest.call('coop.avatar')['out'])
    print(f'  eaten: clicks on the body in the guest\'s game {clicks}; guest hp {hp_g and hp_g.group(1)}', flush=True)
    shot(guest, 'corpse_eaten_guest')
    shot(host, 'corpse_eaten_host')
    hr, gr = snap(host), snap(guest)
    h, g = hr.get(nid), gr.get(nid)
    print(f'  eaten: host body {h and tuple(round(x, 2) for x in h["m"])} anim {h and h["anim"]}, guest body '
          f'{g and tuple(round(x, 2) for x in g["m"])} anim {g and g["anim"]}', flush=True)
    if h and g:
        ck(math.dist(h['m'], g['m']) <= LIMIT, f'eaten: the guest\'s body (clicked to eat in its game) is where the host\'s is '
           f'({math.dist(h["m"], g["m"]):.2f} m)')


if __name__ == '__main__':
    raise SystemExit(main())
