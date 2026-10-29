#!/usr/bin/env python3
"""SporeKit's crash reports in hard cases (wave 3, one game of the lane):

  stackoverflow  BUG-0056: a thread recursing without end (`crash.test stackoverflow`): a crash
                 report naming 0xC00000FD is written before the game ends
  oom            BUG-0127: a crash whose report archive fails like an allocation in a full address
                 space (`crash.test oom`): the minidump and logs\\last-problem.txt are still written,
                 the next start tells the player
  prune          BUG-0137: 12 old crash reports + dumps, 12 reports, 12 report archives planted in
                 mods\\logs; `report test`: only the newest 5 / 5 / 5 / 3 stay, the file named in
                 last-problem.txt stays

  thread_worker  BUG-0248: `crash.test thread`: the report's exception line names the thread that
                 crashed (the tid= of the same fault in "Faults caught before"), not the thread that
                 writes the report
  thread_exit    BUG-0248: a repaired fault on a worker thread (`crash.test faultthread`), then
                 `coop.exittest` (ExitProcess on the main thread): the exit report names the worker
                 thread, without the main-thread marker (it named the exiting main thread)

  --old      switch each case's fix off first (`kit.fix <name> 0`): every case must FAIL then
  --case a,b only these cases

  SPORE_LANE=3 python3 tools/tests/kit_crash.py [--old] [--case prune]
"""
import glob
import os
import re
import socket
import sys
import time

import lane
from twoinst import Checks, wait_up
from sporectl import Console  # noqa: E402 (twoinst puts tools/ on the path)

GAME = lane.game_dir()
LOGDIR = os.path.join(GAME, 'SporebinEP1', 'mods', 'logs')
FIX = {'stackoverflow': 'stackov', 'oom': 'crashsafe', 'prune': 'logprune', 'thread_worker': 'crashtid',
       'thread_exit': 'crashtid'}
CASES = list(FIX)


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 60 60000')
    return c


def call(line, timeout=15):
    try:
        return Console(lane.HOST, timeout=timeout).call(line)
    except (OSError, ValueError, socket.timeout):
        return None


def game_gone(secs):
    """True when the console stops answering within secs"""
    for _ in range(secs):
        if call('frame', 3) is None:
            return True
        time.sleep(1)
    return False


def files(pat):
    return set(glob.glob(os.path.join(LOGDIR, pat)))


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def case_stackoverflow(ck):
    before = files('crash-*.txt')
    call('crash.test stackoverflow', 5)
    gone = game_gone(40)
    time.sleep(3)
    new = sorted(files('crash-*.txt') - before)
    print(f'  game gone: {gone}, new crash reports: {[os.path.basename(p) for p in new]}')
    ck(new, 'a stack overflow writes a crash report')
    if new:
        t = open(new[-1], errors='replace').read()
        ck('0xC00000FD' in t, 'the report names exception 0xC00000FD')
        ck('== Call chain' in t or '== Exception' in t or 'stack overflow' in t.lower(), 'it is a crash report')
    log = open(logname(), errors='replace').read() if os.path.exists(logname()) else ''
    ck('0xC00000FD' in log or 'stack overflow' in log, 'the log has the stack overflow line')
    return False


def crash_thread(ck, how, main):
    before = files('crash-*.txt')
    for line in how:
        call(line, 10)
    gone = game_gone(40)
    time.sleep(3)
    new = sorted(files('crash-*.txt') - before)
    print(f'  game gone: {gone}, new crash reports: {[os.path.basename(p) for p in new]}')
    ck(new, f'{how[-1]} writes a crash report')
    if not new:
        return
    t = open(new[-1], errors='replace').read()
    m = re.search(r'^(0x[0-9A-F]{8}) .*? at (0x[0-9A-F]{8}) .*\(thread (\d+)(, the game\'s main thread)?\)$', t, re.M)
    ck(m, 'the report has an exception line')
    if not m:
        return
    code, eip, tid, suffix = m.group(1), m.group(2), m.group(3), m.group(4)
    print('  exception line:', m.group(0))
    # the fault logger's own record of the same fault (code and eip) in "Faults caught before"
    faults = t[t.find('== Faults caught before'):]
    rec = re.search(rf'^\s*{code} x\d+ at {eip} .*?tid=(\d+)', faults, re.M | re.I)
    print('  fault record:', rec.group(0).strip() if rec else None)
    ck(rec, 'the fault logger recorded the same fault')
    if rec:
        ck(rec.group(1) == tid, f'the exception line names the crashed thread ({tid} vs tid={rec.group(1)})')
    ck(bool(suffix) == main, 'the main-thread marker is ' + ('there' if main else 'absent'))


def case_thread_worker(ck):
    crash_thread(ck, ['crash.test thread'], False)


def case_thread_exit(ck):
    crash_thread(ck, ['crash.test faultthread', 'coop.exittest'], False)


def case_oom(ck):
    before = files('crash-*.dmp')
    mark = os.path.join(LOGDIR, 'last-problem.txt')
    if os.path.exists(mark):
        os.remove(mark)
    call('crash.test oom', 5)
    dmp = set()
    for _ in range(40):
        time.sleep(1)
        dmp = files('crash-*.dmp') - before
        if dmp and os.path.exists(mark):
            break
    time.sleep(2)
    print(f'  new minidumps: {[os.path.basename(p) for p in dmp]}, mark: {os.path.exists(mark)}')
    ck(dmp, 'the archive failed, the minidump is still written')
    named = ''
    if os.path.exists(mark):
        named = open(mark, errors='replace').read().strip().splitlines()[-1]
        print('  last-problem.txt names', named)
    target = os.path.join(LOGDIR, named.replace('\\', '/').split('/')[-1]) if named else ''
    ck(named and os.path.exists(target), 'last-problem.txt names an existing crash report')
    if dmp and named:  # the next start tells the player
        c = start_host(ck)
        c.call('wait.frames 300 60000')
        log = open(logname(), errors='replace').read()
        ck('the previous run had a problem: crashed' in log, 'the next start knows the last game crashed')
        st = call('coop.notify', 10)
        print('  notify:', (st['out'] if st else '')[:200].replace('\n', ' | '))
    return False


def plant(n):
    names = []
    for i in range(n):
        st = f'2020010{1 + i // 10}-0000{i % 10:02d}'
        for name in (f'crash-{st}.txt', f'crash-{st}.dmp', f'report-{st}.txt', f'SporeCoop-report-{st}.zip'):
            with open(os.path.join(LOGDIR, name), 'w') as f:
                f.write('planted by kit_crash.py\n')
            names.append(name)
    return names


def case_prune(ck):
    for p in files('crash-*') | files('report-*.txt') | files('SporeCoop-report-*.zip'):
        os.remove(p)
    plant(12)
    # the mark names the oldest archive (as if the previous run crashed then): it must stay
    keep = 'SporeCoop-report-20200101-000000.zip'
    with open(os.path.join(LOGDIR, 'last-problem.txt'), 'w') as f:
        f.write('crashed\n' + os.path.join('C:\\x\\mods\\logs', keep).replace('/', '\\') + '\n')
    time.sleep(1.1)
    r = call('report test', 60)
    print('  report:', (r['out'].strip() if r else 'no answer'))
    n = {k: len(files(p)) for k, p in (('crash txt', 'crash-*.txt'), ('crash dmp', 'crash-*.dmp'),
                                       ('report txt', 'report-*.txt'), ('zip', 'SporeCoop-report-*.zip'))}
    print('  left:', n)
    ck(n['crash txt'] <= 5 and n['crash dmp'] <= 5, f'at most 5 crash reports and dumps stay ({n})')
    ck(n['report txt'] <= 5, 'at most 5 reports stay')
    ck(n['zip'] <= 4, 'at most 3 archives stay, plus the one last-problem.txt names')
    ck(os.path.exists(os.path.join(LOGDIR, keep)), 'the archive last-problem.txt names stays')
    newest = sorted(files('SporeCoop-report-*.zip'))[-1] if files('SporeCoop-report-*.zip') else ''
    ck(r and os.path.basename(newest) in r['out'], 'the new archive is the newest one kept')
    os.remove(os.path.join(LOGDIR, 'last-problem.txt'))
    return True


def main():
    ck = Checks()
    old = '--old' in sys.argv
    cases = CASES
    if '--case' in sys.argv:
        cases = sys.argv[sys.argv.index('--case') + 1].split(',')
    res = {}
    for case in cases:
        start_host(ck)
        print(f'== {case}' + (' (fix off)' if old else ''))
        fx = call(f'kit.fix {FIX[case]} {0 if old else 1}', 10)
        ck(fx is not None and fx['out'].strip().endswith(str(0 if old else 1)), f'kit.fix {FIX[case]}')
        before = ck.ok
        ck.ok = True
        globals()['case_' + case](ck)
        res[case] = ck.ok
        ck.ok &= before
    print('cases:', ', '.join(f'{k}={"PASS" if v else "FAIL"}' for k, v in res.items()))
    lane.sh('kill')
    return ck.result()  # with --old: expected FAIL in every case


if __name__ == '__main__':
    sys.exit(main())
