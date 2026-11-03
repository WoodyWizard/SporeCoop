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
                pending under 400 and the partner's state younger than 5 s, each on 3 lines in a row (not
                while the host sends its world to that partner and 15 s after: BUG-0405, BUG-0415)
  totals        the shared totals of the own trace lines (dna in Creature, food / money / ... of the
                stage) the same on the host and every guest in its world, within 1 point or 2 %, on
                3 comparisons in a row spanning 20 s
  hp            what a machine's partner line says of a player's hp / max hp (the state it received)
                equals what that player's own trace line says at the same moment, on 2 comparisons
  objects       the game's cell pool (tiles: ... cell pool N/M) stays under 90 % of its size, the
                census of each game (coop.census, every 30 s, live only) stays under the limits of
                OBJECT_LIMITS and does not grow past 3x its first count + 200
  stage_apart   a guest whose own stage / world differs from the host's for 90 s while both play a stage
                (not while the host's line says its world share waits: share_wait=planet / cinematic)

Invariants over the mod's monitor fields (active once a log has the field, so older logs replay as
before; the fields are described in FIELDS and docs/06-testing.md#testing-monitor):

  drawn         a partner's puppet as this game drew it (partner line `drawn=`, read back from the game at
                the start of the frame) within DRAWN_BASE[stage] + its speed x (lag + rtt) of that partner's
                own `pos=` on its line of the same moment (trace lines come at the session clock's 10 s
                marks), on 2 lines in a row; Tribe / Civ only a pointer at rest on both games (the copy's
                drawn_v, the partner's own `ptr_still=`: BUG-0416)
  ground        `agl=` (height over this game's terrain) of the own avatar, a puppet, the Tribe members
                (lowest; `agl_hi=` the highest): under -0.5 m on 2 lines in a row; a Creature puppet over
                3 m while its player's own creature is on its ground (own agl under 1 m: not jumping,
                on a rock, swimming), Tribe members over 3 m, each on 2 lines in a row
  puppet_hp     the puppet's own health (partner line `puppet_hp=`) equals its player's own hp / max hp on
                its line of the same moment (within 1 hp or 10 %), on 2 lines in a row
  posse_damage  `damage: <victim> hit by <kind> ... posse=1 amount=<hp>` with hp > 0: the hit player's own
                party (pack, family) hurt it (BUG-0374 class), at once
  unlocks       a guest's `parts=<count>` in Creature under the count the host had on its lines 70 s and
                60 s before, in the same world, on 2 lines in a row (BUG-0368 class)
  proxies       a guest's NPC proxies (own line `proxy_under=` / `proxy_stuck=`: under the ground for
                1 s, over 3 m off the host's NPC for 2 s) on 2 lines in a row

  monitor.py replay <run dir | game log ...>   the trace invariants over recorded logs
  monitor.py report [out/lab dirs]             the same over every recorded result (calibration)
"""
import bisect
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
# a world transfer to a partner (BUG-0405 / BUG-0415: its reliable chunks fill the link, see Link)
SHARE = re.compile(r'world: (sharing) .* with player (\d+)$|world: (?:transfer) to (\d+) queued completely|'
                   r'session: player (\d+) \'.*\' (left)')
DRAWN = re.compile(r' drawn=\(([-\d.e+naif]+) ([-\d.e+naif]+) ([-\d.e+naif]+)\) drawn_v=([-\d.naif]+)')
AGL = re.compile(r' agl=(-?[\d.naif]+)(?: agl_hi=(-?[\d.naif]+))?')
PUPPET_HP = re.compile(r' puppet_hp=(-?[\d.naif]+)/(-?[\d.naif]+)')
PARTS = re.compile(r' parts=(\d+):([0-9A-Fa-f]{8})')
PROXIES = re.compile(r' proxy_under=(\d+) proxy_stuck=(\d+)')
PTR_STILL = re.compile(r' ptr_still=([\d.]+)')
DAMAGE = re.compile(r'coop: damage: (\d+) hit by (\S+) owner=(\S+) posse=([01?]) amount=(-?[\d.]+)(.*)$')
KV = re.compile(r'(\w+)=(-?[\d.]+)(\(unsynced\))?')
STAGES = ('Cell', 'Creature', 'Tribe', 'Civilization', 'Space')
PF_IN_WORLD, PF_DEAD = 1, 16

DEFAULTS = dict(together_s=20.0, apart_s=90.0, link_ratio=0.5, link_min_sent=50, link_pending=400, link_age=5.0,
                link_lines=3, tot_abs=1.0, tot_rel=0.02, tot_n=3, tot_span=20.0, tot_dt=6.0, hp_n=2, hp_abs=1.0,
                pool_frac=0.9, census_growth=3.0, census_extra=200, period=5.0, census_period=30.0,
                drawn_dt=1.0, drawn_lag=0.3, drawn_n=2, agl_under=-0.5, agl_over=3.0, agl_ground=1.0, agl_n=2,
                php_n=2, unlock_s=60.0, unlock_span=10.0, unlock_n=2, proxy_n=2, drawn_still=1.0, share_after=15.0)
OBJECT_LIMITS = {'c': 4000, 'v': 800}
# the drawn invariant's distance per stage (m) before the speed slack (calibration: docs/06-testing.md#testing-monitor)
DRAWN_BASE = {'Cell': 3.0, 'Creature': 3.0, 'Tribe': 3.0, 'Civilization': 6.0, 'Space': 12.0}
# the mod's output each monitor-field invariant reads (at the END of the trace lines, after `pending=` /
# `share_wait=`, before `version=`; or a log line of its own). The invariant is active once a log has it.
FIELDS = {
    'drawn': "partner line: `drawn=(x y z) drawn_v=<m/s>` = the puppet (Tribe / Civ: the pointer marker) read back "
             "from the game at the start of the frame that writes the line (before any driving), and the "
             "partner's speed as seen here; trace lines are written at the session clock's 10 s marks; Tribe / Civ "
             "own line: `ptr_still=<s>` = how long the own pointer has stood still (BUG-0416)",
    'ground': "own and partner lines in Creature / Tribe: `agl=<m>` = height over this game's terrain "
              "(cPlanetModel::GetHeightAt) of the avatar / puppet as drawn; Tribe: `agl=<lowest> agl_hi=<highest>` "
              "of the player's tribe members",
    'puppet_hp': "partner line in Creature: `puppet_hp=<hp>/<max>` read from the puppet creature itself (BUG-0373)",
    'posse_damage': "log line `damage: <victim id> hit by <kind> owner=<host|guest id|npc|-> posse=<0|1|?> "
                    "amount=<hp>[ forwarded| dropped (npc.party_peace)| from the host]` where a hit on a player is "
                    "applied, forwarded or dropped (npc.cpp)",
    'unlocks': "own line in Creature: `parts=<count>:<hash>` of the unlocked creature parts (cCollectableItems "
               "cPlayer+0x10E8)",
    'proxies': "own line of a guest in Creature / Tribe: `proxy_under=<n> proxy_stuck=<n>` = NPC proxies under the "
               "ground for 1 s / over 3 m off the host's NPC for 2 s since the last line (sampled 4x a second)",
}
NEEDS = FIELDS  # (the name of the hooks' time)


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
            r = dict(kind='own', who=who, t=t, role=o.group(1), id=int(o.group(2)), players=int(o.group(3)),
                     stage=o.group(4), world=o.group(5), avatar=o.group(7) == 'yes',
                     pos=(num(o.group(8)), num(o.group(9)), num(o.group(10))), hp=num(o.group(11)),
                     maxhp=num(o.group(12)), flags=int(o.group(14), 16), dna=float(o.group(15)), res=res,
                     away=o.group(17) == '1', vote=o.group(18) != 'off', rest=o.group(19))
            return monitor_fields(r)
        p = PARTNER.search(line)
        if p:
            lk = LINK.search(p.group(14))
            r = dict(kind='partner', who=who, t=t, pid=int(p.group(1)), name=p.group(2), stage=p.group(3),
                     world=p.group(4), same=p.group(6) == '1', puppet=p.group(7), hp=num(p.group(8)),
                     maxhp=num(p.group(9)), flags=int(p.group(11), 16), age=float(p.group(12)),
                     rtt=int(p.group(13)), rest=p.group(14),
                     link=tuple(int(lk.group(i)) for i in range(1, 5)) if lk else None)
            return monitor_fields(r)
    m = SHARE.search(line)  # a world transfer to a partner (its reliable chunks fill the link, see Link)
    if m:
        return dict(kind='share', who=who, t=t, pid=int(m.group(2) or m.group(3) or m.group(4)),
                    what='start' if m.group(1) == 'sharing' else 'end')
    if 'coop: damage: ' in line:  # FIELDS['posse_damage']
        m = DAMAGE.search(line)
        r = dict(kind='hookline', who=who, t=t, rest=line)
        if m:
            r.update(victim=int(m.group(1)), by=m.group(2), owner=m.group(3), posse=m.group(4),
                     amount=float(m.group(5)), how=m.group(6).strip())
        return r
    m = POOL.search(line)
    if m and 'tiles:' in line:
        return dict(kind='pool', who=who, t=t, n=int(m.group(1)), size=int(m.group(2)))
    return None


def monitor_fields(r):
    """the monitor fields (FIELDS) at the end of an own / partner record"""
    rest = r['rest']
    m = DRAWN.search(rest)
    if m:
        r['drawn'] = (num(m.group(1)), num(m.group(2)), num(m.group(3)))
        r['drawn_v'] = num(m.group(4))
    m = AGL.search(rest)
    if m:
        r['agl'] = num(m.group(1))
        if m.group(2) is not None:
            r['agl_hi'] = num(m.group(2))
    m = PUPPET_HP.search(rest)
    if m:
        r['puppet_hp'] = (num(m.group(1)), num(m.group(2)))
    m = PARTS.search(rest)
    if m:
        r['parts'] = (int(m.group(1)), m.group(2).upper())
    m = PROXIES.search(rest)
    if m:
        r['proxies'] = (int(m.group(1)), int(m.group(2)))
    m = PTR_STILL.search(rest)
    if m:
        r['ptr_still'] = float(m.group(1))
    return r


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


# ---------------------------------------------------------------- the invariants
class Timeline:
    """every record of every machine of the test, in arrival order"""

    def __init__(self):
        self.own = collections.defaultdict(list)       # who -> [own records]
        self.own_t = collections.defaultdict(list)     # who -> their session times (sorted, -inf: none)
        self.partner = collections.defaultdict(list)   # (viewer, pid) -> [partner records]
        self.ids = {}                                  # who -> session id (0 = host)
        self.shares = collections.defaultdict(list)    # (who, pid) -> [[start, end or None]] world transfers
        self.records = []

    def add(self, r):
        self.records.append(r)
        if r['kind'] == 'own':
            t = r['t'] if r['t'] is not None else float('-inf')
            ts = self.own_t[r['who']]
            i = bisect.bisect_right(ts, t)  # in order (a replay of several files sorts by the clock)
            ts.insert(i, t)
            self.own[r['who']].insert(i, r)
            self.ids[r['who']] = r['id']
        elif r['kind'] == 'partner':
            self.partner[(r['who'], r['pid'])].append(r)
        elif r['kind'] == 'share' and r['t'] is not None:
            lst = self.shares[(r['who'], r['pid'])]
            if r['what'] == 'start':
                lst.append([r['t'], None])
            elif lst and lst[-1][1] is None:
                lst[-1][1] = r['t']

    def sharing(self, who, pid, t, after=15.0):
        """`who` sends its world to partner pid at session time t (or did within `after` s)"""
        return t is not None and any(a <= t <= (b if b is not None else float('inf')) + after
                                     for a, b in self.shares.get((who, pid), ()))

    def who_of(self, pid):
        return next((w for w, i in self.ids.items() if i == pid), None)

    def who_at(self, pid, t, dt=15.0, viewer=None):
        """the machine that had session id pid around session time t: ids are reused after a leave (a
        newcomer takes the lowest free one, party_rejoin_net), so the latest id of each machine is not enough;
        None when no machine or more than one had it then"""
        if t is None:
            return self.who_of(pid)
        got = [w for w in self.own if w != viewer and (lambda o: o is not None and o['id'] == pid)(self.own_at(w, t, dt))]
        return got[0] if len(got) == 1 else None

    def window(self, who, t, dt):
        """the own records of `who` within session time t +- dt"""
        ts = self.own_t.get(who)
        if not ts or t is None:
            return []
        return self.own[who][bisect.bisect_left(ts, t - dt):bisect.bisect_right(ts, t + dt)]

    def around(self, who, t):
        """the own records of `who` right before and right after session time t (BUG-0380)"""
        ts = self.own_t.get(who)
        if not ts or t is None:
            return []
        i = bisect.bisect_left(ts, t)
        return [self.own[who][j] for j in (i - 1, i) if 0 <= j < len(ts) and ts[j] != float('-inf')]

    def own_at(self, who, t, dt):
        """the own record of `who` closest to session time t (within dt), None"""
        w = self.window(who, t, dt)
        return min(w, key=lambda r: abs(r['t'] - t)) if w else None


def own_window(tl, who, t, dt):
    """the own records of `who` within session time t +- dt"""
    return tl.window(who, t, dt)


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
            run = []
            for p in lst:
                if p['t'] is None:
                    continue
                subj = tl.who_at(pid, p['t'], viewer=viewer)
                if subj is None or (run and run[-1].get('_subj') != subj):
                    run = []
                    if subj is None:
                        continue
                p['_subj'] = subj
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
                if self.c['share_after'] >= 0 and tl.sharing(viewer, pid, p['t'], self.c['share_after']):
                    # BUG-0405 / BUG-0415: a world transfer: its chunks fill the reliable window (pending 400-600,
                    # 600 in flight on a LAN, more than the 128-packet ack window covers) and a loading guest acks
                    # late: 50-99 % of the packets between two lines are resends on loopback (chaos_tribe's
                    # rejoins 20261101-150122, rejoin_race_long 20261101-134020). Lines after it count again.
                    bad, prev = [], None
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
                # guest's value must lie outside everything the host had in that window. BUG-0380: the window
                # always holds the host's lines right before and right after the guest's (trace lines come
                # every 10 s, so +-6 s often held one host line: a Civ treasury growing by ~400 per 10 s was
                # "apart" at every guest line between two host lines, campaign_space 42 violations)
                hs = [x for x in own_window(tl, host, g['t'], self.c['tot_dt']) + tl.around(host, g['t']) if playing(x)]
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
            run, last = [], None
            for p in lst:
                subj = tl.who_at(pid, p['t'], viewer=viewer)
                if subj is None or subj != last:
                    run, last = [], subj
                    if subj is None:
                        continue
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
                # BUG-0382: the host's share that waits for a reason of the game (`share_wait=planet`: the
                # Space stage saves only off a planet; `cinematic`: BUG-0019) is a declared wait, not "apart"
                apart = h and playing(g) and playing(h) and not h['away'] and g['players'] > 1 and \
                    (g['stage'], g['world']) != (h['stage'], h['world']) and not (g['stage'] == h['stage'] == 'Space') \
                    and 'share_wait=' not in h.get('rest', '')
                if not apart:
                    since = None
                    continue
                since = since if since is not None else g['t']
                if g['t'] - since >= self.c['apart_s']:
                    out.append((who, f'{who} plays {g["stage"]} {g["world"]}, the host {h["stage"]} {h["world"]}, '
                                     f'for {g["t"] - since:.0f} s from s {since:.0f}', (who, since)))
        return out


class Hook(Invariant):
    """an invariant over a monitor field of the mod (FIELDS): active once a record has the field"""
    field = None

    def available(self, tl):
        return any(self.field in r.get('rest', '') for r in tl.records if r['kind'] in ('own', 'partner', 'hookline'))

    @staticmethod
    def runs(lst, bad, n, key):
        """[(first, last)] of the runs of n records in a row for which bad(r) is true (once per run)"""
        out, run = [], []
        for r in lst:
            if bad(r):
                run.append(r)
                if len(run) == n:
                    out.append((run[0], run[-1]))
            else:
                run = []
        return out

    def same_moment(self, tl, p):
        """the own record of partner record p's subject at the same moment (the 10 s marks), None"""
        subj = tl.who_at(p['pid'], p['t'], viewer=p['who'])  # ids are reused after leaves
        if subj is None or p['t'] is None:
            return None, None
        return subj, tl.own_at(subj, p['t'], self.c['drawn_dt'])


class Drawn(Hook):
    name, field, needs = 'drawn', 'drawn=', FIELDS['drawn']

    def check(self, tl):
        out = []
        for (viewer, pid), lst in tl.partner.items():
            def bad(p):
                if 'drawn' not in p or not p['same'] or p['puppet'] != 'shown' or p['flags'] & PF_DEAD:
                    return False
                subj, o = self.same_moment(tl, p)
                v = tl.own_at(viewer, p['t'], self.c['drawn_dt'])
                if not o or not v or not playing(o) or not playing(v) or o['stage'] != p['stage'] or \
                        (o['stage'], o['world']) != (v['stage'], v['world']):
                    return False
                # Tribe / Civ: the pointer jumps with the mouse (tests pin it: tens of m at once), its copy
                # trails while it moves (sync-check finding BUG-0380): judged only at rest
                if p['stage'] in ('Tribe', 'Civilization') and p['drawn_v'] > 0.5:
                    return False
                # BUG-0416: ... at rest on the partner's own game too. A pointer that jumped just before the mark
                # (a click, a minimap jump, the mouse moved during a 0.8 s save hitch) has a copy that still rests
                # at the old point (drawn_v 0: the new states are not interpolated yet): 164 m in chaos_tribe
                # 20261101-140615. The own line's `ptr_still=` says how long the pointer has stood still.
                # (drawn_still < 0: the rule before it, SPORE_MONITOR='{"drawn_still": -1}')
                if p['stage'] in ('Tribe', 'Civilization') and 'ptr_still' in o and self.c['drawn_still'] >= 0 and \
                        o['ptr_still'] < self.c['drawn_still'] + max(p['rtt'], 0) / 1000.0:
                    return False
                lim = DRAWN_BASE.get(p['stage'], 6.0) + p['drawn_v'] * (self.c['drawn_lag'] + max(p['rtt'], 0) / 1000.0
                                                                       + abs(o['t'] - p['t']))
                d = dist(p['drawn'], o['pos'])
                p['_drawn'] = (d, lim, subj)
                return d > lim
            for a, b in self.runs(lst, bad, self.c['drawn_n'], None):
                d, lim, subj = b['_drawn']
                out.append((viewer, f'{viewer} draws {subj} {d:.1f} m from where {subj}\'s own game has it (limit '
                                    f'{lim:.1f} m, {b["stage"]}) on {self.c["drawn_n"]} lines from s {a["t"]:.0f}',
                            (viewer, pid, a['t'])))
        return out


class Ground(Hook):
    name, field, needs = 'ground', 'agl=', FIELDS['ground']

    def check(self, tl):
        c, out = self.c, []
        n = c['agl_n']
        series = [(who, None, lst) for who, lst in tl.own.items()] + \
            [(viewer, pid, lst) for (viewer, pid), lst in tl.partner.items()]
        for who, pid, lst in series:
            what = 'its own creature' if pid is None else f'partner {pid}\'s puppet'

            def ok(r):
                return 'agl' in r and r['stage'] in ('Creature', 'Tribe') and (
                    playing(r) if pid is None else (r['same'] and r['puppet'] == 'shown' and not r['flags'] & PF_DEAD))
            for a, b in self.runs(lst, lambda r: ok(r) and r['agl'] < c['agl_under'], n, None):
                out.append((who, f'{who}: {what if b["stage"] == "Creature" else ("the tribe members of " + (who if pid is None else f"partner {pid}"))} '
                                 f'{-b["agl"]:.2f} m under the ground ({b["stage"]}) on {n} lines from s {a["t"]:.0f}',
                            (who, pid, 'under', a['t'])))

            def over(r):
                if not ok(r):
                    return False
                if r['stage'] == 'Tribe':
                    return r.get('agl_hi', r['agl']) > c['agl_over']
                if pid is None:  # a creature on a rock reads 1-16 m (GetHeightAt is the terrain only)
                    return False
                _, o = self.same_moment(tl, r)
                return r['agl'] > c['agl_over'] and o is not None and 'agl' in o and o['agl'] < c['agl_ground']
            for a, b in self.runs(lst, over, n, None):
                out.append((who, f'{who}: {"the tribe members" if b["stage"] == "Tribe" else what} '
                                 f'{b.get("agl_hi", b["agl"]):.2f} m over the ground ({b["stage"]}) on {n} lines from s '
                                 f'{a["t"]:.0f}', (who, pid, 'over', a['t'])))
        return out


class PuppetHp(Hook):
    name, field, needs = 'puppet_hp', 'puppet_hp=', FIELDS['puppet_hp']

    def check(self, tl):
        out = []
        for (viewer, pid), lst in tl.partner.items():
            def bad(p):
                if 'puppet_hp' not in p or not p['same'] or p['flags'] & PF_DEAD:
                    return False
                # the same moment (the 10 s marks): the puppet's health is ~0.15 s behind, no steady window needed
                subj, o = self.same_moment(tl, p)
                if not o or not playing(o) or o['maxhp'] <= 0 or abs(o['t'] - p['t']) > 0.5:
                    return False
                hp, mx = p['puppet_hp']
                p['_php'] = (subj, o)
                # a player's own hp above its max is a test's invulnerability (`coop.team hp 1000`): the puppet
                # carries the partner's health capped at the max, which is what the game can show
                own = min(o['hp'], o['maxhp'])
                return abs(o['maxhp'] - mx) > 0.5 or abs(own - hp) > max(self.c['hp_abs'], o['maxhp'] * 0.1)
            for a, b in self.runs(lst, bad, self.c['php_n'], None):
                subj, o = b['_php']
                out.append((viewer, f'{viewer} shows {subj}\'s creature at {b["puppet_hp"][0]:.0f}/{b["puppet_hp"][1]:.0f}, '
                                    f'{subj}\'s own game {o["hp"]:.0f}/{o["maxhp"]:.0f} (s {b["t"]:.0f})',
                            (viewer, pid, a['t'])))
        return out


class PosseDamage(Hook):
    name, field, needs = 'posse_damage', 'damage: ', FIELDS['posse_damage']

    def check(self, tl):
        return [(r['who'], f'{r["who"]}: player {r["victim"]} hit by its own party ({r["by"]}, posse=1) for '
                           f'{r["amount"]:.1f} hp {r["how"]} (s {r["t"]})', (r['who'], r['t'], r['victim']))
                for r in tl.records if r['kind'] == 'hookline' and r.get('posse') == '1' and r.get('amount', 0) > 0]


class Unlocks(Hook):
    name, field, needs = 'unlocks', 'parts=', FIELDS['unlocks']

    def check(self, tl):
        c, out = self.c, []
        host = next((w for w, i in tl.ids.items() if i == 0), None)
        if host is None:
            return out
        for who, lst in tl.own.items():
            if who == host:
                continue

            def bad(g):
                if 'parts' not in g or g['stage'] != 'Creature' or not playing(g) or g['t'] is None:
                    return False
                # the host's lines from unlock_s + unlock_span to unlock_s before (2 lines at the 10 s marks)
                t1 = g['t'] - c['unlock_s'] - c['unlock_span'] / 2
                hs = own_window(tl, host, t1, c['unlock_span'] / 2 + 5)
                hs = [h for h in hs if 'parts' in h and h['stage'] == 'Creature' and h['world'] == g['world']]
                if len(hs) < 2:
                    return False
                need = min(h['parts'][0] for h in hs)
                g['_need'] = need
                return g['parts'][0] < need
            for a, b in self.runs(lst, bad, c['unlock_n'], None):
                out.append((who, f'{who} has {b["parts"][0]} unlocked creature parts, the host had {b["_need"]} '
                                 f'{c["unlock_s"]:.0f} s before (s {a["t"]:.0f})', (who, a['t'])))
        return out


class Proxies(Hook):
    name, field, needs = 'proxies', 'proxy_under=', FIELDS['proxies']

    def check(self, tl):
        out = []
        n = self.c['proxy_n']
        for who, lst in tl.own.items():
            for k, what in ((0, 'under the ground'), (1, 'over 3 m off the host\'s NPC')):
                for a, b in self.runs(lst, lambda r: 'proxies' in r and r['proxies'][k] > 0, n, None):
                    out.append((who, f'{who}: {b["proxies"][k]} NPC proxies {what} ({b["stage"]}) on {n} lines from '
                                     f's {a["t"]:.0f}', (who, k, a['t'])))
        return out


HOOKS = [Drawn, Ground, PuppetHp, PosseDamage, Unlocks, Proxies]
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
        # the divergence detector (worlddiff.py): SPORE_DETECTOR=<seconds> compares the host's world with
        # every guest's that often and prints DETECTOR lines (data for tools/detector_report.py, no gate)
        try:
            self.detect_period = float(os.environ.get('SPORE_DETECTOR', '0') or 0)
        except ValueError:
            self.detect_period = 0.0
        self.detect_at = time.time()
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

    def detect(self):
        """one detector sample: the host's world against every guest's (worlddiff.diff)"""
        import json
        import worlddiff
        host = self.players.get('host')
        if host is None:
            return
        for who, (inst, _) in list(self.players.items()):
            if who == 'host':
                continue
            try:
                hw, gw = worlddiff.pair_dump(self.console(host[0]), self.console(inst), 250)
                hw.who, gw.who = 'host', who
            except (OSError, ValueError) as e:
                self.out(f'  (detector: {who} dump failed: {e!r})')
                continue
            found = worlddiff.diff(hw, gw)
            skip = [f for f in found if f['sig'] in ('apart', 'nocentre')]
            real = [f for f in found if f['sig'] not in ('apart', 'nocentre')]
            self.out('DETECTOR-SAMPLE ' + json.dumps(dict(guest=who, stage=hw.mode, gstage=gw.mode, host_rows=len(hw.rows),
                                                          guest_rows=len(gw.rows), findings=len(real),
                                                          skipped=skip[0]['sig'] if skip else '')))
            for f in real:
                self.out('DETECTOR ' + json.dumps(dict(guest=who, stage=f['stage'], sig=f['sig'], n=f['n'], text=f['text'])))

    def step(self):
        with self.lock:
            self.read_logs()
            if self.console is not None and time.time() - self.census_at >= self.cfg['census_period']:
                self.census_at = time.time()
                self.poll_census()
            if self.console is not None and self.detect_period > 0 and time.time() - self.detect_at >= self.detect_period:
                self.detect_at = time.time()
                try:
                    self.detect()
                except Exception as e:  # noqa: BLE001 (the detector must not end a test)
                    self.out(f'  (detector: {e!r})')
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
