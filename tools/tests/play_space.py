#!/usr/bin/env python3
"""Space stage played by two bots the way people play it (docs/stability/space.md).

Both players fly at the same time, mostly through real input: keys (W/A/S/D), clicks on planets,
stars and enemy ships, the mouse wheel and the zoom buttons, the tool palettes and their hot keys,
the minimap, the co-op window (F9, "Jump to") and the captions that wait for OK (Enter). Each
player's own thread picks what to do next from what is on its screen (coop.targets all: obj
<key> <distance> x y; keys s<id> ships, P/C<PlanetID> planets / own colonies in a system,
S/H<StarID> stars in the galaxy view):

  * fly around a planet or a system, land on planets (zoom in), take off again (zoom out),
    leave the system for the galaxy, fly to a nearby star and enter its system;
  * fight NPC ships with the laser (palette "combat", key 1, clicks on the ship), pirates come
    on their own or are spawned as setup (coop.fleet spawn 2) when none came for a while;
  * a disabled partner (lethal hit next to it) is repaired by flying to it; a lost ship's death
    caption and the respawn caption are confirmed with Enter like a player does;
  * a player far from the partner jumps to it through the co-op window (F9, "Jump to ...").

Console commands only measure (coop.space hp/star, coop.res, coop.sync, coop.fleet list, exc)
and set up (the interstellar drive the fixture's ship lacks; pirates; battle damage that makes
the next real hit lethal, so that rescues happen in a test of minutes, not hours).

Measured all the time (main thread): the shared money is the same on both, NPC ships of the
host's place match on the guest (coop.sync p90 of s* keys), no freeze/crash/fault, every death
is followed by a respawn, nobody stays stuck (dead, in a menu, in a cinematic) for long.

  play_space.py [--minutes 12] [--seed N]
"""
import argparse
import math
import os
import random
import re
import struct
import sys
import threading
import time

import lane
from twoinst import Checks, Monitor, shot, start  # (also puts tools/ on the path)
from sporectl import Console  # noqa: E402

DRIVE = 'interstellardrive'  # tool file name (FNV-1 hash 0x2569FAE8), docs/re/space.md 7.9


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    return struct.unpack('>II', head[16:24])


def captain_open(con):
    """the captain screen (opened by the rank badge over the ship's picture) is up: its "Equipment"
    panel is shown. It is not a caption (finale busy=0) and the game ignores the mouse wheel while it
    is up, so a zoom-out to the galaxy view never happens (BUG-0303)"""
    import uifind
    return uifind.shown(con, 'Equipment') is not None


def close_captain(con, tries=3):
    """close the captain screen like a player: its OK button (Esc opens the pause menu over it)"""
    import uifind
    for _ in range(tries):
        if not captain_open(con):
            return True
        p = uifind.shown(con, 'OK')
        con.call(f'click {p[0]} {p[1]}' if p else 'click 1224 856')
        time.sleep(1.5)
    return not captain_open(con)


class Stop(Exception):
    pass


class Player(threading.Thread):
    """one player: own console connection, own random choices (seeded), own log"""

    def __init__(self, name, inst, pid, seed, end, stats):
        super().__init__(daemon=True)
        self.name, self.inst, self.pid = name, inst, pid
        self.c = Console(inst, timeout=30)
        self.rng = random.Random(seed)
        self.end = end
        self.stats = stats  # shared dict of counters (main thread reads)
        self.log = []
        self.W, self.H = 1440, 1080
        self.palette = None  # the tool category the bot opened last
        self.partner = None  # the other Player
        self.error = None
        self.dead_since = 0
        self.last_spawn = 0
        self.last_fight = time.time()
        self.last_damage = time.time() - 100
        self.apart_since = None
        self.last_place = (0, 0)
        self.no_landing = set()

    # ---------------- input / state ----------------
    def cmd(self, line):
        if time.time() > self.end:
            raise Stop()
        r = self.c.call(line)
        return r.get('out', '')

    def act(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {line}')
        del self.log[:-60]
        return self.cmd(line)

    def count(self, key, n=1):
        k = f'{self.name}.{key}'
        self.stats[k] = self.stats.get(k, 0) + n

    def sleep(self, s):
        if time.time() + s > self.end:
            raise Stop()
        time.sleep(s)

    def hp(self):
        out = self.cmd('coop.space hp')
        d = {k: float(v) for k, v in re.findall(r'(\w+)=([-\d.]+)', out)}
        m = re.search(r'hp ([\d.]+) max ([\d.]+)', out)
        if m:
            d['hp'], d['max'] = float(m.group(1)), float(m.group(2))
        return d

    def place(self):
        """(place, StarID): place = PlanetID | 0x40000000 on a planet, StarID | 0x80000000 in a system"""
        m = re.search(r'place ([0-9A-F]+) star (\d+)', self.cmd('coop.space star'))
        return (int(m.group(1), 16), int(m.group(2))) if m else (0, 0)

    def active_planet(self):
        """the planet the ship is at or flies to (the game's active planet), PlanetID hex"""
        m = re.search(r'active ([0-9A-F]+)', self.cmd('coop.space star'))
        return m.group(1) if m else ''

    def ctx(self):
        """'planet' | 'system' | 'galaxy' (the mod's place: PlanetID | 0x40000000, StarID | 0x80000000)"""
        p, _ = self.place()
        return 'planet' if p & 0x40000000 else 'system' if p & 0x80000000 else 'galaxy'

    def targets(self):
        me, players, objs = None, {}, []
        for line in self.cmd('coop.targets all').splitlines():
            f = line.split()
            if f and f[0] == 'me' and len(f) == 5:
                me = (int(f[3]), int(f[4]))
            elif f and f[0] == 'player' and len(f) == 5:
                players[int(f[1])] = (int(f[3]), int(f[4]))
            elif f and f[0] == 'far' and len(f) == 5:  # off the screen: its projected point
                players.setdefault(int(f[2]), (int(f[3]), int(f[4])))
            elif f and f[0] == 'obj' and len(f) == 5:
                x, y = int(f[3]), int(f[4])
                # not under the HUD; the rank badge over the ship's picture (bottom right, up to
                # H - 205) opens the captain screen, which blocks the wheel (BUG-0303)
                if 40 < x < self.W - 40 and 60 < y < self.H - 180 and not (x > self.W - 120 and y > self.H - 225):
                    objs.append((f[1], float(f[2]), x, y))
        return me, players, objs

    def key_hold(self, keys, secs):
        for k in keys:
            self.act(f'key {k} down')
        try:
            time.sleep(secs)
        finally:
            for k in keys:
                self.cmd(f'key {k} up')

    def steer(self, tx, ty, me, secs=None):
        """fly toward a point on the screen with the keys (W up, S down, A left, D right)"""
        mx, my = me if me else (self.W // 2, self.H // 2)
        dx, dy = tx - mx, ty - my
        keys = []
        if abs(dy) > 30:
            keys.append('0x57' if dy < 0 else '0x53')
        if abs(dx) > 30:
            keys.append('0x44' if dx > 0 else '0x41')
        if keys:
            self.key_hold(keys, secs or min(1.5, 0.25 + math.hypot(dx, dy) / 600))

    # HUD (bottom right, anchored to the right edge): the tool categories; closed palette: tabs at
    # H-51, open: H-151. Order: main, combat, gear (scan/abduct), colony, terraform x3, cargo.
    CATS = {'main': 426, 'combat': 387, 'gear': 347, 'colony': 306}

    def palette_open(self):
        """True / False from a screenshot: the tab row has the colony flag (purple) at W-306 and the
        main tab (green) at W-426 on row H-151 when the palette is open, H-51 when it is closed"""
        from PIL import Image
        path = os.path.join(lane.out_dir('shots'), f'palette_{self.name}.png')
        self.cmd('shot ' + path)
        im = Image.open(path).convert('RGB')
        W, H = im.size

        def tabs(y):
            r, g, b = im.getpixel((W - 306, y))
            r2, g2, b2 = im.getpixel((W - 426, y - 6))
            return abs(r - 100) < 30 and abs(g - 65) < 30 and abs(b - 167) < 30 and g2 > 140 and r2 < g2
        return True if tabs(H - 151) else False if tabs(H - 51) else None

    def open_palette(self, cat):
        """open the tool palette of this category (combat: the weapons, keys 1..9 pick the slot)"""
        state = self.palette_open()
        if state is None and self.planner_open():
            # a click on an own city (a shot that hit it) opened the city planner: done closes it
            self.act(f'click {self.W - 25} {self.H - 22}')
            self.count('planner_closed')
            self.sleep(2)
            state = self.palette_open()
        if state is None:
            self.count('palette_unknown')
        y = self.H - 151 if state else self.H - 51
        self.act(f'click {self.W - self.CATS[cat]} {y}')
        self.palette = cat
        self.sleep(0.3)

    def ui_button(self, caption):
        """centre of a visible UI window with this caption (absolute: child rects are relative)"""
        out = self.cmd(f'coop.ui find {caption}')
        rects = []
        for line in out.splitlines():
            m = re.search(r'fl=0x([0-9A-F]+) \[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]', line)
            if not m:
                continue
            if not int(m.group(1), 16) & 1:
                return None
            rects.append(tuple(int(m.group(i)) for i in range(2, 6)))
        if not rects:
            return None
        x0 = sum(r[0] for r in rects[1:])
        y0 = sum(r[1] for r in rects[1:])
        l, t, r, b = rects[0]
        return x0 + (l + r) // 2, y0 + (t + b) // 2

    # ---------------- what a player does ----------------
    def captions(self):
        """a caption waits for OK (death, respawn, missions, badges): read it, press Enter"""
        st = self.hp()
        if st.get('state') == 2:
            if not self.dead_since:
                self.dead_since = time.time()
                self.count('deaths')
        elif self.dead_since:
            self.stats.setdefault('respawn_s', []).append(time.time() - self.dead_since)
            self.dead_since = 0
            self.count('respawns')
        if st.get('movie') or st.get('cine') or st.get('state') == 2:
            self.sleep(self.rng.uniform(1.0, 2.5))
            self.act('key enter')
            self.count('captions_ok')
            return True
        # the captain screen (a click on the rank badge): OK closes it, Esc does not (BUG-0303)
        if captain_open(self.c):
            close_captain(self.c)
            self.count('captain_closed')
            return True
        # a window that pauses the game without a caption (My Collections, the Sporepedia): Esc
        # closes it; the wheel and Enter do nothing there (BUG-0336)
        import coop_space_finale
        if coop_space_finale.close_stray(self.c, self.name):
            self.count('windows_closed')
            return True
        # the pause menu (an Esc too many) or the co-op window left open: close them
        for cap in ('Resume Game',):
            b = self.ui_button(cap)
            if b:
                self.act(f'click {b[0]} {b[1]}')
                self.count('menus_closed')
                return True
        return False

    def rescue(self):
        """the partner's ship is disabled here: fly to it until it is repaired"""
        if not re.search(r'partners_down=[1-9]', self.cmd('coop.status')):
            return False
        self.count('rescue_flights')
        t0 = time.time()
        while time.time() - t0 < 20:
            matched = self.match_height()
            me, players, _ = self.targets()
            p = players.get(self.partner.pid)
            if not p:
                if matched:
                    continue  # (far above: not on the screen until the heights match)
                break
            # the repair counts the distance along the ground (`repair <m>` of coop.space dist): stay
            # put once inside 40 m (the range is 60 m, held 2 s); short pulses near it (the ship
            # coasts ~0.3 s after the keys, overshooting out of the range)
            m = re.search(r'repair ([\d.]+) m;', self.cmd('coop.space dist'))
            ground = float(m.group(1)) if m else 1e9
            if ground < 40 or (me and math.hypot(p[0] - me[0], p[1] - me[1]) < 25):
                self.sleep(0.5)
            else:
                self.steer(p[0], p[1], me, 0.5 if ground > 200 else 0.2)
            if not re.search(r'partners_down=[1-9]', self.cmd('coop.status')):
                break
        return True

    def fight(self, secs):
        """shoot the nearest NPC ship with the laser; fly closer when it is far"""
        me, _, objs = self.targets()
        ships = [o for o in objs if o[0].startswith('s')]
        if not ships:
            return False
        self.open_palette('combat')
        self.act('key 1')
        self.count('fights')
        st = self.hp()
        near = (self.partner_dist() or 1e9) < 150
        if time.time() - self.last_damage > 90 and self.rng.random() < 0.5 and st.get('hp', 0) > 60:
            # setup: battle damage, so the next real hits of the pirates are lethal (a real fight to
            # the death takes far longer than a test): next to the partner the ship is disabled and
            # waits for repair, alone it is destroyed (death caption, respawn at a colony)
            out = self.cmd(f'coop.space hit {st["hp"] - 8:.0f}')
            self.log.append(f'{time.strftime("%H:%M:%S")} battle damage ({"partner near" if near else "alone"}): {out}')
            self.last_damage = time.time()
            self.count('battle_damage')
        self.last_fight = time.time()
        t0 = time.time()
        while time.time() - t0 < secs:
            me, _, objs = self.targets()
            ships = sorted((o for o in objs if o[0].startswith('s')), key=lambda o: o[1])
            if not ships:
                break
            k, d, x, y = ships[0]
            if d > 60 and me:
                self.steer(x, y, me, 0.6)
            self.act(f'click {x} {y}')
            self.count('shots')
            self.sleep(self.rng.uniform(0.3, 0.7))
        return True

    def match_height(self):
        """on a planet a ship flies at the height of its own zoom (~520..~990 m from the centre, ~15-30 m
        per wheel notch on GOG 2024, ~45 m on GOG 2016): a partner far above or below is off the screen (above the camera) or seen
        with parallax. Zoom to its height, as a player looks for it (docs/re/space.md 7.10)."""
        m = re.search(r'alt ([-+]?\d+) m;', self.cmd('coop.space dist'))
        if not m or abs(int(m.group(1))) < 30:
            return False
        # one notch is ~15-30 m on GOG 2024 and ~45 m on GOG 2016, and the zoom is animated: a step of
        # alt/30 notches (at most 4) per call, the next call corrects what is left
        a = int(m.group(1))
        n = max(-4, min(4, -round(a / 30))) or (-1 if a > 0 else 1)
        self.zoom(n)  # wheel up (+) = lower
        self.count('height_matched')
        self.sleep(0.7)
        return True

    def partner_dist(self):
        m = re.search(r' ([\d.]+) m;', self.cmd('coop.space dist'))
        return float(m.group(1)) if m else None

    def regroup(self):
        """the partner is on the same planet but far: fly toward it (its point on the screen, or its
        projected point off the screen) until it is close"""
        d = self.partner_dist()
        if d is None or d < 80:
            return False
        self.count('regroups')
        for _ in range(14):
            me, players, _ = self.targets()
            p = players.get(self.partner.pid)
            if not p or not me:
                break
            self.steer(p[0], p[1], me, 1.0)
            d = self.partner_dist()
            if d is None or d < 80:
                break
        return True

    def planner_open(self):
        """the city planner is up: its done button (bottom right) is green"""
        from PIL import Image
        path = os.path.join(lane.out_dir('shots'), f'planner_{self.name}.png')
        self.cmd('shot ' + path)
        r, g, b = Image.open(path).convert('RGB').getpixel((self.W - 25, self.H - 22))
        return g > 60 and r < 50 and b < 70

    def visit_city(self):
        """an own city on this planet: fly over it, open the city planner (its rollover's planner
        button), look at it, close it with the done button"""
        me, _, objs = self.targets()
        towns = sorted((o for o in objs if o[0][0] == 'T'), key=lambda o: o[1])
        if not towns:
            return False
        k = towns[0][0]
        self.count('city_flights')
        for _ in range(12):
            me, _, objs = self.targets()
            t = [o for o in objs if o[0] == k]
            if not t or t[0][1] < 330 or not me:
                break
            self.steer(t[0][2], t[0][3], me, 1.0)
        me, _, objs = self.targets()
        t = [o for o in objs if o[0] == k]
        if not t:
            return True
        if self.palette not in (None, 'main'):
            self.open_palette('main')
        x, y = t[0][2], t[0][3]
        self.act(f'move {x} {y}')  # the rollover with the planner button comes up
        self.sleep(1.0)
        self.act(f'click {x - 23} {y - 22}')
        self.sleep(2.5)
        if self.planner_open():
            self.count('planners')
            self.sleep(self.rng.uniform(2, 5))
            self.act(f'wheel {self.W // 2} {self.H // 2} {self.rng.choice([-2, 2])}')
            self.sleep(1)
            self.act(f'click {self.W - 25} {self.H - 22}')  # done
            self.sleep(2)
            if self.planner_open():
                self.count('planner_stuck')
        return True

    def more_money(self):
        """the game's own cheat (Ctrl+Shift+C "moreMoney": +1 000 000): the shared money changes on
        one machine and has to reach the other"""
        self.act('cheat moreMoney')
        self.count('money_cheats')

    def cruise(self):
        keys = self.rng.choice([['0x57'], ['0x53'], ['0x41'], ['0x44'], ['0x57', '0x41'], ['0x57', '0x44']])
        self.key_hold(keys, self.rng.uniform(0.4, 2.0))
        self.count('cruise')

    def zoom(self, n):
        me, _, _ = self.targets()
        x, y = me if me and 0 <= me[0] < self.W and 0 <= me[1] < self.H else (self.W // 2, self.H // 2)
        self.act(f'wheel {x} {y} {n}')

    def land_retry(self, want=None, seconds=150):
        """land(), again while still in the system: on GOG 2024 the zoom does not land for up to
        ~80 s right after a world load (docs/known-issues.md)"""
        t0 = time.time()
        ok = self.land(want)
        while self.ctx() == 'system' and time.time() - t0 < seconds:
            ok = self.land(want)
        return ok

    def land(self, want=None):
        """system: click a planet (the ship flies there), then zoom in until on it"""
        for i in range(6):
            me, _, objs = self.targets()
            planets = [o for o in objs if o[0][0] in 'PC' and (want is None or o[0][1:] == want)
                       and (want or o[0] not in self.no_landing)]
            if planets or i == 5:
                break
            self.act(f'wheel {self.W // 2} {self.H // 2} -4')  # zoom out to see the system
            self.sleep(0.8)
        if not planets:
            return False
        k, d, x, y = self.rng.choice(planets)
        if self.palette not in (None, 'main'):
            self.open_palette('main')  # no tool: a click on a planet is a flight, not a shot
        others = [o for o in objs if o[0][0] in 'PC' and o[0] != k]
        for attempt in range(3):
            self.act(f'click {x} {y}')
            self.sleep(1.0)
            if self.active_planet() == k[1:]:
                break
            # a moon next to the planet took the click: click a bit away from it
            self.count('planet_misclicks')
            if others:
                n = min(others, key=lambda o: math.hypot(o[2] - x, o[3] - y))
                dx, dy = x - n[2], y - n[3]
                ln = math.hypot(dx, dy) or 1
                x, y = int(x + dx / ln * 6), int(y + dy / ln * 6)
        self.count('planet_flights')
        t0 = time.time()
        while time.time() - t0 < 25:
            self.sleep(1)
            _, _, objs = self.targets()
            o = [o for o in objs if o[0] == k]
            if o and o[0][1] < 40:
                break
        for _ in range(8):
            self.zoom(5)
            self.sleep(1.0)
            if self.ctx() == 'planet':
                self.count('landings')
                self.sleep(2)
                return True
        self.no_landing.add(k)  # a gas giant (no landing there) or out of reach
        self.count('landings_failed')
        return False

    def take_off(self):
        # captions that wait for OK take the wheel: landing on the first mission's planet (Vulpica,
        # P02460035 in the `space` fixture) shows ~5 tutorial captions ("Something is showing up on
        # the ship's radar..."); confirm them first, they do not count as zoom attempts
        t0 = time.time()
        zooms = 0
        while zooms < 10 and time.time() - t0 < 90:
            if self.captions():
                continue
            zooms += 1
            self.zoom(-5)
            self.sleep(1.0)
            if self.ctx() != 'planet':
                self.count('take_offs')
                return True
        return False

    def to_galaxy(self):
        for _ in range(16):
            self.zoom(-10)
            self.sleep(0.8)
            if self.ctx() == 'galaxy':
                self.count('galaxy_views')
                return True
        return False

    def visit_star(self):
        """galaxy view: click one of the nearest stars (the drive reaches a few of them: a click on
        one in range flies there at once), then zoom into its system"""
        start = self.last_place[1]
        for attempt in range(4):
            me, _, objs = self.targets()
            stars = sorted((o for o in objs if o[0][0] in 'SH' and o[1] > 0.3), key=lambda o: o[1])[:3]
            if not stars:
                self.act(f'wheel {self.W // 2} {self.H // 2} -3')  # zoom out to see more stars
                self.sleep(1)
                continue
            k, d, x, y = self.rng.choice(stars)
            self.act(f'click {x} {y}')
            self.count('star_clicks')
            t0 = time.time()
            while time.time() - t0 < 6:
                self.sleep(1)
                _, _, objs = self.targets()
                o = [o for o in objs if o[0] == k]
                if o and o[0][1] < 0.3:
                    self.count('star_flights')
                    break
            else:
                continue
            break
        for _ in range(12):
            self.zoom(10)
            self.sleep(1.0)
            p = self.place()
            if p[0] & 0x80000000:
                self.count('systems_entered')
                if p[1] != start:
                    self.count('new_systems')
                return True
        return False

    def jump_to_partner(self):
        """the co-op window: F9, "Jump to <partner>", F9"""
        self.act('key f9')
        self.sleep(1.0)
        b = self.ui_button('Jump to')
        if b:
            n0 = self.cmd('coop.status').count("can't jump")
            self.act(f'click {b[0]} {b[1]}')
            self.count('jumps_clicked')
            self.sleep(0.8)
            st = self.cmd('coop.status')
            if st.count("can't jump") > n0:  # the window says why (chat log)
                why = [l for l in st.splitlines() if "can't jump" in l][-1].split("can't jump: ")[-1]
                self.count('jump_refused:' + why.replace(' ', '_')[:30])
        self.sleep(0.8)
        if self.ui_button('CO-OP LOBBY'):
            self.act('key f9')
        return bool(b)

    def step(self):
        if self.captions():
            return
        if self.rescue():
            return
        st = self.hp()
        if st.get('state') == 2 or st.get('down'):
            self.sleep(1)
            return
        self.last_place = self.place()
        mine, partner = self.last_place, self.partner.last_place
        c = 'planet' if mine[0] & 0x40000000 else 'system' if mine[0] & 0x80000000 else 'galaxy'
        other_star = partner[1] and partner[1] != mine[1]
        if other_star or not partner[1]:  # in another system, or in the galaxy view on the way
            self.apart_since = self.apart_since or time.time()
        else:
            self.apart_since = None
        # the guest keeps up with the host (the host's NPC ships are what both share), now and then it
        # goes its own way for a while; the host explores and comes back to the guest less often
        follow = self.pid == 1 and self.rng.random() < 0.85
        if other_star and c != 'galaxy' and time.time() - self.apart_since > (25 if follow else 120):
            self.count('jump_tries')
            if self.jump_to_partner():  # the co-op window (F9): "Jump to <partner>"
                self.sleep(6)
                return
        if follow and not other_star and partner[0] & 0x40000000 and mine[0] != partner[0]:
            if c == 'planet':  # on another planet of the same system: take off first
                self.take_off()
            elif c == 'system':  # fly to the partner's planet and land
                self.land('%08X' % (partner[0] & 0x3FFFFFFF))
            return
        if c == 'planet' and self.fight(self.rng.uniform(4, 10)):
            return
        if c == 'planet' and mine[0] == partner[0] and (follow or self.rng.random() < 0.3) and self.regroup():
            return
        if self.pid == 0 and c == 'planet' and time.time() - self.last_fight > 40 \
                and time.time() - self.last_spawn > 40:
            # setup: no pirates came for a while (combat happens on planets)
            # raiders (type 2) attack only around colonies and leave elsewhere; ambush pirates (8)
            # attack the player anywhere (docs/re/space.md 9.4)
            for _ in range(self.rng.choice([1, 2, 3])):
                self.cmd(f'coop.fleet spawn {self.rng.choice([2, 8, 8])}')
            self.last_spawn = time.time()
            self.count('pirates_spawned')
            return
        if self.rng.random() < 0.012:
            self.more_money()
            return
        r = self.rng.random()
        if c == 'planet':
            if r < 0.1 and self.visit_city():
                return
            if r < 0.75:
                self.cruise()
            elif r < 0.85:
                self.zoom(self.rng.choice([-2, 2]))
            else:
                self.take_off()
        elif c == 'system':
            if r < 0.7:
                self.land()
            elif r < 0.8:
                self.zoom(self.rng.choice([-3, 3]))
            elif self.pid == 0 or not follow:
                self.to_galaxy()
            if self.pid == 0 and self.rng.random() < 0.15:
                self.to_galaxy()  # the host goes exploring other systems now and then
        else:
            self.visit_star()

    def run(self):
        self.last_place = self.place()
        try:
            while time.time() < self.end:
                self.step()
                self.sleep(self.rng.uniform(0.2, 0.8))
        except Stop:
            pass
        except Exception as e:  # noqa: BLE001 - reported by the main thread
            self.error = repr(e)
        for k in ('0x57', '0x41', '0x53', '0x44'):
            try:
                self.c.call(f'key {k} up')
            except OSError:
                pass


def money(con):
    m = re.search(r'shared money=([-\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def sync_sample(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8 and f[0].startswith('s'):
            d[f[0]] = tuple(map(float, f[1:4]))
    return d


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))] if v else float('nan')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=12)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play_space: {a.minutes} min, seed {seed}', flush=True)
    host, guest = start('space')
    ck = Checks()
    path = shot(host, 'play_space_start_host.png')
    host.call('shot ' + path)  # (shot() halves the stored picture: the real size from an unscaled one)
    W, H = png_size(path)
    print(f'  screen {W}x{H}', flush=True)
    # setup: the fixture's ship has no interstellar drive yet (a new player gets it in the first
    # missions); without it the galaxy view is locked
    for con in (host, guest):
        print('  drive:', con.call(f'coop.space tool add {DRIVE}')['out'])
    stats = {}
    end = time.time() + a.minutes * 60
    ph = Player('host', lane.HOST, 0, seed, end, stats)
    pg = Player('guest', lane.GUEST, 1, seed + 1, end, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.W, p.H = W, H
    ph.start()
    pg.start()

    # ---- measurements while they play ----
    mon_h, mon_g = Console(lane.HOST, timeout=30), Console(lane.GUEST, timeout=30)
    money_bad, money_n, last_diff = 0, 0, None
    errs, together_s, samples = [], 0, 0
    stuck, freed, life = {}, {}, {}
    t_last = time.time()
    while time.time() < end and not Monitor.events and ph.is_alive() and pg.is_alive():
        time.sleep(3)
        now = time.time()
        try:
            mh, mg = money(mon_h), money(mon_g)
            if mh is not None and mg is not None:
                money_n += 1
                diff = mh - mg
                if diff and last_diff:  # a difference that lasts (one in flight is fine)
                    money_bad += 1
                    print(f'  money differs: host {mh:.0f} guest {mg:.0f}', flush=True)
                last_diff = diff
            sh = mon_h.call('coop.space star')['out']
            sg = mon_g.call('coop.space star')['out']
            same = sh.split()[1:2] == sg.split()[1:2] and not sh.startswith('place 0')
            if same:
                together_s += now - t_last
                h1, g, h2 = sync_sample(mon_h), sync_sample(mon_g), sync_sample(mon_h)
                for k, gp in g.items():
                    if k in h1 and k in h2:
                        errs.append(min(math.dist(gp, h1[k]), math.dist(gp, h2[k])))
                        samples += 1
            for name, con, pl in (('host', mon_h, ph), ('guest', mon_g, pg)):
                m = re.search(r'freed_by_game=(\d+)', con.call('coop.fleet list')['out'])
                if m and int(m.group(1)) != freed.get(name, 0):
                    # the game freed a puppet / mirror (handled since 2026-10-23): what led to it
                    freed[name] = int(m.group(1))
                    print(f'  {name}: freed_by_game={m.group(1)} after: host ' + ' | '.join(ph.log[-3:])
                          + ' || guest ' + ' | '.join(pg.log[-3:]), flush=True)
                hp = con.call('coop.space hp')['out']
                # deaths and respawns, downs (disabled) and repairs, seen from outside the bots
                dead, down = 'state=2' in hp, 'down=1' in hp
                if dead != life.get(name + '.dead', False):
                    life[name + '.dead'] = dead
                    life[name + ('.deaths' if dead else '.respawns')] = life.get(name + ('.deaths' if dead else '.respawns'), 0) + 1
                    print(f'  {name}: {"destroyed" if dead else "respawned"} ({hp})', flush=True)
                if down != life.get(name + '.down', False):
                    life[name + '.down'] = down
                    if down:
                        life[name + '.downs'] = life.get(name + '.downs', 0) + 1
                    print(f'  {name}: {"disabled" if down else "no longer disabled"} ({hp})', flush=True)
                bad = 'state=2' in hp or 'movie=1' in hp
                stuck[name] = (stuck.get(name) or now) if bad else None
                if stuck[name] and now - stuck[name] > 90:
                    print(f'  {name} stuck (dead or in a cinematic) for 90 s: {hp}', flush=True)
                    Monitor.events.append(f'{name}: stuck dead/in a cinematic for 90 s')
        except (OSError, ValueError) as e:
            Monitor.events.append(f'measurement console error {e!r}')
        t_last = now
    for p in (ph, pg):
        p.end = 0
        p.join(30)

    # ---- results ----
    print('  actions:', ' '.join(f'{k}={v}' for k, v in sorted(stats.items()) if not isinstance(v, list)))
    rs = stats.get('respawn_s', [])
    if rs:
        print(f'  respawns after {", ".join(f"{x:.0f}" for x in rs)} s')
    for name, con in (('host', host), ('guest', guest)):
        try:
            con.sock and con.sock.settimeout(10)
            print(f'  {name}:', con.call('coop.space')['out'].strip())
            print(f'  {name}:', con.call('coop.fleet list')['out'].splitlines()[0])
            print(f'  {name}:', con.call('coop.res')['out'].strip())
        except (OSError, ValueError, IndexError) as e:
            print(f'  {name}: no answer ({e!r})')
    print(f'  together {together_s:.0f} s; ship sync samples {samples}: p50 {pct(errs, .5):.1f} m, '
          f'p90 {pct(errs, .9):.1f} m, max {max(errs) if errs else float("nan"):.1f} m', flush=True)
    for p in (ph, pg):
        ck(p.error is None, f'{p.name} bot ran without errors ({p.error})')
    ck(money_bad == 0, f'the shared money was the same on both ({money_n} checks, {money_bad} lasting differences)')
    ck(not errs or pct(errs, .9) <= 15, 'NPC ships on the guest follow the host (p90 within 15 m)')
    ck(stats.get('host.shots', 0) + stats.get('guest.shots', 0) > 0, 'the players fought')
    print('  lives:', ' '.join(f'{k}={v}' for k, v in sorted(life.items()) if not isinstance(v, bool)))
    for name in ('host', 'guest'):
        d, r = life.get(name + '.deaths', 0), life.get(name + '.respawns', 0)
        ck(r >= d - (1 if life.get(name + '.dead') else 0), f'every death of the {name} was followed by a respawn ({d} deaths)')
    if Monitor.events or ph.error or pg.error:
        for p in (ph, pg):
            print(f'  last actions of {p.name}:')
            for line in p.log[-20:]:
                print('   ', line)
        print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_space.py --minutes {a.minutes} --seed {seed}')
    try:
        shot(host, 'play_space_end_host.png')
        shot(guest, 'play_space_end_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after playing ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
