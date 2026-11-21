#!/usr/bin/env python3
"""Self-test of the lab's verdicts and reports (0.7.15 A6, A7, A8, A15, D5; docs/06-testing.md#testing-lab-verdicts),
no game needed (make test, unit-lab).

  python3 tools/tests/labverdict_selftest.py [--tree <checkout>] [--replay]
  --tree    test the tools of another checkout (rel/0.7.15 before the package: every case FAILs there)
  --replay  also replay the recorded campaign failures of every worktree (not in make test: the records are local):
            how many printed RESULT: FAIL without a FAIL line, and how many get a reason now

Cases:
  verdict     RESULT: INCONCLUSIVE and the older RESULT: SKIP are INCONCLUSIVE, a RESULT: FAIL wins; a recorded result
              without the verdict field maps skipped -> INCONCLUSIVE (A15)
  reasons     FAIL lines as before; a failed campaign without one gets its findings (`FINDING [stuck] ...`, or the
              report's `[desync] HH:MM:SS stage/phase: ...` lines) as the reason, info findings never (A6)
  campaign    campaign_lib: fail_line, a save slot packed as Games/Game0 and unpacked into a fresh slot (members outside
              Games/Game0 are not extracted), its planet, the newest slot, the failed segment of a run's findings (A6)
  twins       the twin knobs of an environment (SPORE_CELL_FIX=0, CAMPAIGN_OLD_TRIBE=1, *_PLANT*, *_TWIN), not
              SPORE_CELL_FIX=1 or CAMPAIGN_STRICT_TRIBE=0; a fix-off line in an older log (A7)
  ledger      twins and quick runs left out of the rates, rel/main apart, a commit with PASS and FAIL counted mixed,
              the INCONCLUSIVE streak and the ticket candidates; labcov's fixcheck never takes an INCONCLUSIVE result
              as a twin half; the release rule lists INCONCLUSIVE apart (A7, A15)
  repeat      the stopping rule (clear / fail / all), the Wilson interval, the rate line (A8)
  lab_run     lab.cmd_run on a fake lane: results.json gets verdict / fix_off (SPORE_LAB_FIXOFF) / twin; --repeat 8
              of a test that alternates stops after both results (clear), --until fail stops at the first FAIL,
              --until all runs every copy; a test INCONCLUSIVE in its 3rd run in a row (the history of the fake
              out/lab) fails as a ticket candidate; a campaign result's CAMPAIGN-SEGMENT line is kept and found by
              find_segment (A6, A7, A8, A15)
  env         environment trouble (a run the lab repeats) only from the harness's own lines: a start wait's
              AssertionError, spore.sh / headless.sh, bwrap; not a fix switch's description, a note or a check that
              says "did not start" (BUG-1379)
  impact      labimpact.select on this tree's sources with made-up changes: docs only -> smoke + unit, a lab tool ->
              unit, a test script -> it and the tests that import it, tribe.cpp -> Tribe tests and cross-stage tests
              but no Cell test, a line with a switch's variable -> the tests that name that switch, coop.cpp -> all of
              full, a changed suite entry -> that test (D5)
"""
import argparse
import contextlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile

FAILS = []
os.environ['SPORE_LAB_MEM_FLOOR_GB'] = '0'


def check(cond, what):
    # the lab fails a test whose output holds the result marker: the unit run prints this self-test, so its texts
    # never spell the marker itself
    print(('  ok   ' if cond else '  FAIL ') + what.replace('RESULT:', 'RESULT='), flush=True)
    if not cond:
        FAILS.append(what)


def verdict_case(lv):
    check(lv.output_verdict('x\nRESULT: SKIP (no second lane free)\n') == (lv.INCONCLUSIVE, 'no second lane free'),
          'verdict: RESULT: SKIP (why) is INCONCLUSIVE with its reason')
    check(lv.output_verdict('RESULT: INCONCLUSIVE the scenario did not happen\n')[0] == lv.INCONCLUSIVE,
          'verdict: RESULT: INCONCLUSIVE')
    check(lv.output_verdict('RESULT: SKIP\nRESULT: FAIL\n')[0] is None, 'verdict: a RESULT: FAIL wins over a SKIP')
    check(lv.output_verdict('RESULT: OK\n')[0] is None, 'verdict: RESULT: OK is no INCONCLUSIVE')
    check(lv.verdict_of({'ok': True, 'skipped': True}) == lv.INCONCLUSIVE and lv.verdict_of({'ok': True}) == lv.PASS
          and lv.verdict_of({'ok': False}) == lv.FAIL, 'verdict: an older result maps skipped -> INCONCLUSIVE')
    check(lv.was_inconclusive({'ok': False, 'verdict': 'FAIL', 'inconclusive': True}),
          'verdict: a streak FAIL is still known as INCONCLUSIVE (never a twin half)')


CAMPAIGN_OUT = '''  campaign: 4 players, cell -> tribe
  FINDING [info] a jump was needed
  FINDING [stuck] the stage change creature -> tribe did not finish in 10 min
  FINDING [desync] Bob: objects of kind n out of sync
  timeline:
  findings: 2
    [info] 01:02:03 creature/advance: a jump was needed
    [stuck] 01:02:04 creature/advance: the stage change creature -> tribe did not finish in 10 min
RESULT: FAIL
'''


def reasons_case(lv):
    check(lv.reasons('FAIL a\nok\n FAIL b\nRESULT: FAIL\n') == ['FAIL a', 'FAIL b'], 'reasons: FAIL lines as before')
    r = lv.reasons(CAMPAIGN_OUT)
    check(r == ['FAIL stuck: the stage change creature -> tribe did not finish in 10 min',
                'FAIL desync: Bob: objects of kind n out of sync'], f'reasons: a campaign\'s findings, no info ({r})')
    only_report = '\n'.join(l for l in CAMPAIGN_OUT.splitlines() if 'FINDING' not in l)
    r = lv.reasons(only_report)
    check(r == ['FAIL stuck creature/advance: the stage change creature -> tribe did not finish in 10 min'],
          f'reasons: the report\'s bracket lines when the FINDING lines are gone ({r})')
    r = lv.reasons('something\nRESULT: FAIL\n')
    check(len(r) == 1 and 'without a FAIL line' in r[0], f'reasons: a FAIL without any reason says so ({r})')
    r = lv.reasons('Traceback (most recent call last):\n  File "x", line 1\n    chunk = recv()\nTimeoutError: timed out\n',
                   failed=True)
    check(r == ['FAIL harness: TimeoutError: timed out'], f'reasons: a traceback without RESULT: its exception ({r})')
    r = lv.reasons('started\n\nLAB: TIMEOUT\n', failed=True)
    check(r == ['FAIL lab: the test ran past its timeout'], f'reasons: the lab\'s timeout ({r})')
    check(lv.reasons(CAMPAIGN_OUT.replace('RESULT: FAIL', 'RESULT: OK')) == [], 'reasons: a passed test has none')
    r = lv.reasons('  FAIL crash_after lane 10 (planted)\nmake[1]: *** [Makefile:177: unit-lint] Error 1\n'
                   'make[1]: *** Waiting for unfinished jobs....\nmake: *** [Makefile:125: test] Error 2\n', failed=True)
    check(r[:2] == ['FAIL make[1]: *** [Makefile:177: unit-lint] Error 1', 'FAIL make: *** [Makefile:125: test] Error 2'],
          f'reasons: make test: the failed part first, before the self-tests\' planted FAIL lines ({r[:2]})')


def campaign_case(tmp):
    import campaign_lib as cl
    check(cl.fail_line('stuck', 'tribe/advance', 'x') == 'FAIL stuck tribe/advance: x', 'campaign: fail_line')
    prof = os.path.join(tmp, 'prof')
    g0, g10 = os.path.join(prof, 'Games', 'Game0'), os.path.join(prof, 'Games', 'Game10')
    for d in (g0, g10, g0 + '.old'):
        os.makedirs(d)
    open(os.path.join(g0, 'Cadian.spo'), 'w').write('old')
    os.utime(os.path.join(g0, 'Cadian.spo'), (1e9, 1e9))
    for n in ('snapshot.spo', 'Zorispe.spo', 'lastSave.pld'):
        open(os.path.join(g10, n), 'w').write(n)
    slot, t = cl.newest_slot(prof)
    check(slot == g10 and t > 1e9, f'campaign: the newest slot is the one written last ({slot})')
    check(cl.slot_planet(g10) == 'Zorispe', 'campaign: a slot\'s planet is its .spo, not snapshot.spo')
    tgz = cl.pack_slot(g10, os.path.join(tmp, 'run', 'segments', 'tribe.tgz'))
    names = tarfile.open(tgz).getnames()
    check('Games/Game0/Zorispe.spo' in names and all(n.startswith('Games/Game0') for n in names),
          f'campaign: pack_slot writes the fixture layout Games/Game0 ({sorted(names)[:3]})')
    dst = cl.unpack_slot(tgz, prof, 30)
    check(os.path.exists(os.path.join(dst, 'Zorispe.spo')) and dst.endswith('Game30')
          and os.path.exists(os.path.join(g10, 'Zorispe.spo')), 'campaign: unpack_slot into a fresh slot, the source kept')
    evil = os.path.join(tmp, 'evil.tgz')
    with tarfile.open(evil, 'w:gz') as tf:
        for name in ('Games/Game0/a.spo', 'outside.txt', 'Games/Game0/../../escape.txt'):
            p = os.path.join(tmp, 'payload')
            open(p, 'w').write('x')
            tf.add(p, arcname=name)
    cl.unpack_slot(evil, prof, 31)
    check(not os.path.exists(os.path.join(prof, 'outside.txt')) and not os.path.exists(os.path.join(tmp, 'escape.txt'))
          and os.path.exists(os.path.join(prof, 'Games', 'Game31', 'a.spo')),
          'campaign: only members under Games/Game0 are extracted')
    items = [dict(kind='info', stage='cell/play', text='i'), dict(kind='stuck', stage='creature/advance', text='s'),
             dict(kind='desync', stage='tribe/play', text='d')]
    segs = {'creature': dict(save='/x/creature.tgz', planet='Cadian'), 'cell': dict(fixture='cell')}
    seg = cl.failed_segment(items, segs, 'space')
    check(seg and seg['stage'] == 'creature' and seg['to'] == 'tribe' and seg['save'] == '/x/creature.tgz'
          and seg['finding'] == 'stuck', f'campaign: the failed segment is the first failing finding\'s stage ({seg})')
    seg = cl.failed_segment([dict(kind='fault', stage='tribe/play', text='f')], {}, 'tribe')
    check(seg and seg['stage'] == 'tribe' and seg['to'] == 'tribe' and 'save' not in seg,
          f'campaign: a failure in the last stage, no save kept: the stage alone from its fixture ({seg})')
    check(cl.failed_segment(items[:1], segs, 'space') is None, 'campaign: info findings fail no segment')


def twins_case(lv, tmp):
    env = {'SPORE_CELL_FIX': '0', 'CAMPAIGN_OLD_TRIBE': '1', 'CAMPAIGN_STRICT_TRIBE': '0', 'UI_GOLDEN_TWIN': 'nudge',
           'CAMPAIGN_PLANT_NOCLICK': '18', 'SPORE_SPACE_FIX': '1', 'PATH': '/bin', 'SPORE_FIX_OFF': 'a.b',
           'NPC_WALK_GUEST_CMDS': 'coop.npc x', 'CAMPAIGN_OLD_EDITOR_DRAG': '0'}
    got = lv.twin_env(env)
    check(got == ['CAMPAIGN_OLD_TRIBE=1', 'CAMPAIGN_PLANT_NOCLICK=18', 'NPC_WALK_GUEST_CMDS=coop.npc x',
                  'SPORE_CELL_FIX=0', 'UI_GOLDEN_TWIN=nudge'], f'twins: the planted knobs of an environment ({got})')
    log = os.path.join(tmp, 'old.log')
    open(log, 'w').write('# cmd\n  SPORE_FIX_OFF: tribe.away off (BUG-0151)\n  SPORE_FIX_OFF (guest, before the join): '
                         'net.x off\nRESULT: FAIL\n')
    check(lv.fix_off_in_log(log) == 'net.x,tribe.away', 'twins: the fix-off lines of an older log')


def rec(lab, run, branch, head, results):
    d = os.path.join(lab, run)
    os.makedirs(d, exist_ok=True)
    rs = []
    for r in results:
        r = dict(r)
        log = os.path.join(d, r['name'] + '.log')
        open(log, 'w').write(r.pop('out', 'RESULT: OK\n'))
        rs.append(dict(dict(ok=True, seconds=5, fails=[], log=log), **r))
    json.dump(dict(head=head, branch=branch, results=rs), open(os.path.join(d, 'results.json'), 'w'))


def ledger_case(lv, tmp):
    sys.path.insert(0, os.path.dirname(lv.__file__))
    import loggates
    import labcov
    lab = os.path.join(tmp, 'ledger', 'out', 'lab')
    rec(lab, '20261101-010000', 'main', 'aaa1111 x', [dict(name='flk'), dict(name='inc', skipped=True)])
    rec(lab, '20261101-020000', 'main', 'aaa1111 x', [dict(name='flk', ok=False, fails=['FAIL one']),
                                                      dict(name='inc', skipped=True)])
    rec(lab, '20261101-030000-fix-y', 'fix/y', 'bbb2222 y', [
        dict(name='flk', ok=False, fails=['FAIL twin'], fix_off='a.b'),
        dict(name='flk#2', ok=False, out='  SPORE_FIX_OFF: c.d off\nRESULT: FAIL\n'),
        dict(name='flk#3', ok=False, quick=True),
        dict(name='inc', skipped=True, verdict='INCONCLUSIVE'),
        dict(name='twinc', ok=True, skipped=True, out='  SPORE_FIX_OFF: e.f off\nRESULT: SKIP (no case)\n'),
        dict(name='twinp', ok=True, out='  SPORE_FIX_OFF: e.f off\nRESULT: OK\n')])
    rows = lv.rows_of(loggates.recorded([lab]), lambda rd, r: lv.fix_off_in_log(loggates.result_logs(rd, r)[1]))
    st = lv.ledger(rows)
    f = st['flk']
    check(f.runs == 2 and f.fails == 1 and f.twins == 2 and f.quick == 1 and f.rel_runs == 2 and f.rel_fails == 1
          and f.mixed_commits == 1, f'ledger: twins (field and log) and the quick run left out, rel/main, mixed ({f})')
    check(st['inc'].streak == 3 and st['inc'].inconclusive == 3 and st['inc'].fails == 0,
          f'ledger: INCONCLUSIVE three runs in a row ({st["inc"]})')
    check([s.test for s in lv.ticket_candidates(st)] == ['inc'], 'ledger: the ticket candidate')
    check(lv.inconclusive_streak(rows, 'inc') == 3 and lv.inconclusive_streak(rows, 'flk') == 0, 'ledger: streak of one test')
    line = lv.known_line(f)
    check('1 of 2 failed on rel/main' in line and 'mixed on 1' in line, f'ledger: the known-flake line ({line})')
    st2 = lv.ledger(rows, branch='fix/*')
    check('flk' not in st2 or st2['flk'].runs == 0, 'ledger: a branch filter')
    sp = os.path.join(tmp, 'claims.py')
    open(sp, 'w').write("add('twinc', P + 'x.py')  # SPORE_FIX_OFF=e.f fails\nadd('twinp', P + 'x.py')  # SPORE_FIX_OFF=e.f fails\n")
    fc = {r[0]: r[5] for r in labcov.fixcheck(sp, [lab])}
    check(fc == {'twinc': 'unverified', 'twinp': 'CONTRADICTED'},
          f'ledger: an INCONCLUSIVE fix-off run is neither half of a twin (fixcheck {fc}; before: twinc "confirmed")')
    tests = [dict(name=n, suites={'smoke'}) for n in ('flk', 'inc')]
    rule = dict(labcov.commit_rule(tests, labcov.runs_by_test([lab]), 'aaa1111', ['smoke']))
    check(rule == {'flk': 'FAIL', 'inc': 'INCONCLUSIVE'}, f'ledger: the release rule lists INCONCLUSIVE apart ({rule})')


def repeat_case(lv):
    P, F, I = lv.PASS, lv.FAIL, lv.INCONCLUSIVE
    check(lv.repeat_stop([P, P, P], 10) == (False, ''), 'repeat: only passes: on to N (a one-in-N bug)')
    check(lv.repeat_stop([P, F], 10)[0], 'repeat: PASS and FAIL seen: clear (mixed)')
    check(not lv.repeat_stop([F, F], 10)[0] and lv.repeat_stop([F, F, F], 10)[0], 'repeat: FAIL 3 of 3: clear')
    check(lv.repeat_stop([I, I, I], 10)[0] and not lv.repeat_stop([I, I, P], 10)[0],
          'repeat: INCONCLUSIVE 3 of 3: clear; a PASS among them: on')
    check(lv.repeat_stop([P, F], 2) == (False, ''), 'repeat: nothing to stop after the last copy')
    check(lv.repeat_stop([P, F], 10, 'fail')[0] and not lv.repeat_stop([P, P], 10, 'fail')[0],
          'repeat: --until fail stops at the first FAIL')
    check(not lv.repeat_stop([P, F, F, F], 10, 'all')[0], 'repeat: --until all never stops')
    lo, hi = lv.wilson(2, 7)
    check(0.07 < lo < 0.10 and 0.58 < hi < 0.64, f'repeat: the 90 % Wilson interval of 2/7 ({lo:.3f}-{hi:.3f})')
    line = lv.repeat_line('x', [P, F, P, I], 10, 'clear: both')
    check('FAIL 1 of 3 (33 %' in line and 'INCONCLUSIVE 1' in line and '4 of 10 runs' in line and 'stopped early' in line,
          f'repeat: the rate line ({line})')


def git(cwd, *a):
    return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', *a], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout


def lab_run_case(tree, tmp):
    import lab
    import suite
    base = os.path.join(tmp, 'labrun')
    game = os.path.join(base, 'game')
    os.makedirs(os.path.join(game, 'SporebinEP1', 'mods', 'logs'))
    git(base, 'init', '-q', '-b', 'main')
    git(base, 'commit', '-q', '--allow-empty', '-m', 'selftest')
    lab.ROOT = base
    lab.preflight = lambda: True
    lab.ensure_lanes = lambda n: None
    lab.spore = lambda *a, **k: subprocess.CompletedProcess(a, 0, '', '')
    lab.lane_game = lambda lane: game
    lab.lane_alive = lambda lane: {2 * lane - 1: False, 2 * lane: False}
    lab.AFTER = 0.1
    os.environ['SPORE_LAB_NO_LANE_LOCK'] = '1'
    os.environ['SPORE_LAB_XWAYLAND'] = '0'
    py = f'{sys.executable} -c'
    cnt = os.path.join(base, 'cnt')
    seg = os.path.join(base, 'segment.json')
    json.dump(dict(stage='creature', to='tribe'), open(seg, 'w'))
    # a test that alternates FAIL, PASS, FAIL, ...
    alt = (f'{py} "import os;p=\'{cnt}\';n=len(open(p).read()) if os.path.exists(p) else 0;open(p,\'a\').write(\'x\');'
           f'print(\'RESULT:\', \'FAIL\' if n % 2 == 0 else \'OK\')"')
    camp = (f'{py} "print(\'  FINDING [stuck] the stage change creature -> tribe did not finish\');'
            f'print(\'CAMPAIGN-SEGMENT {{\\"stage\\": \\"creature\\", \\"to\\": \\"tribe\\", \\"file\\": \\"{seg}\\"}}\');'
            f'print(\'RESULT: FAIL\')"')

    def run(tests, repeat=1, until='clear', env=None):
        suite.T[:] = []
        for name, cmd in tests:
            suite.add(name, cmd, ('smoke',))
        a = argparse.Namespace(branch=None, tests='smoke', repeat=repeat, until=until, skip='', lane=[10], lanes=1,
                               no_build=True, cold=False, retry=0, in_order=False, quick=False, segment=None, base=None)
        old = {k: os.environ.get(k) for k in (env or {})}
        os.environ.update(env or {})
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                lab.cmd_run(a)
        finally:
            for k, v in old.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        labdir = os.path.join(base, 'out', 'lab')
        d = sorted(x for x in os.listdir(labdir) if os.path.isdir(os.path.join(labdir, x)))[-1]
        data = json.load(open(os.path.join(labdir, d, 'results.json')))
        return data, out.getvalue(), open(os.path.join(labdir, d, 'summary.md')).read(), d

    data, out, summary, _ = run([('alt', alt)], repeat=8)
    names = [r['name'] for r in data['results']]
    check(len(names) == 2 and data['repeat'] and 'stopped early' in data['repeat']['lines'][0],
          f'lab_run: --repeat 8 of an alternating test stops after PASS and FAIL ({names}; {data["repeat"]})')
    check('repeat alt: FAIL 1 of 2' in out and '## Repeats' in summary, 'lab_run: the rate is printed and in summary.md')
    os.remove(cnt)
    data, out, _, _ = run([('alt', alt)], repeat=5, until='fail')
    check(len(data['results']) == 1 and data['results'][0]['verdict'] == 'FAIL', 'lab_run: --until fail stops at the first FAIL')
    os.remove(cnt)
    data, out, _, _ = run([('alt', alt)], repeat=4, until='all')
    check(len(data['results']) == 4, 'lab_run: --until all runs every copy')
    data, _, _, _ = run([('fx', f'{py} "print(\'RESULT: OK\')"'), ('other', f'{py} "print(\'RESULT: OK\')"')],
                        env={'SPORE_LAB_FIXOFF': 'fx=tribe.away', 'CAMPAIGN_OLD_TRIBE': '1'})
    res = {r['name']: r for r in data['results']}
    check(res['fx']['fix_off'] == 'tribe.away' and res['other']['fix_off'] == ''
          and res['fx']['twin'] == ['CAMPAIGN_OLD_TRIBE=1'] and res['fx']['verdict'] == 'PASS',
          f'lab_run: results.json records fix_off from SPORE_LAB_FIXOFF and the twin knobs ({res["fx"].get("fix_off")}, '
          f'{res["fx"].get("twin")})')
    inc = f'{py} "print(\'RESULT: SKIP (the case did not happen)\')"'
    data, _, _, _ = run([('incy', inc)])
    r = data['results'][0]
    check(r['verdict'] == 'INCONCLUSIVE' and r['ok'] and r['skipped'] and r['why'] == 'the case did not happen',
          f'lab_run: a SKIP is INCONCLUSIVE, the run not failed by one ({r["verdict"]})')
    data, _, _, _ = run([('incy', inc)])
    check(data['results'][0]['verdict'] == 'INCONCLUSIVE', 'lab_run: INCONCLUSIVE twice in a row: still INCONCLUSIVE')
    data, out, summary, _ = run([('incy', inc)])
    r = data['results'][0]
    check(r['verdict'] == 'FAIL' and not r['ok'] and not data['ok'] and 'INCONCLUSIVE 3 runs in a row' in r['fails'][0]
          and 'ticket candidate' in r['fails'][0], f'lab_run: the third INCONCLUSIVE in a row fails ({r["fails"][:1]})')
    data, out, summary, run_id = run([('camp', camp)])
    r = data['results'][0]
    check(r['fails'] == ['FAIL stuck: the stage change creature -> tribe did not finish']
          and (r.get('segment') or {}).get('stage') == 'creature' and 'campaign_segment --segment' in out,
          f'lab_run: a campaign\'s finding is its reason, its segment kept and the rerun named ({r["fails"]})')
    check(lab.find_segment(run_id, [os.path.join(base, 'out', 'lab')]) == seg, 'lab_run: find_segment by the run id')


def impact_case(tree):
    import labimpact as li
    import suite
    suite.T[:] = []
    import importlib
    importlib.reload(suite)
    T = suite.T
    names = lambda sel: {t['name'] for t, _ in sel}  # noqa: E731
    always = {t['name'] for t in T if 'smoke' in t['suites']} | {'unit'}
    # BUG-1192 / BUG-1193: full needs every test to PASS: no entry that needs --segment, no twin that must FAIL
    seg = [t['name'] for t in T if '$CAMPAIGN_SEGMENT' in t['cmd'] and {'full', 'nightly'} & t['suites']]
    twin = [t for t in T if t['name'] == 'chat_held_send_oldcheck']
    check(not seg and twin and twin[0]['suites'] == {'twin'}
          and {t['name'] for t in suite.select('segment')} == {'campaign_segment'},
          'suite: campaign_segment only in `segment`, the must-FAIL twin chat_held_send_oldcheck only in `twin`, '
          'neither in full')
    sel = names(li.select(T, {'docs/06-testing.md': ['x'], 'CHANGELOG.md': ['y']}, tree))
    check(sel == always, f'impact: docs only -> smoke + unit ({len(sel)} tests)')
    sel = names(li.select(T, {'tools/lab.py': ['x']}, tree))
    check(sel == always, 'impact: a lab tool -> unit (its self-tests) + smoke')
    sel = names(li.select(T, {'tools/tests/coop_tribe_hold_cut.py': ['x']}, tree))
    want = {t['name'] for t in T if 'coop_tribe_hold_cut.py' in t['cmd']}
    check(want and want <= sel and len(sel - always - want) < 10,
          f'impact: a test script -> its entries ({sorted(want)[:3]}), little else ({len(sel)})')
    sel = names(li.select(T, {'tools/tests/coop_ui.py': ['x']}, tree))
    importers = {t['name'] for t in T if 'coop_ui_golden.py' in t['cmd']}
    check(importers <= sel, 'impact: a test library -> the tests that import it')
    full = {t['name'] for t in T if 'full' in t['suites']}
    sel = names(li.select(T, {'mods/coop/tribe.cpp': ['int x = 1;']}, tree))
    tribe = {t['name'] for t in T if 'full' in t['suites'] and li.test_stage(t) == 'Tribe'}
    cell = {t['name'] for t in T if 'full' in t['suites'] and li.test_stage(t) == 'Cell'}
    check(tribe <= sel and not (cell & sel - always) and len(sel) < len(full) * 0.6,
          f'impact: tribe.cpp -> the {len(tribe)} Tribe tests and the cross-stage ones, no Cell test ({len(sel)} of '
          f'{len(full)} full)')
    sel2 = names(li.select(T, {'mods/coop/civ.cpp': ['if (g_fix_build_money) {']}, tree))
    named = {n for n, sw, _ in __import__('labcov').claims(os.path.join(tree, 'tools', 'tests', 'suite.py'))
             if sw == 'civ.build_money'}
    check(named and named <= sel2, f'impact: a changed line with a switch\'s variable -> the tests naming it ({sorted(named)})')
    sel = names(li.select(T, {'mods/coop/coop.cpp': ['x']}, tree))
    check(full <= sel, 'impact: a core file -> all of full')
    sel = names(li.select(T, {'tools/tests/suite.py': ["add('campaign_tc2', P + 'coop_campaign.py')"]}, tree))
    check('campaign_tc2' in sel, 'impact: a changed suite entry -> that test (also outside full)')
    check(li.test_stage(dict(name='npc_walk_tribe', cmd='x', fixture=None)) == 'Tribe'
          and li.test_stage(dict(name='x', cmd='p --fixture civ_npc', fixture=None)) == 'Civ'
          and li.test_stage(dict(name='partner_pass', cmd='x', fixture=None)) is None, 'impact: the stage of a test')


def env_case(lv):
    """BUG-1379: the twin tribe_chief_still_retry_off's FAILs were taken for a game that did not start"""
    env = getattr(lv, 'env_trouble', None)
    if env is None:
        check(False, 'env: labverdict has env_trouble (BUG-1379)')
        return
    fix_line = ("  SPORE_FIX_OFF: tribe.hold_retry off BUG-1347  tribe: a guest's copy of a member takes its item into the "
                "hand again when the first take in the host's animation did not start\n")
    check(not env(fix_line + '  FAIL no stretch over 1.0 s\nRESULT: FAIL\n'),
          'env: a fix switch\'s description with "did not start" is no environment trouble')
    check(not env('  (the guest menu intro did not start)\n  FAIL x\nRESULT: FAIL\n'), 'env: a note of the harness is not')
    check(not env('  FAIL the cinematic did not start\nRESULT: FAIL\n'), 'env: a failed check saying so is not')
    check(not env("RESULT: INCONCLUSIVE the creature never sang in 40 s (the social game did not start)\n"),
          'env: an INCONCLUSIVE reason is not')
    check(env('Traceback (most recent call last):\n  File "x", line 1\nAssertionError: instance 4 did not start\n'),
          'env: a start wait\'s AssertionError is')
    check(env("AssertionError: the guest did not start\n"), 'env: a test\'s own start wait is')
    check(env('the headless test display did not start\n'), 'env: spore.sh\'s display line is')
    check(env("bwrap: Can't find source path /x: No such file or directory\n"), 'env: bwrap\'s line is')


def replay(tree):
    """the recorded campaign failures of every worktree: their reason before and now"""
    import loggates
    import labverdict as lv
    common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=tree,
                            capture_output=True, text=True).stdout.strip()
    n = empty = now = 0
    for rd, data, r in loggates.recorded(loggates.lab_dirs(os.path.dirname(common))):
        if not r['name'].startswith('campaign') or r.get('ok'):
            continue
        n += 1
        if r.get('fails'):
            continue
        empty += 1
        try:
            text = open(loggates.result_logs(rd, r)[1], errors='replace').read()
        except OSError:
            continue
        got = lv.reasons(text, failed=True)
        if got and 'without a FAIL line' not in got[0]:
            now += 1
        else:
            print(f'    still no reason: {os.path.basename(rd)} {r["name"]}')
    print(f'  replay: {n} campaign failures recorded, {empty} without a reason, {now} of them get one now')
    check(empty == 0 or now == empty, f'replay: every empty-reason campaign failure gets a reason ({now} of {empty})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument('--replay', action='store_true')
    a = ap.parse_args()
    tree = os.path.abspath(a.tree)
    sys.path[:0] = [os.path.join(tree, 'tools'), os.path.join(tree, 'tools', 'tests')]
    print(f'lab verdict self-test of {tree}', flush=True)
    try:
        import labverdict as lv
    except ImportError as e:
        check(False, f'no labverdict in the tree ({e})')
        print('RESULT: FAIL')
        return 1
    with tempfile.TemporaryDirectory(prefix='labverdict-') as tmp:
        verdict_case(lv)
        reasons_case(lv)
        campaign_case(tmp)
        twins_case(lv, tmp)
        ledger_case(lv, tmp)
        repeat_case(lv)
        lab_run_case(tree, tmp)
        impact_case(tree)
        env_case(lv)
        if a.replay:
            replay(tree)
    print('RESULT:', 'OK' if not FAILS else f'FAIL ({len(FAILS)})')
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
