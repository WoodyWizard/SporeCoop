#!/usr/bin/env python3
"""A whole co-op game of four bot players, Cell to the galactic core, played like ordinary players
play it, to find bugs, desyncs, crashes and freezes (BUG-0152; docs/06-testing.md#testing-campaign).

Players: Alice (the host) and Bob from the lane (SPORE_LANE), Carol and Dave as instances 20+N and
30+N in the same lane's bottle (coop_multi.start_extra). The run starts from the `cell` fixture (a new
game made by the stage wizard) or, resumed, from the fixture of any later stage (--from).

Phases (campaign_lib.plan): play Cell, advance, play Creature, advance, play Tribe, advance, play
Civ, advance, play Space, the finale (the galactic core). In every play phase each player runs the
goal-directed bot of the stage (play_cell / play_creature / play_tribe / play_civ / play_space: eat,
hunt, socialize, gather, raid, buy, capture, fly, fight) with ordinary-player noise between its
actions: idling, the pause menu, the co-op window (F9), the host saving, now and then a guest who
leaves and joins again. Votes are answered by the guests' real "I'm ready" clicks, editors are left
with "done", captions are confirmed with their green check, as players do.

Checked all the time (every CHECK_EVERY s, on every machine, the game's own state where there is one):
  dead      a game's console stops answering
  crash     a new crash / hang report file of one of the four instances
  freeze    no frames for 10 s outside a world load (twoinst.Monitor)
  fault     a caught fault (`exc` count went up)
  stage     a guest in another stage than the host (two checks in a row)
  apart     the host does not see a guest in its world (two checks in a row)
  desync    per object kind, 90th percentile of the position error vs the host above twice the
            coop_sync limit, or less than 85% of the host's nearby objects (two checks in a row)
  census    the game's objects (coop.census, Creature and Civ): alive/dead, owner, missing, far apart
  shared    shared food / money differ between the host and a guest (two checks in a row)
  vote      a vote ended by its timer although every guest pressed "I'm ready"
  stuck     a stage change that did not finish in its time (per phase timeouts)
Every finding goes to <run>/findings.md and findings.jsonl (time, stage, players, evidence: log tails
of the games, screenshots of every machine). A finding of any kind but `info` fails the run.

Shortcuts (real play to the next stage takes hours; every stage change itself and every co-op
mechanic runs for real):
  * Cell end: `coop.pool food 1000` on the host (the food bar full), then the game's own cinematic,
    caption, History and its check, as in coop_cell_leave.py.
  * Creature end: `AddEvolutionPoints 3000` on the host (brain upgrade 4, the bar full), then the
    History button and its check, the evolution vote, the creature editor, the fire cinematic, its caption.
  * Tribe: `coop.tribe spawn` gives each player's share of the tribe more members (test setup), the
    raids are real orders (play_tribe); when the stage does not end within --advance-minutes the host
    loads the `tribe`-> next fixture (a jump, see below).
  * Civ: the cities still to come appear at once (cCivExpansion::SpawnNextCity, as new_stage_game.py
    --npc-cities), the other nations' cities are taken with the game's cCity::Capture for the
    player's political id (the capture cinematics play), then History and its check, the vote, the
    editors; the same jump fallback.
  * A jump: when a stage change does not finish (no automated end yet, or stuck), the host loads the
    next stage's fixture with `coop.load` in game, the guests follow through the world share (the
    co-op stage switch of coop_stage_switch.py). A stuck change is a finding; a jump is listed in the
    report.
  * Space: the guests get the interstellar drive (`coop.space tool add`, as play_space), the host
    goes to the core with `coop.space finale goto near|core` (crossing the galaxy takes hours); the
    guests fly there with the real "Fly to Alice" button.

  SPORE_LANE=7 python3 tools/tests/coop_campaign.py [--from cell] [--to space] [--minutes 8]
        [--advance-minutes 20] [--no-finale] [--no-jump] [--seed N] [--players 4]
Resume a late failure from its stage: --from tribe (the fixture of that stage, campaign_lib.FIXTURE).
"""
import argparse
import os
import random
import re
import shutil
import sys
import threading
import time

import campaign_lib as cl
import census
import fixtures
import lane
from coop_multi import join_cmd, limit, measure, sample, start_extra, wait_all
from coop_ui import Ui
from sporectl import Console
from twoinst import Monitor, shot, start

CHECK_EVERY = 30
CAPTION_OK = (1410, 1048)    # the green check of a caption that waits for the player
HISTORY_BTN = (1400, 1062)   # the HUD button under the portrait: History (the next stage when it is done)
HISTORY_OK = (1415, 1055)    # its check: closes History, or starts the next stage
EDITOR_DONE = (1416, 1062)   # the green "done" of the editors (1440x1080)
VK_F9 = '0x78'


def now():
    return time.strftime('%H:%M:%S')


class Player:
    """one machine: the main thread's console (checks), its instance, its join command"""

    def __init__(self, name, inst, join=None):
        self.name, self.inst, self.join = name, inst, join
        self.con = Console(inst, timeout=30)
        self.pid = 0 if join is None else None
        self.faults = None

    def call(self, line):
        return self.con.call(line).get('out', '')


class Stop(Exception):
    pass


class Campaign:
    def __init__(self, a, players, run_dir, seed):
        self.a, self.P, self.run_dir = a, players, run_dir
        self.host = players[0]
        self.guests = players[1:]
        self.rng = random.Random(seed)
        self.seed = seed
        self.F = cl.Findings(run_dir)
        self.stage = a.start
        self.phase = 'start'
        self.events_seen = 0
        self.since = time.time()
        self.persist = {k: cl.Persist(2) for k in ('stage', 'apart', 'desync', 'shared', 'census')}
        self.quiet_until = 0.0     # a rejoin or a stage change is going on: no together / sync checks
        self.lock = threading.Lock()
        self.stop = False
        self.dead = False
        self.jumps = []
        self.timeline = []
        self.rejoins = 0
        self.votes = []
        self.log_dir = os.path.dirname(lane.game_log(self.host.inst))
        self.shots = 0

    # ---------------- findings and evidence ----------------
    def finding(self, kind, key, text, players=(), evidence=()):
        ev = list(evidence)
        with self.lock:
            new = self.F.add(kind, key, text, f'{self.stage}/{self.phase}', [p.name for p in players], ev)
        if not new:
            return False
        print(f'  FINDING [{kind}] {text}', flush=True)
        if kind == 'info':
            return True
        more = []
        for p in (players or self.P):
            more += self.evidence(p, f'{len(self.F.items):02d}_{kind}')
        self.F.attach(kind, key, more)
        return True

    def evidence(self, p, tag):
        out = []
        tail = self.log_tail(p.inst, 30)
        path = os.path.join(self.run_dir, f'{tag}_{p.name}.log.txt')
        with open(path, 'w') as f:
            f.write('\n'.join(tail) + '\n')
        out.append(f'{p.name} log tail: {os.path.relpath(path, lane.ROOT)}')
        try:
            src = shot(Console(p.inst, timeout=15), f'campaign_{tag}_{p.name}.png')
            if os.path.exists(src):
                dst = os.path.join(self.run_dir, os.path.basename(src))
                shutil.move(src, dst)
                out.append(f'{p.name} screenshot: {os.path.relpath(dst, lane.ROOT)}')
        except (OSError, ValueError) as e:
            out.append(f'{p.name} screenshot failed: {e!r}')
        return out

    @staticmethod
    def log_tail(inst, n):
        try:
            with open(lane.game_log(inst), 'rb') as f:
                f.seek(0, 2)
                f.seek(max(0, f.tell() - 20000))
                lines = [l for l in f.read().decode('utf-8', 'replace').splitlines() if '[DBG]' not in l]
            return lines[-n:]
        except OSError as e:
            return [f'(no log: {e!r})']

    def note(self, text):
        self.timeline.append(f'{now()} [{self.stage}/{self.phase}] {text}')
        print(f'  {now()} [{self.stage}/{self.phase}] {text}', flush=True)

    # ---------------- the checks ----------------
    def ids(self):
        """player ids from the host's session list ([1] Bob ...)"""
        out = self.host.call('coop.status')
        got = {m.group(2): int(m.group(1)) for m in re.finditer(r'^\s*\[(\d+)\] (\S+)', out, re.M)}
        for p in self.guests:
            p.pid = got.get(p.name, p.pid)
        return got

    def together(self):
        return {int(m.group(1)) for m in re.finditer(r'^together (\d+) 1$', self.host.call('coop.status'), re.M)}

    def health(self):
        """dead games, crash reports, freezes, faults: always, also during stage changes"""
        alive = []
        for p in self.P:
            try:
                ok = p.con.call('ping').get('ok')
            except (OSError, ValueError):
                ok = False
                p.con = Console(p.inst, timeout=30)  # a new connection next time
            if not ok:
                self.finding('dead', p.name, f'{p.name}\'s game does not answer its console', [p])
                self.dead = True
            else:
                alive.append(p)
        ev = Monitor.events[self.events_seen:]
        self.events_seen = len(Monitor.events)
        for e in ev:
            src = {'host': 'Alice', 'guest': 'Bob'}.get(e.split(':')[0], e.split(':')[0])
            who = [p for p in self.P if p.name == src]
            kind = 'freeze' if 'FROZE' in e else 'info' if 'resumed' in e else 'dead'
            self.finding(kind, e.split(':')[0] + ':' + kind, e, who)
        files = []
        for n in (os.listdir(self.log_dir) if os.path.isdir(self.log_dir) else []):
            try:
                files.append((n, os.path.getmtime(os.path.join(self.log_dir, n))))
            except OSError:
                pass
        for n in cl.new_reports(files, self.since, {p.inst for p in self.P}):
            path = os.path.join(self.log_dir, n)
            try:
                head = open(path, errors='replace').read(1500).splitlines()[:12]
            except OSError:
                head = []
            inst = int(re.search(r'-i(\d+)\.txt$', n).group(1))
            who = [p for p in self.P if p.inst == inst]
            self.finding('crash', n, f'{who[0].name if who else inst}: {n.split("-")[0]} report {n}', who,
                         [f'report: {path}'] + [f'`{l.strip()}`' for l in head if l.strip()][:8])
        for p in alive:
            try:
                exc = p.call('exc')
            except (OSError, ValueError):
                continue
            n = cl.faults_of(exc)
            if n is None:
                continue
            if p.faults is not None and n > p.faults:
                lines = [l.strip() for l in exc.splitlines()[1:6] if l.strip()]
                self.finding('fault', f'{p.name}:{lines[0][:60] if lines else n}',
                             f'{p.name}: {n - p.faults} new caught fault(s) (total {n})', [p],
                             [f'`{l}`' for l in lines])
            p.faults = n
        return alive

    def check(self, label=''):
        alive = self.health()
        if self.dead or time.time() < self.quiet_until:
            return
        host = self.host
        try:
            hmode = host.call('game.mode')
            hst = cl.stage_of(hmode)
            if hst is None:  # the host is in an editor or loading: nothing to compare now
                return
            tog = self.together()
            self.ids()
            bad_stage, apart = [], []
            for g in self.guests:
                if g not in alive:
                    continue
                gst = cl.stage_of(g.call('game.mode'))
                if gst != hst:
                    bad_stage.append(f'{g.name}:{hst}:{gst}')
                if g.pid not in tog:
                    apart.append(g.name)
            for k in self.persist['stage'].update(bad_stage):
                name = k.split(':')[0]
                self.finding('stage', k, f'{name} plays {k.split(":")[2]} while the host plays {hst}',
                             [host, self.byname(name)])
            for k in self.persist['apart'].update(apart):
                self.finding('apart', f'{hst}:{k}', f'the host does not see {k} in its world ({hst})',
                             [host, self.byname(k)])
            # sync of every synchronized object kind, guests that are in the host's world only
            h1 = sample(host.con)
            over, short = [], []
            for g in self.guests:
                if g not in alive or g.pid not in tog:
                    continue
                errs, cov, total = measure(host.con, g.con, h1)
                if total >= 5 and cov < 0.85:
                    short.append(f'{g.name}:coverage')
                for kind, v in errs.items():
                    v = sorted(v)
                    p90 = v[min(len(v) - 1, int(0.9 * len(v)))]
                    if p90 > limit(kind):
                        over.append(f'{g.name}:{kind}:{p90:.1f}')
            keys = [k.rsplit(':', 1)[0] if k.count(':') == 2 else k for k in over] + short
            for k in self.persist['desync'].update(keys):
                name = k.split(':')[0]
                detail = [o for o in over if o.startswith(k)] or [k]
                self.finding('desync', f'{hst}:{k}', f'{name}: {"too few of the host objects" if k.endswith("coverage") else "objects of kind " + k.split(":")[1] + " out of sync"} '
                             f'in {hst} ({", ".join(detail)})', [host, self.byname(name)])
            # shared food / money
            hr = cl.res_of(host.call('coop.res'))
            bad = []
            for g in self.guests:
                if g in alive and g.pid in tog:
                    for k, hv, gv in cl.res_mismatch(hr, cl.res_of(g.call('coop.res'))):
                        bad.append(f'{g.name}:{k}')
            for k in self.persist['shared'].update(bad):
                name, what = k.split(':')
                self.finding('shared', f'{hst}:{k}', f'{name}: shared {what} differs from the host\'s in {hst}',
                             [host, self.byname(name)])
            # the game's objects (Creature and Civ)
            if hst in ('creature', 'civ'):
                hc = census.dump(host.con)
                probs = []
                for g in self.guests:
                    if g in alive and g.pid in tog:
                        for pr in cl.census_problems(hc, census.dump(g.con))[:20]:
                            probs.append(f'{g.name}|{pr}')
                for k in self.persist['census'].update(p.split(' (')[0] for p in probs):
                    name, pr = k.split('|', 1)
                    full = next((x for x in probs if x.startswith(k)), k).split('|', 1)[1]
                    if self.finding('census', f'{hst}:{k}', f'{name}: {full} ({hst})', [host, self.byname(name)]):
                        path = os.path.join(self.run_dir, f'{len(self.F.items):02d}_census.txt')
                        with open(path, 'w') as f:
                            f.write('# host\n' + host.call('coop.census') + '\n# ' + name + '\n'
                                    + self.byname(name).call('coop.census') + '\n')
                        self.F.attach('census', f'{hst}:{k}', [f'both censuses: {os.path.relpath(path, lane.ROOT)}'])
        except (OSError, ValueError) as e:
            self.finding('info', f'check:{e!r}'[:80], f'a check could not read a game: {e!r}')

    def byname(self, name):
        return next(p for p in self.P if p.name == name)

    def sleep_checking(self, secs):
        end = time.time() + secs
        while time.time() < end and not self.dead:
            time.sleep(min(CHECK_EVERY, max(0.0, end - time.time())))
            self.check()

    # ---------------- what players do around the bots ----------------
    def service(self, con, p, hcon):
        """captions, votes, editors: what any player answers when it comes up (hcon: this thread's
        console to the host). Returns what it did."""
        st = con.call('coop.status').get('out', '')
        mode = con.call('game.mode').get('out', '')
        if 'Editor' in mode:
            time.sleep(self.rng.uniform(3, 8))  # a look at the creation
            con.call(f'move {EDITOR_DONE[0]} {EDITOR_DONE[1]}')
            time.sleep(0.4)
            con.call(f'click {EDITOR_DONE[0]} {EDITOR_DONE[1]}')
            return 'editor done'
        if p is not self.host and hcon.call('coop.vote').get('out', '').startswith('vote active'):
            return self.ready(con, p)
        if re.search(r'movie=1', st):
            time.sleep(self.rng.uniform(4, 7))  # an OK too early crashed the UI once (coop_space_finale.settle)
            if 'movie=1' in con.call('coop.status').get('out', ''):
                con.call(f'move {CAPTION_OK[0]} {CAPTION_OK[1]}')
                time.sleep(0.4)
                con.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')
                return 'caption ok'
        return None

    def vote_open(self):
        return self.host.call('coop.vote').startswith('vote active')

    def ready(self, con, p):
        """F9 when the window is closed, then "I'm ready" (a real click on the widget)"""
        ui = Ui(con, p.name)
        st = ui.state()
        if st.get('open') != '1':
            con.call(f'key {VK_F9} down')
            time.sleep(0.3)
            con.call(f'key {VK_F9} up')
            st = ui.wait(lambda s: s.get('open') == '1', 6, 'the co-op window')
        if st and st.get('ready', {}).get('vis'):
            ui.click('ready')
            with self.lock:
                self.votes.append((time.time(), p.name))
            time.sleep(1)
            if ui.state().get('open') == '1':
                con.call(f'key {VK_F9} down')
                time.sleep(0.3)
                con.call(f'key {VK_F9} up')
            return 'ready'
        return 'no ready button'

    def noise(self, con, p, stage):
        """one ordinary-player thing that is not the stage's goal"""
        kinds = {'idle': 30, 'f9': 20, 'pause': 15 if stage != 'space' else 0, 'save': 10 if p is self.host else 0}
        k = self.rng.choices(list(kinds), weights=list(kinds.values()))[0]
        if k == 'idle':
            time.sleep(self.rng.uniform(10, 45))
        elif k == 'f9':
            con.call(f'key {VK_F9} down')
            time.sleep(0.3)
            con.call(f'key {VK_F9} up')
            time.sleep(self.rng.uniform(2, 6))
            if 'open=1' in con.call('coop.uistate').get('out', ''):
                con.call(f'key {VK_F9} down')
                time.sleep(0.3)
                con.call(f'key {VK_F9} up')
        elif k == 'pause':
            con.call('key esc')
            time.sleep(self.rng.uniform(2, 6))
            con.call('key esc')
        elif k == 'save':
            con.call('coop.save')
            time.sleep(2.5)
            con.call('key enter')
        return k

    def rejoin(self, g):
        """a guest leaves and joins again: it leaves to its menu, loads the host's world anew and is
        together with the host again (twoinst.warm_reset's wait)"""
        self.quiet_until = time.time() + 300
        self.note(f'{g.name} leaves and joins again')
        g.call('coop.leave')
        t0 = time.time()
        while time.time() - t0 < 15 and g.pid in self.together():
            time.sleep(0.5)
        time.sleep(self.rng.uniform(2, 8))
        g.call(g.join)
        t1 = time.time()
        left = False
        while time.time() - t1 < 40 and not left:  # the world comes: the guest leaves its old copy
            left = any(m in g.call('game.mode') for m in ('GalaxyGameEntry', 'Load')) or \
                'receiving world' in g.call('coop.status')
            time.sleep(0.5)
        ok = wait_all(self.host.con, len(self.guests), 200)
        if ok:  # together, and its game plays the host's stage again
            hst = cl.stage_of(self.host.call('game.mode'))
            t2 = time.time()
            while time.time() - t2 < 60 and cl.stage_of(g.call('game.mode')) != hst:
                time.sleep(1)
            ok = cl.stage_of(g.call('game.mode')) == hst and wait_all(self.host.con, len(self.guests), 60)
        self.rejoins += 1
        if not ok:
            self.finding('apart', f'rejoin:{g.name}:{self.stage}', f'{g.name} did not get back into the host\'s world '
                         f'after a rejoin within 300 s ({self.stage})', [self.host, g])
        else:
            self.note(f'{g.name} is back after {time.time() - t0:.0f} s (left its old world: {left})')
        time.sleep(10)
        self.quiet_until = time.time() + 20

    # ---------------- play phases ----------------
    def play(self, stage, minutes):
        self.stage, self.phase = stage, 'play'
        self.ids()
        self.note(f'play {stage} for {minutes:.1f} min, ids ' + ', '.join(f'{p.name}={p.pid}' for p in self.P))
        end = time.time() + minutes * 60
        hands = self.bots(stage, end)
        rejoin_at = time.time() + minutes * 60 * self.rng.uniform(0.3, 0.7) if self.a.rejoins else None
        for h in hands:
            h.start()
        try:
            last = time.time()
            while time.time() < end and not self.dead and any(h.is_alive() for h in hands):
                time.sleep(2)
                if rejoin_at and time.time() > rejoin_at:
                    rejoin_at = None
                    self.rejoin(self.rng.choice(self.guests))
                if time.time() - last >= CHECK_EVERY:
                    last = time.time()
                    self.check()
        finally:
            self.stop = True
            for h in hands:
                h.join(60)
            self.stop = False
        for h in hands:
            if h.error:
                self.finding('info', f'bot:{stage}:{h.name}', f'{stage} bot of {h.name} stopped: {h.error}')
        self.note('played: ' + '; '.join(f'{h.name} {h.summary()}' for h in hands))
        self.check()

    def bots(self, stage, end):
        """the stage's play bots, wrapped in Hands threads"""
        seed = self.rng.randrange(1 << 30)
        names = [p.name for p in self.P]
        if stage == 'cell':
            import play_cell
            out = []
            for i, p in enumerate(self.P):
                b = play_cell.Player(p.name, Console(p.inst, timeout=30), p.pid, 1 if p.pid == 0 else 0,
                                     random.Random(seed + i))
                out.append(Hands(self, p, stage, [(b, lambda b=b: (b.step(time.time(), play_cell.pool(b.c)),
                                                                  time.sleep(0.25)))], end, release=b.release))
            return out
        if stage == 'creature':
            import play_creature
            world = play_creature.World(self.host.inst)
            sh = play_creature.Shared()
            sh.exit_result = None
            out = []
            for i, p in enumerate(self.P):
                b = play_creature.Player(p.name, p.inst, 1 if p.pid == 0 else 0, world, sh, seed + i, end)
                out.append(Hands(self, p, stage, [(b, b.step)], end))
            return out
        if stage == 'tribe':
            import play_tribe
            # each player's share of the tribe: 3 more members per player (play_tribe's setup) so
            # that raids and gathering go on at once
            print('  tribe setup:', self.host.call(f'coop.tribe spawn {3 * len(self.P)}'), flush=True)
            time.sleep(8)
            g = play_tribe.Game(Console(self.host.inst, timeout=30), Console(self.guests[0].inst, timeout=30),
                                seed, 1.0)
            for p in self.P[2:]:
                g.p.append(play_tribe.Player(p.name, Console(p.inst, timeout=30), p.pid,
                                             random.Random(seed + p.pid), g.log, g.w, g.h))
            for pl, p in zip(g.p, self.P):
                pl.name = p.name
                pl.pid = p.pid
                if not pl.calibrate():
                    self.finding('info', f'tribe:calibrate:{p.name}', f'{p.name}: the minimap did not calibrate')
            self.tribe_game = g
            # one thread: play_tribe's Game is not thread-safe (it steps the players in turn)
            return [Hands(self, None, stage, [(pl, lambda pl=pl: g.step(pl)) for pl in g.p], end, players=self.P)]
        if stage == 'civ':
            import play_civ
            g = play_civ.Game(None, None, seed)
            out = []
            for p in self.P:
                b = play_civ.Bot(p.name, p.inst, p.pid, g)
                b.hc = Console(self.host.inst, timeout=30)
                if p.pid not in (0, 1):  # the co-op window's gift goes to "the partner" of a two-player game
                    b.WEIGHTS = dict(b.WEIGHTS, give=0)
                out.append(Hands(self, p, stage, [(b, b.step)], end))
            self.civ_game = g
            return out
        if stage == 'space':
            import play_space
            stats = {}
            bots = []
            for i, p in enumerate(self.P):
                c = Console(p.inst, timeout=30)
                print(f'  {p.name} drive:', c.call(f'coop.space tool add {play_space.DRIVE}').get('out', '').strip(),
                      flush=True)
                b = play_space.Player(p.name, p.inst, 0 if p is self.host else 1, seed + i, end, stats)
                bots.append(b)
            for b in bots:
                b.partner = bots[1] if b is bots[0] else bots[0]
                b.last_place = b.place()
            return [Hands(self, p, stage, [(b, lambda b=b: (b.step(), b.sleep(b.rng.uniform(0.2, 0.8))))], end)
                    for p, b in zip(self.P, bots)]
        raise ValueError(stage)

    # ---------------- stage changes ----------------
    def advance(self, stage):
        """the stage change the players' way; False when it did not happen in time"""
        nxt = cl.STAGES[cl.STAGES.index(stage) + 1]
        self.stage, self.phase = stage, 'advance'
        self.note(f'advance {stage} -> {nxt}')
        t0 = time.time()
        timeout = self.a.advance_minutes * 60
        kicks = {'cell': self.kick_cell, 'creature': self.kick_creature, 'tribe': self.kick_tribe,
                 'civ': self.kick_civ}[stage]
        state = {'n': 0, 'last': 0.0}
        log0 = self.log_size(self.host.inst)
        services = [Service(self, p) for p in self.P]
        for s in services:
            s.start()
        try:
            while time.time() - t0 < timeout and not self.dead:
                hst = cl.stage_of(self.host.call('game.mode'))
                if hst == nxt:
                    break
                busy = 'movie=1' in self.host.call('coop.status') or self.vote_open() or hst is None
                if not busy and time.time() - state['last'] > 25:
                    kicks(state)
                    state['n'] += 1
                    state['last'] = time.time()
                time.sleep(3)
                if time.time() - state.get('checked', 0) > CHECK_EVERY:
                    state['checked'] = time.time()
                    self.health()
        finally:
            for s in services:
                s.stop = True
            for s in services:
                s.join(30)
        took = time.time() - t0
        if cl.stage_of(self.host.call('game.mode')) != nxt:
            self.note(f'the host did not reach {nxt} in {took / 60:.1f} min')
            self.vote_check(log0, t0)
            return False
        self.note(f'the host plays {nxt} after {took:.0f} s')
        self.stage = nxt
        ok = self.follow(nxt, services)
        self.vote_check(log0, t0)
        return ok

    def follow(self, nxt, services=()):
        """every guest follows the host into the stage (the world share) and is in its world"""
        self.phase = 'follow'
        self.quiet_until = time.time() + 600
        t0 = time.time()
        ok = False
        svc = [Service(self, p) for p in self.P]
        for s in svc:
            s.start()
        try:
            while time.time() - t0 < 300 and not self.dead:
                st = {g.name: cl.stage_of(g.call('game.mode')) for g in self.guests}
                tog = self.together()
                self.ids()
                if all(v == nxt for v in st.values()) and all(g.pid in tog for g in self.guests):
                    ok = True
                    break
                time.sleep(4)
                self.health()
        finally:
            for s in svc:
                s.stop = True
            for s in svc:
                s.join(30)
        self.quiet_until = time.time() + 10
        if ok:
            self.note(f'all guests play {nxt} together with the host after {time.time() - t0:.0f} s')
        else:
            behind = [g for g in self.guests if cl.stage_of(g.call('game.mode')) != nxt or g.pid not in self.together()]
            self.finding('stuck', f'follow:{nxt}', f'{", ".join(g.name for g in behind)} did not follow the host into '
                         f'{nxt} within 300 s', [self.host] + behind)
        return ok

    @staticmethod
    def log_size(inst):
        try:
            return os.path.getsize(lane.game_log(inst))
        except OSError:
            return 0

    def log_since(self, inst, offset):
        try:
            with open(lane.game_log(inst), 'rb') as f:
                f.seek(offset)
                return [l for l in f.read().decode('utf-8', 'replace').splitlines() if '[DBG]' not in l]
        except OSError:
            return []

    def vote_check(self, log0, t0):
        """a vote of this stage change that ran out of time although the guests pressed I'm ready"""
        lines = self.log_since(self.host.inst, log0)
        timeups = [l for l in lines if 'evolution starts (time is up)' in l]
        readies = [v for v in self.votes if v[0] >= t0]
        for l in lines:
            if 'wants to evolve' in l or 'evolution starts' in l:
                self.note('host log: ' + l.split('coop: ')[-1])
        if timeups and readies:
            self.finding('vote', f'{self.stage}:timeup', f'a vote ran out of time although '
                         f'{", ".join(sorted({v[1] for v in readies}))} pressed "I\'m ready" ({self.stage})',
                         self.P, [f'`{l.strip()}`' for l in lines if 'evol' in l or 'ready' in l][:10])

    def kick_cell(self, state):
        h = self.host
        if state['n'] == 0:
            self.note('shortcut: coop.pool food 1000 on the host (the food bar full)')
            h.call('coop.pool food 1000')
            return
        if state['n'] >= 1:  # after the cinematic: History, then its check = the next stage
            self.history()

    def kick_creature(self, state):
        h = self.host
        if state['n'] == 0:
            self.note('shortcut: AddEvolutionPoints 3000 on the host (brain upgrade 4, the bar full)')
            h.call('call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:3000')
            return
        self.history()

    def history(self):
        h = self.host
        h.call(f'move {HISTORY_BTN[0]} {HISTORY_BTN[1]}')
        time.sleep(0.4)
        h.call(f'click {HISTORY_BTN[0]} {HISTORY_BTN[1]}')
        time.sleep(3)
        h.call(f'move {HISTORY_OK[0]} {HISTORY_OK[1]}')
        time.sleep(0.4)
        h.call(f'click {HISTORY_OK[0]} {HISTORY_OK[1]}')
        time.sleep(2)

    def kick_tribe(self, state):
        """real raids by the players (play_tribe with the plan 'raid'), more members now and then"""
        if state['n'] % 4 == 0:
            self.note('shortcut: coop.tribe spawn 6 (more members to raid with)')
            self.host.call('coop.tribe spawn 6')
        g = getattr(self, 'tribe_game', None)
        if g is not None:
            g.plan = lambda: 'raid'
            end = time.time() + 60
            while time.time() < end:
                for pl in g.p:
                    try:
                        g.step(pl)
                    except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
                        self.finding('info', f'tribe:raid:{type(e).__name__}', f'tribe raid bot error: {e!r}')
                        return
        tribes = len(re.findall(r'^tribe \d+ at', self.host.call('coop.tribe'), re.M))
        self.note(f'tribe: {tribes} other tribes left')
        self.history()

    def kick_civ(self, state):
        """the cities still to come appear, every city of another nation goes to the players"""
        h = self.host
        if state['n'] == 0:
            m = re.search(r'eax=(0x[0-9A-Fa-f]+)', h.call('call Simulator::cCivExpansion::Get cdecl'))
            n = 0
            while m and n < 20:
                r = re.search(r'eax=0x([0-9A-Fa-f]+)',
                              h.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 1 0'))
                if not r or int(r.group(1), 16) & 0xFF == 0:  # bool in al: no free site left
                    break
                n += 1
                h.call('wait.frames 60 20000')
            self.note(f'shortcut: {n} cities appeared at once (SpawnNextCity)')
            return
        out = h.call('coop.civ')
        mine = re.search(r'city -?\d+ (\S+) pol=([0-9A-F]+) owner=\S*.*\(player\)', out)
        others = [(m.group(1), m.group(2)) for m in re.finditer(r'city -?\d+ (\S+) pol=([0-9A-F]+) owner=(?!.*\(player\)).*', out)]
        if not mine:
            self.finding('info', 'civ:noplayer', 'civ: no player city in coop.civ', [], [f'`{out[:300]}`'])
            return
        pol = mine.group(2)
        took = 0
        for ptr, opol in others:
            if opol == pol:
                continue
            h.call(f'call Simulator::cCity::Capture thiscall {ptr if ptr.startswith("0x") else "0x" + ptr} 0x{pol} 0 0 0 0')
            took += 1
            time.sleep(3)
            break  # one city per kick: its capture cinematic plays first
        self.note(f'shortcut: cCity::Capture of {took} city for 0x{pol} ({len(others)} of other nations listed)')
        if not took:
            self.history()

    # ---------------- the jump to the next stage's fixture ----------------
    def jump(self, nxt):
        fx = cl.FIXTURE[nxt]
        planet = fixtures.planet_of(fx).split('/')[1]
        slot = 10 + len(self.jumps)
        self.stage, self.phase = nxt, 'jump'
        self.note(f'jump: the host loads the {fx} fixture in game (slot {slot}), the guests follow')
        self.jumps.append(nxt)
        self.quiet_until = time.time() + 600
        lane.sh('fixture', 'slot', fx, lane.HOST_PROFILE, str(slot))
        self.host.call(f'coop.load Game{slot} {planet}')
        t0 = time.time()
        while time.time() - t0 < 180:
            if cl.stage_of(self.host.call('game.mode')) == nxt and self.host.con.call('coop.avatar').get('ok'):
                break
            time.sleep(1)
        else:
            self.finding('stuck', f'jump:{nxt}', f'the host did not load the {fx} fixture within 180 s', [self.host])
            return False
        return self.follow(nxt)

    # ---------------- the end of the game ----------------
    def finale(self):
        """the galactic core with every player: the host goes there, the guests fly to Alice"""
        import coop_space_finale as fz
        self.stage, self.phase = 'space', 'finale'
        self.quiet_until = time.time() + 1200
        h = self.host

        class Ck:  # coop_space_finale's ck(): failures become findings
            def __call__(ck, cond, msg):  # noqa: N805
                if not cond:
                    self.finding('stuck', f'finale:{msg[:60]}', f'finale: {msg}', self.P)
                else:
                    self.note('finale ok: ' + msg)
                return cond
        ck = Ck()
        for p in self.P:
            fz.settle(p.con, p.name, timeout=60)
            if fz.fin(p.con).get('ctx') not in ('2',):
                fz.get_drive(p.con, p.name, ck)
        fz.to_galaxy(h.con, h.name, ck)
        self.note('shortcut: coop.space finale goto near (the host crosses the galaxy)')
        fz.sp(h.con, 'finale goto near')
        fz.wait(lambda: fz.fin(h.con).get('zone') == '2', 30)
        self.note('shortcut: coop.space finale goto core')
        fz.sp(h.con, 'finale goto core')
        fz.wait(lambda: fz.fin(h.con).get('zone') == '3', 30)
        fz.settle(h.con, h.name)
        for _ in range(40):
            if fz.fin(h.con).get('ctx') == '1':
                break
            h.call('wheel 720 540 3')
            time.sleep(1.5)
        fz.settle(h.con, h.name)
        for _ in range(4):
            h.call('click 720 480')
            time.sleep(5)
            for _ in range(4):
                h.call('wheel 720 540 3')
                time.sleep(1.5)
            f = fz.fin(h.con)
            if f.get('waiting') == '1' or f.get('entered') == '1':
                break
        f = fz.fin(h.con)
        ck(f.get('waiting') == '1' or f.get('entered') == '1', f'the host waits at the core ({f["line"]})')
        for g in self.guests:  # each guest flies to Alice twice: next to the core, then to the gate
            ui = Ui(g.con, g.name)
            for step in ('near the core', 'the gate'):
                fz.settle(g.con, g.name, timeout=60)
                if step == 'the gate':
                    fz.to_galaxy(g.con, g.name, ck)
                fz.open_space_page(ui, g.con, ck, g.name)
                b = fz.fly_button(ui)
                ck(b is not None, f'{g.name}: "Fly to Alice" on the Space page ({step})')
                if b:
                    ui.click(b)
                g.call(f'key {VK_F9}')
                time.sleep(8)
        ok = fz.wait(lambda: all(fz.fin(p.con).get('entered') == '1' for p in self.P), 120, 3)
        ck(ok, 'every player entered the galactic core: ' + ' | '.join(f'{p.name} {fz.fin(p.con)["line"][:80]}'
                                                                       for p in self.P))
        done = fz.press_through([p.con for p in self.P], lambda: all(
            fz.fin(p.con).get('busy') == '0' and fz.fin(p.con).get('staff') == '1' for p in self.P), 400)
        ck(done, 'every player got the Staff of Life and the cinematic ended')
        self.quiet_until = time.time() + 20


class Service(threading.Thread):
    """answers captions, votes and editors for one player while a stage changes"""

    def __init__(self, camp, p):
        super().__init__(daemon=True)
        self.camp, self.p, self.stop = camp, p, False
        self.con = Console(p.inst, timeout=30)
        self.hcon = Console(camp.host.inst, timeout=30)
        self.done = {}

    def run(self):
        while not self.stop and not self.camp.dead:
            try:
                r = self.camp.service(self.con, self.p, self.hcon)
            except (OSError, ValueError):
                r = None
                time.sleep(3)
            if r:
                self.done[r] = self.done.get(r, 0) + 1
                self.camp.note(f'{self.p.name}: {r}')
            time.sleep(2)


class Hands(threading.Thread):
    """steps one player's bot (or several in turn) until the end, with ordinary-player noise between
    the actions and the captions / votes / editors answered"""

    def __init__(self, camp, p, stage, steppers, end, release=None, players=None):
        super().__init__(daemon=True)
        self.camp, self.p, self.stage, self.steppers, self.end = camp, p, stage, steppers, end
        self.players = players or [p]
        self.name = p.name if p else '+'.join(x.name for x in self.players)
        self.release = release
        self.error = None
        self.counts = {}
        self.cons = {x.name: Console(x.inst, timeout=30) for x in self.players}
        self.hcon = Console(camp.host.inst, timeout=30)
        self.rng = random.Random(camp.rng.random())

    def bump(self, k):
        self.counts[k] = self.counts.get(k, 0) + 1

    def summary(self):
        return ' '.join(f'{k}={v}' for k, v in sorted(self.counts.items()))

    def run(self):
        n = 0
        try:
            while time.time() < self.end and not self.camp.stop and not self.camp.dead:
                i = n % len(self.steppers)
                who = self.players[i] if len(self.players) == len(self.steppers) else self.players[0]
                n += 1
                con = self.cons[who.name]
                r = self.camp.service(con, who, self.hcon)
                if r:
                    self.bump(r)
                    continue
                if self.rng.random() < self.camp.a.noise:
                    self.bump('noise_' + self.camp.noise(con, who, self.stage))
                    continue
                try:
                    self.steppers[i][1]()
                    self.bump('steps')
                except (KeyError, IndexError, TypeError, AttributeError, ZeroDivisionError) as e:
                    self.bump('bot_errors')  # a bot's own bug, not the game's: go on
                    if self.counts['bot_errors'] <= 3:
                        print(f'  bot error ({who.name}, {self.stage}): {e!r}', flush=True)
                    time.sleep(1)
                except Exception as e:  # noqa: BLE001 - play_space.Stop at its end, console errors
                    if type(e).__name__ == 'Stop':
                        break
                    raise
        except (OSError, ValueError) as e:
            self.error = repr(e)
        finally:
            if self.release:
                try:
                    self.release()
                except (OSError, ValueError):
                    pass
            for c in self.cons.values():
                for k in ('0x57', '0x41', '0x53', '0x44'):
                    try:
                        c.call(f'key {k} up')
                    except (OSError, ValueError):
                        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--from', dest='start', default='cell', choices=cl.STAGES)
    ap.add_argument('--to', dest='end', default='space', choices=cl.STAGES)
    ap.add_argument('--minutes', type=float, default=8, help='minutes of play per stage')
    ap.add_argument('--advance-minutes', type=float, default=20, help='time for one stage change on the host')
    ap.add_argument('--players', type=int, default=4, choices=(2, 3, 4))
    ap.add_argument('--noise', type=float, default=0.06, help='chance of a noise action instead of a bot step')
    ap.add_argument('--no-rejoins', dest='rejoins', action='store_false')
    ap.add_argument('--no-finale', dest='finale', action='store_false')
    ap.add_argument('--no-jump', dest='jump', action='store_false', help='stop at a stage change that does not finish')
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    run = time.strftime('%Y%m%d-%H%M%S')
    run_dir = lane.out_dir('campaign', run)
    phases = cl.plan(a.start, a.end, a.finale)
    print(f'  campaign: {a.players} players, {a.start} -> {a.end}{" + finale" if a.finale and a.end == "space" else ""}, '
          f'{a.minutes} min per stage, seed {seed}, run {os.path.relpath(run_dir, lane.ROOT)}', flush=True)
    print('  phases: ' + ', '.join(f'{k} {s}' for k, s in phases), flush=True)
    t_start = time.time()
    host_con, guest_con = start(cl.FIXTURE[a.start])
    extra = start_extra(a.players - 2) if a.players > 2 else {}
    players = [Player('Alice', lane.HOST), Player('Bob', lane.GUEST, lane.JOIN)]
    for name, (c, inst) in extra.items():
        players.append(Player(name, inst, join_cmd(name)))
    for p in players[2:]:
        p.call(p.join)
        time.sleep(1)
    camp = Campaign(a, players, run_dir, seed)
    ok = wait_all(players[0].con, len(players) - 1, 200)
    if not ok:
        camp.finding('apart', 'start', f'not every guest got into the host\'s world at the start '
                     f'(together {sorted(camp.together())})', players)
    monitors = []
    for p in players[2:]:
        m = Monitor(p.inst, p.name)
        m.start()
        monitors.append(m)
    camp.ids()
    camp.health()  # fault baselines
    camp.note(f'{len(players)} players in {cl.FIXTURE[a.start]} after {time.time() - t_start:.0f} s: '
              + ', '.join(f'{p.name}={p.pid} (instance {p.inst})' for p in players))
    camp.check()
    reached = [a.start]
    try:
        for kind, stage in phases:
            if camp.dead:
                break
            if kind == 'play':
                if stage != camp.stage and stage not in reached:
                    break  # the stage change before it failed and no jump was allowed
                camp.play(stage, a.minutes)
            elif kind == 'advance':
                nxt = cl.STAGES[cl.STAGES.index(stage) + 1]
                ok = camp.advance(stage)
                if not ok and not camp.dead:
                    camp.finding('stuck' if stage in ('cell', 'creature') else 'info', f'advance:{stage}',
                                 f'the stage change {stage} -> {nxt} did not finish in {a.advance_minutes:.0f} min'
                                 + ('' if stage in ('cell', 'creature') else ' (no automated end of this stage yet)'),
                                 camp.P)
                    if a.jump:
                        ok = camp.jump(nxt)
                if ok:
                    reached.append(nxt)
                else:
                    break
            elif kind == 'finale':
                camp.finale()
            camp.check()
    except (OSError, ValueError) as e:
        camp.finding('dead', f'console:{e!r}'[:60], f'a console failed: {e!r}')
    except KeyboardInterrupt:
        camp.note('interrupted')
    camp.phase = 'end'
    time.sleep(5)
    camp.check()
    for m in monitors:
        m.stop = True
    # ---- report
    took = (time.time() - t_start) / 60
    print(f'\n  campaign {run}: {took:.0f} min, reached {" -> ".join(reached)}, jumps {camp.jumps or "none"}, '
          f'rejoins {camp.rejoins}, votes answered {len(camp.votes)}', flush=True)
    print('  timeline:')
    for line in camp.timeline:
        print('   ', line)
    bad = [it for it in camp.F.items if it['kind'] != 'info']
    print(f'  findings: {len(camp.F.items)} ({camp.F.summary()}), file {os.path.relpath(camp.F.md, lane.ROOT)}')
    for it in camp.F.items:
        print(f'    [{it["kind"]}] {it["time"]} {it["stage"]}: {it["text"]}' + (f' (x{it["repeats"] + 1})' if it['repeats'] else ''))
    print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/coop_campaign.py --from {a.start} --to {a.end} '
          f'--minutes {a.minutes} --seed {seed}')
    with open(os.path.join(run_dir, 'summary.txt'), 'w') as f:
        f.write(f'reached {" -> ".join(reached)}; jumps {camp.jumps}; findings {camp.F.summary()}\n')
        f.write('\n'.join(camp.timeline) + '\n')
    subprocess_kill()
    full = reached[-1] == a.end
    if not full:
        print(f'  FAIL the campaign stopped in {reached[-1]} (wanted {a.end})')
    print('RESULT:', 'OK' if not bad and full and not camp.dead else 'FAIL')
    return 0 if not bad and full and not camp.dead else 1


def subprocess_kill():
    """the extra guests run in this lane's bottle: the lab ends the lane's games; a manual run ends them here"""
    if os.environ.get('SPORE_CAMPAIGN_KEEP') == '1':
        return
    import subprocess
    subprocess.run([lane.SPORE, 'kill'], capture_output=True)


if __name__ == '__main__':
    sys.exit(main())
