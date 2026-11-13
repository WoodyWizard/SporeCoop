#!/usr/bin/env python3
"""The host and the guest PLAY the Cell stage together for minutes, through real input, the way
people play (docs/stability/cell.md):

  * swim: hold the left mouse button towards a place and steer with the mouse (`button` + `move`),
    click a place, or hold W/A/S/D;
  * look at the screen (`coop.pool see`: what is on screen, its kind and size) and go for food,
    chase smaller creatures (bite), flee from bigger ones (or not: careless players get eaten),
    wander when nothing is in sight, keep close to the partner now and then;
  * a knocked out partner: swim right next to it (revive); knocked out yourself: wait.

Console commands only measure (coop.pool, coop.status, coop.sync, exc) and set up the session.
Every MEASURE seconds: food progress of both, equal on both machines; positions of every cell on
the guest vs the host (coop_sync.py method: p90 error); the partner visible to both; health
(twoinst monitors: freeze, crash; exc).

  play_cell.py [--minutes 10] [--seed N]
"""
import argparse
import math
import os
import random
import re
import sys
import time

import lane
from coop_sync import angle, pct
from twoinst import Checks, Monitor, keyframe, shot, start

MEASURE = 10.0
PREY_EVERY = 2.0  # coop.pool prey on the host
EDIBLE = {'plant', 'meat'}
HUD_BOTTOM = 130  # the food bar and buttons at the bottom of the screen


def sample(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8:
            try:
                d[f[0]] = (tuple(map(float, f[1:4])), tuple(map(float, f[4:8])))
            except ValueError:
                pass
    return d


def pool(con):
    out = con.call('coop.pool')['out']
    d = {'raw': out}
    for k in ('hp', 'food', 'downed', 'revives', 'respawns', 'mirrors', 'hits_in', 'hits_sent', 'eats_in',
              'eats_sent', 'purged', 'pairs_skipped', 'hurt_local', 'hurt_fwd', 'kept', 'own_eats', 'own_points', 'puppet_new', 'puppet_saves', 'chasing_me', 'chasing_partner', 'bites_taken', 'damage_taken', 'created', 'destroyed', 'local_gameplay_cells'):
        m = re.search(r'\b' + k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else None
    for k in ('size', 'target', 'body', 'partner_dist'):
        m = re.search(r'\b' + k + r'=(-?[\d.]+)', out)
        d[k] = float(m.group(1)) if m else None
    m = re.search(r'cine=(\d)', out)
    d['cine'] = m and m.group(1) == '1'
    m = re.search(r'parts=(\d+)', out)
    d['parts'] = m.group(1) if m else None
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', out)
    d['pos'] = (float(m.group(1)), float(m.group(2))) if m else None
    d['ko'] = {int(m.group(1)): int(m.group(2)) for m in re.finditer(r'ko\[(\d+)\]=(\d)', out)}
    return d


class Player:
    def __init__(self, name, con, pid, partner_id, rng):
        self.name, self.c, self.id, self.partner, self.rng = name, con, pid, partner_id, rng
        self.held = None        # (x, y) while the left button is held
        self.keys = set()       # W/A/S/D held
        self.until = 0.0        # the current intent runs until then
        self.intent = 'idle'
        self.goal = None        # key of the cell we go for
        self.count = {}
        self.log = []
        self.sw, self.sh = 1920, 1080
        self.me = (960, 540)
        self.downed = False
        self.partner_down = False
        self.eaten_kinds = {k: 0 for k in EDIBLE}
        self.tries = {k: 0 for k in EDIBLE}
        self.last_food = None
        self.cine_since = None

    def do(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {line}')
        del self.log[:-40]
        return self.c.call(line).get('out', '')

    # ---- input ----
    def release(self):
        if self.held:
            self.do(f'button {self.held[0]} {self.held[1]} up')
            self.held = None
        for k in list(self.keys):
            self.do(f'key {k} up')
        self.keys.clear()

    def steer(self, x, y, how=None):
        """swim towards screen point (x, y) the way the current intent does it"""
        x = max(5, min(self.sw - 5, int(x)))
        y = max(5, min(self.sh - HUD_BOTTOM, int(y)))
        how = how or self.how
        if how == 'hold':
            if self.held:
                self.do(f'move {x} {y}')
                self.held = (x, y)
            else:
                self.release()
                self.do(f'button {x} {y} down')
                self.held = (x, y)
        elif how == 'click':
            self.release()
            self.do(f'click {x} {y}')
        else:  # keys: the screen direction to W/A/S/D (screen up = W)
            dx, dy = x - self.me[0], y - self.me[1]
            want = set()
            if abs(dx) > 40:
                want.add('0x44' if dx > 0 else '0x41')
            if abs(dy) > 40:
                want.add('0x53' if dy > 0 else '0x57')
            if self.held:
                self.do(f'button {self.held[0]} {self.held[1]} up')
                self.held = None
            for k in self.keys - want:
                self.do(f'key {k} up')
            for k in want - self.keys:
                self.do(f'key {k} down')
            self.keys = want

    # ---- eyes ----
    def look(self):
        out = self.do('coop.pool see 60')
        seen = []
        for line in out.splitlines():
            f = line.split()
            if f and f[0] == 'me' and len(f) >= 5:
                self.me = (int(f[1]), int(f[2]))
                m = re.search(r'screen=(\d+)x(\d+)', line)
                if m:
                    self.sw, self.sh = int(m.group(1)), int(m.group(2))
            elif f and f[0] == 'see' and len(f) == 8:
                seen.append(dict(key=f[1], kind=f[2], ratio=float(f[3]), hp=int(f[4]), d=float(f[5]),
                                 x=int(f[6]), y=int(f[7])))
        return [s for s in seen if s['y'] < self.sh - HUD_BOTTOM]

    def partner_on_screen(self):
        for line in self.do('coop.targets').splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'player' and int(f[1]) == self.partner:
                return int(f[3]), int(f[4])
        return None

    def diet(self):
        """what this cell eats, learned like a player does: food that never counts after a few
        tries is left alone"""
        ok = {k for k in EDIBLE if self.tries[k] < 6 or self.eaten_kinds[k] > 0}
        return ok or set(EDIBLE)

    # ---- one decision ----
    def step(self, now, st):
        """st: this player's coop.pool state (refreshed by the caller)"""
        self.downed = bool(st.get('downed'))
        self.partner_down = st['ko'].get(self.partner) == 1
        if self.downed:  # knocked out: nothing to do but wait for the partner
            if self.intent != 'down':
                self.release()
                self.intent = 'down'
                self.count['knocked_out_steps'] = self.count.get('knocked_out_steps', 0) + 1
            return
        if self.partner_down:  # swim right up to it
            p = self.partner_on_screen()
            if self.intent != 'revive':
                self.intent, self.how = 'revive', self.rng.choice(['hold', 'hold', 'keys'])
                self.count['revive_runs'] = self.count.get('revive_runs', 0) + 1
            if p:
                self.steer(*p)
            else:  # off screen: towards the edge arrow, i.e. where the partner was last reported
                self.steer(self.me[0] + self.rng.randint(-300, 300), self.me[1] + self.rng.randint(-300, 300))
            return
        if not st.get('cine'):
            self.cine_since = None
        elif self.cine_since is None:  # a cinematic: watch it (a zoom ends by itself)
            self.cine_since = now
            self.release()
            return
        elif now - self.cine_since < 2.5:
            return
        if st.get('cine'):  # a caption waits for OK: click the green check (bottom right) or Enter
            self.release()
            self.cine_since = None
            n = self.count.get('caption_ok', 0)
            if n < 3 or n % 10 == 0:  # what was it (docs/stability/cell.md)
                shot(self.c, f'play_cell_caption_{self.name}_{n}.png')
            if self.rng.random() < 0.7:
                self.do(f'click {self.sw - 30} {self.sh - 32}')
            else:
                self.do('key enter')
            self.count['caption_ok'] = self.count.get('caption_ok', 0) + 1
            self.until = 0
            return
        seen = self.look()
        token = [s for s in seen if s['kind'] == 'part']
        if token:  # a new part: swim over the token to collect it
            if self.intent != 'part':
                self.release()
                self.intent, self.how = 'part', self.rng.choice(['hold', 'click', 'keys'])
                self.count['part'] = self.count.get('part', 0) + 1
            t = min(token, key=lambda s: s['d'])
            self.steer(t['x'], t['y'])
            return
        if self.intent == 'part':
            self.until = 0
        if now >= self.until or self.intent in ('down', 'revive', 'idle'):
            self.choose(now, seen)
        if self.intent == 'eat':
            food = [s for s in seen if s['kind'] in self.diet()]
            g = next((s for s in food if s['key'] == self.goal), None)
            if g is None and food:
                g = min(food, key=lambda s: s['d'])
                self.goal = g['key']
                self.tries[g['kind']] += 1
            if g:
                self.steer(g['x'], g['y'])
            else:
                self.intent = 'wander'
                self.wander_dir()
        elif self.intent == 'hunt':
            prey = [s for s in seen if s['kind'] == 'creature' and s['ratio'] < 1.0]
            g = next((s for s in prey if s['key'] == self.goal), None) or (min(prey, key=lambda s: s['d']) if prey else None)
            if g:
                self.goal = g['key']
                self.steer(g['x'], g['y'])
            else:
                self.intent = 'wander'
                self.wander_dir()
        elif self.intent == 'flee':
            big = [s for s in seen if s['kind'] == 'creature' and s['ratio'] > 1.2 and s['d'] < 25]
            if big:
                b = min(big, key=lambda s: s['d'])
                self.steer(2 * self.me[0] - b['x'], 2 * self.me[1] - b['y'])
            else:
                self.until = 0
        elif self.intent == 'partner':
            p = self.partner_on_screen()
            if p and math.dist(p, self.me) > 150:
                self.steer(*p)
            else:
                self.until = 0
        elif self.intent == 'wander':
            self.steer(*self.wdir)

    def wander_dir(self):
        a = self.rng.uniform(0, 2 * math.pi)
        r = min(self.sw, self.sh) * 0.4
        self.wdir = (self.me[0] + r * math.cos(a), self.me[1] + r * math.sin(a))

    def choose(self, now, seen):
        rng = self.rng
        big = [s for s in seen if s['kind'] == 'creature' and s['ratio'] > 1.2 and s['d'] < 12]
        food = [s for s in seen if s['kind'] in self.diet()]
        prey = [s for s in seen if s['kind'] == 'creature' and s['ratio'] < 1.0]
        w = {'eat': 6 if food else 0.5, 'hunt': 3 if prey else 0, 'flee': 4 if big else 0, 'wander': 2,
             'partner': 1.2, 'idle': 0.3}
        if big and rng.random() < 0.3:  # careless: does not see it coming
            w['flee'] = 0
        intent = rng.choices(list(w), weights=list(w.values()))[0]
        self.intent = intent
        self.how = rng.choices(['hold', 'click', 'keys'], weights=[5, 2, 3])[0]
        self.until = now + rng.uniform(2, 8)
        self.goal = None
        self.count[intent] = self.count.get(intent, 0) + 1
        self.count['how_' + self.how] = self.count.get('how_' + self.how, 0) + 1
        self.release()
        if intent == 'wander':
            self.wander_dir()
        elif intent == 'idle':
            self.until = now + rng.uniform(0.5, 2)


def hunters(host, r=80):
    """the host's NPCs that take a player for prey (the game's own IsPrey, `coop.pool prey <r> hunters`):
    ([the host's cell index, the guest's], [dict(idx, chase, flee, range, d=[host, guest], prey=[..], threat=[..])])"""
    out = host.call(f'coop.pool prey {r} hunters')['out']
    ids = [int(m.group(1)) for m in re.finditer(r'^player .*? idx=(\d+)', out, re.M)]
    rows = []
    for line in out.splitlines():
        m = re.match(r'npc (\d+) level=-?\d+ chase=(\d+) flee=(\d+) ranges=([\d.]+)/', line)
        pl = re.findall(r'\| .+? d=([\d.]+) prey=(\d) threat=(\d)', line)
        if m and len(pl) >= 2:
            rows.append(dict(idx=int(m.group(1)), chase=int(m.group(2)), flee=int(m.group(3)), range=float(m.group(4)),
                             d=[float(x[0]) for x in pl], prey=[x[1] == '1' for x in pl],
                             threat=[x[2] == '1' for x in pl]))
    return ids, rows


def hunt_trial(host, guest, who, tries=5, wait=8.0):
    """BUG-0910, the controlled half of "the host's NPCs hunt the guest too": <who> ('host' | 'guest') is put
    inside the prey range of a hunter of the host's game (0.6 of the range from its centre, not touching: a bite
    would send the hunter to rest) while the other player is outside it, and stays there; the game's own pick
    (PickTargets: the nearest prey in the range, docs/re/cell.md 9) must make that player's cell the hunter's chase
    target (+0x22C in the host's game). Returns (hunters tried, hunters that chased it, lines)."""
    wi, con = (1, guest) if who == 'guest' else (0, host)
    tried, chased, lines, seen = 0, 0, [], set()
    t_end = time.time() + 40
    while tried < tries and not chased and time.time() < t_end:
        if pool(con).get('downed'):  # knocked out: no prey for anyone; the partner is not there to revive it
            lines.append(f'{who} is knocked out: waiting')
            time.sleep(3)
            continue
        ids, rows = hunters(host)
        mirrored = sample(guest)
        cand = [r for r in rows if r['idx'] not in seen and not r['flee'] and f'c{r["idx"]}' in mirrored
                and not r['threat'][wi] and r['d'][1 - wi] > r['range'] + 3
                and r['chase'] not in ids]  # (not one that hunts a player already)
        if len(ids) < 2 or not cand:
            lines.append(f'no hunter to try ({len(rows)} hunters within 80 m of a player, {len(seen)} tried)')
            break
        r = min(cand, key=lambda x: (x['chase'] != 0, x['d'][wi]))  # one that hunts nothing first, the nearest
        seen.add(r['idx'])
        tried += 1
        gap = 0.6 * r['range']
        con.call('coop.pool hp 6')  # (a bite or two in the trial is no knockout)
        con.call(f'coop.pool swimto c{r["idx"]} tp {gap:.2f}')
        t1, state, last = time.time(), 'not chased', r
        while time.time() - t1 < wait:
            time.sleep(0.2)
            ids2, rows2 = hunters(host)
            row = next((x for x in rows2 if x['idx'] == r['idx']), None)
            if row is None or len(ids2) < 2:
                state = 'the hunter is gone (or takes nobody for prey now)'
                break
            last = row
            if row['chase'] == ids2[wi]:
                state = f'chased after {time.time() - t1:.1f} s'
                chased += 1
                break
            if row['d'][wi] > 0.9 * row['range']:  # it swam on: stay inside its range
                con.call(f'coop.pool swimto c{r["idx"]} tp {gap:.2f}')
        lines.append(f'{who} at {gap:.1f} m of hunter c{r["idx"]} (prey range {r["range"]:.1f} m, takes the host / guest '
                     f'for prey: {int(last["prey"][0])} / {int(last["prey"][1])}, the other player {last["d"][1 - wi]:.0f} m '
                     f'away, chase target {last["chase"]}, players {ids}): {state}')
    return tried, chased, lines


def desync(host, guest):
    """guest copy of every cell vs the host's (coop_sync.py: closer of the two host samples)"""
    h1, g, h2 = sample(host), sample(guest), sample(host)
    pos, rot, common = {}, {}, 0
    worst = (0, None, None, None)
    for k, (gp, gq) in g.items():
        if k not in h1 or k not in h2:
            continue
        common += 1
        e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
        if min(e1, e2) > worst[0]:
            worst = (min(e1, e2), k, gp, h1[k][0])
        hq = h1[k][1] if e1 <= e2 else h2[k][1]
        pos.setdefault(k[0], []).append(min(e1, e2))
        rot.setdefault(k[0], []).append(angle(gq, hq))
    host_cells = sum(1 for k in h1 if k[0] == 'c')
    if worst[0] > 3:
        print(f'    worst: {worst[1]} guest {tuple(round(v, 1) for v in worst[2])} host {tuple(round(v, 1) for v in worst[3])}',
              flush=True)
        print('      guest', guest.call(f'coop.pool cell {worst[1]}')['out'].strip(), flush=True)
        print('      host ', host.call(f'coop.pool cell {worst[1]}')['out'].strip(), flush=True)
    return pos, rot, common, host_cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play cell: {a.minutes} min, seed {seed}', flush=True)
    host, guest = start('cell')
    ck = Checks()
    rng = random.Random(seed)
    P = [Player('host', host, 0, 1, random.Random(rng.random())), Player('guest', guest, 1, 0, random.Random(rng.random()))]
    t0 = time.time()
    st = {p.name: pool(p.c) for p in P}
    food0 = {p.name: st[p.name]['food'] for p in P}
    st0 = dict(st)  # the counters at the start: the games of a warm run (lab --repeat) have counted before
    hist = []         # (t, host food, guest food)
    pos_err, rot_err = {}, {}
    food_mismatch = []
    partner_missing = []
    ko_seen = {'host': 0, 'guest': 0}
    heal = {'host': [0, 0], 'guest': [0, 0]}
    chase = [0, 0]  # host NPC-steps chasing the host / the guest's cell  # eats while hurt, of them healed
    chase_at_measure = [0, 0]
    # the game's own prey test on the host (coop.pool prey): NPCs that see each player as prey / threat
    # within their range, summed over the measures
    prey = {'me': [0, 0, 0], 'partner': [0, 0, 0]}
    prey_win = {'me': [0, 0, 0], 'partner': [0, 0, 0]}  # since the last measure line
    hunted = {'me': 0, 'partner': 0}  # NPC samples: the player in the prey range, prey, and the chase target
    rules = [0, 0]  # NPC answers compared between the host's cell and the guest's / of them different
    last_measure = last_prey = 0.0
    ok = True
    try:
        # BUG-0980: the guest joins during its own Cell intro (the meteor rock and the letterbox stay a few seconds
        # after the game's cinematic ends): 8 s with neither game busy first
        keyframe('start', 'both cells in the tide pool, the same cells and paints on both screens, name tags over the partner, the HUD in place', calm=8)
        kf_mid = [False]
        while time.time() - t0 < a.minutes * 60 and ok:
            if not kf_mid[0] and time.time() > t0 + a.minutes * 30:
                kf_mid[0] = True
                keyframe('mid', 'play in progress: the partner cell where the host has it, NPC cells and food on both, no white squares, no frozen cell')
            now = time.time()
            for p in P:
                prev = st[p.name]
                st[p.name] = pool(p.c)
                cur = st[p.name]
                # eating heals (AvatarEat: hp += eat.hpValue): an eat while hurt, did hp go up?
                if (prev.get('own_eats') is not None and cur.get('own_eats') is not None and cur['own_eats'] > prev['own_eats']
                        and prev['hp'] is not None and 0 < prev['hp'] < 6 and not cur.get('downed')):
                    heal[p.name][0] += 1
                    heal[p.name][1] += cur['hp'] > prev['hp']
                if p.name == 'host':  # the host's NPCs hunt whom (the guest is a puppet there)
                    chase[0] += cur.get('chasing_me') or 0
                    chase[1] += cur.get('chasing_partner') or 0
                was = p.downed
                p.step(now, st[p.name])
                if p.downed and not was:
                    ko_seen[p.name] += 1
                    print(f'  {now - t0:5.0f}s {p.name} knocked out (hp {st[p.name]["hp"]})', flush=True)
            if now - last_prey >= PREY_EVERY:
                last_prey = now
                out = host.call('coop.pool prey 30 sum')['out']
                for m in re.finditer(r'prey_sum (\w+) .*hunted=(\d+)', out):
                    hunted[m.group(1)] += int(m.group(2))
                for m in re.finditer(r'prey_sum (\w+) around=(\d+) prey=(\d+) threat=(\d+)', out):
                    for i in range(3):
                        prey[m.group(1)][i] += int(m.group(i + 2))
                        prey_win[m.group(1)][i] += int(m.group(i + 2))
                m = re.search(r'prey_rules compared=(\d+) differ=(\d+)', out)
                if m:
                    rules[0] += int(m.group(1))
                    rules[1] += int(m.group(2))
            if now - last_measure >= MEASURE:
                last_measure = now
                hs, gs = st['host'], st['guest']
                # food: the guest follows the host within a second; compare a fresh pair
                hf, gf = pool(host)['food'], pool(guest)['food']
                # a player eating several chunks in a row moves the total again before the last change
                # arrived (seen once: 252/240, 1.2 s later 264/252): only a difference that stays counts
                for _ in range(3):
                    if hf == gf:
                        break
                    time.sleep(1.2)
                    hf, gf = pool(host)['food'], pool(guest)['food']
                if hf != gf:
                    food_mismatch.append((round(now - t0), hf, gf))
                hist.append((round(now - t0), hf, gf))
                pe, re_, common, host_cells = desync(host, guest)
                for k, v in pe.items():
                    pos_err.setdefault(k, []).extend(v)
                for k, v in re_.items():
                    rot_err.setdefault(k, []).extend(v)
                for p in P:
                    pd = st[p.name].get('partner_dist')
                    if pd is None or pd < 0:
                        partner_missing.append((round(now - t0), p.name))
                c = pe.get('c', [])
                dc = [chase[0] - chase_at_measure[0], chase[1] - chase_at_measure[1]]
                chase_at_measure = list(chase)
                print(f'  {now - t0:5.0f}s chased {dc[0]}/{dc[1]}, NPCs around/prey/threat host {prey_win["me"]} guest '
                      f'{prey_win["partner"]}', flush=True)
                prey_win = {'me': [0, 0, 0], 'partner': [0, 0, 0]}
                print(f'  {now - t0:5.0f}s food {hf}/{gf} hp {hs["hp"]}/{gs["hp"]} size {hs["size"]}/{gs["size"]} '
                      f'partner {hs["partner_dist"]}/{gs["partner_dist"]} m, cells {common}/{host_cells} in common, '
                      f'cell err p90 {pct(c, .9):.2f} max {max(c or [0]):.2f}, players p90 {pct(pe.get("p", []), .9):.2f}, '
                      f'revives {hs["revives"]}/{gs["revives"]} respawns {hs["respawns"]}/{gs["respawns"]} '
                      f'own food {hs["own_points"]}/{gs["own_points"]}', flush=True)
            if Monitor.events:
                ok = False
            time.sleep(0.15)
    except (OSError, ValueError) as e:
        print('  !! console error:', repr(e), flush=True)
        ok = False
    for p in P:
        try:
            p.release()
        except (OSError, ValueError):
            pass
    dur = time.time() - t0
    print(f'  played {dur:.0f} s', flush=True)
    for p in P:
        print(f'  {p.name}: ' + ' '.join(f'{k}={v}' for k, v in sorted(p.count.items())), flush=True)
        print(f'  {p.name} diet tries {p.tries}', flush=True)
    ck(ok, 'both games kept running')
    if not ok:
        for p in P:
            print(f'  last actions of {p.name}:')
            for line in p.log[-15:]:
                print('   ', line)
        print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_cell.py --minutes {a.minutes} --seed {seed}')
    try:
        hs, gs = pool(host), pool(guest)
        print('  host ', hs['raw'].strip().replace('\n', ' | '))
        print('  guest', gs['raw'].strip().replace('\n', ' | '))
        # BUG-0910: this run's share of the counters the checks below read (a warm run's games kept the counts of
        # the run before: the third `--repeat` failed "the guest reports only what it ate" with 183 = 57 + 70 + 56)
        for d, d0 in ((hs, st0['host']), (gs, st0['guest'])):
            for k in ('eats_sent', 'own_eats', 'own_points', 'puppet_new'):
                if d.get(k) is not None:
                    d[k] -= d0.get(k) or 0
        gained = (hs['food'] or 0) - (food0['host'] or 0)
        ck(gained >= 20, f'the pair made progress by playing (food {food0["host"]} -> {hs["food"]})')
        ck(not food_mismatch, f'food progress equal on both machines ({len(hist) - len(food_mismatch)}/{len(hist)} checks; '
           f'mismatches {food_mismatch[:5]})')
        # (a meal in the last moment: the guest's total follows the host's within a second, as in the measures above;
        # read once, the end check failed 210 / 204 in lab 20261110-222127 #1, BUG-0910)
        for _ in range(3):
            if hs['food'] == gs['food']:
                break
            time.sleep(1.2)
            hs['food'], gs['food'] = pool(host)['food'], pool(guest)['food']
        ck(hs['food'] == gs['food'], f'and at the end ({hs["food"]} / {gs["food"]})')
        ck((hs['own_points'] or 0) > 0 and (gs['own_points'] or 0) > 0,
           f'both ate by themselves (host {hs["own_eats"]} items / {hs["own_points"]} points, guest {gs["own_eats"]} / '
           f'{gs["own_points"]})')
        # regression: the guest reported every cell its game removed (level change, poison culling, ...)
        # as eaten and the host destroyed them: ~1300 reports in 10 minutes (136 a minute) for ~30 real
        # eats. Now only cells its avatar touched: eats plus small cells swallowed whole (no food
        # points, 0xE791A0 / 0xE7D880): 3-9 a minute
        ck((gs['eats_sent'] or 0) <= 15 * dur / 60 + 10,
           f'the guest reports only what it ate or swallowed ({gs["eats_sent"]} reports in {dur / 60:.0f} min, '
           f'{gs["own_eats"]} of them with food points)')
        # regression: the game removed the partner's cell (level change, an NPC swallowed it): blinking
        ko = ko_seen['host'] + ko_seen['guest']
        for name, d in (('host', hs), ('guest', gs)):
            ck((d['puppet_new'] or 0) <= 2 * ko + 2,
               f'{name}: the partner cell is not replaced while playing ({d["puppet_new"]} times, {ko} knockouts)')
        c, pl = pos_err.get('c', []), pos_err.get('p', [])
        # regression: soft-body mirrors moved without their particles flew off to 1e18 m
        ck(max(c or [0]) < 50, f'no mirror flies off (largest cell error {max(c or [0]):.2f} m)')
        print(f'  sync: cells n={len(c)} med {pct(c, .5):.3f} p90 {pct(c, .9):.3f} max {max(c or [0]):.2f} m; '
              f'facing p90 {pct(rot_err.get("c", []), .9):.1f} deg; players n={len(pl)} p90 {pct(pl, .9):.2f} m '
              f'max {max(pl or [0]):.2f}', flush=True)
        ck(c and pct(c, .9) <= 0.6, f'cells in sync on the guest (p90 {pct(c, .9):.3f} m)')
        ck(pl and pct(pl, .9) <= 1.5, f'players in sync (p90 {pct(pl, .9):.2f} m)')
        ck(len(partner_missing) <= max(1, len(hist) // 10),
           f'each sees the partner ({len(partner_missing)} misses in {2 * len(hist)} looks: {partner_missing[:6]})')
        print(f'  damage taken: host {hs["damage_taken"]} hp in {hs["bites_taken"]} bites, guest {gs["damage_taken"]} hp in '
              f'{gs["bites_taken"]} bites', flush=True)
        print(f'  host NPCs chasing (samples): the host {chase[0]}, the guest {chase[1]}; NPCs around (30 m) / taking '
              f'it for prey / for a threat in range, summed: host {prey["me"]}, guest {prey["partner"]}', flush=True)
        # regression: the guest's cell on the host kept the level it was created at, so after a few
        # level-ups the host's NPCs saw it as too small to hunt and chased only the host (620-2680
        # samples vs 36-117, knockouts 3-16 vs 0-1 per session; docs/stability/cell.md).
        # 1. the game's own prey / threat answers (IsPrey, IsThreat) are the same for both players
        ck(rules[0] > 0 and rules[1] <= 0.02 * rules[0],
           f'the host\'s NPCs judge the guest\'s cell as they judge the host\'s ({rules[1]} of {rules[0]} answers differ)')
        # 2. and hunt it: at least a third as often as the host, unless the guest was rarely within reach
        #    of a hunter that takes it for prey (the prey range is 2-8 m: who swims among the hunters
        #    is hunted; the bot's paths vary a lot from session to session)
        # BUG-0910: that was a ratio of chase samples over the bots' random paths (who swims among the hunters
        # is hunted, a pack that follows one player makes hundreds of samples): 5 of 45 lab runs failed it with
        # the guest hunted as much as the host over all runs (chase samples 24165 / 24916, in reach as prey
        # 3420 / 3225). Now a controlled trial after the play: each player in turn is put into the prey range of
        # a hunter of the host's game, alone; the game's own pick must make it the chase target (the host's
        # turn shows that the trial works). SPORE_CELL_HUNT_RATIO=1: the old ratio check as well.
        trial = {}
        for who in ('guest', 'host'):
            trial[who] = hunt_trial(host, guest, who)
            for line in trial[who][2]:
                print(f'  hunt trial: {line}', flush=True)
        (g_tried, g_chased, _), (h_tried, h_chased, _) = trial['guest'], trial['host']
        print(f'  hunt trial: the guest chased by {g_chased} of {g_tried} hunters tried, the host by {h_chased} of '
              f'{h_tried}; in play: hunted while in reach as prey (samples): host {hunted["me"]} of {prey["me"][1]}, '
              f'guest {hunted["partner"]} of {prey["partner"][1]}', flush=True)
        if g_chased or (g_tried and h_chased):
            ck(g_chased > 0, f'the host\'s NPCs hunt the guest too (a hunter with only the guest in its prey range '
                             f'chases it: {g_chased} of {g_tried} tried; the host: {h_chased} of {h_tried})')
        else:
            # no trial (no hunter at this level, or none took the host either): what the play showed. The guest in a
            # hunter's prey range as prey and that hunter's chase target at the same moment, or hardly ever in reach
            ck(hunted['partner'] > 0 or chase[1] > 0 or prey['partner'][1] < 10,
               f'the host\'s NPCs hunt the guest too (no hunter for a trial: guest {g_tried} tried, host {h_tried}; in '
               f'play the guest was a hunter\'s chase target in {chase[1]} samples, {hunted["partner"]} of them in its '
               f'prey range; in reach as prey {prey["partner"][1]})')
        if os.environ.get('SPORE_CELL_HUNT_RATIO') == '1':
            ck(chase[0] + chase[1] < 60 or 3 * chase[1] >= chase[0] or 3 * prey['partner'][1] < prey['me'][1],
               f'(old ratio check) the host\'s NPCs hunt the guest too (chased: guest {chase[1]}, host {chase[0]}; in '
               f'reach as prey: guest {prey["partner"][1]}, host {prey["me"][1]})')
        print(f'  eats while hurt (healed): host {heal["host"][0]} ({heal["host"][1]}), guest {heal["guest"][0]} '
              f'({heal["guest"][1]})', flush=True)
        print(f'  knockouts: host {ko_seen["host"]} guest {ko_seen["guest"]}; revives {hs["revives"]}/{gs["revives"]}, '
              f'respawns {hs["respawns"]}/{gs["respawns"]}; guest bites sent {gs["hits_sent"]}, host bites applied '
              f'{hs["hits_in"]}; guest eats sent {gs["eats_sent"]}, host eats applied {hs["eats_in"]}', flush=True)
        print(f'  guest: pairs skipped {gs["pairs_skipped"]}, bitten here {gs["hurt_local"]}, bites forwarded by the '
              f'host {gs["hurt_fwd"]}, removals by the game kept {gs["kept"]}; parts host {hs["parts"]} guest {gs["parts"]}', flush=True)
        ck(hs['parts'] == gs['parts'], f'the same parts unlocked on both ({hs["parts"]} / {gs["parts"]})')
        for name, con in (('host', host), ('guest', guest)):
            print(f'  {name} removals:', ' | '.join(con.call('coop.pool why')['out'].strip().splitlines()), flush=True)
        keyframe('end', 'after the play: both games still in Cell play (no menu, no loading screen), the same cells on both')
        shot(host, 'play_cell_host.png')
        shot(guest, 'play_cell_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after playing ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
