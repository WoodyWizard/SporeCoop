#!/usr/bin/env python3
"""Two players PLAY the Civilization stage together, mostly through real input, the way people do:
pan the camera (WASD), open their own cities and buy vehicles with the city menu buttons, box-select
their own vehicles (drag) and right-click to move them, claim spice, attack an NPC city together,
switch roles and give vehicles in the co-op window (F9), react to an attacked city.

Console commands only measure (coop.civ, coop.res, coop.sync, coop.status, exc) and set up (money
for the session). Checked all along: orders, purchases and captures happen on the host and the
guest sees them, money is the same on both, vehicles stay in sync (coop.sync p90 like
coop_sync.py), no freeze / crash / fault.

  play_civ.py [--minutes 10] [--seed N] [--fixture civ_npc]
"""
import argparse
import math
import random
import re
import sys
import threading
import time

import lane
from coop_ui import Ui
from sporectl import Console
from twoinst import Checks, Monitor, shot, start

W, H = 1440, 1080
HUD_BOTTOM = 940  # the palette / minimap / buttons start below this line
BTN_LAND, BTN_WATER, BTN_AIR, BTN_PLANNER = '062D6902', '062D6908', '062D690F', '062D5618'
BTN_ATTACK, BTN_CONVERT = '037E95B9', '037E95BA'  # another nation's city menu (window id = command id)
PAUSE_MENU = '064C23F8'    # the options (pause) menu, Esc
PLANNER_NAME = '272EB68E'  # the city name at the bottom of the city planner
PLANNER_DONE = (1412, 1054)  # the planner's green check


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / n for x in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def civ_state(con):
    """cities {host index: dict}, vehicles {net id: dict}, status line"""
    cities, veh, st = {}, {}, ''
    for line in con.call('coop.civ')['out'].splitlines():
        if line.startswith('civ('):
            st = line
        m = re.match(r'city (-?\d+) \S+ pol=(\w+)( \(player\))? owner=(-?\d+) buildings=(\d+) '
                     r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) capture=([-\d.]+)/(\w+) convert=([-\d.]+)', line)
        if m:
            cities[int(m.group(1))] = dict(
                pol=m.group(2), player=bool(m.group(3)), owner=int(m.group(4)), buildings=int(m.group(5)),
                pos=tuple(float(m.group(i)) for i in (6, 7, 8)), capture=float(m.group(9)), capture_by=m.group(10),
                convert=float(m.group(11)))
            r = re.search(r'ruins=(\d+)', line)
            cities[int(m.group(1))]['ruins'] = int(r.group(1)) if r else 0
        m = re.match(r'\s+veh #(\d+) \S+ pol=(\w+) owner=(-?\d+) purpose=(\d+) loco=(\d+) sel=(\d+) hp=(\S+) '
                     r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)(?: order=(-?\d+))?', line)
        if m and m.group(1) != '0':
            veh[int(m.group(1))] = dict(pol=m.group(2), owner=int(m.group(3)), purpose=int(m.group(4)),
                                        loco=int(m.group(5)), sel=int(m.group(6)), hp=float(m.group(7)),
                                        pos=tuple(float(m.group(i)) for i in (8, 9, 10)),
                                        order=int(m.group(11) or -1))
    return cities, veh, st


def money(con):
    m = re.search(r'shared money=([-\d.]+) synced=1', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def stat(line, key):
    m = re.search(key + r'=(\d+)', line)
    return int(m.group(1)) if m else 0


class Player:
    """one player at the keyboard: camera, clicks, the city menu, the co-op window"""

    def __init__(self, name, con, pid, rng, log):
        self.name, self.c, self.id, self.rng, self.log = name, con, pid, rng, log
        self.ui = Ui(con, name)

    def do(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: {line}')
        return self.c.call(line).get('out', '')

    # ---- camera ----
    def cam(self):
        rows, vw = {}, (0.546, 0.410)
        for line in self.c.call('coop.cam')['out'].splitlines():
            m = re.match(r'\s*view\[(\d)\] =\s+(\S+)\s+(\S+)\s+(\S+)', line)
            if m:
                rows[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
            m = re.search(r'viewWindow=\(([\d.]+) ([\d.]+)\)', line)
            if m:
                vw = (float(m.group(1)), float(m.group(2)))
        if len(rows) < 4:
            return None
        return dict(right=rows[0], fwd=rows[1], up=rows[2], pos=rows[3], vw=vw)

    @staticmethod
    def project(cam, p):
        """screen (x, y) of world point p and whether it can be clicked (in front, not behind the planet)"""
        d = sub(p, cam['pos'])
        z = dot(d, cam['fwd'])
        if z < 1:
            return None, False
        x = W / 2 + dot(d, cam['right']) / z / cam['vw'][0] * W / 2
        y = H / 2 - dot(d, cam['up']) / z / cam['vw'][1] * H / 2
        # hidden behind the planet: the ray from the camera meets the sphere below the point first
        r = math.sqrt(dot(p, p)) * 0.985
        o, u = cam['pos'], norm(d)
        b = dot(o, u)
        c = dot(o, o) - r * r
        disc = b * b - c
        hidden = disc > 0 and (-b - math.sqrt(disc)) < math.sqrt(dot(d, d)) - 2
        return (x, y), not hidden

    def wait_still(self, timeout=2.5):
        """the camera glides on after a key is released: wait until it stops"""
        t0, last = time.time(), None
        while time.time() - t0 < timeout:
            cam = self.cam()
            if cam and last and dist(cam['pos'], last['pos']) < 0.3 and dist(cam['fwd'], last['fwd']) < 0.002:
                return cam
            last = cam
            time.sleep(0.15)
        return last

    def keep_altitude(self, cam):
        """players zoom out to see their units, not along the ground: 150..420 m above the surface"""
        alt = math.sqrt(dot(cam['pos'], cam['pos'])) - 500
        if 150 <= alt <= 420:
            return False
        self.do(f'wheel 720 540 {-2 if alt < 150 else 2}')
        time.sleep(0.6)
        return True

    def look_at(self, p, box=(260, 200, W - 260, HUD_BOTTOM - 160), tries=14):
        """pan the camera with W/A/S/D until world point p is on screen inside box; returns its screen point"""
        for _ in range(tries):
            cam = self.wait_still()
            if cam and self.keep_altitude(cam):
                cam = self.wait_still()
            if not cam:
                return None
            sp, ok = self.project(cam, p)
            if ok and box[0] <= sp[0] <= box[2] and box[1] <= sp[1] <= box[3]:
                return sp
            # where the camera looks on the ground, and the way to the point along the ground in the
            # camera's right / forward directions (W/A/S/D pan along them)
            o, u = cam['pos'], cam['fwd']
            r = math.sqrt(dot(p, p))
            b = dot(o, u)
            disc = b * b - (dot(o, o) - r * r)
            t_hit = -b - math.sqrt(disc) if disc > 0 else math.sqrt(max(dot(o, o) - r * r, 1))
            focus = tuple(oo + t_hit * uu for oo, uu in zip(o, u))
            nrm = norm(focus)
            fwd_t = norm(tuple(f - dot(u, nrm) * n for f, n in zip(u, nrm)))
            right_t = norm(tuple(f - dot(cam['right'], nrm) * n for f, n in zip(cam['right'], nrm)))
            d = sub(p, focus)
            dr, df = dot(d, right_t), dot(d, fwd_t)
            if ok and sp is not None:  # on screen but outside the box: the screen offset is more exact
                dr = (sp[0] - W / 2) / 3.0
                df = -(sp[1] - (box[1] + box[3]) / 2) / 3.0
            for key, amount in sorted(((('d' if dr > 0 else 'a'), abs(dr)), (('w' if df > 0 else 's'), abs(df))),
                                      key=lambda x: -x[1]):
                if amount < 15:
                    continue
                self.do(f'key {key} down')
                time.sleep(min(0.9, 0.08 + amount / 260))
                self.do(f'key {key} up')
                time.sleep(0.1)
        return None

    def screen_of(self, p):
        cam = self.wait_still()
        if not cam:
            return None
        sp, ok = self.project(cam, p)
        return sp if ok and 0 < sp[0] < W and 0 < sp[1] < HUD_BOTTOM else None

    # ---- UI ----
    def where(self, *ids):
        out = {}
        for line in self.c.call('coop.ui where ' + ' '.join(ids))['out'].splitlines():
            f = line.split()
            if len(f) >= 6:
                out[f[0]] = (float(f[1]), float(f[2]), float(f[3]), float(f[4]), f[5] == 'enabled=1')
        return out

    def wait_ui(self, wid, shown, timeout):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if bool(self.where(wid).get(wid)) == shown:
                return True
            time.sleep(0.3)
        return False

    def click_button(self, bid):
        r = self.where(bid).get(bid)
        if not r or not r[4]:
            return False
        self.do(f'click {int((r[0] + r[2]) / 2)} {int((r[1] + r[3]) / 2)}')
        return True

    def open_city(self, city_pos):
        """the city menu of a city: pan to it, click its city hall"""
        sp = self.look_at(city_pos)
        if not sp:
            return False
        for dy in (0, -25, -50):
            self.do(f'click {int(sp[0])} {int(sp[1]) + dy}')
            time.sleep(0.8)
            if self.where(BTN_PLANNER).get(BTN_PLANNER):
                return True
        return False

    def box_select(self, points):
        """a selection box around these screen points (own vehicles); empty ground deselects first"""
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        x1, y1 = max(5, min(xs) - 45), max(5, min(ys) - 45)
        x2, y2 = min(W - 5, max(xs) + 45), min(HUD_BOTTOM - 5, max(ys) + 45)
        self.do(f'drag {int(x1)} {int(y1)} {int(x2)} {int(y2)}')
        time.sleep(0.4)

    def right_click(self, x, y):
        self.do(f'move {int(x)} {int(y)}')
        time.sleep(0.15)
        self.do(f'click {int(x)} {int(y)} right')


def nodes(con):
    """spice nodes: [(pos, pol)]"""
    out = []
    for line in con.call('coop.civ nouns 403DF5C')['out'].splitlines():
        m = re.match(r'obj \S+ (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) pol=(\w+)', line)
        if m:
            out.append(((float(m.group(1)), float(m.group(2)), float(m.group(3))), m.group(6)))
    return out


class Game:
    """what both players share: the consoles for checks, the log, the counters, the pending checks"""

    def __init__(self, host, guest, seed):
        self.host_inst, self.guest_inst = lane.HOST, lane.GUEST
        self.seed = seed
        self.lock = threading.Lock()
        self.log = []
        self.count = {}
        self.problems = []
        self.pending = []  # (deadline, what, check() -> bool)
        self.stop = False
        self.target_city = None  # the NPC city both players besiege
        self.alarm_city = None
        self.pos_err = []
        self.outliers = []
        self.missing = {}
        self.money_checks = 0
        self.joint = False

    def bump(self, key, n=1):
        with self.lock:
            self.count[key] = self.count.get(key, 0) + n

    def problem(self, text):
        with self.lock:
            self.problems.append(f'{time.strftime("%H:%M:%S")} {text}')
        print('  PROBLEM', text, flush=True)

    def expect(self, seconds, what, check, soft=None):
        """check(consoles) must come true within `seconds`; soft: a counter name instead of a problem
        (the game may refuse it: a destination it cannot reach, a node across the sea)"""
        with self.lock:
            self.pending.append((time.time() + seconds, what, check, soft))

    def order_check(self, who, ids, before, what, wanted=None):
        """the order reached the host: a selected vehicle has an order (of `wanted` type) or moved.
        A click the game ignores (water for a land vehicle, a click beside the target) gives no order
        on the host either: the guest's rate is compared with the host's at the end"""
        self.bump(f'orders_{who}')
        def got(c):
            _, hv, _ = civ_state(c['host'])
            return any(i in hv and ((hv[i]['order'] != -1 and (wanted is None or hv[i]['order'] in wanted)) or
                                    dist(hv[i]['pos'], before[i]) > 4) for i in ids if i in before)
        self.expect(6, what + ': the order reached the host', got, soft=f'orders_lost_{who}')


class Bot(Player):
    """one player's hands: a loop of actions chosen at random, weighted like normal play"""

    def __init__(self, name, inst, pid, game):
        self.g = game
        super().__init__(name, Console(inst), pid, random.Random(game.seed * 2 + pid), game.log)
        self.hc = Console(lane.HOST)  # the truth: the host's state, read by this player's thread
        self.partner = 1 - pid
        self.actions = 0

    def do(self, line):
        with self.g.lock:
            self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: {line}')
        return self.c.call(line).get('out', '')

    # ---- what this player can see ----
    def mine(self, cities=None, veh=None):
        if cities is None:
            cities, veh, _ = civ_state(self.c)
        my_c = {i: c for i, c in cities.items() if c['owner'] in (self.id, 253)}
        my_v = {i: v for i, v in veh.items() if v['owner'] == self.id}
        return my_c, my_v

    def screen_vehicles(self):
        self.wait_still()
        out = {}
        for line in self.c.call('coop.targets')['out'].splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'vehicle':
                out[int(f[1])] = (int(f[2]), int(f[3]), int(f[4]))
        return out

    def unpause(self):
        """the pause menu or a city planner left open: back to the planet"""
        w = self.where(PAUSE_MENU, PLANNER_NAME)
        if w.get(PAUSE_MENU):
            self.do('key esc')
            time.sleep(0.5)
        if w.get(PLANNER_NAME):
            self.do(f'click {PLANNER_DONE[0]} {PLANNER_DONE[1]}')
            time.sleep(2)

    def select(self, ids):
        """box-select these own vehicles (they must be on screen); returns the ids selected"""
        sv = self.screen_vehicles()
        pts = [(sv[i][1], sv[i][2]) for i in ids if i in sv and 0 < sv[i][1] < W and 0 < sv[i][2] < HUD_BOTTOM]
        if not pts:
            return []
        self.box_select(pts)
        _, veh, _ = civ_state(self.c)
        sel = [i for i, v in veh.items() if v['sel']]
        foreign = [i for i in sel if veh[i]['owner'] != self.id]
        if foreign:
            self.g.problem(f'{self.name} could select vehicles that are not its own: {foreign}')
        return [i for i in sel if veh[i]['owner'] == self.id]

    def gather_view(self, my_v, ids):
        c = [my_v[i]['pos'] for i in ids]
        mid = tuple(sum(p[k] for p in c) / len(c) for k in range(3))
        return self.look_at(mid, box=(300, 220, W - 300, HUD_BOTTOM - 200))

    # ---- actions ----
    def a_buy(self):
        cities, veh, _ = civ_state(self.c)
        my_c, my_v = self.mine(cities, veh)
        if not my_c or len(my_v) >= 7:
            return 'skip'
        ci = self.rng.choice(sorted(my_c))
        hc0, hv0, hst0 = civ_state(self.hc)
        n0 = len([v for v in hv0.values() if v['owner'] == self.id])
        m0 = money(self.hc)
        if not self.open_city(my_c[ci]['pos']):
            self.g.bump('buy_no_menu')
            return 'no menu'
        btns = self.where(BTN_LAND, BTN_WATER, BTN_AIR)
        usable = [b for b in (BTN_LAND, BTN_WATER, BTN_AIR) if btns.get(b) and btns[b][4]]
        if not usable:
            self.g.bump('buy_no_button')
            return 'no button'
        b = self.rng.choice(usable)
        r = btns[b]
        self.do(f'click {int((r[0] + r[2]) / 2)} {int((r[1] + r[3]) / 2)}')
        t0 = time.time()
        while time.time() - t0 < 6:
            time.sleep(0.5)
            _, hv, _ = civ_state(self.hc)
            new = [i for i, v in hv.items() if v['owner'] == self.id and i not in hv0]
            if new:
                break
        else:
            hcit, _, hst = civ_state(self.hc)
            if hcit.get(ci, {}).get('owner') not in (self.id, 253):
                # the city changed hands meanwhile (a capture splits the cities again): the game
                # says "this city belongs to ..." and buys nothing
                self.g.bump('buy_not_mine')
                return 'the city is not ours any more'
            if stat(hst, 'buys_refused') > stat(hst0, 'buys_refused'):
                # the host has no slot left although this player's button was enabled: it says so
                self.g.bump('buy_refused')
                return 'refused by the host (told)'
            self.g.problem(f'{self.name} bought a vehicle in city {ci} (button {b}) and the host made none '
                           f'(money {m0} -> {money(self.hc)})')
            self.g.bump('buy_lost')
            return 'lost'
        self.g.bump('buy_ok')
        self.g.bump(f'buy_{self.name}')
        nid = new[0]
        m1 = money(self.hc)
        if m0 is not None and m1 is not None and m1 >= m0:
            self.g.problem(f'{self.name}: vehicle #{nid} cost nothing (money {m0} -> {m1})')
        me = self.name
        def sees(c):  # (the host's owner: the vehicle may have been given away or destroyed meanwhile)
            hv = civ_state(c['host'])[1]
            return nid not in hv or civ_state(c[me])[1].get(nid, {}).get('owner') == hv[nid]['owner']
        self.g.expect(5, f'{self.name} sees its new vehicle #{nid}', sees)
        return f'bought #{nid} ({len(new)}) in city {ci}, money {m0} -> {m1}'

    def a_move(self):
        _, my_v = self.mine(*civ_state(self.c)[:2])
        if not my_v:
            return 'skip'
        ids = sorted(my_v)
        pick = self.rng.sample(ids, self.rng.randint(1, len(ids)))
        # the group nearest to the first one (a box can hold only what is on screen)
        p0 = my_v[pick[0]]['pos']
        pick = [i for i in pick if dist(my_v[i]['pos'], p0) < 120]
        if not self.gather_view(my_v, pick):
            return 'no view'
        sel = self.select(pick)
        if not sel:
            self.g.bump('move_nosel')
            return 'nothing selected'
        # 30-70 m away from the group, along the ground (tangent), to a random side
        # (the box may select a vehicle bought after the state was read: use the known ones)
        known = [i for i in sel if i in my_v]
        if not known:
            self.g.bump('move_nosel')
            return 'nothing known selected'
        mid = tuple(sum(my_v[i]['pos'][k] for i in known) / len(known) for k in range(3))
        up = norm(mid)
        a = self.rng.uniform(0, 2 * math.pi)
        t1 = norm(tuple(x - y for x, y in zip((1.0, 0.0, 0.0), tuple(up[0] * u for u in up))))
        t2 = (up[1] * t1[2] - up[2] * t1[1], up[2] * t1[0] - up[0] * t1[2], up[0] * t1[1] - up[1] * t1[0])
        d = self.rng.uniform(30, 70)
        dest = tuple(m + d * (math.cos(a) * x + math.sin(a) * y) for m, x, y in zip(mid, t1, t2))
        sp = self.screen_of(dest)
        if not sp:
            return 'destination off screen'
        _, hv0, _ = civ_state(self.hc)
        self.right_click(*sp)
        before = {i: hv0[i]['pos'] for i in sel if i in hv0}
        self.g.bump('move_sent')

        def moved(c):
            _, hv, _ = civ_state(c['host'])
            return any(i in hv and dist(hv[i]['pos'], p) > 4 for i, p in before.items())
        what = f'{self.name} moved {sel} (right click {int(sp[0])},{int(sp[1])} -> {tuple(int(x) for x in dest)})'
        self.g.order_check(self.name, sel, before, what)
        self.g.expect(10, what, moved, soft='move_stuck')
        return f'move {sel} -> {tuple(int(x) for x in dest)}'

    def a_spice(self):
        _, my_v = self.mine(*civ_state(self.c)[:2])
        land = [i for i, v in my_v.items() if v['loco'] == 0]
        free = [p for p, pol in nodes(self.hc) if pol == 'FFFFFFFF']
        if not land or not free:
            return 'skip'
        vid = self.rng.choice(land)
        node = min(free, key=lambda p: dist(p, my_v[vid]['pos']))
        if dist(node, my_v[vid]['pos']) > 450:
            return 'no free node near'
        if not self.gather_view(my_v, [vid]) or not self.select([vid]):
            return 'no selection'
        sp = self.look_at(node)
        if not sp:
            return 'node not on screen'
        self.right_click(*sp)
        self.g.bump('spice_sent')
        g = self

        def claimed(c):
            hn = {p: pol for p, pol in nodes(c['host'])}
            gn = {p: pol for p, pol in nodes(c['guest'])}
            hp = hn.get(node)
            near = [pol for p, pol in gn.items() if dist(p, node) < 3]
            if hp and hp != 'FFFFFFFF':
                if near and near[0] != hp:
                    g.g.problem(f'spice node {node}: host owner {hp}, guest sees {near[0]}')
                return True
            return False
        what = f'{self.name}: vehicle #{vid} claims the spice node at {tuple(int(x) for x in node)}'
        self.g.order_check(self.name, [vid], {vid: my_v[vid]['pos']}, what, wanted=(8, 6))
        self.g.expect(75, what, claimed, soft='claim_not_reached')
        return f'claim node {tuple(int(x) for x in node)} with #{vid}'

    def a_attack(self):
        cities, veh, _ = civ_state(self.c)
        _, my_v = self.mine(cities, veh)
        army = [i for i, v in my_v.items() if v['purpose'] == 0 and v['loco'] == 0]
        tgt = self.g.target_city
        if not army or tgt is None or tgt not in cities or cities[tgt]['player']:
            return 'skip'
        pick = [i for i in army if dist(my_v[i]['pos'], my_v[army[0]]['pos']) < 120]
        if not self.gather_view(my_v, pick):
            return 'no view'
        sel = self.select(pick)
        if not sel:
            return 'nothing selected'
        sp = self.look_at(cities[tgt]['pos'])
        if not sp:
            return 'city not on screen'
        # a click on the city opens its menu; the crosshair orders the selected vehicles to attack
        self.do(f'click {int(sp[0])} {int(sp[1])}')
        time.sleep(0.8)
        if not self.click_button(BTN_ATTACK):
            self.g.bump('attack_no_button')
            return 'no attack button'
        self.g.bump('attack_sent')
        self.g.bump(f'attack_{self.name}')
        cpos = cities[tgt]['pos']
        start = {i: dist(veh[i]['pos'], cpos) for i in sel}

        def going(c):
            _, hv, _ = civ_state(c['host'])
            return any(i in hv and dist(hv[i]['pos'], cpos) < start[i] - 10 for i in start) or \
                any(i not in hv for i in start)  # or already fought and died
        what = f'{self.name}: {sel} attack city {tgt}'
        self.g.order_check(self.name, sel, {i: veh[i]['pos'] for i in sel}, what, wanted=(2, 1, 6))
        self.g.expect(25, what, going, soft='attack_stuck')
        return f'attack city {tgt} with {sel}'

    def a_role(self):
        self.do('key 0x78')
        time.sleep(0.8)
        st = self.ui.state()
        if st.get('open') != '1' or not st.get('cnext', {}).get('vis'):
            self.do('key 0x78')
            return 'no role buttons'
        n = self.rng.randint(1, 3)
        for _ in range(n):
            self.ui.click('cnext')
        time.sleep(1.5)
        role = re.search(r'My role: (\w+)', ' '.join(str(v) for v in self.ui.state().values()))
        self.do('key 0x78')
        want = role.group(1) if role else '?'
        m = re.search(rf'role {self.id} \S+ (\w+)', self.hc.call('coop.role')['out'])
        self.g.bump('role')
        if not m or m.group(1) != want:
            self.g.problem(f'{self.name} picked the role {want}, the host has {m.group(1) if m else None}')
        return f'role -> {want}'

    def a_give(self):
        _, my_v = self.mine(*civ_state(self.c)[:2])
        if len(my_v) < 2:
            return 'skip'
        vid = self.rng.choice(sorted(my_v))
        if not self.gather_view(my_v, [vid]):
            return 'no view'
        sel = self.select([vid])
        if not sel:
            return 'nothing selected'
        self.do('key 0x78')
        time.sleep(0.8)
        st = self.ui.state()
        if not st.get('give', {}).get('vis'):
            self.do('key 0x78')
            self.g.problem(f'{self.name}: {len(sel)} own vehicles selected but no give button '
                           f'({st.get("creation")})')
            return 'no give button'
        # what the window offers to give: the vehicles selected now (a click into the window can drop one)
        _, veh, _ = civ_state(self.c)
        offered = [i for i, v in veh.items() if v['sel'] and v['owner'] == self.id]
        self.ui.click('give')
        time.sleep(1.0)
        self.do('key 0x78')
        self.g.bump('give')
        _, hv, _ = civ_state(self.hc)
        given = [i for i in offered if i in hv and hv[i]['owner'] == self.partner]
        if offered and not given:
            self.g.problem(f'{self.name} gave {offered} in the co-op window, the host still has owners '
                           f'{[hv.get(i, {}).get("owner") for i in offered]}')
        me = self.name
        self.g.expect(4, f'{self.name} sees {given} owned by the partner and no longer selected',
                      lambda c: all(civ_state(c[me])[1].get(i, {}).get('owner', self.partner) == self.partner and
                                    not civ_state(c[me])[1].get(i, {}).get('sel', 0) for i in given))
        return f'gave {given} of {offered}'

    def a_planner(self):
        my_c, _ = self.mine(*civ_state(self.c)[:2])
        if not my_c:
            return 'skip'
        ci = self.rng.choice(sorted(my_c))
        if not self.open_city(my_c[ci]['pos']) or not self.click_button(BTN_PLANNER):
            return 'no planner button'
        if not self.wait_ui(PLANNER_NAME, True, 8):
            self.g.problem(f'{self.name}: the city planner of city {ci} did not open')
            return 'planner did not open'
        time.sleep(self.rng.uniform(1, 4))  # a look at the city
        for _ in range(3):
            self.do(f'click {PLANNER_DONE[0]} {PLANNER_DONE[1]}')  # the green check: back to the planet
            if self.wait_ui(PLANNER_NAME, False, 5):
                break
        else:
            self.g.problem(f'{self.name}: the city planner does not close')
        self.g.bump('planner')
        if 'Civ' not in self.c.call('game.mode')['out']:
            self.g.problem(f'{self.name}: after the city planner the game mode is {self.c.call("game.mode")["out"]}')
        return f'planner of city {ci}'

    def a_defend(self):
        ci = self.g.alarm_city
        cities, veh, _ = civ_state(self.c)
        _, my_v = self.mine(cities, veh)
        if ci is None or ci not in cities or not my_v:
            return 'skip'
        pick = sorted(my_v)[:6]
        pick = [i for i in pick if dist(my_v[i]['pos'], my_v[pick[0]]['pos']) < 120]
        if not self.gather_view(my_v, pick) or not self.select(pick):
            return 'no selection'
        sp = self.look_at(cities[ci]['pos'])
        if not sp:
            return 'city not on screen'
        self.right_click(sp[0] + 40, sp[1] + 40)
        self.g.bump('defend')
        return f'defend city {ci} with {pick}'

    def a_camera(self):
        k = self.rng.choice(['w', 'a', 's', 'd'])
        self.do(f'key {k} down')
        time.sleep(self.rng.uniform(0.1, 0.6))
        self.do(f'key {k} up')
        if self.rng.random() < 0.4:
            self.do(f'wheel {self.rng.randint(400, 1000)} {self.rng.randint(300, 700)} {self.rng.choice([-2, -1, 1, 2])}')
        return f'camera {k}'

    def a_pause(self):
        self.do('key esc')
        time.sleep(self.rng.uniform(0.5, 2))
        self.do('key esc')
        return 'pause menu'

    WEIGHTS = dict(buy=10, move=22, spice=8, attack=16, role=3, give=4, planner=2, camera=10, pause=1, defend=0)

    def caption(self):
        """a cinematic with a caption waits for OK (a captured city): read it, then press OK like a player"""
        st = civ_state(self.c)[2]
        if 'movie=1' not in st:
            return False
        time.sleep(self.rng.uniform(2, 5))
        self.do('key enter')
        self.g.bump('caption_ok')
        time.sleep(1)
        return True

    def step(self):
        self.unpause()
        if self.caption():
            return
        w = dict(self.WEIGHTS)
        if self.g.alarm_city is not None:
            w['defend'] = 25
        kind = self.rng.choices(list(w), weights=list(w.values()))[0]
        self.g.bump(kind)
        try:
            r = getattr(self, 'a_' + kind)()
        except OSError as e:
            self.g.problem(f'{self.name}: console error in {kind}: {e!r}')
            self.g.stop = True
            return
        except (ValueError, KeyError, IndexError, TypeError) as e:  # a bot bug, not the game's: note it, go on
            import traceback
            self.g.bump('bot_errors')
            print(f'  bot error ({self.name} {kind}): {e!r}\n' + traceback.format_exc(limit=3), flush=True)
            return
        with self.g.lock:
            self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: == {kind}: {r}')
        self.actions += 1
        time.sleep(self.rng.uniform(0.3, 1.5))

    def run(self):
        while not self.g.stop:
            self.step()


def audit(game, hc, gc, final=False):
    """host and guest agree: money, cities, vehicles (sync error), spice nodes; pending checks"""
    hcit, hv, hst = civ_state(hc)
    gcit, gv, gst = civ_state(gc)
    # money: the guest gets the total within a moment
    mh, mg = money(hc), money(gc)
    if mh is not None and mg is not None and abs(mh - mg) > 1:
        time.sleep(1.5)
        mh, mg = money(hc), money(gc)
        if abs(mh - mg) > 1:
            game.problem(f'money differs: host {mh}, guest {mg}')
    game.money_checks += 1
    for i, c in hcit.items():
        g = gcit.get(i)
        if not g:
            # a new city appears on each machine a few seconds after the site was chosen
            game.missing[i] = game.missing.get(i, 0) + 1
            if game.missing[i] >= 3 or final:
                game.problem(f'city {i} missing on the guest')
            continue
        game.missing.pop(i, None)
        if g['pol'] != c['pol'] or g['owner'] != c['owner']:
            time.sleep(1)
            g2 = civ_state(gc)[0].get(i, {})
            if g2.get('pol') != c['pol'] or g2.get('owner') != c['owner']:
                game.problem(f'city {i}: host pol {c["pol"]} owner {c["owner"]}, guest pol {g2.get("pol")} '
                             f'owner {g2.get("owner")}')
        if g.get('ruins') != c.get('ruins'):
            game.bump('ruins_differ')
            if final:
                game.problem(f'city {i}: {c["ruins"]} ruins on the host, {g.get("ruins")} on the guest')
        if abs(g['buildings'] - c['buildings']) > 0:
            game.bump('buildings_differ')
            if final:
                game.problem(f'city {i}: {c["buildings"]} buildings on the host, {g["buildings"]} on the guest')
    if set(hv) != set(gv):
        time.sleep(1)
        _, hv, _ = civ_state(hc)
        _, gv, _ = civ_state(gc)
        if set(hv) != set(gv):
            game.problem(f'vehicles differ: host only {sorted(set(hv) - set(gv))}, guest only {sorted(set(gv) - set(hv))}')
    # BUG-0206: a gift changes the owner on the host at once and reaches the guest with the next
    # snapshot (0.1 s; while the guest's bot is in a city planner, only when it is back): an owner
    # that differs is looked at again 1.5 s later, like the cities, and is a problem only then
    differ = [i for i, v in hv.items() if i in gv and gv[i]['owner'] != v['owner']]
    if differ:
        game.bump('owner_differs_once')
        print(f'    {time.strftime("%H:%M:%S")} vehicle owners differ once: '
              + ', '.join(f'#{i} host {hv[i]["owner"]} guest {gv[i]["owner"]}' for i in differ)
              + f' | guest mode {gc.call("game.mode")["out"].strip()[:40]}', flush=True)
        time.sleep(1.5)
        _, hv2, _ = civ_state(hc)
        _, gv2, _ = civ_state(gc)
        for i in differ:
            if i in hv2 and i in gv2 and gv2[i]['owner'] != hv2[i]['owner']:
                game.problem(f'vehicle #{i}: owner {hv2[i]["owner"]} on the host, {gv2[i]["owner"]} on the guest')
    # sync error of the vehicles (coop.sync, like coop_sync.py: the closer of two host samples)
    from coop_sync import sample
    for _ in range(3):
        h1, g, h2 = sample(hc), sample(gc), sample(hc)
        for k, (gp, _q) in g.items():
            if k.startswith('v') and k in h1 and k in h2:
                e = min(dist(gp, h1[k][0]), dist(gp, h2[k][0]))
                game.pos_err.append(e)
                if e > 10:  # an outlier: which vehicle, where, when
                    game.outliers.append(f'{time.strftime("%H:%M:%S")} {k} {e:.1f} m host {tuple(round(x) for x in h1[k][0])} '
                                         f'guest {tuple(round(x) for x in gp)}')
                    # what the vehicle does on both machines (BUG-0186: a standing vehicle with an offset)
                    for nm, con in (('host', hc), ('guest', gc)):
                        ln = [x.strip() for x in con.call('coop.civ')['out'].splitlines() if f'veh #{k[1:]} ' in x]
                        print(f'    outlier {k} {e:.1f} m {nm}: {ln[0] if ln else "-"}', flush=True)
    hn = nodes(hc)
    gn = nodes(gc)
    for p, pol in hn:
        near = [q for q in gn if dist(q[0], p) < 3]
        if near and near[0][1] != pol:
            time.sleep(3)  # nodes go out once a second (several chunks with a big planet)
            gn2 = nodes(gc)
            near = [q for q in gn2 if dist(q[0], p) < 3]
            if near and near[0][1] != pol:
                game.problem(f'spice node {tuple(int(x) for x in p)}: host owner {pol}, guest {near[0][1]}')
    # the alarm: an attacked player city calls for help
    alarms = stat(hst, 'alarms')
    if alarms > game.count.get('alarms_seen', 0):
        game.count['alarms_seen'] = alarms
        st = gc.call('coop.status')['out']
        game.bump('alarm')
        mine = [i for i, c in hcit.items() if c['player']]
        game.alarm_city = mine[0] if mine else None
    now = time.time()
    with game.lock:
        pend, game.pending = game.pending, []
    keep = []
    for deadline, what, check, soft in pend:
        try:
            ok = check({'host': hc, 'guest': gc})
        except (OSError, ValueError, KeyError):
            ok = False
        if ok:
            game.bump('checks_ok')
        elif now > deadline or final:
            if soft:
                game.bump(soft)
                game.log.append(f'{time.strftime("%H:%M:%S")} (game) did not happen: {what}')
            else:
                game.problem('did not happen: ' + what)
                game.bump('checks_failed')
        else:
            keep.append((deadline, what, check, soft))
    with game.lock:
        game.pending = keep + game.pending
    return hcit, hv, hst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--fixture', default='civ_npc')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play civ: fixture {a.fixture}, {a.minutes} min, seed {seed}', flush=True)
    host, guest = start(a.fixture)
    ck = Checks()
    time.sleep(3)
    game = Game(host, guest, seed)
    # setup: money for a session of play (a new game starts with 3000)
    host.call('coop.res add money 30000')
    hcit, hv, _ = civ_state(host)
    pc = [c for c in hcit.values() if c['player']]
    npc = sorted((i for i, c in hcit.items() if not c['player']),
                 key=lambda i: dist(hcit[i]['pos'], pc[0]['pos']) if pc else 0)
    game.target_city = npc[0] if npc else None
    print(f'  cities: player {[i for i, c in hcit.items() if c["player"]]}, NPC {npc}, siege target {game.target_city}',
          flush=True)
    bots = [Bot('host', lane.HOST, 0, game), Bot('guest', lane.GUEST, 1, game)]
    threads = [threading.Thread(target=b.run, daemon=True) for b in bots]
    for t in threads:
        t.start()
    hc, gc = Console(lane.HOST), Console(lane.GUEST)
    t0 = time.time()
    last_report = t0
    small_chunks = False
    capture_max = {}
    owners0 = {i: c['pol'] for i, c in hcit.items()}
    try:
        while time.time() - t0 < a.minutes * 60 and not game.stop:
            time.sleep(8)
            if Monitor.events:
                game.problem('invariant: ' + '; '.join(Monitor.events))
                break
            hcit, hv, hst = audit(game, hc, gc)
            if stat(hst, 'max_chunk') > 1200 - 48:
                game.problem(f'a snapshot chunk of {stat(hst, "max_chunk")} bytes (packets hold 1200)')
            if not small_chunks and time.time() - t0 > a.minutes * 30:
                # the second half with small snapshot chunks: many chunks per cycle, like a big planet
                small_chunks = True
                print('  ' + host.call('coop.civ chunk 300')['out'], flush=True)
            for i, c in hcit.items():
                capture_max[i] = max(capture_max.get(i, 0), c['capture'])
            if money(hc) is not None and money(hc) < 4000:
                host.call('coop.res add money 10000')  # setup: keep buying possible
            if game.target_city is not None and hcit.get(game.target_city, {}).get('player'):
                game.bump('captured')
                left = [i for i, c in hcit.items() if not c['player']]
                print(f'  city {game.target_city} captured; next target {left[:1]}', flush=True)
                game.target_city = left[0] if left else None
            if time.time() - last_report > 60:
                last_report = time.time()
                print(f'  {time.time() - t0:5.0f} s: ' + ' '.join(f'{k}={v}' for k, v in sorted(game.count.items()))
                      + f' | host {hst}', flush=True)
    except (OSError, ValueError) as e:
        game.problem(f'console error: {e!r}')
    game.stop = True
    try:
        host.call('coop.civ chunk 1200')  # back to full chunks (the clamp keeps the packet limit)
    except (OSError, ValueError):
        pass
    for t in threads:
        t.join(timeout=30)
    for b in bots:  # a player left in the pause menu gets no updates until it goes back to the game
        try:
            b.unpause()
        except (OSError, ValueError):
            pass
    with game.lock:  # the checks still running get their time
        wait = max([p[0] for p in game.pending] + [time.time()]) - time.time()
    time.sleep(min(max(wait, 0), 80))
    try:
        audit(game, hc, gc, final=True)
    except (OSError, ValueError) as e:
        game.problem(f'final audit: console error {e!r}')
    elapsed = time.time() - t0
    try:
        hcit, hv, hst = civ_state(hc)
        _, _, gst = civ_state(gc)
    except OSError as e:  # a game is gone: the problems above and the invariants tell why
        game.problem(f'the games do not answer after the session: {e!r}')
        hcit, hst, gst = {}, '', ''
        Monitor.events.append('a console stopped answering')
    print(f'  played {elapsed / 60:.1f} min: {sum(b.actions for b in bots)} actions '
          f'(host {bots[0].actions}, guest {bots[1].actions})', flush=True)
    print('  ' + ' '.join(f'{k}={v}' for k, v in sorted(game.count.items())), flush=True)
    print(f'  host: {hst}\n  guest: {gst}', flush=True)
    print('  cities: ' + ', '.join(f'{i}: {c["pol"]} owner {c["owner"]} capture max {capture_max.get(i, 0):.0f}'
                                   + (' (new)' if i not in owners0 else ' (taken)' if c['pol'] != owners0[i] else '') for i, c in hcit.items()))
    pe = sorted(game.pos_err)
    if pe:
        p90 = pe[int(len(pe) * 0.9)]
        print(f'  vehicle sync: n={len(pe)} median {pe[len(pe) // 2]:.2f} p90 {p90:.2f} max {pe[-1]:.2f} m', flush=True)
        ck(p90 <= 3.0, f'vehicles in sync: 90% within 3 m (p90 {p90:.2f} m)')
        for o in game.outliers[:10]:
            print('    outlier', o, flush=True)
    ck(elapsed >= a.minutes * 60 * 0.95, f'played the whole session ({elapsed / 60:.1f} min)')
    ck(game.count.get('buy_ok', 0) > 0, f'vehicles bought through the city menu ({game.count.get("buy_ok", 0)})')
    ck(game.count.get('checks_ok', 0) > 0, f'orders, purchases, claims and gifts arrived ({game.count.get("checks_ok", 0)} checks)')
    # orders: the guest's go through the host, the host's do not; the same clicks, the same rate
    rate = {}
    for who in ('host', 'guest'):
        n = game.count.get(f'orders_{who}', 0)
        lost = game.count.get(f'orders_lost_{who}', 0)
        rate[who] = lost / n if n else 0
        print(f'  orders by the {who}: {n}, no order on the host {lost} ({100 * rate[who]:.0f}%)', flush=True)
    if game.count.get('orders_guest', 0) >= 8 and rate['guest'] > rate['host'] + 0.25:
        game.problem(f'guest orders are lost more often than the host\'s ({100 * rate["guest"]:.0f}% vs '
                     f'{100 * rate["host"]:.0f}%)')
    # money: the guest never earns on its own (the host's economy pays everybody)
    try:
        m = re.search(r'shared money=\S+ synced=1 pending=\S+ from_guests=\+(\d+)/(-?\d+)', hc.call('coop.res')['out'])
    except OSError:
        m = None
    if m:
        print(f'  money changes from the guest: +{m.group(1)} / {m.group(2)}', flush=True)
        if int(m.group(1)) > 500:
            game.problem(f'the guest earned {m.group(1)} on its own (counted twice: the host earns it too)')
    sent, done, rej = stat(gst, 'cmds_sent'), stat(hst, 'cmds_done'), stat(hst, 'rejected')
    print(f'  guest commands: sent {sent}, executed {done}, rejected {rej} '
          f'(refused purchases {stat(hst, "buys_refused")})', flush=True)
    if sent != done + rej or rej > stat(hst, 'buys_refused'):
        game.problem(f'guest commands: sent {sent}, the host executed {done} and rejected {rej}')
    for p in game.problems:
        print('   ', p)
    ck(not game.problems, f'no problems ({len(game.problems)})')
    if game.problems:
        print('  last actions:')
        for line in game.log[-40:]:
            print('   ', line)
        print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_civ.py --minutes {a.minutes} --seed {seed}')
    try:
        shot(host, 'play_civ_host.png')
        shot(guest, 'play_civ_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after the session ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
