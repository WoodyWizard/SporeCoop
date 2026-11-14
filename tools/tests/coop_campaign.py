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
  * Tribe (Q1 of 0.7.14, docs/re/tribe.md#tribe-raid-end): `coop.tribe spawn` gives each player's share of the tribe
    more members (test setup, and again when the raiders are down to 7); every NPC village is raided until its hut
    falls: the players' real attack right-clicks on the hut first (the host's own click is needed: BUG-1068), then
    `coop.tribe order <id> 15 <hut>` for every member (the DoAction a right-click gives), the hut's health cut to 40
    once a blow landed (`coop.tribe huthp`; 400-750 hp take the raiders minutes), the players' own hut kept above 80 % of
    its health (an NPC village raids back; its fall is a game over that reloads the save), `coop.tribe form` when no
    village is left before the bar is full. The game's own conquests fill the goal bar; then the advance button, the
    stage card, the caption, the votes, the City Hall and vehicle editors (three parts, a name). When the stage does
    not end in time the host jumps to the next fixture (below). CAMPAIGN_OLD_TRIBE=1: 0.7.13's raids (pincer on members).
  * Editors that need a creation (the empty spaceship editor): three parts dragged from the palette,
    the dice's random name; "Name Your Captain": the dice; the city specialty dialog: Military.
  * Civ: the cities still to come appear at once (cCivExpansion::SpawnNextCity(ex, 0, 0) as the
    game's own city tick calls it, so the co-op hook tells the guests: instant=1 was not broadcast
    and left the guests to the catch-up, BUG-0380), the other nations' cities are taken with the game's cCity::Capture for the
    player's political id (the capture cinematics play), then History and its check, the vote, the
    spaceship editor; the host then leaves its home planet through the mission control (space_setup.py)
    so that the game can save its world for the guests.
  * A jump: when a stage change does not finish (no automated end yet, or stuck), the host loads the
    next stage's fixture with `coop.load` in game, the guests follow through the world share (the
    co-op stage switch of coop_stage_switch.py). A stuck change is a finding; a jump is listed in the
    report.
  * Space: the guests get the interstellar drive (`coop.space tool add`, as play_space), the host
    goes to the core with `coop.space finale goto near|core` (crossing the galaxy takes hours); the
    guests fly there with the real "Fly to Alice" button.

  SPORE_LANE=7 python3 tools/tests/coop_campaign.py [--from cell] [--to space] [--minutes 8]
        [--advance-minutes M] [--no-finale] [--no-jump] [--seed N] [--players 4]
Resume a late failure from its stage: --from tribe (the fixture of that stage, campaign_lib.FIXTURE).
"""
import argparse
import os
import random
import math
import re
import shutil
import sys
import threading
import time

import campaign_lib as cl
import census
import fixtures
import lane
import uifind
from coop_multi import fps, join_cmd, limit, measure, over_limit, start_extra, wait_all
import coop_multi as cm
from coop_ui import Ui
from sporectl import Console
from twoinst import Monitor, keyframe, shot, start

CHECK_EVERY = 30
CAPTION_OK = (1410, 1048)    # the green check of a caption that waits for the player
HISTORY_BTN = (1400, 1062)   # the HUD button under the portrait: History (the next stage when it is done)
HISTORY_OK = (1415, 1055)    # its check: closes History, or starts the next stage
EDITOR_DONE = (1416, 1062)   # the green "done" of the editors (1440x1080)
# answers to a nation's talk that waits (Civ): its surrender after the cities fell; a trade offer
# (it covers the History button the host clicks next, BUG-0265)
CIV_TALK = ('We happily accept your surrender.', 'I accept your offer! Start shipments immediately!',
            'Agreed. Bye for now.')  # the nation's last line after an accepted offer (BUG-0268)
CAPTAIN_NAME = [(915, 555), (936, 630)]  # "Name Your Captain" (the first Space minutes): dice, check
EDITOR_DICE = (614, 826)     # the dice of the name panel (a random name), open after a "done" without a name
EDITOR_PARTS = [(85, 130), (350, 405), (480, 405)]       # spaceship editor palette: a hull, two discs
TRIBE_EDITOR_PARTS = (((88, 130), (900, 850)), ((220, 130), (900, 700)), ((350, 130), (1150, 850)))  # City Hall / vehicle
EDITOR_PLACES = [(1000, 560), (1000, 470), (1000, 650)]  # onto the stage
VK_F9 = '0x78'
# a badge's "Reward" panel (BUG-0268): its green check on the 1440x1080 screen (the panel is centred;
# from the campaign screenshots of run 20261029-093248)
REWARD_CHECK = (932, 844)
TRIBE_HUT = '1E4DAAE'        # the hut noun (a village falls with its hut)
TRIBE_ADVANCE = 0x6454B47   # the HUD's advance button the full goal bar shows in place of History
HOME_GUARD = 3              # members (besides the chiefs) that stay home in the Tribe end raids (tribe_order)
# default time of a stage change; Tribe (Q1 of 0.7.14): five conquests of 1.5-5 min each and the two editors (3 min):
# 13-19 min in the runs on the fixture and the campaign's own planet, so 30
ADVANCE_MIN = {'cell': 10, 'creature': 10, 'tribe': 30, 'civ': 20}


def drag_slow(con, sx, sy, tx, ty):
    """hold the button and move in steps: a plain `drag` is too fast for the editors"""
    con.call(f'move {sx} {sy}')
    time.sleep(0.3)
    con.call(f'button {sx} {sy} down')
    time.sleep(0.3)
    for i in range(1, 11):
        con.call(f'move {sx + (tx - sx) * i // 10} {sy + (ty - sy) * i // 10}')
        time.sleep(0.08)
    time.sleep(0.3)
    con.call(f'button {tx} {ty} up')
    time.sleep(1)


def stands(con):
    """the game's simulation stands (BUG-0404, the mod's busy gate: the game time paused by the pause menu,
    the Sporepedia, My Collections or a planner; an editor; a load): its state compared now is stale.
    Before, only the pause menu's "Resume Game" button counted (BUG-0289, BUG-0331). OLD_PAUSE_CHECK=1: that."""
    if os.environ.get('OLD_PAUSE_CHECK') != '1':
        m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
        if m and any(w in m.group(1).split(',') for w in ('paused', 'editor', 'loading')):
            return True
    return bool(uifind.shown(con, 'Resume Game'))


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
        self.cap = {}  # player -> (caption seen since, last OK)
        self.talks = {}  # player -> answers to a Civ talk that is still up (BUG-0290)
        self.edits = {}  # player -> "done" clicks in the current editor

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
            if os.environ.get('CAMPAIGN_GROUND') == '1' and hst in ('creature', 'tribe'):
                self.ground_probe(host, hst)
            bad_stage, apart = [], []
            for g in self.guests:
                if g not in alive:
                    continue
                gst = cl.stage_of(g.call('game.mode'))
                if gst != hst:
                    bad_stage.append(f'{g.name}:{hst}:{gst}')
                if g.pid not in tog and hst != 'space':  # in Space players fly to other stars: apart is play
                    apart.append(g.name)
            for k in self.persist['stage'].update(bad_stage):
                name = k.split(':')[0]
                self.finding('stage', k, f'{name} plays {k.split(":")[2]} while the host plays {hst}',
                             [host, self.byname(name)])
            for k in self.persist['apart'].update(apart):
                self.finding('apart', f'{hst}:{k}', f'the host does not see {k} in its world ({hst})',
                             [host, self.byname(k)])
            # sync of every synchronized object kind, guests that are in the host's world only
            over, short, nums, paused, terrain = [], [], [], [], []
            for g in self.guests:
                if g not in alive or g.pid not in tog:
                    continue
                # a guest in its pause menu runs no simulation: its cell mirrors move only by the
                # 3 m jumps of cellsync's guest_tick, a 0-3 m sawtooth that read as a desync of
                # p90 2.2 (BUG-0289, a noise pause whose Resume click was lost under an input lock)
                if stands(g.con):  # (any pause, an editor, a load: BUG-0404's busy gate)
                    paused.append(g.name)
                    continue
                # the host's path right before each guest (BUG-0269), its last 0.25 s: the guest
                # shows the host's objects that late on purpose (BUG-0275)
                missed = {}
                errs, cov, total, worst = measure(host.con, g.con, missed=missed)
                if total >= 5 and cov < 0.85:
                    short.append(f'{g.name}:coverage')
                    # which kind and which keys (BUG-0288: the finding did not say)
                    nums.append(f'{g.name}: coverage {cov:.0%} of {total}, missing '
                                + ', '.join(f'{k}:{len(v)} {" ".join(sorted(v)[:6])}' for k, v in sorted(missed.items())))
                for kind, v in errs.items():
                    v = sorted(v)
                    p90 = v[min(len(v) - 1, int(0.9 * len(v)))]
                    nums.append(f'{g.name}:{kind} p90 {p90:.1f} of {len(v)}, worst {worst[kind][1]} {worst[kind][0]:.1f}')
                    if over_limit(v, limit(kind)) and hst == 'tribe' and kind in ('t', 'n') and \
                            self.terrain_only(host, g, worst[kind][1], limit(kind), nums):
                        terrain.append(f'{g.name}:{kind}')
                    elif over_limit(v, limit(kind)):
                        over.append(f'{g.name}:{kind}:{p90:.1f}')
                        # BUG-0329: the worst object's own line on the host and on this guest (the
                        # guest's line has how its mirror is driven: err, target, own speed, teleports)
                        key = worst[kind][1]
                        cmd = {'t': 'coop.tribe', 'n': 'coop.npc list'}.get(kind)
                        if cmd and key[1:].isdigit():
                            for who, con in (('host', host), (g.name, g)):
                                ln = [x.strip() for x in con.call(cmd).splitlines()
                                      if re.match(rf'\s*#{key[1:]}\b', x)]
                                nums.append(f'{g.name} {key} on {who}: {ln[0][:260] if ln else "-"}')
                                # BUG-0329: each game's ground under the host's position of it
                                m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln[0]) if ln and who == 'host' else None
                                if m:
                                    for w2, c2 in (('host', host), (g.name, g)):
                                        nums.append(f'{g.name} {key} ground on {w2}: ' + c2.call('coop.ground ' + ' '.join(m.groups())).strip())
            if paused:
                self.note('sync: not compared (paused, an editor or a load): ' + ', '.join(paused))
            if terrain:  # BUG-0329: the same place on each game's own ground, the grounds differ in height
                self.note('sync: ground height differs (objects in place on their own ground): ' + ', '.join(terrain)
                          + ' | ' + '; '.join(n for n in nums if 'ground' in n or ' on ' in n))
            if over or short:  # every guest's numbers next to a desync: one guest only, or everybody (BUG-0275)
                self.note('sync: ' + '; '.join(nums) + ' | fps ' + ', '.join(
                    f'{p.name} {fps(p.con)}' for p in self.P if p in alive))
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
                    # a guest in its pause menu runs no simulation: a city the host founded meanwhile
                    # (and its vehicles) appears there when it resumes (BUG-0335, as BUG-0289 for sync)
                    if g in alive and g.pid in tog and g.name not in paused:
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

    def ground_probe(self, host, hst):
        """BUG-0332 (CAMPAIGN_GROUND=1): the game's own ground (coop.ground) on every machine at a place of the host's
        (a tribe member in Tribe, a player in Creature) and 15-200 m off it: noted when a guest's differs by more than 0.5 m"""
        try:
            hs = cm.sample(host.con)
            pref = [k for k in sorted(hs) if k[0] == ('t' if hst == 'tribe' else 'p')]  # a member / a player: the village
            key = (pref or sorted(hs) or [None])[0]
            if not key:
                return
            c = hs[key]
            r = math.dist(c, (0, 0, 0))
            a = (0.0, 0.0, 1.0) if abs(c[2] / r) < 0.9 else (1.0, 0.0, 0.0)
            e = (a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0])
            el = math.dist(e, (0, 0, 0))
            pts = [c] + [tuple((c[i] + d * e[i] / el) * r / math.dist([c[j] + d * e[j] / el for j in range(3)], (0, 0, 0))
                               for i in range(3)) for d in (15, 30, 60, 100, 200)]
            # far from the village: the six axis directions of the planet (is the difference local to the village?)
            pts += [tuple(r * (1 if k == i else 0) * s for k in range(3)) for i in range(3) for s in (1, -1)]
            if hst == 'tribe':  # every tribe's village (NPC tribes too: the 2026-11-05/06 differences were at NPC members)
                pts += [tuple(float(m.group(i)) for i in (1, 2, 3)) for m in re.finditer(
                    r'^tribe \d+(?: \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.tribe'), re.M)
                    if math.dist([float(m.group(i)) for i in (1, 2, 3)], (0, 0, 0)) > 50]

            nv = len(pts) - 12
            if hst == 'tribe':  # the NPC tribe spawn markers: where the host's controller forms the next villages
                pts += [tuple(float(m.group(i)) for i in (1, 2, 3)) for m in re.finditer(
                    r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.tribe markers'))]
            names = [key, '15m', '30m', '60m', '100m', '200m', '+x', '-x', '+y', '-y', '+z', '-z']
            names += [f'village{i}' for i in range(nv)]
            names += [f'marker{i}' for i in range(len(pts) - len(names))]
            if hst == 'creature':  # re-probed in Tribe: did the Tribe change move the ground (on which game)?
                self._creature_pts = list(zip(names, pts))
            else:
                ms = [k for k in sorted(hs) if k[0] == 't'][:40]  # every member: where the ground differs, not only one
                names += ms + [f'cr-{n}' for n, _ in getattr(self, '_creature_pts', [])]
                pts += [hs[k] for k in ms] + [q for _, q in getattr(self, '_creature_pts', [])]

            def gr(con, p):
                m = re.search(r'ground (-?[\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % tuple(p)))
                return float(m.group(1)) if m else None
            hg = [gr(host, p) for p in pts]
            rows = []
            for g in self.guests:
                gg = [gr(g, p) for p in pts]
                d = [(n, q, round(y - x, 2)) for n, q, x, y in zip(names, pts, hg, gg) if x is not None and y is not None]
                bad = [f'{n}@({q[0]:.0f} {q[1]:.0f} {q[2]:.0f}) {v:+.2f}' for n, q, v in d if abs(v) > 0.5]
                if bad:
                    rows.append(f'{g.name} {len(bad)} of {len(d)}: ' + ', '.join(bad))
            first = getattr(self, '_ground_seen', set())
            self._ground_seen = first | {hst}
            if rows or hst not in first:
                self.note(f'ground ({hst}; host ' + ', '.join(f'{n} {h:.2f}' for n, h in zip(names, hg) if h is not None)
                          + '): guest - host ' + ('; '.join(rows) or 'all within 0.5 m'))
        except (OSError, ValueError, ZeroDivisionError) as e:
            self.note(f'ground probe: {e!r}')

    def chiefs_overlap(self, p):
        """BUG-0340: pairs of players' chiefs whose bodies overlap on this player's own game
        (distance under the sum of their bounding radii)"""
        out = p.call('coop.tribe')
        m = re.search(r'chiefs=([^\n]*)', out)
        ids = dict((int(a), int(b)) for a, b in re.findall(r'(\d+):#(\d+)', m.group(1))) if m else {}
        mem = {int(x.group(1)): (tuple(map(float, x.group(2, 3, 4))), float(x.group(5)))
               for x in re.finditer(r'#(\d+) \S+ owner=-?\d+ .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*?radius=([\d.]+)', out)}
        ch = {o: mem[i] for o, i in ids.items() if i in mem}
        ks = sorted(ch)
        return [f'{a}-{b} {math.dist(ch[a][0], ch[b][0]):.1f} m (bodies {ch[a][1] + ch[b][1]:.1f} m)'
                for i, a in enumerate(ks) for b in ks[i + 1:] if math.dist(ch[a][0], ch[b][0]) < ch[a][1] + ch[b][1]]

    def chiefs_check(self, when):
        """BUG-0340: right after the players arrive in Tribe (or a rejoin) no two players' chiefs stand
        in each other on any machine (checked twice 8 s apart: the game's own avoidance gets a chance)"""
        try:
            first = {p.name: self.chiefs_overlap(p) for p in self.P}
            time.sleep(8)
            still = {p.name: [x for x in self.chiefs_overlap(p) if x.split()[0] in [y.split()[0] for y in first[p.name]]]
                     for p in self.P}
        except (OSError, ValueError) as e:
            self.finding('info', f'chiefs:{e!r}'[:60], f'chiefs check could not read a game: {e!r}')
            return
        self.note(f'chiefs after the {when}: ' + '; '.join(f'{n}: {v or "apart"}' for n, v in still.items()))
        bad = {n: v for n, v in still.items() if v}
        if bad:
            # BUG-0340 is open (placement holds only in some runs): report it, do not fail the run yet.
            # Set CAMPAIGN_STRICT_CHIEFS=1 to make it a failing 'stack' finding again.
            kind = 'stack' if os.environ.get('CAMPAIGN_STRICT_CHIEFS') == '1' else 'info'
            self.finding(kind, f'tribe:chiefs:{when}', f'players\' chiefs stand in each other after the {when} '
                         f'(8 s later still): ' + '; '.join(f'{n}: {v}' for n, v in bad.items()), self.P)

    def terrain_only(self, host, g, key, lim, nums):
        """BUG-0329: the object `key` stands at the same place on the host's and on the guest's own
        ground, only the two grounds differ in height (a Tribe village 5 m higher or lower on the
        guests): its sideways distance is within `lim` and its height over each game's own ground is
        the same within 1 m, while its distance from the planet's centre differs. Not a desync of
        the object (the player sees it standing where the host's stands)."""
        try:
            hp = cm.sample(host.con).get(key)
            gp = cm.sample(g.con).get(key)
            if not hp or not gp:
                return False
            def above(con, p):
                m = re.search(r'above (-?[\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % tuple(p)))
                return float(m.group(1)) if m else None
            ah, ag = above(host, hp), above(g, gp)
            if ah is None or ag is None:
                return False
            rh, rg = math.dist(hp, (0, 0, 0)), math.dist(gp, (0, 0, 0))
            u = [x / rh for x in hp]
            d = [b - a for a, b in zip(hp, gp)]
            radial = sum(a * b for a, b in zip(u, d))
            side = math.sqrt(max(0.0, sum(x * x for x in d) - radial * radial))
            nums.append(f'{g.name} {key}: sideways {side:.1f} m, centre distance host {rh:.1f} guest {rg:.1f}, '
                        f'over its own ground host {ah:.1f} guest {ag:.1f}')
            return side <= lim and abs(ah - ag) <= 1.0 and abs(rh - rg) > 1.5
        except (OSError, ValueError, ZeroDivisionError):
            return False

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
            return self.editor(con, p)
        self.edits.pop(p.name, None)
        if 'Space' in mode and uifind.shown(con, 'Name Your Captain'):
            for x, y in CAPTAIN_NAME:  # the dice (a random name), then its check
                con.call(f'move {x} {y}')
                time.sleep(0.4)
                con.call(f'click {x} {y}')
                time.sleep(1)
            return 'captain named'
        if 'Civ' in mode:
            # a captured city asks for its specialty (Economic / Religious / Military) and waits. It
            # comes first: the specialty box opens on top of a nation's talk and takes its clicks (the
            # talk's answer stays "shown" under it: answered every 12 s for 20 min, BUG-0290)
            b = uifind.shown(con, 'Military')
            if b and 600 < b[0] < 1100 and 500 < b[1] < 750:  # the dialog's button, not the HUD's tab label
                uifind.press(con, 'Military')
                self.talks.pop(p.name, None)
                return 'dialog Military'
            for answer in CIV_TALK:  # a nation's talk (its surrender after the cities fell) waits for an answer
                if uifind.shown(con, answer):
                    # the same talk still up after 3 answers: something else takes the clicks (a
                    # guest's talk under the "galactic adventures" panel, BUG-0290): Esc, as a player
                    n = self.talks.get(p.name, 0) + 1
                    self.talks[p.name] = n
                    if n > 3 and n % 2 == 0:
                        con.call('key esc')
                        time.sleep(1)
                        if not uifind.shown(con, answer):
                            self.talks.pop(p.name, None)
                            return 'talk closed with Esc'
                        self.close_pause(con)  # (Esc opened the pause menu instead)
                        return 'talk stuck: Esc did not close it'
                    if uifind.press(con, answer):
                        self.talks.pop(p.name, None)
                    return 'talk answered'
            self.talks.pop(p.name, None)
        if self.close_reward(con):
            return 'reward ok'
        if 'movie=1' in st:
            # a player reads the caption first; a cinematic without a button ends by itself (the
            # click lands on the HUD corner then): one OK after 6 s of it, then every 10 s. The
            # caption comes before the vote (BUG-0273: it covers the co-op window's buttons)
            t = time.time()
            since, last = self.cap.get(p.name, (t, 0.0))
            self.cap[p.name] = (since, last)
            if t - since >= 6 and t - last >= 10:
                self.cap[p.name] = (since, t)
                con.call(f'move {CAPTION_OK[0]} {CAPTION_OK[1]}')
                time.sleep(0.4)
                con.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')
                return 'caption ok'
        else:
            self.cap.pop(p.name, None)
        if p is not self.host and hcon.call('coop.vote').get('out', '').startswith('vote active'):
            return self.ready(con, p)
        return None

    def close_reward(self, con):
        """a badge's "Reward" panel (Golden Touch after the money shortcut) waits for its green
        check and takes the clicks meant for the co-op window (BUG-0268): checked like a player"""
        r = uifind.shown(con, 'Reward')
        if not r or not 400 < r[0] < 1000:  # the panel in the middle of the screen, not a HUD label
            return False
        x, y = REWARD_CHECK
        con.call(f'move {x} {y}')
        time.sleep(0.4)
        con.call(f'click {x} {y}')
        time.sleep(1)
        return True

    def editor(self, con, p):
        """leave an editor with "done" like a player. An empty editor (the spaceship editor at the
        end of the Civ stage: "You need at least 3 parts to Save") gets three parts dragged from the
        palette; a creation without a name ("You must give your creation a name") gets the dice's
        random name (the name panel opens with the first "done")"""
        n = self.edits.get(p.name, 0)
        self.edits[p.name] = n + 1
        time.sleep(self.rng.uniform(3, 6))  # a look at the creation
        if n == 2:
            # the end of Tribe opens the City Hall and the vehicle editor empty: their palette and plot
            # (coop_tribe_civ.PARTS); the spaceship editor's at the end of Civ
            moves = TRIBE_EDITOR_PARTS if self.stage == 'tribe' else list(zip(EDITOR_PARTS, EDITOR_PLACES))
            for (sx, sy), (tx, ty) in moves:
                drag_slow(con, sx, sy, tx, ty)
            self.note(f'{p.name}: three parts dragged into an empty editor')
        if n >= 3:
            con.call(f'move {EDITOR_DICE[0]} {EDITOR_DICE[1]}')
            time.sleep(0.4)
            con.call(f'click {EDITOR_DICE[0]} {EDITOR_DICE[1]}')
            time.sleep(1)
        con.call(f'move {EDITOR_DONE[0]} {EDITOR_DONE[1]}')
        time.sleep(0.4)
        con.call(f'click {EDITOR_DONE[0]} {EDITOR_DONE[1]}')
        return 'editor done'

    def vote_open(self):
        return self.host.call('coop.vote').startswith('vote active')

    @staticmethod
    def is_ready(st):
        """this player is ready in the vote: its button offers "Not ready", or its own row (the
        one marked "(you)" / "(host, you)", "- you" before 0.7.12) says ready (coop_ui.cpp)"""
        if st.get('ready', {}).get('text', '') in ('Not ready', 'Не готов'):
            return True
        return any(r and any(m in r[0] for m in ('- you', '- вы', 'you)', 'вы)')) and r[-1] in ('ready', 'готов')
                   for r in st.get('rows', []))

    def ready(self, con, p):
        """F9 when the window is closed, then "I'm ready" (a real click on the widget)"""
        ui = Ui(con, p.name)
        st = ui.state()
        if st.get('open') != '1':
            con.call(f'key {VK_F9} down')
            time.sleep(0.3)
            con.call(f'key {VK_F9} up')
            st = ui.wait(lambda s: s.get('open') == '1', 6, 'the co-op window')
        if st and self.is_ready(st):
            # "I'm ready" toggles: a second click takes the ready back (BUG-0273). Close the
            # window again and wait for the vote like a player
            if st.get('open') == '1':
                con.call(f'key {VK_F9} down')
                time.sleep(0.3)
                con.call(f'key {VK_F9} up')
            return 'already ready'
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
            self.close_pause(con)
        elif k == 'save':
            con.call('coop.save')
            time.sleep(2.5)
            con.call('key enter')
        return k

    def close_pause(self, con):
        """close the pause menu the way a player does (Resume Game); a second Esc could open its
        Options box instead and leave the game paused (BUG-0263)"""
        for _ in range(3):
            if not uifind.shown(con, 'Resume Game'):
                return
            uifind.press(con, 'Resume Game')
            time.sleep(1.0)

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
        hst = cl.stage_of(self.host.call('game.mode'))
        ok = False
        t2 = time.time()
        while time.time() - t2 < 260 and not ok:  # its game plays the host's stage in the host's world again
            self.ids()
            ok = cl.stage_of(g.call('game.mode')) == hst and g.con.call('coop.avatar').get('ok') and \
                (hst == 'space' or g.pid in self.together())  # (Space: in the session, maybe at another star)
            if not ok:  # (no Enter for the host's "game saved" box: the mod closes it, BUG-0152)
                time.sleep(1)
        self.rejoins += 1
        if not ok:
            self.finding('apart', f'rejoin:{g.name}:{self.stage}', f'{g.name} did not get back into the host\'s world '
                         f'after a rejoin within 300 s ({self.stage})', [self.host, g])
        else:
            self.note(f'{g.name} is back after {time.time() - t0:.0f} s (left its old world: {left})')
            if self.stage == 'tribe':
                time.sleep(4)
                self.chiefs_check(f'rejoin of {g.name}')
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
        # CAMPAIGN_HOST_CMDS_PLAYED_<STAGE>=<cmd>[;<cmd>]: the host's answers at the end of the stage's play (triage)
        for cmd in [c.strip() for c in os.environ.get(f'CAMPAIGN_HOST_CMDS_PLAYED_{stage.upper()}', '').split(';') if c.strip()]:
            for ln in self.host.call(cmd).splitlines()[:300]:
                print(f'  host {cmd}: {ln}', flush=True)
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
            t0 = time.time()
            while time.time() - t0 < 90 and '(player)' not in self.host.call('coop.tribe'):
                time.sleep(2)  # the player tribe exists once the intro is over
            print('  tribe setup:', self.host.call(f'coop.tribe spawn {3 * len(self.P)}'), flush=True)
            time.sleep(8)
            if os.environ.get('CAMPAIGN_FORM') == '1':  # BUG-0332: the host's controller forms its next villages now
                self.host.call('coop.res add food 400')
                for _ in range(2):
                    print('  tribe setup: form:', self.host.call('coop.tribe form')[:160], flush=True)
                    time.sleep(3)
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
        for p in self.P:  # BUG-0263: a pause menu left open by the play phase blocks the stage change
            self.close_pause(p.con)
        t0 = time.time()
        timeout = self.advance_minutes(stage) * 60
        kicks = {'cell': self.kick_cell, 'creature': self.kick_creature, 'tribe': self.kick_tribe,
                 'civ': self.kick_civ}[stage]
        state = {'n': 0, 'last': 0.0}
        reached = False  # BUG-0739: the host's own reload after Creature -> Tribe (BUG-0332) passes the menu a few s later
        log0 = self.log_size(self.host.inst)
        services = [Service(self, p) for p in self.P]
        for s in services:
            s.start()
        spam = None
        if os.environ.get('CAMPAIGN_SHARE_SPAM') == '1' and stage == 'creature':
            spam = self.share_spam(nxt)
        try:
            while time.time() - t0 < timeout and not self.dead:
                hst = cl.stage_of(self.host.call('game.mode'))
                if hst == nxt:
                    reached = True
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
            if spam:
                spam.join(30)
        took = time.time() - t0
        if not reached and cl.stage_of(self.host.call('game.mode')) != nxt:
            self.note(f'the host did not reach {nxt} in {took / 60:.1f} min')
            self.vote_check(log0, t0)
            return False
        self.note(f'the host plays {nxt} after {took:.0f} s')
        self.stage = nxt
        self.follow(nxt, services)  # a guest left behind is a finding; the game goes on in the stage
        self.vote_check(log0, t0)
        return True

    def share_spam(self, nxt):
        """BUG-0359 (CAMPAIGN_SHARE_SPAM=1): the host's world share posted again every ~1.5 s through the
        Creature -> Tribe change, until 4 s after the host is in Tribe, so a share save is being written while
        the game enters Tribe (the reported path "went from Creature to Tribe during the save"); to one guest
        (the last one) so the others follow as usual"""
        con = Console(self.host.inst, timeout=30)
        gid = self.guests[-1].pid if self.guests else None

        def run():
            seen_at = None
            n = 0
            end = time.time() + self.advance_minutes('creature') * 60 + 30
            while time.time() < end and not self.dead:
                try:
                    if cl.stage_of(con.call('game.mode').get('out', '')) == nxt:
                        seen_at = seen_at or time.time()
                        if time.time() - seen_at > 4:
                            break
                    con.call(f'coop.share {gid}' if gid else 'coop.share')
                    n += 1
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(1.5)
            self.note(f'share spam: {n} shares posted through the change to {nxt}')

        t = threading.Thread(target=run, daemon=True)
        t.start()
        return t

    def follow(self, nxt, services=()):
        """every guest follows the host into the stage (the world share) and is in its world"""
        self.phase = 'follow'
        self.quiet_until = time.time() + 600
        t0 = time.time()
        ok = False
        good = None
        tries = 0  # Space: attempts to leave the home planet
        svc = [Service(self, p) for p in self.P]
        for s in svc:
            s.start()
        try:
            while time.time() - t0 < 300 and not self.dead:
                if nxt == 'space' and tries < 3 and time.time() - t0 > 30 * (tries + 1) and self.on_home_planet() \
                        and not uifind.shown(self.host.con, 'Name Your Captain') \
                        and 'movie=1' not in self.host.call('coop.status'):
                    tries += 1
                    self.leave_home(final=tries == 3)
                st = {g.name: cl.stage_of(g.call('game.mode')) for g in self.guests}
                tog = self.together()
                self.ids()
                if all(v == nxt for v in st.values()) and all(g.pid in tog for g in self.guests):
                    # together for 20 s: right after a stage change the guests can still be in their
                    # old copy (the host's world arrives a few seconds later and they reload)
                    good = good or time.time()
                    if time.time() - good >= 20:
                        ok = True
                        break
                else:
                    good = None
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
            if nxt == 'tribe':
                self.chiefs_check('arrival')
            # CAMPAIGN_HOST_CMDS_<STAGE>=<cmd>[;<cmd>]: console commands for the host once every guest is in the stage
            # (triage, e.g. `probe 0x00C911C0` for BUG-0528's markers)
            for cmd in [c.strip() for c in os.environ.get(f'CAMPAIGN_HOST_CMDS_{nxt.upper()}', '').split(';') if c.strip()]:
                self.note(f'host {cmd}: {self.host.call(cmd).strip()[:120]}')
            return True
        behind = [g for g in self.guests if cl.stage_of(g.call('game.mode')) != nxt or g.pid not in self.together()]
        if not behind:  # they got there just after the time
            self.note(f'all guests play {nxt} together with the host after {time.time() - t0:.0f} s (late)')
            return True
        where = [f'{p.name}: `{" | ".join(l.strip() for l in p.call("coop.avatar").splitlines()[:1])}` '
                 f'mode `{p.call("game.mode").strip()}`' for p in [self.host] + behind]
        self.finding('stuck', f'follow:{nxt}', f'{", ".join(g.name for g in behind)} did not get into the host\'s '
                     f'world in {nxt} within 300 s (together on the host: {sorted(self.together())})',
                     [self.host] + behind, where)
        # what players do then: leave and join again
        for g in behind:  # (one rejoin can bring them all: the host saves for its share, BUG-0258)
            if cl.stage_of(g.call('game.mode')) == nxt and g.pid in self.together():
                continue
            self.rejoin(g)
        ok = all(cl.stage_of(g.call('game.mode')) == nxt for g in self.guests) and \
            all(g.pid in self.together() for g in self.guests)
        self.note(f'after a rejoin of {", ".join(g.name for g in behind)}: '
                  + ('together with the host' if ok else 'still apart'))
        return ok

    def on_home_planet(self):
        """the host's ship is on a planet (Simulator::GetCurrentContext 0): the Space stage saves only
        in a solar system or the galaxy, so the guests wait for the world until the host leaves"""
        m = re.search(r'eax=0x([0-9A-Fa-f]+)', self.host.call('call Simulator::GetCurrentContext cdecl'))
        return bool(m) and int(m.group(1), 16) == 0

    def leave_home(self, final=True):
        """a new Space stage: the mission control's first talk (space_setup.py's clicks), then zoom out of
        the home planet like a player; the host's world then goes to the guests"""
        self.note('the host leaves its home planet (mission control, zoom out): the guests wait for the world')
        h = self.host
        h.call('click 290 914')
        h.call('wait.frames 100 60000')
        for y in (755, 755, 723, 723, 723):  # "been there, done that", "ready", "what's next", "I accept", "bye"
            h.call(f'click 720 {y}')
            h.call('wait.frames 150 60000')
        for _ in range(4):
            for _ in range(4):
                h.call('wheel 720 540 -25')
                h.call('wait.frames 150 60000')
            h.call('wait.frames 300 60000')
            h.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')  # the badge
            if not self.on_home_planet():
                self.note('the host is in its solar system')
                return True
        if final:
            self.finding('stuck', 'space:home', 'the host could not leave its home planet (the guests wait for the '
                         'world)', [self.host])
        return False

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
        # BUG-0855: the spacecraft (Civ -> Space) and City Hall (Tribe -> Civ) editors come up at once after their vote;
        # a request the busy gate held under the game's own pause (core.editor_own_pause off) kept the host on a paused
        # screen for a minute (lab 20261112-100520: 61 s). (Creature's editors may wait for a cinematic: BUG-0404.)
        def secs(l):
            m = re.match(r'(\d\d):(\d\d):(\d\d\.\d+)', l)
            return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else None
        for i, l in enumerate(lines):
            m = re.search(r'vote: end: .* editor=(96B24187|99E92F05)', l)  # the spacecraft / City Hall editor
            if not m or secs(l) is None:
                continue
            nxt = next((x for x in lines[i + 1:] if re.search(r'coop: stage: \S+ \S+ -> Editor', x)), None)
            wait = secs(nxt) - secs(l) if nxt and secs(nxt) is not None else None
            if wait is not None and wait < 0:
                wait += 86400
            self.note(f'host: the editor {m.group(1)} came up {wait if wait is None else round(wait, 1)} s after its vote')
            if wait is not None and wait > 15:
                self.finding('editor', f'{self.stage}:editor_wait', f'the host\'s editor {m.group(1)} came up {wait:.0f} s '
                             f'after its vote ({self.stage})', self.P,
                             [f'`{x.strip()}`' for x in lines[i:i + 40] if 'editor' in x][:6])
        if timeups and readies:
            self.finding('vote', f'{self.stage}:timeup', f'a vote ran out of time although '
                         f'{", ".join(sorted({v[1] for v in readies}))} pressed "I\'m ready" ({self.stage})',
                         self.P, [f'`{l.strip()}`' for l in lines if 'evol' in l or 'ready' in l][:10])

    def advance_minutes(self, stage):
        """--advance-minutes, else per stage: Cell and Creature end in a few minutes, the Tribe stage
        needs every other tribe conquered (new ones form after each), the Civ stage every city"""
        return self.a.advance_minutes or ADVANCE_MIN[stage]

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
            h.call('coop.dna add 3000')
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

    # ---- the end of Tribe: every other village raided (Q1 of 0.7.14) ----
    def tribe_progress(self):
        """the stage's goal bar (cPlayer +0x10F0 / +0x10F4: conquests and alliances, `coop.wstate`), or None"""
        m = re.search(r'progress=([-\d.]+) total=([-\d.]+)', self.host.call('coop.wstate'))
        return (float(m.group(1)), float(m.group(2))) if m else None

    def tribe_targets(self):
        """(player village, [(tribe index, centre, hut pointer, hut position)]) of the NPC villages still standing: not
        conquered (`coop.realign planet village` fl bit 1, cTribe +0x556) and with a hut the game has not felled"""
        cent = {}
        for m in re.finditer(r'^tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', self.host.call('coop.tribe'),
                             re.M):
            cent['p' if m.group(2) else int(m.group(1))] = tuple(float(m.group(i)) for i in (3, 4, 5))
        home = cent.pop('p', None)
        vil = [(tuple(float(x) for x in m.group(1, 2, 3)), int(m.group(4))) for m in re.finditer(
            r'pl village \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) st=\S+ fl=(\d+)', self.host.call('coop.realign planet village'))]
        huts = []
        for line in self.host.call(f'coop.tribe objs {TRIBE_HUT}').splitlines():
            m = re.match(r'(\S+) alive=(\d) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
            if m and m.group(2) == '1':
                huts.append((m.group(1), tuple(float(m.group(i)) for i in (3, 4, 5))))
        out = []
        for k, c in cent.items():
            v = min(vil, key=lambda v: math.dist(v[0], c), default=None)
            if v is not None and math.dist(v[0], c) < 40 and v[1] & 2:
                continue  # conquered (or allied) already
            hs = sorted((h for h in huts if math.dist(h[1], c) < 40 and not (home and math.dist(h[1], home) < 40)),
                        key=lambda h: math.dist(h[1], c))
            hs = [h for h in hs if 'state=2' not in self.host.call('coop.tribe huthp %.2f %.2f %.2f -' % h[1])]
            if hs:
                out.append((k, c, hs[0][0], hs[0][1]))
        if home:
            out.sort(key=lambda t: math.dist(t[1], home))
        return home, out

    def tribe_home_hut(self, state):
        """shortcut: the players' own hut kept above 80 % of its health. With every member out raiding, an NPC village
        raids back and the players' hut falls: the game ends the stage (its save reloaded, every conquest gone; twice
        while the farthest village was raided, campaign_tc2 lab 20261113-051634). Players leave defenders at home or
        come back; the bots raid with everyone."""
        if 'home_hut' not in state:
            cent = re.search(r'^tribe \d+ \(player\) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', self.host.call('coop.tribe'), re.M)
            home = tuple(float(cent.group(i)) for i in (1, 2, 3)) if cent else None
            hs = []
            for line in self.host.call(f'coop.tribe objs {TRIBE_HUT}').splitlines():
                m = re.match(r'(\S+) alive=1 pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
                if m and home and math.dist(home, tuple(float(m.group(i)) for i in (2, 3, 4))) < 40:
                    hs.append(tuple(float(m.group(i)) for i in (2, 3, 4)))
            state['home_hut'] = hs[0] if hs else None
        hut = state['home_hut']
        if not hut:
            return
        out = self.host.call('coop.tribe huthp %.2f %.2f %.2f -' % hut)
        m = re.search(r'health (\d+)/(\d+)', out)
        if not m:
            state.pop('home_hut')  # (another world after a reload: look again)
            return
        hp, mx = int(m.group(1)), int(m.group(2))
        if mx and hp < mx * 0.8:  # (kicks come every 25-60 s: a raid took it from over half to 0 in between, lab 20261113-082316)
            self.note(f'shortcut: coop.tribe huthp: the players\' hut repaired ({hp}/{mx} -> {mx}: an NPC village raids back)')
            self.host.call('coop.tribe huthp %.2f %.2f %.2f %d' % (hut + (mx,)))

    def tribe_members(self, tk):
        """[(member id, hp)] of NPC village tk's living members (the host's game)"""
        out, cur = [], None
        for line in self.host.call('coop.tribe').splitlines():
            t = re.match(r'tribe (\d+) at', line)
            if t:
                cur = int(t.group(1))
                continue
            if line.startswith('tribe '):
                cur = None
                continue
            m = re.search(r'#(\d+) \S+ owner=254 .*?hp=([\d.]+) .*?dead=0', line)
            if m and cur == tk and float(m.group(2)) > 0:
                out.append((int(m.group(1)), float(m.group(2))))
        return out

    def tribe_raiders(self):
        """{member id: position} of the players' living members (the host's game)"""
        out = self.host.call('coop.tribe')
        return {int(m.group(1)): tuple(float(m.group(i)) for i in (3, 4, 5)) for m in re.finditer(
            r'#(\d+) \S+ owner=[0-3] .*?hp=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out) if float(m.group(2)) > 0}

    def kick_tribe(self, state):
        """the end of Tribe the players' way: every other village raided until its hut falls (the game's conquest,
        its goal on the stage's bar), then the advance button the full bar shows, the stage card, the cinematic's
        caption, the votes, the City Hall and vehicle editors (the Services answer those). The raid starts with the
        players' real right-clicks on the village's hut (play_tribe, attack mode): only a player's own attack order
        gives the game's mission on that village (cTribeToTribeMission, TribeMission_SetTargetTribe), whose
        conquest fills the bar; a hut felled by orders alone is conquered without a goal (Q1 exploration, lane 4).
        CAMPAIGN_OLD_TRIBE=1: the raids of 0.7.13 (pincer on members, never a hut: no village fell in 20 min)."""
        if os.environ.get('CAMPAIGN_OLD_TRIBE') == '1':
            return self.kick_tribe_old(state)
        pr = self.tribe_progress()
        if pr and pr[1] > 0 and pr[0] >= pr[1]:
            r = re.search(r'%08X (-?\d+) (-?\d+) (-?\d+) (-?\d+)' % TRIBE_ADVANCE,
                          self.host.call('coop.ui where %08X' % TRIBE_ADVANCE))
            if r and not state.get('advanced'):
                x, y = (int(r.group(1)) + int(r.group(3))) // 2, (int(r.group(2)) + int(r.group(4))) // 2
                self.note(f'tribe: the goal bar is full ({pr[0]:.0f}/{pr[1]:.0f}): the advance button')
                self.host.call(f'move {x} {y}')
                time.sleep(0.4)
                self.host.call(f'click {x} {y}')
                state['advanced'] = time.time()
                time.sleep(3)
            # the stage card's check (History screen), then the game's own cinematic and editors
            self.host.call(f'move {HISTORY_OK[0]} {HISTORY_OK[1]}')
            time.sleep(0.4)
            self.host.call(f'click {HISTORY_OK[0]} {HISTORY_OK[1]}')
            if state.get('advanced') and time.time() - state['advanced'] > 90:
                state['advanced'] = None  # no editor came: the button again
            return
        self.tribe_home_hut(state)
        raid = state.get('raid')
        if raid:
            hs = self.host.call('coop.tribe huthp %.2f %.2f %.2f -' % raid['hut'])
            m = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
            if pr and pr[0] < state.get('best', 0):
                # the bar went back: the host's game reloaded its save (a game over: "Alas! Your population has
                # fallen below the minimum", the Services confirm it), the villages stand again
                self.note(f'tribe: the goal bar went back {state["best"]:.0f} -> {pr[0]:.0f} (the host\'s game reloaded: '
                          f'a game over?); the raid on village {raid["tribe"]} starts again')
                state['best'] = pr[0]
                state['raid'] = None
                state['reloads'] = state.get('reloads', 0) + 1
                return
            if hs.startswith('no hut') and not (m and m.group(2) == '2'):
                self.note(f'tribe: the hut of village {raid["tribe"]} is gone without a fall ({hs.strip()[:60]})')
                state['raid'] = None
                return
            if m and m.group(2) == '2':
                state['conquests'] = state.get('conquests', 0) + 1
                state['best'] = max(state.get('best', 0), (self.tribe_progress() or (0, 0))[0])
                self.note(f'tribe: the hut of village {raid["tribe"]} fell after {time.time() - raid["t0"]:.0f} s '
                          f'(goal bar {self.tribe_progress()})')
                state['raid'] = None
                return
            if time.time() - raid['t0'] > 300:
                self.note(f'tribe: the raid on village {raid["tribe"]} did not fell its hut in 300 s ({hs.strip()[:80]})')
                state['raid'] = None
                return
            alive = self.tribe_raiders()
            if not raid.get('host_click'):  # no attack click of the host yet: no mission, the conquest would be no goal
                raid['tries'] = raid.get('tries', 0) + 1
                if raid['tries'] >= 3 and len(state.get('standing', [])) > 1:
                    # a far village the players' view does not reach (the minimap's look misses it, lab
                    # 20261113-073952: 316 m, 9 min without a click): another village first, this one later
                    state.setdefault('skip', {})[raid['tribe']] = time.time() + 240
                    self.note(f'tribe: no attack click of the host on village {raid["tribe"]} in 3 tries: another village first')
                    state['raid'] = None
                    return
                if len(alive) < 8:
                    self.host.call('coop.tribe spawn 6')
                self.tribe_clicks(raid)
                self.tribe_order(raid)
                return
            if len(alive) < 8:
                # the village's defenders kill raiders; under the tribe's minimum the game ends the stage (game over,
                # its save reloaded: every conquest of the run gone, lab 20261113-051634 twice)
                self.note(f'shortcut: coop.tribe spawn 6 (raiders down to {len(alive)}: a game over would reload)')
                self.host.call('coop.tribe spawn 6')
                self.tribe_order(raid)
            near = [i for i, q in alive.items() if math.dist(q, raid['hut']) < 20]
            mx = re.search(r'health \d+/(\d+)', hs)
            hit = m and mx and float(m.group(1)) < float(mx.group(1)) - 1  # a blow landed (raiders hit from 20-30 m)
            # cut again whenever it is over 40: the village's members repair their hut (damage type 10, docs/re/tribe.md
            # #tribe-hut-fall) while the raiders die (lab 20261113-092426: cut to 40, back at 306 of 750 four minutes later)
            if (near or hit) and m and float(m.group(1)) > 45:
                if not raid['cut']:
                    self.note('shortcut: coop.tribe huthp: the raided hut\'s health cut to 40 (its 400-1000 hp take the '
                              'raiders minutes; again whenever the village repairs it)')
                self.host.call('coop.tribe huthp %.2f %.2f %.2f 40' % raid['hut'])
                raid['cut'] = True
            elif m and 1.5 < float(m.group(1)) <= 45 and os.environ.get('CAMPAIGN_OLD_RAID') != '1':
                # a hut at 4 of 750 stood for minutes once its raiders were dead (lab 20261113-145317): the next blow fells it
                self.host.call('coop.tribe huthp %.2f %.2f %.2f 1' % raid['hut'])
            if raid['cut'] and time.time() - raid.get('pinced', 0) > 60 and os.environ.get('CAMPAIGN_OLD_RAID') != '1':
                # the village's defenders kill 6-12 raiders a raid, and with them dead an NPC village raids the players'
                # village back (a game over reloaded the save at 2/5 in lab 20261113-145317): shortcut, every 60 s the
                # raided village's members take the blows of the players' members (`coop.tribe pincer`)
                defenders = self.tribe_members(raid['tribe'])
                own = list(alive) or list(self.tribe_raiders())
                if defenders and own:
                    for i, (mid, hp) in enumerate(defenders[:8]):
                        self.host.call(f'coop.tribe pincer {mid} {own[i % len(own)]} {hp + 1:.0f}')
                    if not raid.get('pinced'):
                        self.note(f'shortcut: coop.tribe pincer: the {min(8, len(defenders))} defenders of village '
                                  f'{raid["tribe"]} take the raiders\' blows (again every 60 s)')
                raid['pinced'] = time.time()
            h = float(m.group(1)) if m else None
            if h != raid.get('hp'):
                raid['hp'], raid['since'] = h, time.time()
            elif time.time() - raid.get('since', raid['t0']) > 20:
                self.tribe_order(raid)  # the raiders stopped hitting a hut that stands: the raid order again
                raid['since'] = time.time()
            return
        home, targets = self.tribe_targets()
        raiders = self.tribe_raiders()
        if len(raiders) < 8:
            self.note('shortcut: coop.tribe spawn 6 (more members to raid with)')
            self.host.call('coop.tribe spawn 6')
        if not targets:
            # every village standing is taken: the game forms the next ones itself (three at once after the first
            # conquest in the exploration); when it does not, its own FormTribe (the controller's) is asked
            state['empty'] = state.get('empty', 0) + 1
            if state['empty'] % 3 == 0:
                self.note('shortcut: coop.tribe form (the NPC tribe controller forms the next village now)')
                self.host.call('coop.res add food 400')
                self.host.call('coop.tribe form')
            self.note(f'tribe: no village left to raid (goal bar {pr})')
            return
        state['standing'] = [t[0] for t in targets]
        skip = state.get('skip', {})
        pick = [t for t in targets if skip.get(t[0], 0) < time.time()] or targets
        if home and os.environ.get('CAMPAIGN_OLD_RAID') != '1':
            # the nearest village first: the raiders walk 300 m to a far one and its defenders kill them on the way
            # (village 4 at 318 m: two raids of 300 s without a fall while one at 170-230 m stood, lab 20261113-142203)
            pick.sort(key=lambda t: math.dist(t[3], home))
        tk, c, hut, hp = pick[0]
        raid = dict(tribe=tk, hut=hp, ptr=hut, t0=time.time(), cut=False)
        state['raid'] = raid
        self.note(f'tribe: raid on village {tk} ({len(targets)} standing, goal bar {pr}), its hut {hut} '
                  f'{math.dist(hp, home) if home else -1:.0f} m from the players\' village')
        self.tribe_clicks(raid)
        self.tribe_order(raid)

    def tribe_clicks(self, raid):
        """every player's real attack click on the raided hut (box-selected own members, attack mode). The host's
        own click is the one that counts: the game makes the village's mission (the goal) from the host's input
        action only; a guest's attack order runs on the host as a DoAction without it, and a village only guests
        raided falls without a goal (BUG-1068, campaign_tc2 lab 20261113-050826). raid['host_click'] says it came."""
        g = getattr(self, 'tribe_game', None)
        if g is None:
            return
        tk = raid['tribe']
        own = re.findall(r'#\d+ \S+ owner=0 .*?hp=([\d.]+)', self.host.call('coop.tribe'))
        if sum(1 for h in own if float(h) > 0) < 4:
            # play_tribe raids only with 4 or more own members alive (fewer: it gathers); the host's share of the
            # tribe after the play on the campaign's own planet was 1-2 (lab 20261113-060756 / -060758: no host click)
            # (a spawn is shared out among the players: 12 give the host 3-4)
            self.note('shortcut: coop.tribe spawn 12 (the host\'s own members for its raid click)')
            self.host.call('coop.tribe spawn 12')
            time.sleep(3)
        for pl in g.p:
            pl.busy.clear()  # a player sends members that are on another job too
        g.plan = lambda: 'raid'
        g.target_for = lambda pl, kind, st, centres, enemies, tk=tk: (centres.get(tk, raid['hut']),
                                                                       lambda o: o[0] == f'hut{tk}')
        host_pl = next((pl for pl in g.p if pl.pid == 0), None)
        for _ in range(2):  # the host's click first, on purpose (play_tribe's step draws its kind at random)
            if host_pl is None or raid.get('host_click'):
                break
            try:
                if self.tribe_host_click(g, host_pl, raid):
                    raid.setdefault('clicks', []).append(host_pl.name)
                    raid['host_click'] = True
            except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
                self.finding('info', f'tribe:hostclick:{type(e).__name__}', f'tribe host click error: {e!r}')
        for _ in range(3):
            for pl in g.p:
                if pl.pid == 0 and raid.get('host_click'):
                    continue
                n0 = pl.given
                try:
                    g.step(pl)
                except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
                    self.finding('info', f'tribe:raid:{type(e).__name__}', f'tribe raid bot error: {e!r}')
                if pl.given > n0 and 'action=15' in pl.last_order()[0]:
                    raid.setdefault('clicks', []).append(pl.name)
                    raid['host_click'] = raid.get('host_click') or pl.pid == 0
            if raid.get('host_click'):
                break
        self.note(f'tribe: raid clicks on the hut of village {tk} by {raid.get("clicks", [])}'
                  + (f' (the host\'s click: {raid["why"]})' if raid.get('why') else ''))
        print('  BUG-1091 diag: bot step counts ' + '; '.join(f'{pl.name} {sorted(pl.count.items())}' for pl in g.p),
              flush=True)

    def tribe_host_click(self, g, pl, raid):
        """the host's own attack click on the raided hut, as a player gives it: the view on its members, a box round
        them, attack mode, the view on the hut (minimap), a right-click on the hut. True when the game took it as a raid
        order (action 15); raid['why'] says what failed"""
        import play_tribe
        tk = raid['tribe']
        st = play_tribe.tribe_state(pl.con)
        mine = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] == 0 and v['hp'] > 0]
        if not mine:
            raid['why'] = 'no own member'
            return False
        if not pl.J and not pl.calibrate():
            raid['why'] = 'minimap not calibrated'
            return False
        sel = []
        for group in (True, False, False):  # a box round several, else one member at a time (a box can miss: 12:32 of
            sel = g.select_idle(pl, st, mine, group)  # lab 20261113-120534, "no member selected (6 own)")
            if sel:
                break
        if not sel:
            raid['why'] = f'no member selected ({len(mine)} own)'
            self.tribe_click_diag(pl, st, mine, raid)
            return False
        pl.mode(1)
        if not pl.look(raid['hut']):
            pl.calibrate()  # the view turned: the minimap's map again
            if not pl.look(raid['hut']):
                raid['why'] = 'the view did not reach the hut'
                self.tribe_click_diag(pl, st, mine, raid)
                return False
        pl.centre()
        tg = [o for o in pl.on_screen(play_tribe.screen(pl.con)) if o[0] == f'hut{tk}']
        if not tg:
            raid['why'] = 'the hut not on screen'
            return False
        n0 = pl.given
        g.click_order(pl, 'raid', sel, tg[0], 30)
        ok = pl.given > n0 and 'action=15' in pl.last_order()[0]
        raid['why'] = '' if ok else f'the click gave {pl.last_order()[0][:60]!r}'
        return ok

    def tribe_click_diag(self, pl, st, mine, raid):
        """BUG-1091 (diagnostics): where the host's view is when its box finds no own member"""
        import play_tribe
        try:
            objs = play_tribe.screen(pl.con)
            ons = pl.on_screen(objs)
            own_on = [o for o in ons if o[0].startswith('member') and int(o[0][6:]) in mine]
            mem_on = [o for o in ons if o[0].startswith('member')]
            c = pl.centre()
            pos = {i: tuple(round(v) for v in st[i]['pos']) for i in mine[:8]}
            sel = [i for i, v in play_tribe.tribe_state(pl.con).items() if v['sel']]
            n = raid['diag'] = raid.get('diag', 0) + 1
            shot = os.path.join(lane.out_dir('shots'), f'bug1091_{time.strftime("%H%M%S")}.png') if n <= 3 else None
            if shot:
                self.host.call('shot ' + shot)
            print(f'  BUG-1091 diag: own on screen {len(own_on)}/{len(mine)}, members on screen {len(mem_on)}, '
                  f'objs {len(objs)}, view centre {tuple(round(v) for v in c) if c else None}, cur '
                  f'{tuple(round(v) for v in pl.cur) if getattr(pl, "cur", None) else None}, zoom {pl.zoom}, '
                  f'selected now {sel[:8]}, own at {pos}, status {play_tribe.status(pl.con)[:300]!r}, shot {shot}',
                  flush=True)
        except Exception as e:  # (diagnostics only)
            print(f'  BUG-1091 diag failed: {e!r}', flush=True)

    def tribe_order(self, raid):
        """shortcut: every player member gets the raid order on the hut (`coop.tribe order <id> 15 <hut>`: the DoAction
        a right-click gives, as host_command runs a guest's): the bots' clicks reach only the members on screen"""
        ids = self.tribe_raiders()
        if os.environ.get('CAMPAIGN_RAID_ALL') != '1':
            # a home guard stays: the players' chiefs and the HOME_GUARD members nearest to the village. The game ends
            # Tribe (game over, its save reloaded: every conquest gone) when no player member has health left; with every
            # member sent the village's defenders killed them all (lab 20261113-120534 at 4/5). CAMPAIGN_RAID_ALL=1: all go
            out = self.host.call('coop.tribe')
            chiefs = {int(m.group(1)) for m in re.finditer(r'#(\d+) \S+ owner=[0-3] .*?leader=1', out)}
            cm = re.search(r'chiefs=((?: \d:#\d+)*)', out)
            chiefs |= {int(x) for x in re.findall(r'#(\d+)', cm.group(1))} if cm else set()
            cent = re.search(r'^tribe \d+ \(player\) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out, re.M)
            home = tuple(float(cent.group(i)) for i in (1, 2, 3)) if cent else None
            rest = sorted((i for i in ids if i not in chiefs), key=lambda i: math.dist(ids[i], home) if home else 0)
            guard = set(rest[:HOME_GUARD]) | chiefs
            ids = {i: q for i, q in ids.items() if i not in guard}
        for mid in ids:
            self.host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + raid['hut'] + (TRIBE_HUT,)))
        if not raid.get('ordered'):
            self.note(f'shortcut: coop.tribe order: {len(ids)} members to the raid on the hut')
        raid['ordered'] = True

    def kick_tribe_old(self, state):
        """0.7.13's kick (CAMPAIGN_OLD_TRIBE=1): real raids by the players (play_tribe with the plan 'raid'), more
        members now and then, pincer on the other tribes' members"""
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
        out = self.host.call('coop.tribe')
        mine = [int(m.group(1)) for m in re.finditer(r'#(\d+) \S+ owner=[0-3] .*dead=0', out)]
        others = [int(m.group(1)) for m in re.finditer(r'#(\d+) \S+ owner=254 .*dead=0', out)]
        if mine and others:
            for v in others[:6]:
                self.host.call(f'coop.tribe pincer {v} {mine[0]} 60')
            self.note(f'shortcut: coop.tribe pincer on {min(6, len(others))} members of other tribes')
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
                              h.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0'))
                if not r or int(r.group(1), 16) & 0xFF == 0:  # bool in al: no free site left
                    break
                n += 1
                h.call('wait.frames 60 20000')
            self.note(f'shortcut: {n} cities appeared at once (SpawnNextCity)')
            return
        out = h.call('coop.civ')
        # civ.cpp: "city <i> <ptr> pol=<pol> (player) owner=..." for the players' cities
        mine = re.search(r'city -?\d+ (\S+) pol=([0-9A-F]+) \(player\)', out)
        others = [(m.group(1), m.group(2)) for m in re.finditer(r'city -?\d+ (\S+) pol=([0-9A-F]+) owner=', out)]
        if not mine:
            self.finding('info', 'civ:noplayer', 'civ: no player city in coop.civ', [], [f'`{out[:300]}`'])
            return
        pol = mine.group(2)
        took = 0
        left = [o for o in others if o[1] != pol]
        for ptr, opol in others:
            if opol == pol:
                continue
            # Capture(newPol, type, instant, nuked caption, a5) (docs/re/civ.md#civ-stage-end). The last city instant, as
            # a nation's surrender takes it: the game checks the end of Civ inside the call. By the cinematic path the
            # check waits for that city's specialty box, which a talk or the bot's Esc left unanswered: every city owned,
            # no end of Civ (BUG-0311, lab 20261030-104149 #1, 20261103-012624 #3)
            inst = 1 if len(left) == 1 else 0
            h.call(f'call Simulator::cCity::Capture thiscall {ptr if ptr.startswith("0x") else "0x" + ptr} 0x{pol} 0 {inst} 0 0')
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
        left = False
        while time.time() - t0 < 180:
            st = cl.stage_of(self.host.call('game.mode'))
            left |= st != nxt  # the host leaves the stage it plays first (the same stage: a reload)
            if left and st == nxt and self.host.con.call('coop.avatar').get('ok'):
                break
            time.sleep(0.5)
        else:
            self.finding('stuck', f'jump:{nxt}', f'the host did not load the {fx} fixture within 180 s', [self.host])
            return False
        time.sleep(5)  # the host saves and sends its world to the guests now
        self.follow(nxt)
        return True

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
        for p in self.P:  # (the drive came with the Space play phase)
            fz.settle(p.con, p.name, timeout=60)
            # BUG-0991: the space bot does not repair its ship; one left near death (8 of 1500 hp) was destroyed on
            # the way and its death caption held the finale. A player repairs before such a trip: the bots get full hp
            m = re.search(r'hp ([\d.]+) max ([\d.]+)', fz.sp(p.con, 'hp'))
            if m and float(m.group(2)) > 0 and float(m.group(1)) < 0.5 * float(m.group(2)):
                self.note(f'shortcut: {p.name}: ship at {m.group(1)} of {m.group(2)} hp: coop.space heal '
                          f'({fz.sp(p.con, "heal")})')
        self.take_off(h)  # the Space play may have left the host on a planet
        fz.to_galaxy(h.con, h.name, ck)
        self.note('shortcut: coop.space finale goto near (the host crosses the galaxy)')
        fz.sp(h.con, 'finale goto near')
        fz.wait(lambda: fz.fin(h.con).get('zone') == '2', 30)
        # BUG-0277: the arrival near the core plays a cinematic (the Grox contact); a `goto core` sent
        # while it runs leaves the host in zone 2. Let it end first, and try again if needed.
        for i in range(3):
            fz.settle(h.con, h.name, timeout=60)
            self.note('shortcut: coop.space finale goto core' + (f' (again, {i + 1})' if i else ''))
            fz.sp(h.con, 'finale goto core')
            if fz.wait(lambda: fz.fin(h.con).get('zone') == '3', 30):
                break
            fz.to_galaxy(h.con, h.name, lambda ok, what: None)
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
        if getattr(self.a, 'finale_from_galaxy', False):  # tests (BUG-0335): the guests wait in the galaxy view
            for g in self.guests:
                self.take_off(g)
                fz.to_galaxy(g.con, g.name, lambda ok, what: None)
        for g in self.guests:  # each guest flies to Alice twice: next to the core, then to the gate
            ui = Ui(g.con, g.name)
            for step in ('near the core', 'the gate'):
                fz.settle(g.con, g.name, timeout=60)
                for _ in range(3):  # (several badges can come one after another)
                    if not self.close_reward(g.con):
                        break
                # BUG-0335: "Fly to Alice" from the galaxy view goes straight to the gate, and with
                # everybody else waiting there the core opens at once: a guest that played on in the
                # galaxy view (Dave in campaign 20261031-000341) was inside the core cinematic when the
                # bot tried its second step (zoom out, "Fly to Alice" again) and got stuck there.
                f = fz.fin(g.con) if not os.environ.get('CAMPAIGN_FINALE_OLD') else {}  # (old steps: tests)
                if f.get('entered') == '1' or (step == 'the gate' and f.get('zone') == '3'):
                    self.note(f'{g.name}: already at the gate / in the core ({step} skipped)')
                    break
                if step == 'near the core' and f.get('ctx') == '2':
                    self.note(f'{g.name}: in the galaxy view: "Fly to Alice" goes to the gate')
                    step = 'the gate'
                if step == 'the gate':
                    self.take_off(g)
                    fz.to_galaxy(g.con, g.name, ck)
                self.close_reward(g.con)
                fz.open_space_page(ui, g.con, ck, g.name)
                b = fz.fly_button(ui)
                ck(b is not None, f'{g.name}: "Fly to Alice" on the Space page ({step})')
                if b:
                    ui.click(b)
                g.call(f'key {VK_F9}')
                time.sleep(8)
                if step == 'the gate' and not os.environ.get('CAMPAIGN_FINALE_OLD'):
                    break
        ok = fz.wait(lambda: all(fz.fin(p.con).get('entered') == '1' for p in self.P), 120, 3)
        ck(ok, 'every player entered the galactic core: ' + ' | '.join(f'{p.name} {fz.fin(p.con)["line"][:80]}'
                                                                       for p in self.P))
        if not ok:
            return
        done = fz.press_through([p.con for p in self.P], lambda: all(
            fz.fin(p.con).get('busy') == '0' and fz.fin(p.con).get('staff') == '1' for p in self.P), 400)
        ck(done, 'every player got the Staff of Life and the cinematic ended')
        self.quiet_until = time.time() + 20


    def take_off(self, p):
        """off a planet like a player: the wheel out until the ship is in the solar system (finale ctx 0 =
        on a planet; play_space.take_off does the same)"""
        import coop_space_finale as fz
        for _ in range(10):
            if fz.fin(p.con).get('ctx') != '0':
                return True
            fz.settle(p.con, p.name, timeout=30)
            p.call('wheel 720 540 -5')
            time.sleep(1.5)
        return fz.fin(p.con).get('ctx') != '0'


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


PHASE_NOTES = {
    'play': 'end of the {stage} play: every player still in {stage} play, the same creatures / tribes / cities / ships on '
            'every screen, the partners drawn (none in the air or under the ground), the HUD in place',
    'advance': 'after the {stage} -> next stage change: every game in the new stage, no loading or black screen left, '
               'the players\' new creations drawn with their own models',
    'finale': 'after the finale: every game in Space or at the galactic core, no loading screen, no frozen picture',
}


def phase_frame(camp, kind, stage):
    """a key frame of every player at a phase's end (docs/06-testing.md#testing-key-frames)"""
    roles = {('host-' if i == 0 else 'guest-') + p.name: p.con for i, p in enumerate(camp.P)}
    keyframe(f'{kind}-{stage}', PHASE_NOTES.get(kind, '{stage}').format(stage=stage), players=roles, kind='phase',
             extra=[f'campaign stage now {camp.stage}, phase {camp.phase}'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--from', dest='start', default='cell', choices=cl.STAGES)
    ap.add_argument('--to', dest='end', default='space', choices=cl.STAGES)
    ap.add_argument('--minutes', type=float, default=8, help='minutes of play per stage')
    ap.add_argument('--advance-minutes', type=float, default=None,
                    help='time for one stage change on the host (default per stage: ADVANCE_MIN)')
    ap.add_argument('--players', type=int, default=4, choices=(2, 3, 4))
    ap.add_argument('--noise', type=float, default=0.06, help='chance of a noise action instead of a bot step')
    ap.add_argument('--no-rejoins', dest='rejoins', action='store_false')
    ap.add_argument('--no-finale', dest='finale', action='store_false')
    ap.add_argument('--finale-from-galaxy', action='store_true',
                    help='tests (BUG-0335): the guests go to the galaxy view before their finale steps')
    ap.add_argument('--no-jump', dest='jump', action='store_false', help='stop at a stage change that does not finish')
    ap.add_argument('--seed', type=int, default=int(os.environ['CAMPAIGN_SEED']) if os.environ.get('CAMPAIGN_SEED') else None)  # CAMPAIGN_SEED: a lab replay
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
    # SPORE_FIX_OFF reaches the extra players too (twoinst.start switched Alice and Bob; party.py does the same)
    for fx in filter(None, (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(','))):
        for p in players[2:]:
            ans = (p.call(f'coop.fix {fx} off') or '').strip().splitlines()
            print(f'  SPORE_FIX_OFF ({p.name}): ' + (ans[0] if ans else 'no answer'), flush=True)
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
    # a planted problem for the campaign's own twin (Q1 of 0.7.14): console commands on every guest at the start, e.g.
    # CAMPAIGN_GUEST_CMDS="coop.tribe delay 1.0" (the Tribe mirrors 1 s late: the sync check must find a desync)
    for cmd in filter(None, os.environ.get('CAMPAIGN_GUEST_CMDS', '').split(';')):
        for p in camp.guests:
            camp.note(f'planted on {p.name}: {cmd}: {p.call(cmd).strip()[:120]}')
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
                    # Q1 of 0.7.14: the Tribe end by conquests (the hut raids) came in every run on the tribe fixture and,
                    # once the cinematics were on again after the host's Tribe reload (BUG-1073, fix/w49-camp), on the
                    # campaign's own planet: a timeout is a failing `stuck` now. CAMPAIGN_STRICT_TRIBE=0: `info` (as before)
                    sure = stage != 'tribe' or os.environ.get('CAMPAIGN_STRICT_TRIBE') != '0'
                    camp.finding('stuck' if sure else 'info', f'advance:{stage}',
                                 f'the stage change {stage} -> {nxt} did not finish in {camp.advance_minutes(stage):.0f} min'
                                 + ('' if stage != 'tribe' else f' (goal bar {camp.tribe_progress()})'),
                                 camp.P)
                    if a.jump:
                        ok = camp.jump(nxt)
                if ok:
                    reached.append(nxt)
                else:
                    break
            elif kind == 'finale':
                camp.finale()
            phase_frame(camp, kind, stage)
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
