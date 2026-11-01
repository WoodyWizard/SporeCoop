#!/usr/bin/env python3
"""Continuous invariants while a test plays (docs/06-testing.md#testing-monitor).

A thread follows every game of the test (host, guest, the extra guests of a party): it reads the
new lines of each game's log every few seconds (the mod's `trace:` lines, written every 10 s on
every machine with the session clock `[s T]` that all games of a session share) and polls a few
console commands (`coop.status`, `coop.census` counts), then evaluates the invariants below over
the shared timeline. A violation is printed at once as

    MONITOR-VIOLATION <invariant> <who>: <text>

and tools/lab.py fails the test on it (gate `monitor`, allowed per test in suite.py with
allow('monitor', ticket, reason, kind='<invariant>')). Invariants (each needs its state for a while,
a moment between two reads is no violation):

  together      two machines whose own trace lines say the same stage and world for 20 s see each other
                in the same place (the partner line's same_place=1)
  puppet        a partner in the same place, alive and in a world, is shown (puppet=shown) on 2 trace
                lines in a row (not "not spawned (yet)" / "state older than 5 s")
  link          on a clean network (no SPORE_NET): resends / sent between two trace lines under 0.5,
                pending under 400 and the partner's state younger than 5 s, each on 3 lines in a row
  totals        the shared totals of the own trace lines (dna in Creature, food / money / ... of the
                stage) the same on the host and every guest in its world, within 1 point or 2 %, on
                3 comparisons in a row spanning 20 s
  hp            what a machine's partner line says of a player's hp / max hp (the state it received)
                equals what that player's own trace line says at the same moment, on 2 comparisons
  objects       the game's cell pool (tiles: ... cell pool N/M) stays under 90 % of its size, the
                census of each game (coop.census, every 30 s, live only) stays under the limits of
                OBJECT_LIMITS and does not grow past 3x its first count + 200
  stage_apart   a guest whose own stage / world differs from the host's for 90 s while both play a stage

Hooks for invariants that need new mod diagnostics (inactive until the mod writes the field; the
exact output each needs is in NEEDS and docs/06-testing.md#testing-monitor): drawn, ground,
puppet_hp, posse_damage, unlocks.

  monitor.py replay <run dir | game log ...>   the trace invariants over recorded logs
  monitor.py report [out/lab dirs]             the same over every recorded result (calibration)
"""
import collections
import os
import re
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

SESS = re.compile(r'\[s (-?[\d.]+)\]')
OWN = re.compile(r"trace: (host|guest) id=(\d+) players=(\d+) (.+?) world=([0-9A-F]+) '(.*?)' avatar=(yes|no) "
                 r"pos=\(([-\d.e+naif]+) ([-\d.e+naif]+) ([-\d.e+naif]+)\) hp=(-?[\d.naif]+)/(-?[\d.naif]+) species=(\S+) "
                 r"flags=0x([0-9A-Fa-f]+) dna=(-?[\d.]+)(.*?) host_away=(\d) vote=(\w+)(.*)$")
PARTNER = re.compile(r"trace:\s+partner (\d+) '(.*?)' (.+?) world=([0-9A-F]+) d=(\S+) same_place=(\d) puppet=(.*?) "
                     r"hp=(-?[\d.naif]+)/(-?[\d.naif]+) species=(\S+) flags=0x([0-9A-Fa-f]+) state_age=([\d.]+) s "
                     r"rtt=(-?\d+) ms(.*)$")
LINK = re.compile(r'resends=(\d+)/(\d+) silent=(\d+) ms pending=(\d+)')
POOL = re.compile(r'cell pool (\d+)/(\d+)')
KV = re.compile(r'(\w+)=(-?[\d.]+)(\(unsynced\))?')
STAGES = ('Cell', 'Creature', 'Tribe', 'Civilization', 'Space')
PF_IN_WORLD, PF_DEAD = 1, 16

DEFAULTS = dict(together_s=20.0, apart_s=90.0, link_ratio=0.5, link_min_sent=50, link_pending=400, link_age=5.0,
                link_lines=3, tot_abs=1.0, tot_rel=0.02, tot_n=3, tot_span=20.0, tot_dt=6.0, hp_n=2, hp_abs=1.0,
                pool_frac=0.9, census_growth=3.0, census_extra=200, period=5.0, census_period=30.0)
OBJECT_LIMITS = {'c': 4000, 'v': 800}
# what each hook invariant needs from the mod (a field appended at the END of the trace lines, after
# `pending=` / `editor_pending=`, so the parsers of today keep working, or a log line of its own)
NEEDS = {
    'drawn': "partner line: `drawn=(x y z)` = the puppet's position as the game draws it (the creature / "
             "vehicle object's own position read back from the game, not the mod's interpolation target); the monitor compares it with the partner's own `pos=` at the same session time (limit 3 m in "
             "Creature / Tribe, 6 m Civ, 12 m Space)",
    'ground': "own and partner lines: `agl=<m>` = height of the avatar / puppet above the terrain under it "
              "(terrain height query at its x/z); violation below -0.5 m or above 3 m while not jumping, flying, "
              "swimming or in Space for 2 lines",
    'puppet_hp': "partner line: `puppet_hp=<hp>/<max>` read from the puppet creature object itself (what the "
                 "player sees on the health bar); equal to the partner's own `hp=` (BUG of the audit: "
                 "modes.cpp pins puppet health to its max)",
    'posse_damage': "log line on the hit player's game: `damage: <player id> hit by <attacker kind> owner=<host|"
                    "guest id|npc> posse=<0|1> amount=<hp>`; violation: amount > 0 from an attacker of a player's "
                    "posse / pack / ally (npc.cpp MSG_DAMAGE forwarding)",
    'unlocks': "own trace line: `parts=<count>:<hash>` of the unlocked creature parts; violation: a guest's count "
               "below the host's 60 s after a part was unlocked in a shared world",
}


def num(x):
    try:
        return float(x)
    except ValueError:
        return float('nan')


# ---------------------------------------------------------------- records
def parse_line(who, line):
    """a record of one log line, None for other lines"""
    m = SESS.search(line)
    t = float(m.group(1)) if m else None
    if 'trace:' in line:
        o = OWN.search(line)
        if o:
            extra = o.group(16)
            res = {k: float(v) for k, v, uns in KV.findall(extra) if not uns}
            return dict(kind='own', who=who, t=t, role=o.group(1), id=int(o.group(2)), players=int(o.group(3)),
                        stage=o.group(4), world=o.group(5), avatar=o.group(7) == 'yes',
                        pos=(num(o.group(8)), num(o.group(9)), num(o.group(10))), hp=num(o.group(11)),
                        maxhp=num(o.group(12)), flags=int(o.group(14), 16), dna=float(o.group(15)), res=res,
                        away=o.group(17) == '1', vote=o.group(18) != 'off', rest=o.group(19))
        p = PARTNER.search(line)
        if p:
            lk = LINK.search(p.group(14))
            return dict(kind='partner', who=who, t=t, pid=int(p.group(1)), name=p.group(2), stage=p.group(3),
                        world=p.group(4), same=p.group(6) == '1', puppet=p.group(7), hp=num(p.group(8)),
                        maxhp=num(p.group(9)), flags=int(p.group(11), 16), age=float(p.group(12)),
                        rtt=int(p.group(13)), rest=p.group(14),
                        link=tuple(int(lk.group(i)) for i in range(1, 5)) if lk else None)
    if 'coop: damage: ' in line:  # a hook's own log line (NEEDS['posse_damage'])
        return dict(kind='hookline', who=who, t=t, rest=line)
    m = POOL.search(line)
    if m and 'tiles:' in line:
        return dict(kind='pool', who=who, t=t, n=int(m.group(1)), size=int(m.group(2)))
    return None


# ---------------------------------------------------------------- the invariants
class Timeline:
    """every record of every machine of the test, in arrival order"""

    def __init__(self):
        self.own = collections.defaultdict(list)       # who -> [own records]
        self.partner = collections.defaultdict(list)   # (viewer, pid) -> [partner records]
        self.ids = {}                                  # who -> session id (0 = host)
        self.records = []

    def add(self, r):
        self.records.append(r)
        if r['kind'] == 'own':
            self.own[r['who']].append(r)
            self.ids[r['who']] = r['id']
        elif r['kind'] == 'partner':
            self.partner[(r['who'], r['pid'])].append(r)

    def who_of(self, pid):
        return next((w for w, i in self.ids.items() if i == pid), None)

    def own_at(self, who, t, dt):
        """the own record of `who` closest to session time t (within dt), None"""
        best = None
        for r in reversed(self.own.get(who, ())):
            if r['t'] is None:
                continue
            if best is None or abs(r['t'] - t) < abs(best['t'] - t):
                best = r
            if r['t'] < t - dt:
                break
        return best if best is not None and abs(best['t'] - t) <= dt else None


def own_window(tl, who, t, dt):
    """the own records of `who` within session time t +- dt"""
    return [r for r in tl.own.get(who, ()) if r['t'] is not None and abs(r['t'] - t) <= dt]


def playing(r):
    """an own record of a machine that plays a stage (not a load, a menu, an editor, a vote)"""
    return r['stage'] in STAGES and r['avatar'] and not r['vote']


class Invariant:
    name = '?'
    needs = None  # a hook: the mod output it needs (NEEDS)

    def __init__(self, cfg):
        self.c = cfg
        self.told = set()

    def available(self, tl):
        return True

    def check(self, tl):
        """[(who, text, key)] of the violations seen so far (key: once per key)"""
        return []


class Together(Invariant):
    name = 'together'

    def check(self, tl):
        out = []
        for (viewer, pid), lst in tl.partner.items():
            subj = tl.who_of(pid)
            if subj is None:
                continue
            run = []
            for p in lst:
                if p['t'] is None:
                    continue
                a, b = tl.own_at(viewer, p['t'], 6), tl.own_at(subj, p['t'], 6)
                agree = a and b and playing(a) and playing(b) and a['stage'] == b['stage'] and a['world'] == b['world'] \
                    and (b['flags'] & PF_IN_WORLD)
                if agree and not p['same']:
                    run.append(p)
                    if len(run) >= 2 and run[-1]['t'] - run[0]['t'] >= self.c['together_s'] - 10:
                        out.append((viewer, f'{viewer} and {subj} play {a["stage"]} {a["world"]} by their own games, '
                                            f'but {viewer} sees {subj} elsewhere (same_place=0) for '
                                            f'{run[-1]["t"] - run[0]["t"]:.0f} s from s {run[0]["t"]:.0f}',
                                    (viewer, pid, run[0]['t'])))
                else:
                    run = []
        return out


class Puppet(Invariant):
    name = 'puppet'
    BAD = ('hidden: not spawned', 'hidden: state older than 5 s')

    def check(self, tl):
        out = []
        for (viewer, pid), lst in tl.partner.items():
            run = []
            for p in lst:
                if p['same'] and any(p['puppet'].startswith(b) for b in self.BAD) and (p['flags'] & PF_IN_WORLD) \
                        and not p['flags'] & PF_DEAD:
                    run.append(p)
                    if len(run) >= 2:
                        out.append((viewer, f'{viewer}: partner {pid} ({p["name"]}) in the same place is not shown '
                                            f'({p["puppet"]}) on {len(run)} trace lines from s {run[0]["t"]}',
                                    (viewer, pid, run[0]['t'])))
                else:
                    run = []
        return out


class Link(Invariant):
    name = 'link'

    def available(self, tl):
        return not os.environ.get('SPORE_NET') and not self.c.get('netem')

    def check(self, tl):
        out = []
        n = self.c['link_lines']
        for (viewer, pid), lst in tl.partner.items():
            bad, prev = [], None
            for p in lst:
                if p['link'] is None:
                    continue
                why = None
                rs, sent, _, pend = p['link']
                if prev is not None and sent - prev[1] >= self.c['link_min_sent'] and rs >= prev[0]:
                    ratio = (rs - prev[0]) / (sent - prev[1])
                    if ratio > self.c['link_ratio']:
                        why = f'resends/sent {ratio:.2f}'
                if pend > self.c['link_pending']:
                    why = f'pending {pend}'
                if p['same'] and p['age'] >= self.c['link_age']:
                    why = f'state {p["age"]:.1f} s old'
                prev = (rs, sent)
                bad = bad + [(p['t'], why)] if why else []
                if len(bad) >= n:  # once per episode (its first line is the key)
                    out.append((viewer, f'{viewer} -> partner {pid}: link unhealthy on a clean network on {n} trace '
                                        f'lines in a row from s {bad[0][0]}: {"; ".join(w for _, w in bad[:n])}',
                                (viewer, pid, bad[0][0])))
        return out


class Totals(Invariant):
    name = 'totals'

    def differ(self, a, b):
        return abs(a - b) > max(self.c['tot_abs'], abs(a) * self.c['tot_rel'])

    def check(self, tl):
        out = []
        host = next((w for w, i in tl.ids.items() if i == 0), None)
        if host is None:
            return out
        for who, lst in tl.own.items():
            if who == host:
                continue
            run = []
            for g in lst:
                if g['t'] is None or not playing(g):
                    run = []
                    continue
                h = tl.own_at(host, g['t'], self.c['tot_dt'])
                if not h or not playing(h) or h['stage'] != g['stage'] or h['world'] != g['world'] or h['away']:
                    run = []
                    continue
                # a total that changes (income, a meal) may have moved between the two machines' lines: the
                # guest's value must lie outside everything the host had in that window
                hs = [x for x in own_window(tl, host, g['t'], self.c['tot_dt']) if playing(x)]
                diffs = []
                for k, v in g['res'].items():
                    hv = [x['res'][k] for x in hs if k in x['res']]
                    if hv and all(self.differ(x, v) for x in hv) and (v > max(hv) or v < min(hv)):
                        diffs.append(f'{k}: host {h["res"].get(k, hv[-1]):.0f}, {who} {v:.0f}')
                hd = [x['dna'] for x in hs if x['dna'] >= 0]
                if g['stage'] == 'Creature' and hd and g['dna'] >= 0 and all(self.differ(x, g['dna']) for x in hd):
                    diffs.append(f'dna: host {h["dna"]:.0f}, {who} {g["dna"]:.0f}')
                run = run + [(g['t'], diffs)] if diffs else []
                if len(run) >= self.c['tot_n'] and run[-1][0] - run[0][0] >= self.c['tot_span']:
                    out.append((who, f'shared totals differ between the host and {who} in {g["stage"]} for '
                                     f'{run[-1][0] - run[0][0]:.0f} s from s {run[0][0]:.0f}: {"; ".join(run[-1][1])}',
                                (who, run[0][0])))
        return out


class Hp(Invariant):
    name = 'hp'

    def check(self, tl):
        out = []
        for (viewer, pid), lst in tl.partner.items():
            subj = tl.who_of(pid)
            if subj is None:
                continue
            run = []
            for p in lst:
                o = tl.own_at(subj, p['t'], 5.0) if p['t'] is not None else None
                if not o or not p['same'] or not playing(o) or o['maxhp'] <= 0 or p['age'] > 1.0:
                    run = []
                    continue
                # the subject's own hp unchanged around the moment (a fight changes it between the lines)
                around = own_window(tl, subj, p['t'], 12.0)
                if len(around) < 2 or len({(x['hp'], x['maxhp']) for x in around}) != 1:
                    run = []
                    continue
                bad = abs(o['maxhp'] - p['maxhp']) > 0.5 or abs(o['hp'] - p['hp']) > max(self.c['hp_abs'], o['maxhp'] * 0.1)
                run = run + [p] if bad else []
                if len(run) >= self.c['hp_n']:
                    out.append((viewer, f'{viewer} has {subj} at hp {p["hp"]:.0f}/{p["maxhp"]:.0f}, {subj}\'s own game '
                                        f'{o["hp"]:.0f}/{o["maxhp"]:.0f} (s {p["t"]})', (viewer, pid, run[0]['t'])))
        return out


class Objects(Invariant):
    name = 'objects'

    def __init__(self, cfg):
        super().__init__(cfg)
        self.census_first = {}

    def check(self, tl):
        out = []
        for r in tl.records:
            if r['kind'] == 'pool' and r['size'] > 0 and r['n'] > r['size'] * self.c['pool_frac']:
                out.append((r['who'], f'{r["who"]}: cell pool {r["n"]}/{r["size"]} (over '
                                      f'{self.c["pool_frac"]:.0%})', (r['who'], 'pool')))
            if r['kind'] == 'census':
                for k, n in r['counts'].items():
                    first = self.census_first.setdefault((r['who'], r['stage'], k), n)
                    if n > OBJECT_LIMITS.get(k, 10 ** 9):
                        out.append((r['who'], f'{r["who"]}: {n} objects of kind {k} (limit {OBJECT_LIMITS[k]})',
                                    (r['who'], 'limit', k)))
                    if n > first * self.c['census_growth'] + self.c['census_extra']:
                        out.append((r['who'], f'{r["who"]}: objects of kind {k} grew from {first} to {n} in '
                                              f'{r["stage"]}', (r['who'], 'growth', k)))
        return out


class StageApart(Invariant):
    name = 'stage_apart'

    def check(self, tl):
        out = []
        host = next((w for w, i in tl.ids.items() if i == 0), None)
        for who, lst in tl.own.items():
            if who == host or host is None:
                continue
            since = None
            for g in lst:
                h = tl.own_at(host, g['t'], 12) if g['t'] is not None else None
                # Space: every star system is its own world, players fly apart on purpose
                apart = h and playing(g) and playing(h) and not h['away'] and g['players'] > 1 and \
                    (g['stage'], g['world']) != (h['stage'], h['world']) and not (g['stage'] == h['stage'] == 'Space')
                if not apart:
                    since = None
                    continue
                since = since if since is not None else g['t']
                if g['t'] - since >= self.c['apart_s']:
                    out.append((who, f'{who} plays {g["stage"]} {g["world"]}, the host {h["stage"]} {h["world"]}, '
                                     f'for {g["t"] - since:.0f} s from s {since:.0f}', (who, since)))
        return out


class Hook(Invariant):
    """an invariant that needs a mod diagnostic not written yet: active once a trace line has the field"""
    field = None

    def available(self, tl):
        return any(self.field in r.get('rest', '') for r in tl.records[-200:] if r['kind'] in ('own', 'partner', 'hookline'))


def hook(name, field):
    return type(f'Hook_{name}', (Hook,), dict(name=name, field=field, needs=NEEDS[name]))


HOOKS = [hook('drawn', 'drawn='), hook('ground', 'agl='), hook('puppet_hp', 'puppet_hp='),
         hook('posse_damage', 'damage: '), hook('unlocks', 'parts=')]
INVARIANTS = [Together, Puppet, Link, Totals, Hp, Objects, StageApart]


def evaluate(tl, cfg=None, only=None, skip=()):
    """[(invariant, who, text, key)] of a timeline"""
    c = dict(DEFAULTS, **(cfg or {}))
    out, seen = [], set()
    for cls in INVARIANTS + HOOKS:
        if (only and cls.name not in only) or cls.name in skip:
            continue
        inv = cls(c)
        if not inv.available(tl):
            continue
        for who, text, key in inv.check(tl):
            if (cls.name, key) not in seen:
                seen.add((cls.name, key))
                out.append((cls.name, who, text, key))
    return out


# ---------------------------------------------------------------- live: the thread
class Watch(threading.Thread):
    """the live monitor of a test's games: {who: (instance, log path)}"""

    def __init__(self, players, cfg=None, console=None, out=print):
        super().__init__(daemon=True)
        self.cfg = dict(DEFAULTS, **(cfg or {}))
        self.players = dict(players)
        self.offsets = {who: self._size(p) for who, (_, p) in self.players.items()}
        self.tl = Timeline()
        self.violations = []
        self.seen = set()
        self.stop_ev = threading.Event()
        self.console = console  # factory inst -> Console (None: the logs only)
        self.out = out
        self.lock = threading.Lock()
        self.census_at = 0.0
        skip = os.environ.get('SPORE_MONITOR_SKIP', '')
        self.skip = {s.strip() for s in skip.split(',') if s.strip()}

    @staticmethod
    def _size(path):
        try:
            return os.path.getsize(path)
        except OSError:
            return 0

    def add_player(self, who, inst, path):
        with self.lock:
            self.players[who] = (inst, path)
            self.offsets[who] = self._size(path)

    def read_logs(self):
        for who, (_, path) in list(self.players.items()):
            try:
                size = os.path.getsize(path)
                if size < self.offsets.get(who, 0):  # the game started again: a new log
                    self.offsets[who] = 0
                with open(path, 'rb') as f:
                    f.seek(self.offsets.get(who, 0))
                    data = f.read()
            except OSError:
                continue
            cut = data.rfind(b'\n') + 1  # whole lines only
            self.offsets[who] = self.offsets.get(who, 0) + cut
            for line in data[:cut].decode('utf-8', 'replace').splitlines():
                r = parse_line(who, line)
                if r:
                    self.tl.add(r)

    def poll_census(self):
        if self.console is None:
            return
        for who, (inst, _) in list(self.players.items()):
            try:
                text = self.console(inst).call('coop.census').get('out', '')
            except (OSError, ValueError):
                continue
            m = re.search(r'census v1 role=\S+ me=\d+ mode=(.*?) modeid=', text)
            counts = collections.Counter(mm.group(1) for mm in re.finditer(r'^obj ([cv])', text, re.M))
            if m and m.group(1) in STAGES:
                last = self.tl.own.get(who)
                self.tl.add(dict(kind='census', who=who, t=last[-1]['t'] if last else None, stage=m.group(1),
                                 counts=dict(counts)))

    def step(self):
        with self.lock:
            self.read_logs()
            if self.console is not None and time.time() - self.census_at >= self.cfg['census_period']:
                self.census_at = time.time()
                self.poll_census()
            for inv, who, text, key in evaluate(self.tl, self.cfg, skip=self.skip):
                if (inv, key) in self.seen:
                    continue
                self.seen.add((inv, key))
                self.violations.append((inv, who, text))
                self.out(f'MONITOR-VIOLATION {inv} {who}: {text}')

    def run(self):
        while not self.stop_ev.wait(self.cfg['period']):
            try:
                self.step()
            except Exception as e:  # noqa: BLE001 (the monitor must not end a test)
                self.out(f'  (monitor: {e!r})')

    def stop(self):
        self.stop_ev.set()
        try:
            self.step()  # the last lines
        except Exception:  # noqa: BLE001
            pass
        return self.violations


# ---------------------------------------------------------------- replay over recorded logs
def replay(logs, cfg=None, only=None):
    """{who: path} of recorded game logs -> violations of the trace invariants (no console)"""
    tl = Timeline()
    for who, path in logs.items():
        with open(path, errors='replace') as f:
            for line in f:
                r = parse_line(who, line)
                if r:
                    tl.add(r)
    # the machines' lines interleaved by the session clock (the live thread reads them in steps)
    for k in tl.own:
        tl.own[k].sort(key=lambda r: r['t'] if r['t'] is not None else -1)
    return evaluate(tl, cfg, only=only)


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('replay')
    r.add_argument('paths', nargs='+')
    rp = sub.add_parser('report')
    rp.add_argument('dirs', nargs='*')
    a = ap.parse_args(argv)
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import loggates
    if a.cmd == 'replay':
        logs = {}
        for p in a.paths:
            if os.path.isdir(p):
                for f in sorted(os.listdir(p)):
                    if f.endswith('.game.log'):
                        test, who = f[:-len('.game.log')].rsplit('.', 1)
                        logs.setdefault(test, {})[who] = os.path.join(p, f)
            else:
                logs.setdefault('logs', {})[os.path.basename(p)] = p
        for test, lg in logs.items():
            for inv, who, text, _ in replay(lg):
                print(f'{test}: {inv} {who}: {text}')
        return 0
    if not a.dirs:
        import subprocess
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        a.dirs = loggates.lab_dirs(os.path.dirname(common) if common else ROOT)
    per = collections.defaultdict(lambda: collections.Counter())
    runs = collections.Counter()
    examples = {}
    for rundir, data, res in loggates.recorded(a.dirs):
        if not res.get('ok'):
            continue
        logs, tlog = loggates.result_logs(rundir, res)
        if not logs:
            continue
        name = res['name'].split('#')[0]
        net = 'SPORE_NET' in (open(tlog, errors='replace').readline() if os.path.exists(tlog) else '')
        runs[name] += 1
        # a recorded log of a warm game holds earlier tests: only lines from the test's window
        end = os.path.getmtime(tlog)
        start = end - max(0, res.get('seconds', 0) - 3)
        tl = Timeline()
        for who, path in logs.items():
            for ln in loggates.window_lines(path, None, start, end + 7):
                rec = parse_line(who, ln.text)
                if rec:
                    tl.add(rec)
        for inv, who, text, _ in evaluate(tl, dict(netem=net)):
            per[name][inv] += 1
            examples.setdefault((name, inv), f'{os.path.basename(rundir)}: {text[:200]}')
    print('| test | passed runs | violations per invariant | example |\n|---|---|---|---|')
    for name in sorted(per, key=lambda n: -sum(per[n].values())):
        inv = per[name].most_common(1)[0][0]
        print(f'| {name} | {runs[name]} | ' + ', '.join(f'{k} {v}' for k, v in per[name].most_common())
              + f' | {examples[(name, inv)]} |')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
