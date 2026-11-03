#!/usr/bin/env python3
"""Creature stage, played: the host and the guest play together for minutes through real input,
the way people play (docs/stability/creature.md): walk (clicks on the ground, WASD), look around
(right-drag), pick NPC creatures on the screen (coop.targets all), attack them with the combat
abilities (keys 1..4 in the combat stance), hunt the same animal together (the pack bonus),
socialize (social stance), eat the kill, follow each other, open the pause menu and the co-op
window (F9, chat), zoom; the guest once leaves to the main menu and comes back.
Console commands only set up and measure: NPC health and kills (coop.npc), the shared DNA
(coop.dna) on both machines, the pack mechanics (coop.team: joint hits, rescues, regeneration),
the pose of every shared object on both machines (coop.sync, like coop_sync.py), faults (exc)
and the twoinst monitors (freeze, crash).

  play_creature.py [--minutes 10] [--seed N] [--no-exit]

Each player runs in its own thread with its own seeded random choices; the measurements run in
the main thread. The printed replay line repeats the choices (the game itself is not
deterministic).
"""
import argparse
import math
import random
import re
import sys
import threading
import time

import lane
import uifind
from coop_sync import LIMITS, angle, pct, sample
from sporectl import Console
from twoinst import Checks, Monitor, shot, start

COMBAT_KEYS = ['1', '1', '1', '2', '3', '4']  # the first ability is the one every creature has
DIALOGS = [('Do you want to save your game?', 'Cancel'), ('Are you sure?', 'Cancel'),
           ('Resume Game', 'Resume Game'),  # what a player closes when it is in the way
           ('You were killed', 'key enter'),  # the death caption waits for OK (docs/re/creature.md 7)
           # BUG-0500: the brain upgrade (100 DNA and up) plays cinematic F6A4B1E1 with this caption and waits
           # for OK; unanswered, both bots stood in it for the rest of the run (no hunts, no joint hits,
           # every chat line typed under its input lock)
           ('Brain power!', 'key enter')]


def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def num(rx, text, default=None):
    m = re.search(rx, text)
    return float(m.group(1)) if m else default


class World:
    """what the host knows: NPC health, death, species (the host simulates every NPC)"""

    def __init__(self, host_inst):
        self.con = Console(host_inst)
        self.lock = threading.Lock()
        m = re.search(r'key=0x([0-9A-F]+)', self.con.call('coop.avatar')['out'])
        self.family = m.group(1).zfill(8) if m else None  # the host's species: its nest family

    def npcs(self):
        with self.lock:
            out = self.con.call('coop.npc list')['out']
        d = {}
        for line in out.splitlines():
            m = re.match(r'\s*#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+) dist=([\d.]+)( DEAD)?', line)
            if m:
                d['n' + m.group(1)] = dict(species=m.group(2), hp=float(m.group(3)), dead=bool(m.group(5)))
        return d


class Shared:
    """counters and the pack's current prey, shared by both player threads"""

    def __init__(self):
        self.lock = threading.Lock()
        self.c = {}
        self.target = None  # (key, hunter)
        self.killed = {}    # key -> set of players that hunted it when it died

    def add(self, key, n=1):
        with self.lock:
            self.c[key] = self.c.get(key, 0) + n

    def get(self, key):
        return self.c.get(key, 0)


class Player(threading.Thread):
    def __init__(self, name, inst, partner_id, world, sh, seed, deadline, exit_at=None):
        super().__init__(daemon=True)
        self.name, self.inst, self.partner, self.world, self.sh = name, inst, partner_id, world, sh
        self.rng = random.Random(seed)
        self.deadline, self.exit_at = deadline, exit_at
        self.con = Console(inst)
        self.W, self.H = screen(self.con)
        self.log, self.hunt_log, self.notes = [], [], []
        self.error = None

    # ---- input and looking
    def do(self, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {self.name}: {line}')
        del self.log[:-80]
        return self.con.call(line).get('out', '')

    def sleep(self, a, b=None):
        time.sleep(a if b is None else self.rng.uniform(a, b))

    def add(self, what, n=1):
        self.sh.add(f'{self.name}.{what}', n)

    def in_view(self, x, y):
        return 20 < x < self.W - 20 and 90 < y < self.H - 170

    def objs(self):
        """{key: (dist, x, y)} of the shared objects on our screen; the partner as 'p<id>'"""
        d = {}
        for line in self.do('coop.targets all').splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'obj':
                d[f[1]] = (float(f[2]), int(f[3]), int(f[4]))
            elif len(f) == 5 and f[0] == 'player':
                d['p' + f[1]] = (-1.0, int(f[3]), int(f[4]))
        return d

    def look_for(self, key, tries=4, loose=False):
        """turn the camera (right-drag, always the same way: a sweep) until the object is on the
        screen; loose: anywhere on the screen above the HUD (the partner far away near the horizon)"""
        sign = self.rng.choice([-1, 1])
        for i in range(tries):
            o = self.objs().get(key)
            if o and (self.in_view(o[1], o[2]) or (loose and 0 < o[1] < self.W and 0 < o[2] < self.H - 170)):
                return o
            dx = sign * self.rng.randint(300, 450)
            y = self.H // 2
            self.do(f'drag {self.W // 2} {y} {self.W // 2 + dx} {y} right')
            self.add('look_around')
            self.sleep(0.4)
        return None

    def partner_dist(self):
        s = sample(self.con)
        ps = [v[0] for k, v in s.items() if k[0] == 'p']
        return math.dist(ps[0], ps[1]) if len(ps) == 2 else None

    def stance(self, kind):
        """the green (social) / red (combat) button above the ability bar"""
        self.do(f'click {self.W // 2 + (43 if kind == "combat" else -30)} {self.H - 82}')
        self.sleep(0.3)

    def clear_dialogs(self):
        """a dialog or the pause menu in the way (a stray click): close it like a player would"""
        for caption, button in DIALOGS:
            if uifind.shown(self.con, caption):
                self.notes.append(f'{time.strftime("%H:%M:%S")} "{caption}" was up: {button}')
                if button.startswith('key '):
                    self.add('died' if caption == 'You were killed' else 'caption_ok')
                    self.sleep(1, 3)
                    self.do(button)
                    self.sleep(2)
                else:
                    self.add('dialog_in_the_way')
                    uifind.press(self.con, button)
                return True
        return False

    # ---- behaviours
    def prey(self, objs, npcs, max_d=60):
        c = [(d, k) for k, (d, x, y) in objs.items() if k[0] == 'n' and k in npcs and not npcs[k]['dead']
             and npcs[k]['species'] != self.world.family and d < max_d and self.in_view(x, y)]
        c.sort()
        return self.rng.choice(c[:3])[1] if c else None

    def hunt(self, key, why):
        t0 = time.time()
        d0 = self.objs().get(key, (-1,))[0]
        r, hits = self._hunt(key)
        self.add(f'hunt_{r}')
        self.hunt_log.append(f'{key} {why} {d0:.0f} m: {r} {time.time() - t0:.0f} s {hits} hits')
        return r

    def _hunt(self, key):
        """walk to the animal (a click on it) and use the combat abilities until it is dead"""
        self.stance('combat')
        t0, last_hp, hits = time.time(), None, 0
        while time.time() - t0 < 35 and time.time() < self.deadline:
            info = self.world.npcs().get(key)
            if info is None:
                return 'gone', hits
            if last_hp is not None and info['hp'] < last_hp - 1e-3:
                hits += 1
            last_hp = info['hp']
            if info['dead']:
                with self.sh.lock:
                    self.sh.killed.setdefault(key, set()).add(self.name)
                if self.rng.random() < 0.6:  # eat the kill: a click on the carcass
                    o = self.objs().get(key)
                    if o and self.in_view(o[1], o[2]):
                        self.do(f'click {o[1]} {o[2]}')
                        self.add('eat')
                        self.sleep(2, 4)
                return 'killed', hits
            o = self.objs().get(key) or self.look_for(key, 2)
            if not o:
                return 'lost', hits
            if self.in_view(o[1], o[2]):
                self.do(f'click {o[1]} {o[2]}')
            self.sleep(0.2, 0.5)
            self.do('key ' + self.rng.choice(COMBAT_KEYS))
            self.add('attacks')
            self.sleep(0.6, 1.3)
        return 'timeout', hits

    def socialize(self, key):
        self.stance('social')
        self.add('social')
        for _ in range(self.rng.randint(2, 5)):
            o = self.objs().get(key)
            if not o or not self.in_view(o[1], o[2]):
                break
            self.do(f'click {o[1]} {o[2]}')
            self.sleep(0.3, 0.8)
            k = '0x3' + self.rng.choice('1234')  # a player's key press lasts a few frames (the mod polls)
            self.do(f'key {k} down')
            self.sleep(0.12, 0.2)
            self.do(f'key {k} up')
            self.sleep(1.0, 2.5)

    def wander(self):
        if self.rng.random() < 0.5:
            x, y = self.rng.randint(150, self.W - 150), self.rng.randint(self.H // 2, self.H - 220)
            self.do(f'click {x} {y}')
            self.add('walk_click')
            self.sleep(1.5, 4)
        else:  # WASD: hold a direction for a moment
            vk = f'0x{ord(self.rng.choice("WWWADS")):02X}'
            self.do(f'key {vk} down')
            self.sleep(0.8, 2.5)
            self.do(f'key {vk} up')
            self.add('wasd')

    def follow(self):
        o = self.look_for(f'p{self.partner}', 7, loose=True)
        if not o:
            self.add('follow_lost')
            return False
        # on the ground next to the partner (not the sky above a far one)
        self.do(f'click {max(60, min(self.W - 60, o[1] + self.rng.randint(-80, 80)))} '
                f'{max(int(self.H * 0.45), min(self.H - 180, o[2] + 40))}')
        self.add('follow')
        self.sleep(2, 5)
        return True

    def menus(self):
        r = self.rng.random()
        if r < 0.35:  # the pause menu: open, a look, Resume Game
            for _ in range(2):
                self.do('key esc')
                self.sleep(1.2)
                if uifind.shown(self.con, 'Resume Game'):
                    break
            if uifind.shown(self.con, 'Resume Game'):
                self.add('pause_menu')
                self.sleep(0.5, 2)
                uifind.press(self.con, 'Resume Game')
            else:
                self.add('pause_menu_not_seen')
        elif r < 0.7:  # the co-op window (F9), sometimes a chat line
            self.f9()
            st = self.do('coop.uistate')
            if 'open=1' in st:
                self.add('coop_window')
                if self.rng.random() < 0.5:
                    m = re.search(r'chatedit (\d+) (\d+) (\d+) (\d+) 1', st)
                    m2 = re.search(r'send (\d+) (\d+) (\d+) (\d+) 1', st)
                    if m and m2:
                        msg = f'{self.name} hunting {self.rng.randint(100, 999)}'
                        for attempt in range(2):  # a player sees no caret and clicks the line again
                            self.do(f'click {(int(m.group(1)) + int(m.group(3))) // 2} '
                                    f'{(int(m.group(2)) + int(m.group(4))) // 2}')
                            self.sleep(0.6 + attempt)  # the edit takes the focus a frame or two later
                            self.do(f'text "{msg}"')
                            self.sleep(0.3)
                            typed = re.search(r"chatedit .* '(.*)'", self.do('coop.uistate'))
                            if typed and msg in typed.group(1):
                                break
                            self.add('chat_retyped')
                        if not (typed and msg in typed.group(1)):
                            # not typed: is the creature dying (the death cinematic takes no UI input)?
                            av = re.search(r'hp=([\d.]+)/', self.do('coop.avatar'))
                            dying = (av and float(av.group(1)) <= 0) or uifind.shown(self.con, 'You were killed')
                            self.add('chat_not_typed_dying' if dying else 'chat_not_typed')
                            self.notes.append(f'{time.strftime("%H:%M:%S")} chat line not typed '
                                              f'(hp {av.group(1) if av else "?"}, dying {bool(dying)})')
                            self.sleep(0.5)
                            self.f9()
                            return
                        self.do(f'click {(int(m2.group(1)) + int(m2.group(3))) // 2} {(int(m2.group(2)) + int(m2.group(4))) // 2}')
                        self.add('chat_sent')
                        self.sleep(1.5)
                        other = Console(lane.GUEST if self.inst == lane.HOST else lane.HOST)
                        mine, theirs = self.do('coop.status'), other.call('coop.status')['out']
                        ok = msg in mine and msg in theirs
                        self.add('chat_seen' if ok else 'chat_missing')
                        if not ok:
                            last = lambda t: [x[6:] for x in t.splitlines() if x.startswith('chat: ')][-2:]  # noqa: E731
                            self.notes.append(f'chat "{msg}" missing: typed {typed.group(1) if typed else "?"!r}, '
                                              f'mine {last(mine)}, theirs {last(theirs)}')
                self.sleep(0.5, 2)
                self.f9()
                if 'open=1' in self.do('coop.uistate'):
                    self.add('coop_window_stuck_open')
                    self.f9()
            else:
                self.add('coop_window_not_open')
        else:  # camera zoom
            self.do(f'wheel {self.W // 2} {self.H // 2} {self.rng.choice([-3, -2, 2, 3])}')
            self.add('zoom')
            self.sleep(0.5)

    def f9(self):
        self.do('key 0x78 down')
        self.sleep(0.25)
        self.do('key 0x78 up')
        self.sleep(0.8)

    def exit_and_return(self):
        """pause menu -> Exit to Main Menu -> Don't Save -> Don't Save; the host's world comes back"""
        self.exit_tries = getattr(self, 'exit_tries', 0) + 1
        self.exit_at = None
        if 'open=1' in self.do('coop.uistate'):  # the co-op window would take the Esc
            self.f9()
        for _ in range(3):
            self.do('key esc')
            self.sleep(1.2)
            if uifind.shown(self.con, 'Resume Game'):
                break
        t0 = time.time()
        ok = uifind.press(self.con, 'Exit to Main Menu', 'Do you want to save your game?') \
            and uifind.press(self.con, "Don't Save", 'Are you sure?') and uifind.press(self.con, "Don't Save")
        if not ok:  # a player tries again a bit later (dying, a caption in the way)
            self.notes.append(f'{time.strftime("%H:%M:%S")} exit to the main menu: the menu path failed '
                              f'(try {self.exit_tries})')
            self.sh.exit_result = 'the menu path failed'
            self.clear_dialogs()
            if self.exit_tries < 3:
                self.exit_at = time.time() + 20
            return
        self.add('exit_to_menu')
        seen_menu = False
        while time.time() - t0 < 120:
            mode = self.do('game.mode')
            seen_menu |= 'GalaxyGameEntry' in mode
            if seen_menu and 'Creature' in mode and self.con.call('coop.avatar')['ok']:
                self.sh.exit_result = f'back in the world after {time.time() - t0:.0f} s'
                self.sleep(4)
                return
            time.sleep(1)
        self.sh.exit_result = f'not back after 120 s (menu seen: {seen_menu})'

    def step(self):
        rng = self.rng
        if self.clear_dialogs():
            return
        if not self.con.call('coop.avatar')['ok']:  # dead or loading: wait for the respawn
            self.add('no_avatar_wait')
            self.sleep(2)
            return
        if self.exit_at and time.time() > self.exit_at:
            self.exit_and_return()
            return
        objs, npcs = self.objs(), self.world.npcs()
        tgt = self.sh.target
        # the pack: join the partner's hunt
        if tgt and tgt[1] != self.name and tgt[0] in npcs and not npcs[tgt[0]]['dead'] and rng.random() < 0.8:
            if tgt[0] in objs or self.look_for(tgt[0], 3):
                self.add('joined_hunt')
                self.hunt(tgt[0], 'pack')
                return
        d = self.partner_dist()
        if d is not None and d > 25 and rng.random() < 0.7:
            self.follow()
            return
        kind = rng.choices(['hunt', 'social', 'wander', 'follow', 'menu', 'look'],
                           weights=[45, 10, 18, 10, 9, 8])[0]
        if kind in ('hunt', 'social'):
            key = self.prey(objs, npcs)
            if not key:
                kind = 'look'
            elif kind == 'hunt':
                self.sh.target = (key, self.name)
                self.hunt(key, 'lead')
                if self.sh.target and self.sh.target[0] == key:
                    self.sh.target = None
                return
            else:
                self.socialize(key)
                return
        if kind == 'look':
            dx = rng.choice([-1, 1]) * rng.randint(200, 500)
            self.do(f'drag {self.W // 2} {self.H // 2} {self.W // 2 + dx} {self.H // 2} right')
            self.add('look_around')
            self.sleep(0.5)
        elif kind == 'follow':
            self.follow()
        elif kind == 'wander':
            self.wander()
        elif kind == 'menu':
            self.menus()

    def run(self):
        try:
            while time.time() < self.deadline and not Monitor.events:
                self.step()
        except (OSError, ValueError) as e:
            self.error = repr(e)


def measure(host, guest, acc):
    """one round: pose sync (n*, p*) host/guest/host, DNA on both, health and pack state"""
    t1 = time.time()
    h1, g, h2 = sample(host), sample(guest), sample(host)
    dt = max(0.02, time.time() - t1)
    for k, (gp, gq) in g.items():
        if k not in h1 or k not in h2 or k[0] not in 'np':
            continue
        e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
        hq = h1[k][1] if e1 <= e2 else h2[k][1]
        e = min(e1, e2)
        acc.setdefault('pos_' + k[0], []).append(e)
        acc.setdefault('rot_' + k[0], []).append(angle(gq, hq))
        if k[0] == 'p':
            acc.setdefault('pos_' + k, []).append(e)
            # a moving player trails by the smoothing delay: judge the delay, not the metres
            v = math.dist(h1[k][0], h2[k][0]) / dt
            if v < 0.5:
                acc.setdefault('p_still', []).append(e)
            else:
                acc.setdefault('p_lag', []).append(e / v)
        if e > 5:
            # moving? (the two host samples are ~0.1 s apart)
            v = math.dist(h1[k][0], h2[k][0])
            extra = ''
            if k[0] == 'p':  # the viewer's puppet: behind its target (local) or the target behind (network)?
                viewer = guest if k == 'p0' else host
                dl = re.search(r'drive err=([\d.]+) speed=([\d.]+)', viewer.call('coop.status')['out'])
                extra = f' | puppet to its target {dl.group(1)} m, puppet speed {dl.group(2)}' if dl else ''
            if k[0] == 'n':
                line = [x for x in guest.call('coop.npc list')['out'].splitlines() if f'#{k[1:]} ' in x]
                hl = [x for x in host.call('coop.npc list')['out'].splitlines() if f'#{k[1:]} ' in x]
                extra = f' | guest: {line[0].strip() if line else "-"} | host: {hl[0].strip() if hl else "-"}'
            acc.setdefault('far', []).append(f'{time.strftime("%H:%M:%S")} {k} {e:.1f} m (host moved {v:.2f} m '
                                             f'between samples){extra}')
    d1 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    dg = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
    d2 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    acc.setdefault('dna', []).append((d1, dg, d2))
    for con, who in ((host, 'host'), (guest, 'guest')):
        a = con.call('coop.avatar')['out']
        hp, mx = num(r'hp=([\d.]+)/', a), num(r'hp=[\d.]+/([\d.]+)', a)
        acc.setdefault('hp_' + who, []).append((time.time(), hp, mx, 'regen' in con.call('coop.team')['out']))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--no-exit', action='store_true', help='the guest does not leave to the main menu')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  play creature: {a.minutes} min, seed {seed}', flush=True)
    host, guest = start('creature')
    ck = Checks()
    world = World(lane.HOST)
    sh = Shared()
    sh.exit_result = None
    team0 = host.call('coop.team')['out']
    dna0 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    hits0 = num(r'hits_applied=(\d+)', host.call('coop.npc')['out'], 0)
    t0 = time.time()
    deadline = t0 + a.minutes * 60
    rng = random.Random(seed)
    exit_at = None if a.no_exit or a.minutes < 3 else t0 + a.minutes * 60 * rng.uniform(0.3, 0.6)
    players = [Player('host', lane.HOST, 1, world, sh, seed, deadline),
               Player('guest', lane.GUEST, 0, world, sh, seed + 1, deadline, exit_at)]
    print(f'  screen {players[0].W}x{players[0].H}, family species {world.family}', flush=True)
    for p in players:
        p.start()
    acc = {}
    t_print = time.time()
    try:
        while any(p.is_alive() for p in players):
            measure(host, guest, acc)
            if time.time() - t_print > 60:
                t_print = time.time()
                dl = acc['dna'][-1]
                print(f'  {time.time() - t0:4.0f} s: dna host {dl[0]} guest {dl[1]} | '
                      + host.call('coop.team')['out'].splitlines()[-1] + f' | kills {len(sh.killed)} | '
                      + ' '.join(f'{k}={v}' for k, v in sorted(sh.c.items()) if 'attacks' in k or 'hunt_' in k),
                      flush=True)
            time.sleep(4)
    except (OSError, ValueError) as e:
        ck(False, f'the consoles answer during play ({e!r})')
    for p in players:
        p.join(10)
        if p.error:
            ck(False, f'{p.name} bot: {p.error}')
    # ---- results
    print('  actions:', ' '.join(f'{k}={v}' for k, v in sorted(sh.c.items())), flush=True)
    team1, gteam = host.call('coop.team')['out'], guest.call('coop.team')['out']
    npc1 = host.call('coop.npc')['out']
    dna1 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    print('  host :', team1.splitlines()[-1], '|', npc1.strip().splitlines()[-1])
    print('  guest:', gteam.splitlines()[-1], '|', guest.call('coop.npc')['out'].strip().splitlines()[-1])
    joint = num(r'joint hits (\d+)', team1, 0) - num(r'joint hits (\d+)', team0, 0)
    rescues = num(r'rescues (\d+)', team1, 0) + num(r'rescues (\d+)', gteam, 0) - num(r'rescues (\d+)', team0, 0)
    ghits = num(r'hits_applied=(\d+)', npc1, 0) - hits0
    gkills = num(r'guest_kills=(\d+)', npc1, 0)
    for kind in 'np':
        p, r = acc.get('pos_' + kind, []), acc.get('rot_' + kind, [])
        if not p:
            continue
        lp, lr = LIMITS[kind]
        print(f'  sync {kind}: samples {len(p)} pos med {pct(p, .5):.2f} p90 {pct(p, .9):.2f} max {max(p):.2f} m | '
              f'facing med {pct(r, .5):.1f} p90 {pct(r, .9):.1f} deg', flush=True)
        ck(pct(r, .9) <= lr, f'sync {kind}*: 90% facing within {lr} deg ({pct(r, .9):.1f})')
        if kind == 'n':
            ck(pct(p, .9) <= lp, f'sync n*: 90% within {lp} m ({pct(p, .9):.2f})')
    # players run and charge (5-10 m/s): standing players must match, moving ones trail by the delay
    st, lag = acc.get('p_still', []), acc.get('p_lag', [])
    print(f'  players standing: {len(st)} samples p90 {pct(st, .9):.2f} m; moving: {len(lag)} samples '
          f'delay med {pct(lag, .5):.2f} p90 {pct(lag, .9):.2f} s', flush=True)
    # limits for real play: coop_sync.py's 1.5 m holds for standing players; running, lunging and
    # charging players trail by the interpolation + walking delay (~0.4-0.5 s, plain walking 1 m at
    # 2.7 m/s), and a partner's puppet snaps at 4 m
    pp = acc.get('pos_p', [])
    ck(not pp or pct(pp, .9) <= 2.5, f'sync p*: 90% within 2.5 m in play ({pct(pp, .9):.2f})')
    ck(not pp or max(pp) <= 6.0, f'sync p*: never more than 6 m apart ({max(pp) if pp else 0:.2f})')
    ck(not st or pct(st, .9) <= LIMITS['p'][0], f'sync p*, standing: 90% within {LIMITS["p"][0]} m')
    ck(not lag or pct(lag, .5) <= 0.75, f'sync p*, moving: trail by 0.75 s or less (median {pct(lag, .5):.2f})')
    for k in ('p0', 'p1'):
        p = acc.get('pos_' + k, [])
        if p:
            print(f'    {k}: samples {len(p)} med {pct(p, .5):.2f} p90 {pct(p, .9):.2f} max {max(p):.2f} m')
    seen = {}
    for line in acc.get('far', []):
        key = line.split()[1]
        seen[key] = seen.get(key, 0) + 1
        if seen[key] <= 2:
            print('    far:', line)
    if seen:
        print('    far samples per key:', seen)
    same = [1 for h1, g, h2 in acc.get('dna', []) if g is not None and h1 is not None
            and (abs(g - h1) < 0.5 or abs(g - h2) < 0.5)]
    n_dna = len(acc.get('dna', []))
    print(f'  DNA {dna0} -> {dna1}; the same on both in {len(same)}/{n_dna} samples', flush=True)
    ck(n_dna and len(same) >= 0.9 * n_dna, 'the shared DNA is the same on both machines')
    ck(dna1 is not None and dna0 is not None and dna1 > dna0, 'the pack earned DNA')
    host_hits = sum(int(h.split()[-2]) for h in players[0].hunt_log)
    ck(sh.get('host.attacks') > 0 and host_hits > 0, f'the host hit NPC creatures ({host_hits} hp drops seen)')
    ck(ghits > 0, f'the guest hit NPC creatures ({ghits:.0f} hits applied by the host)')
    together = sum(1 for v in sh.killed.values() if len(v) > 1)
    print(f'  kills: {len(sh.killed)} ({together} by both), guest kills credited on the host {gkills:.0f}')
    ck(len(sh.killed) > 0, 'NPC creatures were killed')
    ck(joint > 0, f'joint hunts happened (+{joint:.0f} joint hits)')
    regen = sum(1 for who in ('host', 'guest') for (t1, a1, m1, r1), (t2, a2, m2, r2) in
                zip(acc.get('hp_' + who, []), acc.get('hp_' + who, [])[1:]) if r1 and a1 and a2 and a2 > a1 + 1e-3)
    print(f'  pack: rescues {rescues:.0f}, regeneration rises {regen}', flush=True)
    if sh.get('host.chat_sent') + sh.get('guest.chat_sent'):
        seen = sh.get('host.chat_seen') + sh.get('guest.chat_seen')
        sent = sh.get('host.chat_sent') + sh.get('guest.chat_sent')
        ck(seen == sent, f'chat lines typed in the co-op window reach both players ({seen}/{sent})')
    not_typed = sh.get('host.chat_not_typed') + sh.get('guest.chat_not_typed')
    ck(not_typed == 0, f'the chat line takes the typing while the creature lives ({not_typed} lines lost; '
                       f'while dying: {sh.get("host.chat_not_typed_dying") + sh.get("guest.chat_not_typed_dying")})')
    if exit_at is not None or sh.exit_result:
        print('  guest left to the main menu:', sh.exit_result)
        ck(sh.exit_result is not None and sh.exit_result.startswith('back'),
           f'the guest left to the main menu and came back ({sh.exit_result})')
    for p in players:
        print(f'  {p.name} hunts:', '; '.join(p.hunt_log[-10:]))
        for n in p.notes[-5:]:
            print(f'    {p.name}:', n)
        if Monitor.events:
            print(f'  last actions of {p.name}:')
            for line in p.log[-15:]:
                print('   ', line)
    hs, gs = host.call('coop.social')['out'].strip(), guest.call('coop.social')['out'].strip()
    print('  social host :', hs)
    print('  social guest:', gs)
    table = lambda c: sorted(x for x in re.findall(r'species \S+ relation=-?\d+ x=\d+ impressed=\d+ killed=\d+',  # noqa: E731
                                                  c.call('coop.social species')['out'])
                             if not x.endswith('relation=0 x=0 impressed=0 killed=0'))  # an empty record = none
    time.sleep(1.5)
    ck(table(host) == table(guest), 'the species table (impressed / killed / ally) is the same on both')
    print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/play_creature.py --minutes {a.minutes} --seed {seed}')
    shot(host, 'play_creature_host.png')
    shot(guest, 'play_creature_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
