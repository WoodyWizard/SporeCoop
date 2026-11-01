#!/usr/bin/env python3
"""A session of the previous release and this build: players do not all update at once.

Lane A (SPORE_LANE) runs this build, lane B (SPORE_LANE_B) the previous release's DLLs (coop.dll,
sporekit.dll, the loader, its symbol files; built from `git archive <tag>` into out/old/<tag>, once).

--old guest   this build hosts (Alice) with this build's Bob from lane A; the old release joins as Carol
              from lane B (instance 2B-1, an empty profile mp1: the host's world arrives).
--old host    the old release hosts (Alice) with an old Bob in lane B; this build joins as Carol from
              lane A. (The test runs itself again with the two lanes swapped, so that twoinst.start
              builds the host side in lane B.)

Checked on the players' games:
  version   every game logs a version line for each other player ("player N 'X': coop ..." / "joined from
            ...: coop ..."), none "version unknown"
  join      Carol plays the host's stage with an avatar, the host has her in its world
  puppets   each game draws every other player within 3 m (Creature; Tribe 3 m, Civ 6 m) of where that
            player's own game has it, before and after Alice and Carol walk (W held 3 s)
  new msgs  (this build hosting) the shared DNA total with its cause tail (AddEvolutionPoints on the
            host): every guest's DNA follows the host's; the host's nest eggs (MSG_NEST) are sent while
            Carol stands at a nest; in Tribe the chunks carry the host's time trailer: the old game
            keeps playing, draws the others right, has no caught fault
  health    no caught fault on any game (exc), no crash / hang report of any of the three, all answer
FAIL demo: --old-tag v0.7.2.4 or MIXED_OLD_TAG=v0.7.2.4 (no version lines before 0.7.3.x: "version unknown").

  SPORE_LANE=9 SPORE_LANE_B=10 python3 tools/tests/coop_mixed_version.py --old guest|host [--fixture creature|tribe]
"""
import argparse
import glob
import math
import os
import re
import shutil
import subprocess
import sys
import time

import lane
import twoinst
from coop_multi import sample
from party import NEAR
from sporectl import Console
from twoinst import Checks, lane_state, start, wait_up

HERE = os.path.dirname(os.path.abspath(__file__))


def old_tree(tag):
    """out/old/<tag> of this checkout: `git archive <tag>`, built once"""
    d = os.path.join(lane.ROOT, 'out', 'old', tag)
    if not os.path.exists(os.path.join(d, 'build', 'coop.dll')):
        os.makedirs(d, exist_ok=True)
        arch = subprocess.run(['git', 'archive', tag], cwd=lane.ROOT, capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', d], input=arch, check=True)
        r = subprocess.run(['make', '-j16'], cwd=d, capture_output=True, text=True)
        if r.returncode:
            print(r.stdout[-2000:], r.stderr[-2000:])
            raise SystemExit(f'RESULT: FAIL (the old release {tag} did not build)')
    return d


def sh_lane(n, *args):
    return subprocess.run([lane.SPORE, *args], env=dict(os.environ, SPORE_LANE=str(n)), capture_output=True, text=True)


def deploy_old(n, tree):
    """the old release's files into lane n (as tools/spore.sh deploy does with this build's)"""
    binp = os.path.join(lane.game_dir(n), 'SporebinEP1')
    mods = os.path.join(binp, 'mods')
    b = os.path.join(tree, 'build')
    if os.path.exists(os.path.join(binp, 'steam_api_orig.dll')):
        shutil.copy(os.path.join(b, 'steam_api.dll'), os.path.join(binp, 'steam_api.dll'))
    shutil.copy(os.path.join(b, 'sporekit.dll'), os.path.join(binp, 'sporekit.dll'))
    for f in glob.glob(os.path.join(b, '*.dll')):
        if os.path.basename(f) not in ('steam_api.dll', 'sporekit.dll'):
            shutil.copy(f, os.path.join(mods, os.path.basename(f)))
    os.makedirs(os.path.join(mods, 'sporekit', 'symbols'), exist_ok=True)
    for f in glob.glob(os.path.join(tree, 'data', 'symbols', '*.sym')):
        shutil.copy(f, os.path.join(mods, 'sporekit', 'symbols'))
    pk = os.path.join(b, 'SporeCoop.package')
    if os.path.exists(pk):
        shutil.copy(pk, os.path.join(lane.game_dir(n), 'Data', 'SporeCoop.package'))
    print(f'  lane {n}: the old release deployed from {tree}', flush=True)


def run_extra(n, inst, profile, old):
    """one more game in lane n (its own bottle), this build or the old release; its console"""
    sh_lane(n, 'kill')
    time.sleep(2)
    sh_lane(n, 'deploy')
    if old:
        deploy_old(n, old)
    bottle = 'Games' if n == 1 else f'SporeLab{n}'
    appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/{bottle}/')
    subprocess.run(['rm', '-rf', f'{appdata}/SporeKit-{profile}/Spore/Games'])
    sh_lane(n, 'run', '-i', str(inst), '-p', profile)
    assert wait_up(inst, 120), f'instance {inst} (lane {n}) did not start'
    c = Console(inst)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 200 60000')
    return c


class P:
    def __init__(self, name, con, inst, old):
        self.name, self.con, self.inst, self.old, self.pid = name, con, inst, old, None

    def call(self, line):
        try:
            return self.con.call(line).get('out', '')
        except OSError as e:
            return f'(no answer: {e})'


def own_pos(p):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', p.call('coop.avatar'))
    return tuple(map(float, m.groups())) if m else None


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def reports(dirs, since):
    out = []
    for d in dirs:
        for f in glob.glob(os.path.join(d, '*.txt')):
            if re.match(r'(crash|report|hang)-', os.path.basename(f)) and os.path.getmtime(f) >= since - 2:
                out.append(f)
    return out


def views(ps, ck, label, near):
    bad, got = [], []
    for v in ps:
        s_ = sample(v.con)
        for s in ps:
            if s is v:
                continue
            at, me = s_.get(f'p{s.pid}'), own_pos(s)
            d = None if at is None or me is None else math.dist(at, me)
            if d is None or d > near:
                for _ in range(2):
                    time.sleep(1)
                    at, me = sample(v.con).get(f'p{s.pid}'), own_pos(s)
                    d = None if at is None or me is None else math.dist(at, me)
                    if d is not None and d <= near:
                        break
            got.append(f'{v.name}->{s.name} {"-" if d is None else "%.1f" % d}')
            if d is None or d > near:
                bad.append(f'{v.name} draws {s.name} ' + ('nowhere' if d is None else f'{d:.1f} m off'))
    print(f'  [{label}] drawn vs own position (m): {", ".join(got)}', flush=True)
    ck(not bad, f'[{label}] every game draws every other player where it is (<= {near} m): {bad}')


def dna(p):
    m = re.search(r'dna=(-?[\d.]+)', p.call('coop.dna'))
    return float(m.group(1)) if m else None


def nest_eggs(host, carol):
    """the host's herd at the nearest nest of another species lays eggs while Carol stands there"""
    from coop_nest_eggs import nest_herds
    ns = nest_herds(host.con)
    if not ns:
        print('  (no nest of another species near the host)', flush=True)
        return False
    _, herd, members, home = ns[0]
    print('  carol: ' + carol.call('coop.npc gopos %.1f %.1f %.1f' % home).strip()[:120], flush=True)
    print('  host: ' + host.call('coop.nest look %.2f %.2f %.2f 6' % home).strip()[:120], flush=True)
    time.sleep(3)
    host.call(f'coop.nest respawn {herd} 2000')
    host.call(f'coop.nest target {herd} {members + 2}')
    time.sleep(25)
    out = host.call('coop.nest')
    print('  host nest status: ' + out.strip().replace('\n', ' | ')[:240], flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', choices=('guest', 'host'), required=True)
    ap.add_argument('--fixture', default='creature', choices=('creature', 'tribe'))
    ap.add_argument('--old-tag', default=os.environ.get('MIXED_OLD_TAG', 'v0.7.3.3'))
    a = ap.parse_args()
    lane_a = lane.LANE
    # the other version's lane: SPORE_LANE_B, else the neighbour (the suite marks these tests exclusive)
    lane_b = int(os.environ.get('SPORE_LANE_B') or (lane_a + 1 if lane_a < 10 else lane_a - 1))
    if not lane_b or lane_b == lane_a:
        print('RESULT: FAIL (SPORE_LANE_B: a second lane for the other version)')
        return 1
    if a.old == 'host' and not os.environ.get('MIXED_SWAPPED'):
        # the host side (twoinst.start) in lane B: this script again with the lanes swapped
        env = dict(os.environ, SPORE_LANE=str(lane_b), SPORE_LANE_B=str(lane_a), MIXED_SWAPPED='1')
        return subprocess.run([sys.executable, *sys.argv], env=env).returncode
    tree = old_tree(a.old_tag)
    t_start = time.time()
    print(f'  {a.old_tag} {"hosts" if a.old == "host" else "joins"} (lane {lane_b if a.old == "guest" else lane_a}); '
          f'this build {"joins" if a.old == "host" else "hosts"}; fixture {a.fixture}', flush=True)
    if a.old == 'host':  # this process: lane = the old lane; its deploy puts the old files in
        orig_sh = twoinst.sh

        def sh_old(*args, **kw):
            r = orig_sh(*args, **kw)
            if args and args[0] == 'deploy':
                deploy_old(lane.LANE, tree)
            return r
        twoinst.sh = sh_old
    # every game here starts cold and writes a new log (SporeKit starts its log anew): read each from its start
    offs = {}
    h, g = start(a.fixture)
    lane_state(**{**lane_state(), 'build': ''})  # never reused by a warm test (old files / an extra game)
    old_side = a.old == 'host'
    ps = [P('Alice', h, lane.HOST, old_side), P('Bob', g, lane.GUEST, old_side)]
    c = run_extra(lane_b, 2 * lane_b - 1, 'mp1', None if old_side else tree)
    carol = P('Carol', c, 2 * lane_b - 1, not old_side)
    ps.append(carol)
    ck = Checks()
    host = ps[0]
    carol.call(f'cheat coop join 127.0.0.1:{lane.PORT} Carol')
    t0 = time.time()
    inside = False
    while time.time() - t0 < 240 and not inside:
        st = host.call('coop.status')
        ids = {m.group(2).strip(): int(m.group(1)) for m in re.finditer(r'^\s*\[(\d+)\] (.{1,16})', st, re.M)}
        tog = {int(m.group(1)) for m in re.finditer(r'^together (\d+) 1$', st, re.M)}
        carol.pid = ids.get('Carol')
        inside = carol.pid in tog and bool(carol.con.call('coop.avatar').get('ok')) and \
            'receiving world' not in carol.call('coop.status')
        time.sleep(2)
    ps[0].pid, ps[1].pid = 0, ids.get('Bob', 1)
    ck(inside, f'Carol ({"this build" if old_side else a.old_tag}) plays the host\'s world ({time.time() - t0:.0f} s, '
               f'ids {ids})')
    time.sleep(8)
    mode = {p.name: p.call('game.mode').strip() for p in ps}
    ck(len({m.split()[-1] if m else '' for m in mode.values()}) == 1, f'every game plays the same stage: {mode}')
    # versions
    for p in ps:
        text = log_since(p.inst, offs.get(p.inst, 0))
        for q in ps:
            if q is p:
                continue
            lines = [l for l in text.splitlines() if f"'{q.name}'" in l and ('coop ' in l or 'version unknown' in l)
                     and ('joined' in l or re.search(r"player \d+ '", l))]
            known = [l for l in lines if 'version unknown' not in l]
            ck(known, f'[version] {p.name}\'s game logs {q.name}\'s version: '
               + (known[-1].split('net: ')[-1].strip()[:160] if known else (lines[-1].strip()[-120:] if lines else 'no line')))
    near = NEAR.get(a.fixture, 6.0)
    views(ps, ck, 'join', near)
    # walks
    for p in (ps[0], carol):
        p.call('key 0x57 down')
    time.sleep(3)
    for p in (ps[0], carol):
        p.call('key 0x57 up')
    time.sleep(3)
    views(ps, ck, 'walked', near)
    if not old_side and a.fixture == 'creature':
        d0 = {p.name: dna(p) for p in ps}
        print('  host: ' + host.call('call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:40').strip()[:80], flush=True)
        time.sleep(5)
        d1 = {p.name: dna(p) for p in ps}
        print(f'  DNA before {d0}, after {d1}', flush=True)
        ck(all(v is not None and abs(v - d1['Alice']) <= 1 for v in d1.values()) and d1['Alice'] != d0['Alice'],
           f'[new msgs] the shared DNA (with its cause tail) reached every game, the old one too: {d1}')
        nest_eggs(host, carol)
        views(ps, ck, 'after the nest', near)
    if a.fixture == 'tribe':
        time.sleep(30)  # tribe chunks with the host's time trailer
        views(ps, ck, 'tribe 30 s', near)
    # health
    for p in ps:
        out = p.call('exc').splitlines()
        ck(out and 'total faults: 0' in out[0], f'[health] {p.name} ({"old" if p.old else "this build"}): '
           f'no caught fault ({out[0] if out else "no answer"})')
        ck(p.con.call('ping').get('ok'), f'[health] {p.name}\'s game answers')
    dirs = {os.path.dirname(lane.game_log(p.inst)) for p in ps}
    rep = reports(dirs, t_start)
    ck(not rep, f'[health] no crash / hang report: {rep}')
    # Carol's game log (the other lane's: the lab keeps this lane's only)
    keep = os.path.join(lane.out_dir('mixed'), f'{time.strftime("%Y%m%d-%H%M%S")}-carol-{carol.inst}.log')
    try:
        shutil.copy(lane.game_log(carol.inst), keep)
        print(f'  Carol\'s game log: {os.path.relpath(keep, lane.ROOT)}', flush=True)
    except OSError as e:
        print(f'  (Carol\'s log not kept: {e})', flush=True)
    print(f'  ({time.time() - t_start:.0f} s)', flush=True)
    r = ck.result()
    # the lanes go back to this build's files (the next test of lane B must not start the old release)
    for n in (lane_a, lane_b):
        sh_lane(n, 'kill')
    time.sleep(2)
    for n in (lane_a, lane_b):
        sh_lane(n, 'deploy')
    return r


if __name__ == '__main__':
    raise SystemExit(main())
