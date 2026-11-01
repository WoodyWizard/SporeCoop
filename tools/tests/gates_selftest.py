#!/usr/bin/env python3
"""Self-test of the lab's judges, no game needed (make test; docs/06-testing.md#testing-log-gates,
#testing-monitor, #testing-degraded):

  loggates  the log gates' parsing: mismatch kinds, the time of day over midnight and an hour off
            (Wine's clock around a DST change), forced windows, allow entries
  monitor   every invariant of tools/tests/monitor.py on planted logs: caught when broken, quiet when
            not; the live thread over logs that grow
  netem     the bad network's statistics offline (netem.Model with its own clock): loss, Gilbert-Elliott
            bursts, latency spikes and steps, the bandwidth cap, duplicates; the presets; one port per guest
  fps       SPORE_FPS parsing (twoinst.fps_limits)
  labcov    lab.py coverage flags a test that never ran, the release rule finds a missing / failed test on
            a commit, lab.py fixcheck calls a "SPORE_FIX_OFF=x fails" claim contradicted when the test only
            passed with x off (the world.leave_avatar claim of 0.7.3.3: 5 PASS runs)
  suite     every allow(...) of suite.py names an existing ticket and a reason; the players variants
"""
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [HERE, os.path.join(ROOT, 'tools')]
import loggates  # noqa: E402
import monitor  # noqa: E402

FAILS = []


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what, flush=True)
    if not cond:
        FAILS.append(what)


# ---------------------------------------------------------------- loggates
def loggates_cases():
    k = loggates.mismatch_kinds('t: 3 missing on the guest (t11 t12 t13), p: 1 apart (p1 22.3), dna: host 120.0, '
                                'guest 95.0, vote: host on, guest off')
    check(k == ['t missing', 'p apart', 'dna total', 'vote'], f'loggates: mismatch kinds {k}')
    check(loggates.mismatch_kinds('c: 2 extra on the guest (c1 c2)') == ['c extra'], 'loggates: extra kind')
    # time of day over midnight: the last line on the day of the end
    import datetime
    end = datetime.datetime(2026, 11, 1, 0, 2, 0).timestamp()
    lines = loggates.timed(['23:59:00.000 a', '00:01:00.000 b'], end)
    check(abs(lines[1].t - (end - 60)) < 1 and abs(lines[0].t - (end - 180)) < 1,
          'loggates: lines over midnight get the right days')
    # the game's clock an hour ahead of the lab's (2026-10-29: last line 23:01, the copy 22:01)
    end = datetime.datetime(2026, 10, 29, 22, 1, 28).timestamp()
    lines = loggates.timed(['21:38:31.000 boot', '23:01:19.550 last'], end)
    check(abs(lines[1].t - (end - 8.45)) < 1, f'loggates: a game clock an hour off is put on the lab\'s ({lines[1].t - end:.1f} s)')
    w = loggates.forced_windows('LAB-MARK sync-force on 100.0\nx\nLAB-MARK sync-force off 110.0\n'
                                'LAB-MARK sync-force off 111.0\nLAB-MARK sync-force on 200.0\n', 15)
    check(w == [(99.0, 125.0), (199.0, float('inf'))], f'loggates: forced windows {w}')
    f = dict(gate='sync-check', who='guest', stage='Tribe', kinds=['p apart'], line='x', detail='Tribe: p apart')
    a = [dict(gate='sync-check', ticket='BUG-0001', reason='r', stage='Tribe', kind='t missing', who=None, match=None),
         dict(gate='sync-check', ticket='BUG-0002', reason='r', stage='Tribe', kind='p apart', who=None, match=None)]
    check(loggates.allowed(f, a)['ticket'] == 'BUG-0002', 'loggates: an allow matches its stage and kind only')
    fails, notes = loggates.verdict([f], a[:1])
    check(fails and not notes, 'loggates: a finding no allow covers fails')
    m = loggates.monitor_findings('x\nMONITOR-VIOLATION puppet guest: not shown\n')
    check(len(m) == 1 and m[0]['kinds'] == ['puppet'], 'loggates: monitor lines are findings of the monitor gate')


# ---------------------------------------------------------------- monitor
def own(t, role='host', pid=0, stage='Creature', world='68AC222A', hp=10, maxhp=10, dna=50, res='', players=2,
        away=0, vote='off', avatar='yes', flags=1):
    return (f"12:00:00.000 [s {t:.3f}] [INF] coop: trace: {role} id={pid} players={players} {stage} world={world} "
            f"'X.spo' avatar={avatar} pos=(1.0 2.0 3.0) hp={hp}/{maxhp} species=1234ABCD flags=0x{flags:X} "
            f"dna={dna}{(' ' + res) if res else ''} host_away={away} vote={vote} input_lock=0 editor_pending=-")


def partner(t, pid=1, name='Bob', stage='Creature', world='68AC222A', same=1, puppet='shown', hp=10, maxhp=10, flags=1,
            age=0.1, rtt=30, link=(0, 1000, 0, 0)):
    lk = f' resends={link[0]}/{link[1]} silent={link[2]} ms pending={link[3]}' if link else ''
    return (f"12:00:00.000 [s {t:.3f}] [INF] coop: trace:   partner {pid} '{name}' {stage} world={world} d=5 "
            f"same_place={same} puppet={puppet} hp={hp}/{maxhp} species=1234ABCD flags=0x{flags:X} state_age={age} s "
            f"rtt={rtt} ms{lk}")


def scenario(tmp, name, host_lines, guest_lines, cfg=None):
    paths = {}
    for who, lines in (('host', host_lines), ('guest', guest_lines)):
        p = os.path.join(tmp, f'{name}.{who}.log')
        open(p, 'w').write('\n'.join(lines) + '\n')
        paths[who] = p
    return {inv for inv, _, _, _ in monitor.replay(paths, dict(cfg or {}))}


def clean_pair(n=8, **kw):
    """host (id 0) and guest (id 1) together in Creature for n trace periods"""
    h, g = [], []
    for k in range(n):
        t = 10.0 * k + 1
        link = (5 * k, 1000 * (k + 1), 0, 0)
        h += [own(t, **kw.get('host_own', {})), partner(t + 0.1, link=link, **kw.get('host_sees', {}))]
        g += [own(t + 2, role='guest', pid=1, **kw.get('guest_own', {})),
              partner(t + 2.1, pid=0, name='Alice', link=link, **kw.get('guest_sees', {}))]
    return h, g


def monitor_cases(tmp):
    got = scenario(tmp, 'clean', *clean_pair())
    check(not got, f'monitor: a clean pair has no violation ({got})')
    got = scenario(tmp, 'together', *clean_pair(host_sees=dict(same=0, puppet='hidden: partner in another world')))
    check('together' in got, f'monitor: together: same stage and world by their own games, seen elsewhere ({got})')
    got = scenario(tmp, 'puppet', *clean_pair(guest_sees=dict(puppet='hidden: not spawned (yet)')))
    check(got == {'puppet'}, f'monitor: puppet: a partner in the same place not shown ({got})')
    got = scenario(tmp, 'puppet_dead', *clean_pair(guest_sees=dict(puppet='hidden: partner dead', flags=17)))
    check(not got, f'monitor: puppet: a dead partner hidden is no violation ({got})')
    h, g = clean_pair()
    h = [x.replace('pending=0', 'pending=900') if 'partner' in x else x for x in h]
    got = scenario(tmp, 'link', h, g)
    check(got == {'link'}, f'monitor: link: pending 900 on a clean network ({got})')
    got = scenario(tmp, 'link_net', h, g, dict(netem=True))
    check(not got, f'monitor: link: not judged over a bad network on purpose ({got})')
    h, g = clean_pair()
    h = [x.replace('resends=', 'resends=9') if 'partner' in x else x for x in h]  # 90k, 95k, ...: ratio > 0.5
    h = [x.replace('resends=9', f'resends={900 * i}', 1) if 'partner' in x else x for i, x in enumerate(h)]
    got = scenario(tmp, 'resend', h, g)
    check('link' in got, f'monitor: link: a resend ratio over 0.5 for 3 lines ({got})')
    got = scenario(tmp, 'totals', *clean_pair(host_own=dict(res='food=40'), guest_own=dict(res='food=12')))
    check(got == {'totals'}, f'monitor: totals: food 40 on the host, 12 on the guest for a minute ({got})')
    got = scenario(tmp, 'dna', *clean_pair(host_own=dict(dna=120), guest_own=dict(dna=95)))
    check(got == {'totals'}, f'monitor: totals: dna 120 / 95 ({got})')
    got = scenario(tmp, 'totals_ok', *clean_pair(host_own=dict(res='food=40'), guest_own=dict(res='food=40(unsynced)')))
    check(not got, f'monitor: totals: an unsynced value is not compared ({got})')
    got = scenario(tmp, 'hp', *clean_pair(guest_own=dict(hp=3), host_sees=dict(hp=10)))
    check(got == {'hp'}, f'monitor: hp: the guest at 3/10 by its own game, 10/10 on the host ({got})')
    got = scenario(tmp, 'maxhp', *clean_pair(guest_own=dict(hp=10, maxhp=16), host_sees=dict(hp=10, maxhp=10)))
    check(got == {'hp'}, f'monitor: hp: another max hp ({got})')
    got = scenario(tmp, 'apart', *clean_pair(n=14, guest_own=dict(stage='Tribe', world='11112222'),
                                             guest_sees=dict(same=0, puppet='hidden: partner in another mode'),
                                             host_sees=dict(same=0, puppet='hidden: partner in another mode')))
    check(got == {'stage_apart'}, f'monitor: stage_apart: the guest in Tribe for 2 minutes while the host plays Creature ({got})')
    got = scenario(tmp, 'apart_space', *clean_pair(n=14, host_own=dict(stage='Space', world='80460035'),
                                                   guest_own=dict(stage='Space', world='40460035'),
                                                   guest_sees=dict(same=0, stage='Space', puppet='hidden: partner in another world'),
                                                   host_sees=dict(same=0, stage='Space', puppet='hidden: partner in another world')))
    check(not got, f'monitor: stage_apart: two star systems in Space are no violation ({got})')
    h, g = clean_pair()
    h.append('12:00:00.000 [s 50.0] [INF] coop: tiles: pool 125/512, host list 51, partner lists 3 (74 tiles), level 3, '
             'flushes 6, full 0; cell pool 3900/4096')
    got = scenario(tmp, 'pool', h, g)
    check(got == {'objects'}, f'monitor: objects: the cell pool at 95 % ({got})')
    # the hooks stay off until the mod writes their field, then they are available
    tl = monitor.Timeline()
    for line in clean_pair()[0]:
        tl.add(monitor.parse_line('host', line))
    hooks = {c.name: c(monitor.DEFAULTS).available(tl) for c in monitor.HOOKS}
    check(not any(hooks.values()), f'monitor: the hooks wait for their mod output ({hooks})')
    tl.add(monitor.parse_line('host', partner(99) + ' drawn=(1 2 3)'))
    check(monitor.HOOKS[0](monitor.DEFAULTS).available(tl), 'monitor: the drawn hook is available once a line has drawn=')
    check(all(monitor.NEEDS.get(c.name) for c in monitor.HOOKS), 'monitor: every hook says what mod output it needs')
    # the live thread over growing logs, no console
    paths = {w: os.path.join(tmp, f'live.{w}.log') for w in ('host', 'guest')}
    for p in paths.values():
        open(p, 'w').write('12:00:00.000 [INF] sporekit: an older test\'s line\n')
    lines_out = []
    w = monitor.Watch({who: (0, p) for who, p in paths.items()}, dict(period=0.05), out=lines_out.append)
    w.start()
    h, g = clean_pair(guest_sees=dict(puppet='hidden: not spawned (yet)'))
    for a, b in zip(h, g):
        with open(paths['host'], 'a') as f:
            f.write(a + '\n')
        with open(paths['guest'], 'a') as f:
            f.write(b + '\n')
        time.sleep(0.02)
    time.sleep(0.3)
    v = w.stop()
    check(any(x.startswith('MONITOR-VIOLATION puppet guest:') for x in lines_out) and len(v) == len(lines_out),
          f'monitor: the live thread prints a violation once ({lines_out[:2]})')


def netem_cases():
    import netem

    def run(spec, seconds=600, rate=100, size=200, seed=3):
        m = netem.Model(spec, seed, 0.0)
        sent, got = [], []
        n = int(seconds * rate)
        for i in range(n):
            t = i / rate
            d = m.decide(i % 2, size, t)
            sent.append(t)
            got.append(d)
        return m, sent, got
    m, sent, got = run('loss=0.1')
    loss = sum(not g for g in got) / len(got)
    check(0.09 < loss < 0.11, f'netem: loss=0.1 loses {loss:.3f}')
    m, sent, got = run('delay=50,jitter=20')
    d = [g[0] - t for t, g in zip(sent, got)]
    check(0.030 <= min(d) and max(d) <= 0.070 and abs(sum(d) / len(d) - 0.05) < 0.002,
          f'netem: delay 50 +- 20 ms ({min(d) * 1000:.0f}..{max(d) * 1000:.0f} ms)')
    m, sent, got = run('burst_every=20,burst_s=3,burst_loss=0.3', seconds=3000)
    loss = sum(not g for g in got) / len(got)
    want = 3 / 23 * 0.3
    # loss in 5 s windows: bursts, not a steady trickle
    win = {}
    for t, g in zip(sent, got):
        w = win.setdefault(int(t // 5), [0, 0])
        w[0] += 1
        w[1] += not g
    heavy = sum(1 for a, b in win.values() if b / a >= 0.1)
    clean = sum(1 for a, b in win.values() if b == 0)
    check(abs(loss - want) < want * 0.35 and 90 < m.stats['bursts'] < 170 and heavy >= 40 and clean >= 300,
          f'netem: Gilbert-Elliott bursts: loss {loss:.3f} (expected {want:.3f}), {m.stats["bursts"]} bursts, '
          f'{heavy} 5 s windows with >= 10 % loss, {clean} without loss')
    m, sent, got = run('delay=40,spike_every=30,spike_s=3,spike_ms=250', seconds=600)
    d = [g[0] - t for t, g in zip(sent, got)]
    hi = sum(1 for x in d if x > 0.25) / len(d)
    check(m.stats['spikes'] >= 15 and 0.05 < hi < 0.16, f'netem: spikes: {m.stats["spikes"]}, {hi:.1%} of the packets '
                                                       f'over 250 ms')
    m, sent, got = run('delay=35,step=30:200/60:35', seconds=90)
    d = {int(t): g[0] - t for t, g in zip(sent, got)}
    check(abs(d[10] - 0.035) < 1e-6 and abs(d[45] - 0.2) < 1e-6 and abs(d[80] - 0.035) < 1e-6,
          f'netem: a latency step at 30 s to 200 ms and back at 60 s ({d[10]:.3f}, {d[45]:.3f}, {d[80]:.3f})')
    m, sent, got = run('rate=512,queue=400', seconds=60, rate=200, size=1000)
    up = [g[0] for t, g, i in zip(sent, got, range(len(got))) if g and i % 2 == 0]
    thr = len(up) * 1000 * 8 / (max(up) - min(up)) / 1000
    worst = max(g[0] - t for t, g in zip(sent, got) if g)
    check(480 < thr < 530 and m.stats['rate_drop'] > 0 and worst <= 0.41,
          f'netem: rate=512 kbit/s carries {thr:.0f} kbit/s, drops {m.stats["rate_drop"]} past the 400 ms queue '
          f'(longest wait {worst * 1000:.0f} ms)')
    m, sent, got = run('dup=0.05')
    dups = sum(len(g) == 2 for g in got) / len(got)
    check(0.04 < dups < 0.06, f'netem: dup=0.05 duplicates {dups:.3f}')
    ok = all(netem.parse(k) for k in netem.PRESETS)
    try:
        netem.parse('delay=10,lsos=0.1')
        typo = False
    except ValueError:
        typo = True
    check(ok and typo, 'netem: every preset parses, a misspelt key is refused')
    ports = {(n, w): netem.port_of(n, w) for n in range(1, 11) for w in ('guest', 'extra1', 'extra2')}
    check(len(set(ports.values())) == 30, 'netem: every guest of every lane has its own proxy port')
    # two guests through their own proxies: the host sees two addresses, each guest gets its own answers
    import socket
    host = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    host.bind(('127.0.0.1', 0))
    host.settimeout(0.05)
    px = [netem.Proxy(0, host.getsockname()[1], 'delay=5', seed=i) for i in (1, 2)]
    gs = []
    for i, p in enumerate(px):
        g = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        g.bind(('127.0.0.1', 0))
        g.settimeout(1.0)
        g.sendto(f'hello {i}'.encode(), p.a.getsockname())
        gs.append(g)
    seen = {}
    t0 = time.time()
    while len(seen) < 2 and time.time() - t0 < 2:
        try:
            data, addr = host.recvfrom(100)
        except socket.timeout:
            continue
        seen[addr] = data
        host.sendto(b're ' + data, addr)
    answers = []
    for g in gs:
        try:
            answers.append(g.recvfrom(100)[0])
        except socket.timeout:
            answers.append(None)
    for p in px:
        p.stop = True
    check(len(seen) == 2 and answers == [b're hello 0', b're hello 1'],
          f'netem: two guests through two proxies: {len(seen)} host-side addresses, answers {answers}')
    # the players preset: rtt 70-150 ms most of the time (one-way delay doubled)
    m, sent, got = run('players', seconds=1200)
    rtt = sorted(2 * (g[0] - t) for t, g in zip(sent, got) if g)
    med, p99 = rtt[len(rtt) // 2], rtt[int(len(rtt) * 0.995)]
    check(0.07 <= med <= 0.15 and p99 >= 0.4, f'netem: players: median rtt {med * 1000:.0f} ms, 99.5 % {p99 * 1000:.0f} ms')


def fps_cases():
    import twoinst
    check(twoinst.fps_limits('host:15,guest:30,extra:20') == {'host': 15, 'guest': 30, 'extra': 20},
          'fps: host:15,guest:30,extra:20')
    lim = twoinst.fps_limits('15')
    check(lim == {'host': 15, 'guest': 15, 'extra': 15} and twoinst.fps_of('extra2', lim) == 15, 'fps: one number for all')
    check(twoinst.fps_of('extra1', twoinst.fps_limits('extra1:10')) == 10 and
          twoinst.fps_of('extra2', twoinst.fps_limits('extra1:10')) is None, 'fps: one extra guest')
    try:
        twoinst.fps_limits('gust:15')
        bad = False
    except ValueError:
        bad = True
    check(bad, 'fps: a misspelt role is refused')


def labcov_cases(tmp):
    import json
    import labcov
    lab = os.path.join(tmp, 'cov', 'out', 'lab')

    def rec(run, head, results):
        d = os.path.join(lab, run)
        os.makedirs(d, exist_ok=True)
        rs = []
        for name, ok, out in results:
            open(os.path.join(d, name + '.log'), 'w').write(out)
            rs.append(dict(name=name, ok=ok, seconds=10, log=os.path.join(d, name + '.log')))
        json.dump(dict(head=head, branch='main', results=rs), open(os.path.join(d, 'results.json'), 'w'))
    rec('20261101-010000', 'abc1234 x', [('a', True, 'RESULT: OK\n'), ('b', False, 'RESULT: FAIL\n')])
    rec('20261101-020000-fix-y', 'def5678 y', [('a#1', True, '  SPORE_FIX_OFF: sw.one off (BUG-1)\nRESULT: OK\n'),
                                              ('b', False, '  SPORE_FIX_OFF: sw.two off (BUG-2)\nRESULT: FAIL\n')])
    tests = [dict(name=n, suites={'smoke'} if n != 'c' else {'full'}) for n in ('a', 'b', 'c')]
    runs = labcov.runs_by_test([lab])
    cov = {r[0]: r for r in labcov.coverage(tests, runs)}
    check(cov['c'][6] == 'NEVER RUN' and cov['a'][4] == 2 and cov['a'][3] == 'ok',
          f'labcov: c never ran, a ran twice ({cov["c"][6]}, {cov["a"][4]})')
    rule = dict(labcov.commit_rule(tests, runs, 'abc1234', ['smoke']))
    check(rule == {'a': 'ok', 'b': 'FAIL'}, f'labcov: the release rule on abc1234 ({rule})')
    rule = dict(labcov.commit_rule(tests, runs, 'abc1234', ['smoke', 'full']))
    check(rule.get('c') == 'missing', 'labcov: a test of a required suite that did not run on the commit is missing')
    sp = os.path.join(tmp, 'suite_claims.py')
    open(sp, 'w').write("add('a', P + 'x.py')  # BUG-1 (SPORE_FIX_OFF=sw.one fails)\n"
                        "# BUG-2: the comment above names the switch: SPORE_FIX_OFF=sw.two / sw.three fail\n"
                        "add('b', P + 'y.py')\n")
    fc = {(r[0], r[1]): r[5] for r in labcov.fixcheck(sp, [lab])}
    check(fc == {('a', 'sw.one'): 'CONTRADICTED', ('b', 'sw.two'): 'confirmed', ('b', 'sw.three'): 'unverified'},
          f'labcov: fixcheck ({fc})')


def suite_cases():
    import suite
    tickets = {f.split('-')[0] + '-' + f.split('-')[1] for f in os.listdir(os.path.join(ROOT, 'docs', 'tickets'))
               if f.startswith('BUG-')}
    bad = []
    for t in suite.T:
        for a in list(t.get('allow') or []) + [dict(x, test='*') for x in suite.ALLOW_ALL]:
            if a['ticket'] not in tickets or not (a.get('reason') or '').strip() or a['gate'] not in loggates.GATES:
                bad.append((t['name'], a['ticket'], a['gate']))
        g = t.get('gates') or {}
        unknown = set(g) - set(loggates.DEFAULTS) - {'off'} or set(g.get('off', ())) - set(loggates.GATES)
        if unknown:
            bad.append((t['name'], 'gates', sorted(unknown)))
    names = [t['name'] for t in suite.T]
    pl = [t for t in suite.T if 'players' in t['suites']]
    check(len(names) == len(set(names)) and len(pl) >= 60 and all(not t['warm'] for t in pl if t.get('variant_of'))
          and all('full' not in t['suites'] for t in pl if t.get('variant_of')),
          f'suite: {len(pl)} players variants, cold, outside full; no name twice')
    check(not bad, f'suite: every allow names an existing ticket, a reason and a gate; gates= keys known ({bad[:5]})')


def main():
    print('gates self-test', flush=True)
    loggates_cases()
    with tempfile.TemporaryDirectory(prefix='gatesselftest-') as tmp:
        monitor_cases(tmp)
        labcov_cases(tmp)
    netem_cases()
    fps_cases()
    suite_cases()
    print('RESULT:', 'OK' if not FAILS else f'FAIL ({len(FAILS)})')
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
