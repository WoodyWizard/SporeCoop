"""Pure logic of the co-op campaign (tools/tests/coop_campaign.py): no game, no console, no lane.
Tested by tools/tests/tools_selftest.py (check `campaign`, in make test).

  STAGES / plan()        the stage order, the phases of a run from a stage (resume)
  stage_of()             the stage a `game.mode` answer names (Editor / Loading / Menu are no stage)
  faults_of()            the count of `exc`'s first line
  res_mismatch()         shared food / money that differ between the host and a guest
  census_problems()      what a guest's census shows differently from the host's, only what the
                         guest is meant to have (the host streams NPCs within ~110 m of each guest)
  Persist                a problem counts once it is seen in N checks in a row (a churn is no desync)
  Findings               the findings file: one JSON line and one markdown block per finding,
                         repeats of the same key are counted, not written again
  new_reports()          crash / hang report files of the run's instances that appeared since the start
"""
import json
import math
import os
import re
import time

STAGES = ['cell', 'creature', 'tribe', 'civ', 'space']
# the fixture each stage starts from when a run is resumed there (tools/tests/fixtures.py)
FIXTURE = {'cell': 'cell', 'creature': 'creature', 'tribe': 'tribe', 'civ': 'civ_npc', 'space': 'space'}
# game.mode names (`0x01654C01 Creature`) of the stages
MODE = {'Cell': 'cell', 'Creature': 'creature', 'Tribe': 'tribe', 'Civ': 'civ', 'Space': 'space'}


def plan(start='cell', end='space', finale=True):
    """[(kind, stage)] of a run: ('play', s) for every stage from start to end, ('advance', s) between
    two of them (the stage change the players' way), ('finale', 'space') at the end of the game"""
    a, b = STAGES.index(start), STAGES.index(end)
    if a > b:
        raise ValueError(f'start {start} is after end {end}')
    out = []
    for i in range(a, b + 1):
        out.append(('play', STAGES[i]))
        if i < b:
            out.append(('advance', STAGES[i]))
    if finale and end == 'space':
        out.append(('finale', 'space'))
    return out


def stage_of(mode_text):
    """'0x01654C02 Tribe' -> 'tribe'; editors, loads and menus -> None"""
    for word in re.findall(r'[A-Za-z]+', mode_text or ''):
        for name, st in MODE.items():
            if word == name:
                return st
    return None


def faults_of(exc_text):
    m = re.search(r'total faults: (\d+)', exc_text or '')
    return int(m.group(1)) if m else None


def res_of(text):
    """`coop.res` -> {'food': 12.0, 'money': 3000.0}"""
    return {m.group(1): float(m.group(2)) for m in re.finditer(r'shared (\w+)=(-?[\d.]+)', text or '')}


def res_mismatch(host, guest, rel=0.02, absolute=2.0):
    """[(name, host value, guest value)] of the shared resources that differ more than the tolerance
    (an income tick between the two reads is within it)"""
    out = []
    for k, v in sorted(host.items()):
        g = guest.get(k)
        if g is None:
            out.append((k, v, None))
        elif abs(g - v) > max(absolute, abs(v) * rel):
            out.append((k, v, g))
    return out


def census_problems(host, guest, near=90.0, pos_m=10.0):
    """problems of one guest's census (census.Census) against the host's taken right before it:
    host NPCs near the guest's own avatar that the guest lacks, proxies the host does not have,
    alive / dead and owner differences, positions more than pos_m apart. [] when not comparable."""
    if host.modeid != guest.modeid or host.world != guest.world:
        return [f'not in the same world: host {host.mode} {host.world}, guest {guest.mode} {guest.world}']
    me = next((r['pos'] for r in guest.rows if r['own'] == 'avatar'), None)
    out = []
    for kind, hown, gown in (('c', ('npc',), ('proxy',)), ('v', ('host',), ('mirror',))):
        h, g = host.by_id(kind, hown), guest.by_id(kind, gown)
        for i in sorted(set(h) - set(g)):
            if kind == 'c' and (me is None or math.dist(h[i]['pos'], me) > near):
                continue  # not streamed to this guest
            out.append(f'{kind}{i}: the host has it, the guest not ({h[i]["own"]} {h[i]["key"]})')
        for i in sorted(set(g) - set(h)):
            out.append(f'{kind}{i}: the guest has it, the host not ({g[i]["own"]} {g[i]["key"]})')
        for i in sorted(set(h) & set(g)):
            a, b = h[i], g[i]
            if a['alive'] != b['alive']:
                out.append(f'{kind}{i}: alive={a["alive"]} on the host, {b["alive"]} on the guest')
            if kind == 'v' and a['owner'] != b['owner']:
                out.append(f'{kind}{i}: owner {a["owner"]} on the host, {b["owner"]} on the guest')
            d = math.dist(a['pos'], b['pos'])
            if d > pos_m:
                out.append(f'{kind}{i}: {d:.0f} m apart')
    return out


class Persist:
    """a problem key counts once it was seen in `n` checks in a row; a check without it resets it"""

    def __init__(self, n=2):
        self.n = n
        self.seen = {}

    def update(self, keys):
        keys = set(keys)
        self.seen = {k: self.seen.get(k, 0) + 1 for k in keys}
        return sorted(k for k, c in self.seen.items() if c == self.n)  # once, when it gets confirmed


class Findings:
    """findings.jsonl + findings.md in the run folder. add() returns True for a new key (the caller
    then takes screenshots and log tails); a repeat only counts."""

    def __init__(self, folder, clock=time.time):
        self.folder = folder
        self.clock = clock
        self.t0 = clock()
        self.keys = {}
        self.items = []
        os.makedirs(folder, exist_ok=True)
        self.md = os.path.join(folder, 'findings.md')
        self.jl = os.path.join(folder, 'findings.jsonl')
        if not os.path.exists(self.md):
            with open(self.md, 'w') as f:
                f.write('# Campaign findings\n\n')

    def add(self, kind, key, text, stage, players, evidence=None):
        k = f'{kind}:{key}'
        if k in self.keys:
            self.keys[k]['repeats'] += 1
            return False
        t = self.clock()
        item = dict(kind=kind, key=key, text=text, stage=stage, players=list(players),
                    time=time.strftime('%H:%M:%S', time.localtime(t)), t=round(t - self.t0, 1),
                    evidence=list(evidence or []), repeats=0)
        self.keys[k] = item
        self.items.append(item)
        with open(self.jl, 'a') as f:
            f.write(json.dumps(item) + '\n')
        with open(self.md, 'a') as f:
            f.write(f'## {len(self.items)}. [{kind}] {text}\n\n* time {item["time"]} (+{item["t"]:.0f} s), '
                    f'stage {stage}, players {", ".join(players) or "-"}\n')
            for e in item['evidence']:
                f.write(f'* {e}\n')
            f.write('\n')
        return True

    def attach(self, kind, key, evidence):
        """more evidence for a finding already written (screenshots taken after it)"""
        k = f'{kind}:{key}'
        if k in self.keys:
            self.keys[k]['evidence'] += list(evidence)
            with open(self.md, 'a') as f:
                for e in evidence:
                    f.write(f'* (finding {self.items.index(self.keys[k]) + 1}) {e}\n')
                f.write('\n')

    def summary(self):
        by = {}
        for it in self.items:
            by[it['kind']] = by.get(it['kind'], 0) + 1
        return by


REPORT = re.compile(r'^(crash|report|hang)-(\d{8}-\d{6})-i(\d+)\.txt$')


def new_reports(files, since, instances):
    """report files (crash-YYYYmmdd-HHMMSS-iN.txt, report-..., hang-...) of the given instances
    written at or after `since`. files: [(name, mtime)]. The time in the name is the game's (Wine's)
    local time, which need not be this machine's: the file time decides (2 s for the coarse clock)."""
    out = []
    for n, mtime in sorted(files):
        m = REPORT.match(n)
        if m and int(m.group(3)) in instances and mtime >= since - 2:
            out.append(n)
    return out


def timeouts(minutes_per_stage):
    """per phase: how long a stage is played, how long the stage change may take on the host and
    how long the guests may take to follow (a world load each)"""
    return dict(play=minutes_per_stage * 60, host_advance=40 * 60, follow=240, join=180)
