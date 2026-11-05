#!/usr/bin/env python3
"""Tribe stage played by two bot players for many minutes, mostly through real input, the way two
friends play an RTS together: they look around with the minimap and the mouse wheel, box-select
their own tribe members, right-click fruit trees and food plants (gather), animals (hunt), the
other tribe's members and hut (raid, in the attack mode of the tribe UI) or the other tribe's hut in
the social mode (socialize), send their chiefs along, give members to each other (F9 window +
coop.give) and click the partner's members too (those must never get selected). Like friends they
agree on a plan for a while (gather, hunt, raid together, socialize together, kill the epic
creature together), with some own ideas in between.

Console commands only measure or find things (coop.targets objs, coop.avatar for the point under
the mouse when the minimap is calibrated, coop.tribe for where the other tribe lives) and never
play. Measured all the time (docs/stability/tribe.md):
  * orders: every order a click gave reaches the host (guest: never rejected) and the ordered
    members act on it on the host (they move or the target loses health);
  * food: gathering and hunting bring food; the shared food is the same on both machines;
  * the player tribe: the same members, owners and chiefs on both machines, nobody has a
    partner's member selected;
  * sync of citizens (t*) and animals (n*): p90 position error like coop_sync.py;
  * co-op mechanics that happen on their own (pincer, chiefs' aura and rescues, duets, epic);
  * no freeze, crash or fault (twoinst monitors, exc).

  play_tribe.py [--minutes 10] [--seed N] [--rate 1.0]
  PLAY_TRIBE_GUEST_CMDS="cmd;cmd": console commands on the guest first (`coop.tribe delay 0.6`: the
  mirrors 0.6 s late, a planted desync the citizens' sync check must fail; BUG-0481)
"""
import argparse
import math
import random
import re
import sys
import time

import lane
from coop_sync import LIMITS, angle, game_paused, pct, sample, wait_running
from twoinst import Checks, Monitor, shot, start

SOCIAL_BTN, ATTACK_BTN = (725, 1030), (795, 1030)  # tribe UI interaction mode (1440x1080)
MINIMAP = (110, 980)                               # a point inside the minimap
PLAN = ['gather', 'hunt', 'raid', 'gather', 'social', 'hunt', 'gather', 'raid']


def tribe_state(con):
    """{id: dict(owner, age, sel, hp, pos, tribe)}, tribe 'p' = the player tribe"""
    out, tribe = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            tribe = 'p' if m.group(2) else int(m.group(1))
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+) sel=(\d+) hp=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                     line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = dict(owner=int(m.group(2)), age=int(m.group(3)), sel=int(m.group(4)),
                                        hp=float(m.group(5)), pos=tuple(float(m.group(i)) for i in (6, 7, 8)),
                                        tribe=tribe)
    return out


def tribe_centres(con):
    out = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            out['p' if m.group(2) else int(m.group(1))] = tuple(float(m.group(i)) for i in (3, 4, 5))
    return out


def status(con):
    return con.call('coop.status')['out']


def counter(text, name):
    m = re.search(r'\b' + name + r'=(\d+)', text)
    return int(m.group(1)) if m else 0


def food(con):
    m = re.search(r'food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def screen(con):
    """things on screen: [(key, x, y, dist)]: member<id>, t<id>, n<id>, fruittree, tree, hut<i>[p],
    tribe<i>[p], o<noun>; dist = metres from the planet point under the mouse (-1: unknown).
    Points behind hills and the horizon are listed too: filter far ones by dist."""
    out = []
    for line in con.call('coop.targets objs')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'member':
            out.append(('member' + f[1], int(f[3]), int(f[4]), -1.0))
        elif len(f) == 5 and f[0] == 'obj':
            out.append((f[1], int(f[3]), int(f[4]), float(f[2])))
    return out


def epic_key(host):
    """the epic creature (when one came): the animal with the most health"""
    if counter(status(host), 'epics') == 0 or 'epic=00000000' in status(host):
        return None
    best = None
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s+#(\d+) .*hp=([\d.]+)', line)
        if m and float(m.group(2)) >= 200 and (not best or float(m.group(2)) > best[1]):
            best = ('n' + m.group(1), float(m.group(2)))
    return best[0] if best else None


class Player:
    def __init__(self, name, con, pid, rng, log, w, h):
        self.name, self.con, self.pid, self.rng, self.log = name, con, pid, rng, log
        self.w, self.h = w, h
        self.count = {}
        self.pending = []            # orders to check later
        self.orders = {'ok': 0, 'lost': 0, 'idle': 0, 'none': 0}  # none: the click gave no order
        self.busy = {}               # member -> busy until (a real player lets a gatherer work)
        self.J = None                # minimap calibration
        self.zoom = 0                # wheel notches from the start view
        self.actions = {}            # kind -> {CitizenAction id the click became: count}

    def do(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: {line}')
        return self.con.call(line).get('out', '')

    def note(self, k):
        self.count[k] = self.count.get(k, 0) + 1

    def on_screen(self, objs):
        return [o for o in objs if 20 < o[1] < self.w - 60 and 60 < o[2] < self.h - 130]

    # ---- the camera: the minimap (calibrated by looking at where a click on it takes the view) ----
    def centre(self):
        self.con.call(f'move {self.w // 2} {self.h // 2}')
        time.sleep(0.3)
        m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', self.con.call('coop.avatar')['out'])
        return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None

    def minimap(self, x, y):
        self.do(f'click {int(x)} {int(y)}')
        time.sleep(2.0)
        return self.centre()

    def calibrate(self):
        x0, y0 = MINIMAP
        o, a, b = self.minimap(x0, y0), self.minimap(x0 + 30, y0), self.minimap(x0, y0 + 20)
        if o and a and b and math.dist(o, a) > 20 and math.dist(o, b) > 20:
            self.J = ([(a[i] - o[i]) / 30 for i in range(3)], [(b[i] - o[i]) / 20 for i in range(3)])
            self.base = (x0, y0, o)
            self.cur = b
        else:  # BUG-0481: why (no point under the mouse, or the view did not move)
            st = self.con.call('coop.status')['out']
            busy = re.search(r'busy=\S+|input_lock=\d+', st)
            why = f'{busy.group(0) if busy else "-"}, game time paused={game_paused(self.con)}'
            self.log.append(f'   {self.name}: minimap calibration: o {o} a {a} b {b} ({why})')
            print(f'  {self.name}: minimap calibration: o {o} a {a} b {b} ({why})', flush=True)
        return self.J is not None

    def solve(self, w, x, y, c):
        J = self.J
        d = [w[i] - c[i] for i in range(3)]
        a11, a22 = sum(v * v for v in J[0]), sum(v * v for v in J[1])
        a12 = sum(J[0][i] * J[1][i] for i in range(3))
        b1, b2 = sum(J[0][i] * d[i] for i in range(3)), sum(J[1][i] * d[i] for i in range(3))
        det = a11 * a22 - a12 * a12 or 1
        x, y = x + (b1 * a22 - b2 * a12) / det, y + (a11 * b2 - a12 * b1) / det
        return min(205, max(15, x)), min(1025, max(940, y))

    def look(self, w, tol=30):
        """centre the view on a world point through the minimap (a linear map from the calibration,
        then one correction from where the view went)"""
        if not self.J:
            return False
        if getattr(self, 'cur', None) and math.dist(self.cur, w) < tol:
            return True
        x, y = self.solve(w, *self.base)
        for _ in range(2):
            c = self.minimap(x, y)
            if not c:
                return False
            self.cur = c
            if math.dist(c, w) < max(tol, 30):
                self.note('look')
                return True
            x, y = self.solve(w, x, y, c)
        self.note('look_far')
        return False

    # ---- selecting own members ----
    def select(self, ids, objs, group=False):
        """box-select own members: a small box around one of `ids` (or a box around all of them)"""
        own = [o for o in self.on_screen(objs) if o[0].startswith('member') and int(o[0][6:]) in ids]
        if not own:
            return []
        if group and len(own) > 1:
            xs, ys = [o[1] for o in own], [o[2] for o in own]
            box = (min(xs) - 30, min(ys) - 45, max(xs) + 30, max(ys) + 25)
        else:
            _, x, y, _ = self.rng.choice(own)
            box = (x - 35, y - 55, x + 35, y + 25)
        x1, y1, x2, y2 = max(5, box[0]), max(65, box[1]), min(self.w - 65, box[2]), min(self.h - 130, box[3])
        if x2 - x1 < 10 or y2 - y1 < 10:
            return []
        self.do(f'drag {x1} {y1} {x2} {y2}')
        time.sleep(1.2)  # the drag takes ~15 frames
        return [i for i, v in tribe_state(self.con).items() if v['sel'] and v['tribe'] == 'p']

    def last_order(self):
        """(the last order line, number of orders): host = its own orders, guest = orders sent"""
        out = status(self.con)
        if self.pid == 0:
            m = re.search(r'tribe last local order: #(\d+) (.*)', out)
            return (m.group(2), int(m.group(1))) if m else ('', 0)
        m = re.search(r'tribe last order: (.*)', out)
        return (m.group(1) if m else ''), counter(out, 'cmds_sent')

    def mode(self, want):
        m = re.search(r'tribe ui mode=(-?\d)', status(self.con))
        if m and int(m.group(1)) != want:
            btn = SOCIAL_BTN if want == 0 else ATTACK_BTN
            self.do(f'click {btn[0]} {btn[1]}')
            time.sleep(0.6)
            self.note('mode_social' if want == 0 else 'mode_attack')


class Game:
    def __init__(self, host, guest, seed, rate):
        self.host, self.guest = host, guest
        self.rng = random.Random(seed)
        self.rate = rate
        self.log = []
        w, h = 1440, 1080
        m = re.search(r'(\d+)x(\d+)', host.call('shot ' + shot_path('size')).get('out', ''))
        if m:
            w, h = int(m.group(1)), int(m.group(2))
        self.w, self.h = w, h
        self.p = [Player('host', host, 0, random.Random(seed * 3 + 1), self.log, w, h),
                  Player('guest', guest, 1, random.Random(seed * 3 + 2), self.log, w, h)]
        self.food = []
        self.food_rises = 0
        self.food_mismatch = 0
        self.roster_mismatch = []
        self.partner_selected = []
        self.sync_pos = {'t': [], 'n': []}
        self.sync_raw = {'t': [], 'n': []}
        self.sync_rot = {'t': [], 'n': []}
        self.outliers = {}
        self.rejections = []
        self.rejected0 = counter(status(host), 'rejected')
        self.attacked = {}
        self.both_attacked = 0
        self.plan_i, self.plan_until = -1, 0
        self.maxhp = {}
        self.dead_since = None
        self.game_overs = 0
        self.phases = []
        self.pending_gift = None
        self.probe = []

    # ---------------- the plan the two friends agree on ----------------
    def plan(self):
        now = time.time()
        if epic_key(self.host):
            if not self.phases or self.phases[-1][1] != 'epic':
                self.phases.append((now, 'epic'))
            return 'epic'
        if now > self.plan_until:
            self.plan_i += 1
            self.plan_until = now + self.rng.uniform(70, 120)
            self.phases.append((now, PLAN[self.plan_i % len(PLAN)]))
        return PLAN[self.plan_i % len(PLAN)]

    # ---------------- playing ----------------
    def step(self, pl):
        rng = pl.rng
        st = tribe_state(pl.con)
        mine = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] == pl.pid and v['hp'] > 0]
        partner = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] not in (pl.pid, 254, -1)]
        now = time.time()
        idle = [i for i in mine if pl.busy.get(i, 0) < now]
        for i, v in st.items():
            self.maxhp[i] = max(self.maxhp.get(i, 0), v['hp'])
        alive = [i for i, v in st.items() if v['tribe'] == 'p' and v['hp'] > 0]
        hurt = [i for i in idle if st[i]['hp'] < 0.6 * self.maxhp.get(i, 1)]
        plan = self.plan()
        kind = plan if rng.random() < 0.75 else rng.choice(['gather', 'hunt', 'partner', 'camera', 'give', 'walk'])
        if kind in ('raid', 'epic') and (len(alive) < 4 or len(idle) < 2):
            kind = 'gather'  # too few to fight: friends gather first
        if hurt and rng.random() < 0.7:
            kind, idle = 'heal', hurt
        if kind in ('gather', 'hunt', 'raid', 'social', 'epic', 'walk', 'heal') and not idle:
            kind = rng.choice(['partner', 'camera', 'wait', 'wait'])
        pl.note(kind)
        centres = tribe_centres(self.host)  # where the tribes live (a player sees them on the minimap)
        enemies = [t for t in centres if t != 'p']
        if kind in ('gather', 'hunt', 'raid', 'social', 'epic', 'walk', 'heal'):
            group = kind in ('raid', 'epic') or (kind == 'hunt' and rng.random() < 0.5)
            sel = self.select_idle(pl, st, idle, group)
            if not self.check_sel(pl, sel, st):
                return self.pause(pl)
            if kind == 'walk':
                tgt = ('ground', rng.randint(300, self.w - 300), rng.randint(400, self.h - 300))
                return self.click_order(pl, kind, sel, tgt, busy=10)
            where, pick = self.target_for(pl, kind, st, centres, enemies)
            if where is None:
                pl.note(kind + '_nothing')
                return self.pause(pl)
            pl.look(where)
            pl.centre()  # the mouse in the middle: the listed distances (and the 250 nearest) are from there
            objs = screen(pl.con)
            tg = [o for o in pl.on_screen(objs) if pick(o)]
            if kind == 'heal':  # the own food mat: the nearest one, it sits low in the default view
                tg = sorted([o for o in objs if pick(o) and 20 < o[1] < self.w - 60 and 60 < o[2] < self.h - 70],
                            key=lambda o: o[3])[:1]
            if not tg:
                pl.note(kind + '_nothing')
                return self.pause(pl)
            if kind in ('raid', 'social', 'epic'):
                pl.mode(0 if kind == 'social' else 1)
            if kind == 'gather':  # a fruit tree in the open: not behind the fire, the hut, members
                busy = [o for o in objs if o[0].startswith(('hut', 'member', 't', 'o18C8F0C', 'o18C6D19', 'o629BAFE',
                                                            'o55CF865', 'o18C431C')) and o[0] != 'tree']
                tg = [o for o in tg if o[3] < 90 and all(math.hypot(o[1] - b[1], o[2] - b[2]) > 70 for b in busy)]
                tg.sort(key=lambda o: math.hypot(o[1] - self.w / 2, o[2] - self.h / 2))
                tg = tg[:4]
            if kind == 'raid':
                hits = self.attacked.setdefault(enemies[0], {})
                hits[pl.pid] = time.time()
                if len([t for t in hits.values() if time.time() - t < 10]) >= 2:
                    self.both_attacked += 1
            busy = {'gather': 40, 'hunt': 25, 'heal': 30}.get(kind, 30)
            if kind == 'gather':  # another tree when the pick misses
                for t in tg[:3]:
                    if self.click_order(pl, kind, sel, t, busy):
                        break
            else:
                self.click_order(pl, kind, sel, rng.choice(tg), busy)
        elif kind == 'partner':  # real clicks on the partner's members: they must never get selected
            pm = [o for o in pl.on_screen(screen(pl.con)) if o[0].startswith('member') and int(o[0][6:]) in partner]
            if not pm and partner:
                pl.look(st[rng.choice(partner)]['pos'])
                pm = [o for o in pl.on_screen(screen(pl.con)) if o[0].startswith('member') and int(o[0][6:]) in partner]
            if not pm:
                return self.pause(pl)
            k, x, y, _ = rng.choice(pm)
            how = rng.choice(['click', 'box', 'right'])
            if how == 'click':
                pl.do(f'click {x} {y}')
            elif how == 'box':
                pl.do(f'drag {x - 40} {y - 50} {x + 40} {y + 30}')
            else:  # an order of an own member on the partner's member
                own = pl.select(mine, screen(pl.con))
                if own:
                    pl.do(f'click {x} {y} right')
            time.sleep(1.3)
            st2 = tribe_state(pl.con)
            bad = [i for i, v in st2.items() if v['sel'] and v['tribe'] == 'p' and v['owner'] != pl.pid]
            if bad:
                self.partner_selected.append(f'{pl.name} {how} on #{k[6:]}: selected {bad}')
            pl.note('partner_' + how)
        elif kind == 'camera':
            what = rng.choice(['wheel', 'wheel', 'pan', 'rotate'])
            pl.cur = None
            if what == 'wheel':  # zoom, but not so close that the village leaves the view
                n = rng.choice([-3, -2, 2, 3])
                n = max(-4 - pl.zoom, min(2 - pl.zoom, n))
                if n:
                    pl.zoom += n
                    pl.do(f'wheel {self.w // 2} {self.h // 2} {n}')
            elif what == 'pan':
                k = rng.choice(['0x25', '0x26', '0x27', '0x28'])  # arrow keys
                pl.do(f'key {k} down')
                time.sleep(rng.uniform(0.2, 0.6))
                pl.do(f'key {k} up')
            else:  # the minimap turns with the view: look where it points again
                x = rng.randint(400, self.w - 400)
                pl.do(f'drag {x} 600 {x + rng.choice([-200, 200])} 600 right')
                time.sleep(0.8)
                pl.calibrate()
        elif kind == 'give' and len(mine) > 2:  # one own member to the partner: select, F9, give
            sel = self.select_idle(pl, st, mine, False)
            if len(sel) == 1:
                pl.do('key 0x78')  # F9: the co-op window
                time.sleep(1.0)
                out = pl.do('coop.give')
                time.sleep(0.5)
                pl.do('key 0x78')
                self.log.append(f'   give #{sel[0]}: {out.strip()[:80]}')
                self.pending_gift = (time.time() + 3, sel[0], 1 - pl.pid)
                pl.note('gave')
        self.pause(pl)

    def select_idle(self, pl, st, idle, group):
        """the view on an idle own member (like a click on its card), then a box around it (and the
        members next to it for a group)"""
        if not idle:
            return []
        m = pl.rng.choice(idle)
        objs = screen(pl.con)
        central = [o for o in objs if o[0] == f'member{m}' and 200 < o[1] < self.w - 200 and 150 < o[2] < self.h - 260]
        if not central:
            pl.look(st[m]['pos'])
            objs = screen(pl.con)
            if not any(o[0] == f'member{m}' for o in pl.on_screen(objs)) and pl.zoom > -4:
                pl.do(f'wheel {self.w // 2} {self.h // 2} -2')  # too close to see it: zoom out
                pl.zoom -= 2
                time.sleep(0.8)
                objs = screen(pl.con)
        if group:
            return pl.select(idle, objs, group=True)
        return pl.select([m], objs)

    def target_for(self, pl, kind, st, centres, enemies):
        """(world point to look at, which screen object to right-click)"""
        if kind == 'gather':  # fruit trees around the village
            return centres.get('p'), lambda o: o[0] == 'fruittree'
        if kind in ('raid', 'social'):
            if not enemies:
                return None, None
            et = enemies[0]
            want = {f'hut{et}'} if kind == 'social' else {f'hut{et}'} | {f't{i}' for i, v in st.items() if v['tribe'] == et}
            return centres[et], lambda o: o[0] in want
        if kind in ('hunt', 'epic'):
            hs = sample(self.host)
            home = centres.get('p')
            if kind == 'epic':
                key = epic_key(self.host)
            else:  # one of the animals nearest to the village
                near = sorted((math.dist(v[0], home), k) for k, v in hs.items() if k[0] == 'n' and home)
                key = pl.rng.choice(near[:6])[1] if near else None
            if not key or key not in hs:
                return None, None
            return hs[key][0], lambda o, k=key: o[0] == k
        return None, None

    def pause(self, pl):
        time.sleep(pl.rng.expovariate(self.rate))

    def check_sel(self, pl, sel, st):
        if not sel:
            pl.note('no_selection')
            return False
        bad = [i for i in sel if st.get(i, {}).get('owner') != pl.pid]
        if bad:
            self.partner_selected.append(f'{pl.name} box-selected {bad}')
        return True

    def click_order(self, pl, kind, sel, tgt, busy):
        """right-click the target; a click that misses the target's model hits the ground (a walk):
        a player then clicks a bit higher (on the trunk, the body)"""
        last, n, act = '', None, None
        for dy in ((0,) if kind == 'walk' else (0, -10, -20, -35)):
            before = pl.last_order()
            # hover first: the game picks the object under the cursor per frame, a click right
            # after the jump of the cursor hits whatever was under it before (mostly the ground)
            pl.do(f'move {tgt[1]} {tgt[2] + dy}')
            time.sleep(0.4)
            pl.do(f'click {tgt[1]} {tgt[2] + dy} right')
            time.sleep(0.8)
            last, n = pl.last_order()
            if n == before[1]:
                continue  # the click gave no order at all (the UI, the sky)
            m = re.search(r'action=(\d+)', last)
            act = 'walk' if ('walk' in last or 'kind=1' in last) else (m.group(1) if m else '?')
            if act != 'walk' or kind == 'walk':
                break
            pl.note('missed_pick')
        if act is None:
            pl.orders['none'] += 1
            return False
        if act == 'walk' and kind != 'walk':
            pl.orders['none'] += 1  # only walks: the pick missed every time
            return False
        acts = pl.actions.setdefault(kind, {})
        acts[act] = acts.get(act, 0) + 1
        for i in sel:
            pl.busy[i] = time.time() + busy
        hs = tribe_state(self.host)
        hp = self.target_hp(tgt[0]) if tgt[0][0] in 'tn' and tgt[0][1:].isdigit() else None
        pl.pending.append(dict(due=time.time() + 8, kind=kind, sel=sel, tgt=tgt[0], last=last, hp=hp,
                               rej=counter(status(self.host), 'rejected'),
                               pos={i: hs[i]['pos'] for i in sel if i in hs}))
        return True

    def target_hp(self, key):
        if key[0] == 't':
            v = tribe_state(self.host).get(int(key[1:]))
            return v['hp'] if v else 0
        for line in self.host.call('coop.npc list')['out'].splitlines():
            m = re.match(rf'\s+#{key[1:]} .*hp=([\d.]+)', line)
            if m:
                return float(m.group(1))
        return 0

    def check_orders(self, pl, final=False):
        now = time.time()
        for o in [o for o in pl.pending if final or o['due'] < now]:
            pl.pending.remove(o)
            st = tribe_state(self.host)
            moved = [i for i, p in o['pos'].items() if i in st and math.dist(p, st[i]['pos']) > 1.5]
            hurt = o['hp'] is not None and self.target_hp(o['tgt']) < o['hp'] - 0.01
            if pl.pid != 0 and counter(status(self.host), 'rejected') > o['rej']:
                pl.orders['lost'] += 1
                self.log.append(f'   {pl.name} {o["kind"]} {o["tgt"]} {o["sel"]}: LOST ({o["last"][:110]})')
            elif moved or hurt:
                pl.orders['ok'] += 1
            else:  # the order arrived but nobody moved (the target right there, gone, or dead)
                pl.orders['idle'] += 1
                self.log.append(f'   {pl.name} {o["kind"]} {o["tgt"]} {o["sel"]}: no movement ({o["last"][:110]})')
                # BUG-0505: what the host's game has the members do (behaviour node, place, health)
                for line in self.host.call('coop.tribe')['out'].splitlines():
                    m = re.match(r'\s+#(\d+) ', line)
                    if m and int(m.group(1)) in o['sel']:
                        self.log.append(f'      host: {line.strip()[:150]}')
                for i in o['sel']:  # its order record, node, node state and walk (docs/re/tribe.md#tribe-orders)
                    self.log.append(f'      host ai #{i}: {self.host.call(f"coop.tribe ai {i}")["out"].strip()[:260]}')

    # ---------------- measuring ----------------
    def measure(self):
        st = status(self.host)
        rej = counter(st, 'rejected')
        if rej > getattr(self, 'rej_seen', self.rejected0):  # why: the host's last guest order
            self.rej_seen = rej
            m = re.search(r'tribe last order: (.*)', st)
            self.rejections.append(m.group(1) if m else '?')
            self.log.append(f'   host rejected a guest order: {self.rejections[-1]}')
        fh, fg = food(self.host), food(self.guest)
        if fh is not None and fg is not None:
            if abs(fh - fg) > 0.5:  # the shared food travels every few frames: look twice
                time.sleep(1.0)
                fh, fg = food(self.host), food(self.guest)
                if abs(fh - fg) > 0.5:
                    self.food_mismatch += 1
                    self.log.append(f'   food differs: host {fh} guest {fg}')
            if self.food and fh > self.food[-1][1] + 0.5:
                self.food_rises += 1
            self.food.append((time.time(), fh, fg))
        hs, gs = tribe_state(self.host), tribe_state(self.guest)
        # the tribe died out: the game shows "Alas! Your population has fallen below the minimum..."
        # and waits for OK, which bounces back to the last save (the host's world; the guest follows)
        if not [i for i, v in hs.items() if v['tribe'] == 'p' and v['hp'] > 0]:
            self.dead_since = self.dead_since or time.time()
            if time.time() - self.dead_since > 15:
                self.log.append(f'{time.strftime("%H:%M:%S")} host: key enter (game over: bounce back)')
                self.host.call('key enter')
                self.game_overs += 1
                self.dead_since = None
                time.sleep(12)
                for p in self.p:  # the reload resets the view: look where the minimap points again
                    p.cur = None
                    p.busy.clear()
                    p.zoom = 0
                    p.calibrate()
                hs, gs = tribe_state(self.host), tribe_state(self.guest)
        else:
            self.dead_since = None

        def roster(s):
            return {i: v['owner'] for i, v in s.items() if v['tribe'] == 'p'}
        if roster(hs) != roster(gs):
            time.sleep(1.5)  # a member born or killed this moment: look again
            hs, gs = tribe_state(self.host), tribe_state(self.guest)
            if roster(hs) != roster(gs):
                self.roster_mismatch.append(f'host {roster(hs)} guest {roster(gs)}')
        def chief_set(m):  # owner:#member pairs only (the line goes on with the host's own counters spread= placed=)
            return set(re.findall(r'\d+:#\d+', m.group(1)))
        hc, gc = re.search(r'chiefs=(.*)', status(self.host)), re.search(r'chiefs=(.*)', status(self.guest))
        if hc and gc and chief_set(hc) != chief_set(gc):
            time.sleep(1.5)
            hc, gc = re.search(r'chiefs=(.*)', status(self.host)), re.search(r'chiefs=(.*)', status(self.guest))
            if hc and gc and chief_set(hc) != chief_set(gc):
                self.roster_mismatch.append(f'chiefs host {hc.group(1)} guest {gc.group(1)}')
        for name, s, me in (('host', hs, 0), ('guest', gs, 1)):
            bad = [i for i, v in s.items() if v['sel'] and v['tribe'] == 'p' and v['owner'] != me]
            if bad:
                self.partner_selected.append(f'{name} has partner members selected: {bad}')
        # the host's path over the last PROBE_S before the guest is read (BUG-0481: how late the
        # mirror is, whether it is on that path at all, how fast the member walked)
        hist, t_end = [], time.time() + PROBE_S
        while True:
            ta = time.time()
            hs_ = sample(self.host)
            hist.append(((ta + time.time()) / 2, hs_))
            if time.time() > t_end:
                break
        h1 = hist[-1][1]
        ta = time.time()
        g = sample(self.guest)
        tg = (ta + time.time()) / 2
        ta = time.time()
        h2 = sample(self.host)
        hist.append(((ta + time.time()) / 2, h2))
        on_path = {}
        for k, (gp, _) in g.items():
            if k[0] == 't' and all(k in s_ for _, s_ in hist):
                self.probe.append(probe_row(k, gp, tg, [(t_, s_[k][0]) for t_, s_ in hist]))
                on_path[k] = self.probe[-1]['ew']
        for k, (gp, gq) in g.items():
            if k[0] not in 'tn' or k not in h1 or k not in h2:
                continue
            e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
            # citizens: against the host's path over the mirrors' interpolation delay (BUG-0481), as
            # coop_sync.py compares with its LAG_WINDOW; the raw error (lag included) is printed
            self.sync_raw[k[0]].append(min(e1, e2))
            self.sync_pos[k[0]].append(min(e1, e2, on_path.get(k, 1e9)))
            if min(e1, e2) > 50 and k not in self.outliers and math.dist(h1[k][0], (0, 0, 0)) > 100:
                # (at the planet centre on the host: a creature the host stops tracking after a
                # reload, it reads a destroyed object for up to 2 s; not shown anywhere)
                info = [ln.strip() for ln in self.host.call('coop.npc list' if k[0] == 'n' else 'coop.tribe')['out']
                        .splitlines() if re.match(rf'\s*#{k[1:]} ', ln)]
                self.outliers[k] = f'host {[round(x) for x in h1[k][0]]} guest {[round(x) for x in gp]} {info[:1]}'
                self.log.append(f'   sync outlier {k}: {self.outliers[k]}')
            self.sync_rot[k[0]].append(angle(gq, h1[k][1] if e1 <= e2 else h2[k][1]))
        if self.pending_gift and self.pending_gift[0] < time.time():
            _, mid, to = self.pending_gift
            self.pending_gift = None
            ho, go = hs.get(mid, {}).get('owner'), gs.get(mid, {}).get('owner')
            if mid in hs and (ho != to or go != to):
                self.roster_mismatch.append(f'gift of #{mid} to {to}: host owner {ho}, guest owner {go}')


PROBE_S = 0.45  # s of the host's path kept before each guest sample (>= TRIBE_LAG; each costs the bots ~0.5 s)
# BUG-0481: a guest shows the host's members TRIBE_LAG s late on purpose: the mirrors follow the host's
# states kMirrorDelay = 0.2 s back (tribe.cpp, net::SnapshotBuffer) and the host sends every 0.12 s or
# more. Measured in play_tribe (lab 20261103-150636): walking members (p50 8.4 m/s) were on the host's path
# 0.21-0.25 s back (p10-p90), 0.09 m p90 off it there, while the raw distance to where the host has them
# now was 2.14 m p90 (= 0.22 s x 8.4 m/s): the 1.5 m limit of coop_sync.py is for that path measure
TRIBE_LAG = 0.32


def probe_row(k, gp, tg, path):
    """one guest sample of a member against the host's path [(t, pos)]: the error now (raw), the lag
    that fits best (0..PROBE_S, 10 ms steps) and the error at it, the member's speed before tg"""
    def at(t):
        if t <= path[0][0]:
            return path[0][1]
        for (t0, p0), (t1, p1) in zip(path, path[1:]):
            if t0 <= t <= t1:
                f = (t - t0) / ((t1 - t0) or 1)
                return tuple(a + (b - a) * f for a, b in zip(p0, p1))
        return path[-1][1]
    errs = [(math.dist(gp, at(tg - lag / 100)), lag / 100) for lag in range(0, int(PROBE_S * 100) + 1)]
    best = min(errs)
    older = at(tg - 0.4)
    speed = math.dist(at(tg), older) / 0.4
    return dict(k=k, raw=errs[0][0], err=best[0], lag=best[1], speed=speed,
                ew=min(e for e, l in errs if l <= TRIBE_LAG + 1e-9),
                e15=min(e for e, l in errs if l <= 0.15), e30=min(e for e, l in errs if l <= 0.30),
                e45=min(e for e, l in errs if l <= 0.45))


def probe_report(rows, log):
    """BUG-0481: the members' sync split by standing / walking and by the lag window"""
    if not rows:
        return
    import json
    import os
    path = os.path.join(lane.out_dir('track'), f'play_tribe_probe_{time.strftime("%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump(rows, f)
    for name, sel in (('all', rows), ('standing (<0.3 m/s)', [r for r in rows if r['speed'] < 0.3]),
                      ('walking', [r for r in rows if r['speed'] >= 0.3])):
        if not sel:
            continue
        lags = sorted(r['lag'] for r in sel)
        print(f'  probe {name:20}: n {len(sel):4} raw p90 {pct([r["raw"] for r in sel], .9):5.2f} | on the path '
              f'within 0.15 s p90 {pct([r["e15"] for r in sel], .9):5.2f}, 0.30 s {pct([r["e30"] for r in sel], .9):5.2f}, '
              f'0.45 s {pct([r["e45"] for r in sel], .9):5.2f}, best lag p50 {lags[len(lags) // 2]:.2f} s; '
              f'speed p50 {pct([r["speed"] for r in sel], .5):4.1f} m/s', flush=True)
    print(f'  probe rows: {path}', flush=True)


def shot_path(name):
    import os
    return os.path.join(lane.out_dir('shots'), f'play_tribe_{name}.png')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--rate', type=float, default=1.0, help='mean pauses per second between actions')
    a = ap.parse_args()
    import os
    if a.seed is None and os.environ.get('PLAY_TRIBE_SEED'):  # a replay through the lab (BUG-0505)
        a.seed = int(os.environ['PLAY_TRIBE_SEED'])
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play_tribe: {a.minutes} min, seed {seed}', flush=True)
    host, guest = start('tribe')
    ck = Checks()
    import os
    for cmd in filter(None, os.environ.get('PLAY_TRIBE_GUEST_CMDS', '').split(';')):  # e.g. a planted late mirror
        print(f'  guest: {cmd}: {guest.call(cmd)["out"].strip()}', flush=True)   # (coop.tribe delay 0.6, BUG-0481)
    # setup: the fixture's tribe has 3 members; 3 more give each player 3 to play with
    print('  setup:', host.call('coop.tribe spawn 3')['out'], flush=True)
    t0 = time.time()
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        if len([v for v in tribe_state(guest).values() if v['owner'] == 1]) >= 2:
            break
        time.sleep(2)
    g = Game(host, guest, seed, a.rate)
    food0 = food(host)
    # BUG-0586: the guest's calibration failed (the view did not move on its minimap clicks, o == a) only while its
    # game was still in the pause that follows a cold join (BUG-0444: busy=1 at the failure in lab 20261104-220200
    # and fix/w43-realign 20261104-213145). Holding the pause bit by hand does not stop the view (lab lane 3,
    # 2026-11-04): the bit marks the join's pause, it is not its cause. Wait for the game's own clock to run on
    # both machines, as a player waits for the game to respond.
    w = wait_running(host, guest, 90)
    print(f'  both games\' time runs: {"after %.0f s" % w if w is not None else "NOT within 90 s"}', flush=True)
    for p in g.p:
        ck(p.calibrate(), f'{p.name}: the minimap takes the view where it points')
    print(f'  start: food {food0}, {len(tribe_state(host))} citizens, screen {g.w}x{g.h}', flush=True)
    t0, last_measure, n, ok = time.time(), 0, 0, True
    try:
        while time.time() - t0 < a.minutes * 60 and ok:
            g.step(g.p[n % 2])
            n += 1
            if time.time() - last_measure > 10:
                last_measure = time.time()
                for p in g.p:
                    g.check_orders(p)
                g.measure()
            if Monitor.events:
                ok = False
            if n % 30 == 0:
                print(f'  {time.time() - t0:5.0f} s [{g.phases[-1][1] if g.phases else "-"}]: food '
                      f'{g.food[-1][1] if g.food else "?"}, ' + ', '.join(f'{p.name} {p.orders}' for p in g.p), flush=True)
    except (OSError, ValueError) as e:
        g.log.append(f'  !! console error: {e!r}')
        ok = False
    played = time.time() - t0
    time.sleep(5)
    for p in g.p:
        g.check_orders(p, final=True)
    st1, gst = status(host), status(guest)
    print(f'  played {played / 60:.1f} min, {n} actions, game overs {g.game_overs}; plan: '
          + ' '.join(f'{(t - t0) / 60:.1f}:{k}' for t, k in g.phases), flush=True)
    for p in g.p:
        print(f'  {p.name}: ' + ' '.join(f'{k}={v}' for k, v in sorted(p.count.items())), flush=True)
        print(f'  {p.name} orders: {p.orders}; actions the clicks became: {p.actions}', flush=True)
    fs = [f for _, f, _ in g.food]
    print(f'  food: start {food0}, end {fs[-1] if fs else "?"}, max {max(fs) if fs else "?"}, '
          f'rises {g.food_rises}, samples {len(fs)}, host/guest mismatches {g.food_mismatch}', flush=True)
    for name, s in (('host', st1), ('guest', gst)):
        m = re.search(r'tribe\((?:host|guest)\).*', s)
        print(f'  {name}:', m.group(0) if m else '?')
    co = re.search(r'tribe co-op: .*', st1)
    print('  co-op:', co.group(0) if co else '?', f'(both raided one tribe within 10 s: {g.both_attacked}x)')
    for kind, name in (('t', 'citizens'), ('n', 'animals')):
        p, r = g.sync_pos[kind], g.sync_rot[kind]
        if p:
            if kind == 't':
                print(f'  sync {name:9}: raw (the host now, the {TRIBE_LAG} s delay included) p90 '
                      f'{pct(g.sync_raw[kind], .9):5.2f}', flush=True)
            print(f'  sync {name:9}: samples {len(p):4} pos p50 {pct(p, .5):5.2f} p90 {pct(p, .9):5.2f} '
                  f'max {max(p):6.2f} m | facing p90 {pct(r, .9):5.1f} deg', flush=True)
    for k, v in g.outliers.items():
        print(f'  sync outlier {k}: {v}', flush=True)
    probe_report(g.probe, g.log)
    ck(ok, 'the games survived the play')
    ck(not g.partner_selected, f'nobody selects a partner\'s member ({g.partner_selected[:3]})')
    ck(g.food_mismatch <= max(1, len(fs) // 20), f'the shared food is the same on both ({g.food_mismatch} differences)')
    ck(g.food_rises >= 2, f'gathering and hunting bring food ({g.food_rises} rises)')
    ck(len(g.roster_mismatch) <= 1, f'the same members, owners and chiefs on both ({g.roster_mismatch[:3]})')
    for p in g.p:
        tot = p.orders['ok'] + p.orders['lost'] + p.orders['idle']
        ck(p.orders['lost'] == 0, f'{p.name}: no order lost ({p.orders["lost"]} of {tot})')
        ck(tot >= 10, f'{p.name}: gave orders through real clicks ({tot})')
        ck(p.orders['ok'] >= 0.7 * tot, f'{p.name}: the members act on the orders ({p.orders})')
    ck(counter(st1, 'rejected') == g.rejected0,
       f'the host rejected no guest order ({counter(st1, "rejected") - g.rejected0}: {g.rejections[:3]})')
    far = g.outliers
    ck(not far, f'no citizen or animal more than 50 m off on the guest ({list(far)[:5]})')
    for kind in 'tn':
        if g.sync_pos[kind]:
            ck(pct(g.sync_pos[kind], .9) <= LIMITS[kind][0],
               f'sync {kind}*: 90% within {LIMITS[kind][0]} m' + (f' of the host\'s path over {TRIBE_LAG} s' if kind == 't' else ''))
    if not ok or not ck.ok:
        print('  last actions:')
        for line in g.log[-40:]:
            print('   ', line)
    print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_tribe.py --minutes {a.minutes} --seed {seed}')
    try:
        shot(host, 'play_tribe_host.png')
        shot(guest, 'play_tribe_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after the play ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
