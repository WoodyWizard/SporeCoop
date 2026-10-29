#!/usr/bin/env python3
"""Self-tests of the dev tools (make test): they must never touch the owner's own Spore, profile or
saves, and keep the knowledge files honest. Every check runs on fakes in a temporary folder:

  docslint  docs_lint flags a known-issues entry without body, heading or ticket id (BUG-0153)
  noenv     spore.sh and lane.py work without $USER / $HOME (BUG-0252)
  kill      spore.sh kill / pids on fake game processes inside a private PID namespace
            (unshare): lane 1 leaves the owner's game alone, a lane leaves other lanes alone,
            a half-dead game of the lane is ended (BUG-0025, BUG-0101, BUG-0103)
  fixture   spore.sh fixture restore|slot into the player's own profile ("-") is refused and deletes
            nothing (BUG-0102), with HOME pointed at a fake bottle
  headless  headless.sh down with a stale pgid file (the shell is gone, the number now belongs to
            another process group of the user) leaves that group alone
  tickets   tickets.py new: ids used on a branch without a worktree are not given out again
            (BUG-0057); a private ticket made in a worktree is seen from the main checkout and
            survives the worktree's removal (BUG-0062)
  tickets_check  tickets.py check: a new fix names its commits (in history, not all released
            before), an existing test, FAIL and PASS lab run ids or `no-fail-run:`, no path outside the
            repository, known `switch:` names; release refuses such tickets (BUG-0215, BUG-0214)
  relint    re_lint.py flags an overrides entry without a confidence word also when a generated
            .sym has the same name (BUG-0086)
  triage    tools/crash_triage.py reads a report and finds the known ticket (BUG-0158); one signature per
            place: exit-time, log and short crash files, crash line first (BUG-0247)
  campaign  the pure logic of the co-op campaign (campaign_lib.py): plan, stages, findings (BUG-0152)
  release   tools/release_check.sh: the tag must match COOP_VERSION and the notes are exactly the
            tag's CHANGELOG section (BUG-0097)

  tools_selftest.py [check ...]      all checks by default; exit 1 when one fails
Environment: SELFTEST_SPORE / SELFTEST_TICKETS / SELFTEST_RELINT / SELFTEST_RELEASE / SELFTEST_TRIAGE run a check
against another copy of the tool (an old version: the check must FAIL there)."""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SPORE = os.environ.get('SELFTEST_SPORE', os.path.join(ROOT, 'tools', 'spore.sh'))
TICKETS = os.environ.get('SELFTEST_TICKETS', os.path.join(ROOT, 'tools', 'tickets.py'))
RELINT = os.environ.get('SELFTEST_RELINT', os.path.join(ROOT, 'tools', 're_lint.py'))
RELEASE = os.environ.get('SELFTEST_RELEASE', os.path.join(ROOT, 'tools', 'release_check.sh'))
TRIAGE = os.environ.get('SELFTEST_TRIAGE', os.path.join(ROOT, 'tools', 'crash_triage.py'))
HEADLESS = os.environ.get('SELFTEST_HEADLESS', os.path.join(ROOT, 'tools', 'headless.sh'))
fails = []


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what)
    if not cond:
        fails.append(what)


# ---------------------------------------------------------------- kill (inside a PID namespace)
# comm of the half-dead fakes. A real game's comm is SporeApp.exe, and main's spore.sh kill (BUG-0101)
# SIGKILLs every half-dead SporeApp on the machine: the parent PID namespace sees our fakes, so a
# parallel lane's old `spore.sh kill` would end them in the middle of this test. Only a run against
# an old copy (SELFTEST_SPORE, the run that must FAIL) uses the real name.
COMM = b'SporeApp.exe' if 'SELFTEST_SPORE' in os.environ else b'sk-fake-game'
HALF_DEAD = ('import ctypes, threading, time\n'
             f'ctypes.CDLL(None).prctl(15, {COMM!r})  # PR_SET_NAME\n'
             'threading.Thread(target=time.sleep, args=(600,)).start()\n'
             'time.sleep(0.2)\n'
             'ctypes.CDLL(None).syscall(60, 0)  # SYS_exit: only the main thread ends (zombie leader)\n')
SLEEP = 'import time; time.sleep(600)'


# game folders of the fake lanes 1, 2, 3: names no real lane has, so a parallel `spore.sh kill` of a
# real lane (its folder is SPORE or laneN) never matches our fakes
F1, F2, F3 = 'sk-selftest-L1', 'sk-selftest-L2', 'sk-selftest-L3'


def win(folder):
    return f'Z:\\home\\someone\\{folder}\\SporebinEP1\\SporeApp.exe'


def fake(folder, *args, code=SLEEP):
    """a process whose command line looks like a Wine SporeApp.exe of that game folder"""
    return subprocess.Popen([win(folder), '-c', code, *args], executable=sys.executable)


def alive(p):
    return p.poll() is None


def state(p):
    try:
        return open(f'/proc/{p.pid}/stat').read().rsplit(')', 1)[1].split()[0]
    except OSError:
        return '-'


def spore(lane, game, *args):
    env = dict(os.environ, SPORE_LANE=str(lane), SPORE_GAME=game, SPORE_FIXTURES=os.path.join(TMP, 'fx'),
               HOME=TMP, USER=os.environ.get('USER', 'u'))
    return subprocess.run(['bash', SPORE, *args], env=env, capture_output=True, text=True, timeout=60)


def kill_inside():
    """runs as PID 1's child of a fresh PID namespace: only our fakes are visible and killable"""
    t = TMP
    lane1 = os.path.join(t, F1)
    lane2 = os.path.join(t, F2)
    owner = fake(F1)                                       # the owner's own game: no test argument
    host = fake(F1, '-sporekit-profile:test')              # a lane 1 test host
    guest = fake(F1, '-sporekit-instance:2')               # a lane 1 test guest
    l2 = fake(F2, '-sporekit-instance:3', '-sporekit-profile:test')
    l2_half = fake(F2, '-sporekit-instance:4', code=HALF_DEAD)
    l3_half = fake(F3, '-sporekit-instance:6', code=HALF_DEAD)
    time.sleep(1)
    check(state(l2_half) == 'Z' and state(l3_half) == 'Z', 'the half-dead fakes have a zombie leader '
          f'(states {state(l2_half)} {state(l3_half)})')
    if 'pids' in open(SPORE).read():
        out = spore(1, lane1, 'pids', '1').stdout.split()
        check(out == [str(host.pid)], f'lane 1 pids 1: only the test host ({out})')
        out = spore(1, lane1, 'pids', '2').stdout.split()
        check(out == [str(guest.pid)], f'lane 1 pids 2: only the test guest ({out})')
    else:
        check(False, 'spore.sh pids exists (the PIDs of a lane\'s games, for kill_game)')
    spore(2, lane2, 'kill')
    time.sleep(0.5)
    check(not alive(l2), 'lane 2 kill ends the game of lane 2')
    check(not alive(l2_half), 'lane 2 kill ends the half-dead game of lane 2')
    check(alive(l3_half), 'lane 2 kill leaves the half-dead game of lane 3 alone (BUG-0101)')
    check(alive(owner) and alive(host) and alive(guest), 'lane 2 kill leaves lane 1 alone')
    spore(1, lane1, 'kill')
    time.sleep(0.5)
    check(alive(owner), 'lane 1 kill leaves the owner\'s own Spore alone (BUG-0025)')
    check(not alive(host) and not alive(guest), 'lane 1 kill ends the test games of lane 1')
    check(alive(l3_half), 'lane 1 kill leaves lane 3 alone')
    for p in (owner, host, guest, l2, l2_half, l3_half):
        if alive(p):
            p.kill()
            p.wait()


def test_kill():
    print('kill: spore.sh kill on fake games in a private PID namespace')
    if not shutil.which('unshare'):
        print('  skip: no unshare')
        return
    probe = subprocess.run(['unshare', '-Urpf', '--mount-proc', 'true'], capture_output=True)
    if probe.returncode:
        print('  skip: no unprivileged PID namespace here (' + probe.stderr.decode(errors='replace').strip() + ')')
        return
    r = subprocess.run(['unshare', '-Urpf', '--mount-proc', sys.executable, '-I', __file__, '--kill-inside', TMP],
                       env=dict(os.environ), capture_output=True, text=True, timeout=120)
    sys.stdout.write(r.stdout)
    sys.stdout.write(r.stderr[-2000:])
    check(r.returncode == 0, 'the namespace part ran and passed')


# ---------------------------------------------------------------- fixture
def test_fixture():
    print('fixture: restore / slot into the player\'s own profile')
    user = os.environ.get('USER', 'u')
    roaming = os.path.join(TMP, '.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users', user,
                           'AppData/Roaming')
    save = os.path.join(roaming, 'Spore', 'Games', 'Game0', 'x')
    os.makedirs(os.path.dirname(save), exist_ok=True)
    open(save, 'w').write('the player\'s save')
    # a fixture whose restore would replace the save
    src = os.path.join(TMP, 'fxsrc')
    os.makedirs(os.path.join(src, 'Games', 'Game0'), exist_ok=True)
    os.makedirs(os.path.join(TMP, 'fx'), exist_ok=True)
    subprocess.run(['tar', '-C', src, '-czf', os.path.join(TMP, 'fx', 'creature.tgz'), 'Games'], check=True)
    game = os.path.join(TMP, 'SPORE')
    r = spore(1, game, 'fixture', 'restore', 'creature', '-')
    check(r.returncode != 0, f'fixture restore into "-" is refused (rc {r.returncode})')
    check(os.path.exists(save), 'the player\'s save is still there after restore (BUG-0102)')
    r = spore(1, game, 'fixture', 'slot', 'creature', '-', '0')
    check(r.returncode != 0 and os.path.exists(save), 'fixture slot into "-" is refused, the save stays')
    r = spore(1, game, 'fixture', 'restore', 'nosuchfixture', 'test')
    check(r.returncode != 0, 'restore of a missing fixture fails')
    r = spore(1, game, 'fixture', 'restore', 'creature', 'test')
    check(r.returncode == 0 and os.path.isdir(os.path.join(roaming, 'SporeKit-test', 'Spore', 'Games', 'Game0')),
          'restore into a test profile still works')


# ---------------------------------------------------------------- headless
def test_headless():
    print('headless: down with a stale pgid file')
    rt_parent = os.path.join(TMP, 'run')
    rt = os.path.join(rt_parent, 'sporecoop-headless')
    os.makedirs(rt)
    # a process group of "the user" (its own session) that is not our shell
    other = subprocess.Popen([sys.executable, '-c', SLEEP], start_new_session=True)
    open(os.path.join(rt, 'pgid'), 'w').write(f'{other.pid}\n')
    subprocess.run(['bash', HEADLESS, 'down'], env=dict(os.environ, XDG_RUNTIME_DIR=rt_parent),
                   capture_output=True, timeout=30)
    time.sleep(0.3)
    check(alive(other), 'headless.sh down leaves a process group that is not its shell alone')
    if alive(other):
        other.kill()
    other.wait()


# ---------------------------------------------------------------- tickets
def git(cwd, *a):
    return subprocess.run(['git', '-C', cwd, *a], capture_output=True, text=True, check=True).stdout


def test_tickets():
    print('tickets: ids across branches and worktrees, private tickets of worktrees')
    repo = os.path.join(TMP, 'repo')
    os.makedirs(os.path.join(repo, 'tools'))
    os.makedirs(os.path.join(repo, 'docs', 'tickets'))
    shutil.copy(TICKETS, os.path.join(repo, 'tools', 'tickets.py'))
    env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t',
               GIT_COMMITTER_EMAIL='t@t')
    run = lambda cwd, *a: subprocess.run([sys.executable, '-I', os.path.join(cwd, 'tools', 'tickets.py'), *a],
                                         cwd=cwd, env=env, capture_output=True, text=True)
    git(repo, 'init', '-q', '-b', 'main')
    open(os.path.join(repo, '.gitignore'), 'w').write('out/\n')
    subprocess.run(['git', '-C', repo, 'add', '-A'], check=True)
    subprocess.run(['git', '-C', repo, 'commit', '-qm', 'init'], env=env, check=True)
    wt = os.path.join(TMP, 'wt-a')
    git(repo, 'worktree', 'add', '-q', wt, '-b', 'fix/a')
    first = run(wt, 'new', 'found on the way').stdout.strip()
    subprocess.run(['git', '-C', wt, 'add', '-A'], check=True)
    subprocess.run(['git', '-C', wt, 'commit', '-qm', 'ticket'], env=env, check=True)
    git(repo, 'worktree', 'remove', wt)
    second = run(repo, 'new', 'another one').stdout.strip()
    check(os.path.basename(first).startswith('BUG-0001') and os.path.basename(second).startswith('BUG-0002'),
          f'an id committed on a branch without a worktree is not reused (BUG-0057): '
          f'{os.path.basename(first)}, {os.path.basename(second)}')
    wt = os.path.join(TMP, 'wt-sec')
    git(repo, 'worktree', 'add', '-q', wt, '-b', 'fix/sec')
    sec = run(wt, 'new', 'open security hole', '--kind', 'security', '--private').stdout.strip()
    found = run(repo, 'find', 'security hole').stdout
    check('open security hole' in found, f'a private ticket made in a worktree is found from the main checkout '
          f'(BUG-0062): {sec}')
    git(repo, 'worktree', 'remove', '--force', wt)
    found = run(repo, 'find', 'security hole').stdout
    nxt = run(repo, 'new', 'next', '--private').stdout.strip()
    check('open security hole' in found and not os.path.basename(nxt).startswith(os.path.basename(sec)[:8]),
          f'it survives the worktree\'s removal and its id is not given out again ({os.path.basename(nxt)})')


# ---------------------------------------------------------------- tickets check (BUG-0215)
TICKET = """---
id: {id}
title: t
status: fixed
kind: bug
stage: Cell
reported: 2026-10-28
source: x
role: both
versions: 0.7.3.0
fixed_in: {fixed_in}
branch: fix/x
commits: {commits}
tests: {tests}
signature:
docs:
related:
{extra}---

## Verification
{verif}

## History
- 2026-10-28: opened.
"""


def test_tickets_check():
    print('tickets_check: the evidence rules of new fixes (BUG-0215)')
    repo = os.path.join(TMP, 'repo')
    for d in ('tools/tests', 'docs/tickets', 'mods/coop'):
        os.makedirs(os.path.join(repo, d))
    shutil.copy(TICKETS, os.path.join(repo, 'tools', 'tickets.py'))
    open(os.path.join(repo, 'tools', 'tests', 'suite.py'), 'w').write("add('cell_x', 'python3 x')\nadd('unit', 'make test')\n")
    open(os.path.join(repo, 'mods', 'coop', 'a.cpp'), 'w').write('COOP_FIX("pool.purge", "BUG-0001", g, "x");\n')
    env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t',
               GIT_COMMITTER_EMAIL='t@t')
    g = lambda *a: subprocess.run(['git', '-C', repo, *a], env=env, capture_output=True, text=True, check=True).stdout
    g('init', '-q', '-b', 'main')
    g('add', '-A')
    g('commit', '-qm', 'old fix (BUG-0001)')
    old = g('rev-parse', '--short', 'HEAD').strip()
    g('tag', 'v0.7.3.0')
    open(os.path.join(repo, 'x'), 'w').write('x')
    g('add', '-A')
    g('commit', '-qm', 'new fix (BUG-0001)')
    new = g('rev-parse', '--short', 'HEAD').strip()
    path = os.path.join(repo, 'docs', 'tickets', 'BUG-0001-t.md')
    good = dict(id='BUG-0001', fixed_in='unreleased', commits=new, tests='cell_x', extra='',
                verif='FAIL with the fix off: out/lab/20261028-101010. PASS: 20261028-111111.')

    def check(cwd=repo, **kw):
        open(os.path.join(cwd, 'docs', 'tickets', 'BUG-0001-t.md'), 'w').write(TICKET.format(**{**good, **kw}))
        subprocess.run([sys.executable, '-I', os.path.join(cwd, 'tools', 'tickets.py'), 'index'], capture_output=True)
        r = subprocess.run([sys.executable, '-I', os.path.join(cwd, 'tools', 'tickets.py'), 'check'], cwd=cwd,
                           capture_output=True, text=True)
        return r.returncode, r.stdout
    rc, out = check()
    globals()['check'](rc == 0, f'a complete new fix passes ({out.strip().splitlines()[-1] if out.strip() else ""})')
    cases = [
        (dict(commits=''), 'commits', 'a fixed ticket with empty commits: fails'),
        (dict(commits='deadbee'), 'does not exist', 'a commit that does not exist: fails'),
        (dict(commits=old), 'already released', 'commits only in an earlier release: fails (BUG-0167)'),
        (dict(verif='FAIL and PASS, by hand'), 'run ids', 'a lab test named and no run id: fails'),
        (dict(verif='it works'), 'no FAIL run', 'no FAIL / PASS in Verification: fails'),
        (dict(tests='no_such_test'), 'names no lab suite', 'a test name that resolves nowhere: fails'),
        (dict(verif='/tmp/x/run.log ' + good['verif']), 'outside the repository', 'a path outside the repository: fails'),
        (dict(extra='switch: session_end\n'), 'unknown coop.fix name', 'an unknown switch: name fails (BUG-0214)'),
    ]
    for kw, want, what in cases:
        rc, out = check(**kw)
        globals()['check'](rc == 1 and want in out, what)
    rc, out = check(verif='no-fail-run: docs only')
    globals()['check'](rc == 0, 'the same with `no-fail-run: docs only` passes')
    rc, out = check(extra='switch: pool.purge\n')
    globals()['check'](rc == 0, 'a known switch: name passes')
    rc, out = check(fixed_in='0.7.2', commits='')
    globals()['check'](rc == 0, 'an old fix (released up to 0.7.3.0) is not failed retroactively')
    check(commits=old)
    rel = subprocess.run([sys.executable, '-I', os.path.join(repo, 'tools', 'tickets.py'), 'release', '0.7.4'],
                         cwd=repo, capture_output=True, text=True)
    globals()['check'](rel.returncode == 1 and 'BUG-0001' in rel.stdout and 'unreleased' in open(path).read(),
                       'release refuses a fixed ticket without a commit of the release (BUG-0167)')
    g('add', '-A')
    g('commit', '-qm', 'tickets')
    shallow = os.path.join(TMP, 'shallow')
    subprocess.run(['git', 'clone', '-q', '--depth', '1', 'file://' + repo, shallow], capture_output=True, check=True)
    rc, out = check(cwd=shallow, commits='deadbee')
    globals()['check'](rc == 0 and 'git checks skipped' in out, 'a shallow clone: the git checks are skipped with a note')


# ---------------------------------------------------------------- re_lint
def test_relint():
    print('relint: confidence of overrides entries that a generated .sym also names')
    tree = os.path.join(TMP, 'relint')
    os.makedirs(os.path.join(tree, 'tools'))
    shutil.copytree(os.path.join(ROOT, 'data', 'symbols'), os.path.join(tree, 'data', 'symbols'))
    shutil.copytree(os.path.join(ROOT, 'mods'), os.path.join(tree, 'mods'),
                    ignore=shutil.ignore_patterns('*.o', '*.d', '*.dll'))
    shutil.copytree(os.path.join(ROOT, 'sporekit'), os.path.join(tree, 'sporekit'),
                    ignore=shutil.ignore_patterns('*.o', '*.d', '*.dll'))
    shutil.copy(RELINT, os.path.join(tree, 'tools', 're_lint.py'))
    lint = lambda: subprocess.run([sys.executable, '-I', os.path.join(tree, 'tools', 're_lint.py')],
                                  capture_output=True, text=True)
    base = lint()
    check(base.returncode == 0, 're_lint passes on the tree as it is')
    gen = open(os.path.join(ROOT, 'data', 'symbols', 'march2017_dc04aee5.sym'), errors='replace').read()
    import re
    name = re.search(r'^([A-Za-z_][A-Za-z0-9_:]*)\s*=', gen, re.M).group(1)
    ovr = os.path.join(tree, 'data', 'symbols', 'overrides_dc04aee5.sym')
    # a name the overrides file already has (a second entry: which one counts depended on the order)
    dup = re.search(r'^([A-Za-z_][A-Za-z0-9_:]*)\s*=', open(ovr, errors='replace').read(), re.M).group(1)
    with open(ovr, 'a') as f:
        f.write(f'\n{name}=0x00401000 ; TODO\nFoo::NewThing=0x00401000 ; TODO\n{dup}=0x00401000 ; TODO\n')
    r = lint()
    flagged = [l for l in r.stdout.splitlines() if 'say how sure' in l]
    check(any(name in l for l in flagged), f'the override of the generated name {name} is flagged (BUG-0086)')
    check(any('Foo::NewThing' in l for l in flagged), 'the override of a new name is flagged')
    check(any(l.startswith('overrides_dc04aee5.sym:') and dup in l for l in flagged),
          f'a second overrides entry of {dup} without a confidence word is flagged')
    # BUG-0216: a name looked up through a table, only in the GOG 2024 overrides: used, missing on the
    # frozen build, and it needs its frozen_off line; a plain "free" is no symbol
    shutil.copy(RELINT, os.path.join(tree, 'tools', 're_lint.py'))
    shutil.copy(os.path.join(ROOT, 'data', 'symbols', 'overrides_dc04aee5.sym'), ovr)
    with open(ovr, 'a') as f:
        f.write('\nSimulator::FooTable=0x00401000 ; verified test entry\nfree=0x00401000 ; verified test entry\n')
    open(os.path.join(tree, 'mods', 'coop', 'zz_table.cpp'), 'w').write(
        'static const char* t[] = {"Simulator::FooTable", "free"};\n')
    r = lint()
    check(r.returncode == 1 and 'Simulator::FooTable' in r.stdout and 'frozen_off' in r.stdout,
          'a table-looked-up GOG 2024-only name needs a frozen_off line')
    check('free (used' not in r.stdout and ' free,' not in r.stdout, 'a literal "free" is not counted as a symbol')
    fo = os.path.join(tree, 'data', 'symbols', 'frozen_off_922af7c7.txt')
    with open(fo, 'a') as f:
        f.write('Simulator::FooTable ; a test feature (BUG-0001)\n')
    r = lint()
    check(r.returncode == 0, f'with its line it passes ({r.stdout.strip().splitlines()[-1:]})')
    os.remove(os.path.join(tree, 'mods', 'coop', 'zz_table.cpp'))
    r = lint()
    check(r.returncode == 1 and 'no longer used' in r.stdout, 'a frozen_off line for a name no longer used fails')


# ---------------------------------------------------------------- release
def test_release():
    print('release: tag vs COOP_VERSION, notes = exactly the tag\'s section')
    if not os.path.exists(RELEASE):
        check(False, f'{os.path.relpath(RELEASE, ROOT)} exists (the release job\'s checks)')
        return
    tree = os.path.join(TMP, 'rel')
    os.makedirs(os.path.join(tree, 'mods', 'coop'))
    open(os.path.join(tree, 'mods', 'coop', 'coop.cpp'), 'w').write('#define COOP_VERSION "0.7.3.0"\n')
    open(os.path.join(tree, 'CHANGELOG.md'), 'w').write(
        '# Changelog\n\n## [Unreleased]\n\n## [0.7.3.0] - 2026-10-30\n\n- three\n\n## [0.7.3] - 2026-10-01\n\n'
        '- older\n\n## [0.7.2.4] - 2026-09-01\n\n- four\n\n## [0.7.2] - 2026-08-01\n\n- two\n')

    def rel(tag):
        r = subprocess.run(['bash', RELEASE, tag, os.path.join(tree, 'notes.md')], cwd=tree, capture_output=True,
                           text=True)
        notes = open(os.path.join(tree, 'notes.md')).read() if os.path.exists(os.path.join(tree, 'notes.md')) else ''
        return r.returncode, notes
    rc, notes = rel('v0.7.3.0')
    check(rc == 0 and notes.strip() == '- three', f'v0.7.3.0: ok, notes are its own section ({notes.strip()!r})')
    rc, _ = rel('v0.7.3.1')
    check(rc != 0, 'a tag that is not COOP_VERSION fails')
    open(os.path.join(tree, 'mods', 'coop', 'coop.cpp'), 'w').write('#define COOP_VERSION "0.7.2"\n')
    rc, notes = rel('v0.7.2')
    check(rc == 0 and notes.strip() == '- two', f'v0.7.2: notes without the 0.7.2.N sections ({notes.strip()!r})')
    open(os.path.join(tree, 'mods', 'coop', 'coop.cpp'), 'w').write('#define COOP_VERSION "0.7.4"\n')
    rc, _ = rel('v0.7.4')
    check(rc != 0, 'a version without a CHANGELOG section fails')
    # BUG-0216: notes-check, on the real tickets
    notes = os.path.join(tree, 'n.md')

    def nc(body):
        open(notes, 'w').write('### Fixed\n' + body)
        return subprocess.run([sys.executable, '-I', TICKETS, 'notes-check', '9.9.9', notes], capture_output=True,
                              text=True)
    open_t = next((l.split('|')[1].strip().split(']')[0].lstrip('[') for l in
                   open(os.path.join(os.path.dirname(TICKETS), '..', 'docs', 'tickets', 'README.md'))
                   if l.startswith('| [BUG-') and '| open |' in l), None)
    if open_t:
        check(nc(f'- **x** fixed ({open_t}).\n').returncode == 1, f'notes-check: an open ticket ({open_t}) under Fixed fails')
        check(nc(f'- **x** ({open_t}), still open: part 2.\n').returncode == 0, 'notes-check: the same item saying "still open" passes')
    check(nc('- **x** (BUG-9999).\n').returncode == 1, 'notes-check: an id without a ticket fails')


# ---------------------------------------------------------------- crash triage
def test_triage():
    print('triage: crash_triage.py on a made-up report of a known crash')
    tool = TRIAGE
    if not os.path.exists(tool):
        check(False, 'tools/crash_triage.py exists (CLAUDE.md 5 and the fix-player-report skill name it)')
        return
    check(os.path.exists(os.path.join(ROOT, '.claude', 'skills', 'triage-crash', 'SKILL.md')),
          'the triage-crash skill exists')
    rep = os.path.join(TMP, 'crash-20261025-000000-i2.txt')
    open(rep, 'w').write('SporeCoop crash 20261025-000000 (unhandled exception)\n\n== Environment\n'
                         'SporeKit 0.3.4, game GOG 2024 (exe dc04aee5a3debc3f), frames from Present\n'
                         'game mode: Creature, frame 1, up 1 s\nmods: coop 0.7.2.3  \n\n== Exception\n'
                         '0xC0000005 access violation at 0x00C6AE60 SporeApp.exe Foo::Bar+0x20 (thread 1)\n'
                         'read of address 0x00000160\n\n== Call chain\n'
                         '  10043000 coop.i2.1.dll+0x43000\n')
    r = subprocess.run([sys.executable, '-I', tool, rep], capture_output=True, text=True)
    out = r.stdout
    check('GOG 2024' in out and 'coop 0.7.2.3' in out and 'mode Creature' in out, 'build, version and stage read')
    check('dc04aee5:SporeApp.exe Foo::Bar+0x20' in out, 'the signature names build and place')
    check('BUG-0018' in out, 'the known ticket of 0xC6AE60 is found (BUG-0018)')

    # BUG-0247: one signature per place, whatever thread wrote the report and whatever file it is
    def sigs(name, text):
        p = os.path.join(TMP, name)
        open(p, 'w').write(text)
        out = subprocess.run([sys.executable, '-I', tool, p], capture_output=True, text=True).stdout
        return [l for l in out.splitlines() if l.startswith(('signature', 'build'))]
    env = 'SporeKit 0.7.3.0, game GOG 2024 (exe dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37)\n'
    want = 'signature  dc04aee5:SporeApp.exe Simulator::cNest::Foo+0x20   (0xC0000005 at 0x00C6AE60)'
    got = sigs('crash-20261028-101010.txt', 'SporeCoop crash (the game ends (ExitProcess 0), seconds after a fault)\n'
               + env + 'game mode: Creature\n== Exception\n0xC0000005 access violation at 0x00C6AE60 SporeApp.exe '
               'Simulator::cNest::Foo+0x20 (thread 4242, the game\'s main thread)\n')
    check(want in got, f'(a) an exit-time report on the main thread: no thread text in the signature ({got[-1:]})')
    got = sigs('sporekit.log', 'line a\n[ERROR] the game crashed: exception 0xC0000005 at 0x00C6AE60 SporeApp.exe '
               'Simulator::cNest::Foo+0x20\nsymbols: overrides_dc04aee5.sym\nlast line\n')
    check([g for g in got if g.startswith('signature')] == [want], f'(b) the crash line in the middle of a log ({got})')
    got = sigs('crash-20261028-2.txt', 'SporeCoop crash crash-20261028-2 (short: the full report is being written '
               'over this file)\nSporeKit 0.7.3.0, game GOG 2024\nexception 0xC00000FD stack overflow at 0x00401000 '
               'SporeApp.exe Foo+0x1 (thread 12)\n')
    check('signature  dc04aee5:SporeApp.exe Foo+0x1   (0xC00000FD at 0x00401000)' in got,
          f'(c) a short stack-overflow crash file: the full report\'s signature and exe ({got})')
    log = ''.join(f'[WARN] exception 0xC0000005 at 0x0040100{i} SporeApp.exe Bar+0x{i} (data 0x0)\n' for i in range(4))
    got = sigs('sporekit.1.log', 'symbols: overrides_dc04aee5.sym\n' + log + '[ERROR] the game crashed: exception '
               '0xC0000005 at 0x00C6AE60 SporeApp.exe Simulator::cNest::Foo+0x20\n')
    check([g for g in got if g.startswith('signature')][:1] == [want],
          f'(d) four fault-logger lines before the crash line: the crash comes first ({got[1:2]})')
    got = sigs('crash-20261028-3.txt', 'SporeCoop crash\n' + env + 'run: instance 1, profile own, pid 1234\n'
               'mods: coop 0.7.3.0\n== Exception\n0xC0000005 access violation at 0x00C6AE60 SporeApp.exe '
               'Simulator::cNest::Foo+0x20 (thread 7)\n')
    check(want in got and 'build      GOG 2024 (exe dc04aee5), SporeKit 0.7.3.0, coop 0.7.3.0, mode ?' in got,
          f'(e) a `run:` Environment line changes nothing ({got})')


# ---------------------------------------------------------------- noenv (BUG-0252)
def test_noenv():
    print('noenv: the tools without $USER / $HOME (containers, env -i, cron)')
    env = {k: v for k, v in os.environ.items() if k != 'USER'}
    for what, e in (('without USER', dict(env, SPORE_LANE='10')),
                    ('with env -i', {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'SPORE_LANE': '10'})):
        r = subprocess.run(['bash', SPORE, 'status'], env=e, capture_output=True, text=True)
        check(r.returncode == 0 and 'unbound variable' not in r.stderr,
              f'spore.sh status {what} ({r.returncode}, {r.stderr.strip()[-80:]})')
    lane_dir = os.path.join(os.path.dirname(SPORE), 'tests')
    r = subprocess.run([sys.executable, '-I', '-c', f'import sys; sys.path.insert(0, {lane_dir!r}); import lane; '
                        'print(lane.APPDATA)'], env=dict(env, SPORE_LANE='10'), capture_output=True, text=True)
    check(r.returncode == 0 and '/drive_c/users/' in r.stdout, f'lane.py imports without USER ({r.stderr.strip()[-80:]})')


# ---------------------------------------------------------------- docslint (BUG-0153)
def test_docslint():
    print('docslint: known-issues.md entries have a heading, a body and a ticket id')
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import docs_lint
    ok = '## A: x\n<a id="issue-a"></a>\n\n* What happens: y. BUG-0001.\n'
    check(docs_lint.known_issues_problems(ok) == [], 'a complete entry passes')
    check(any('no body' in p for p in docs_lint.known_issues_problems('## A: x\n## B: y\n* z BUG-0002\n')),
          'a heading followed by a heading is flagged')
    check(any('fragment' in p for p in docs_lint.known_issues_problems(ok + '\n of 20-27 m\n\n* body BUG-0003\n')),
          'a lost heading (a paragraph starting with a fragment) is flagged')
    check(any('names no ticket' in p for p in docs_lint.known_issues_problems('## A: x\n\n* y\n')),
          'an entry without BUG-NNNN is flagged')


def test_campaign():
    print('campaign: the pure logic of coop_campaign.py (plan, stages, checks, findings)')
    sys.path.insert(0, HERE)
    import campaign_lib as cl
    import census
    check(cl.plan('cell', 'space') == [('play', 'cell'), ('advance', 'cell'), ('play', 'creature'),
                                       ('advance', 'creature'), ('play', 'tribe'), ('advance', 'tribe'),
                                       ('play', 'civ'), ('advance', 'civ'), ('play', 'space'), ('finale', 'space')],
          'the whole game: five stages, four stage changes, the finale')
    check(cl.plan('tribe', 'civ', finale=True) == [('play', 'tribe'), ('advance', 'tribe'), ('play', 'civ')],
          'a resumed run starts at its stage, no finale before Space')
    try:
        cl.plan('civ', 'cell')
        check(False, 'a plan that goes backwards is refused')
    except ValueError:
        check(True, 'a plan that goes backwards is refused')
    check(cl.stage_of('0x01654C02 Tribe') == 'tribe' and cl.stage_of('0x00DBDBA1 Editor') is None
          and cl.stage_of('0x02CCD1D2 GalaxyGameEntry') is None and cl.stage_of('') is None,
          'game.mode: stages by name, editors and menus are no stage')
    check(cl.faults_of('total faults: 3 (unique 1)') == 3 and cl.faults_of('no answer') is None, 'exc fault count')
    r = cl.res_of('shared food=12.5 money=-3\nshared money=3000')
    check(r == {'food': 12.5, 'money': 3000.0}, f'coop.res parsed ({r})')
    check(cl.res_mismatch({'money': 3000.0, 'food': 10}, {'money': 3040.0, 'food': 11}) == [],
          'an income tick between two reads is no mismatch')
    check(cl.res_mismatch({'money': 3000.0}, {'money': 2000.0}) == [('money', 3000.0, 2000.0)]
          and cl.res_mismatch({'food': 5.0}, {}) == [('food', 5.0, None)], 'a real difference and a missing total')
    head = 'census v1 role={r} me={m} mode=Creature modeid=01654C01 world=AABBCCDD t=1.0\n'
    row = 'obj c{i} key=11111111 alive={a} hp=10 ai=1 own={o} owner=0 pos=({x} 0 0)\n'
    h = census.Census(head.format(r='host', m=0) + row.format(i=1, a=1, o='npc', x=0) + row.format(i=2, a=1, o='npc', x=500)
                      + row.format(i=3, a=1, o='npc', x=5) + row.format(i=4, a=0, o='npc', x=6) + 'obj c-1 key=00000001 alive=1 hp=1 ai=1 own=avatar owner=0 pos=(0 0 0)\n')
    g = census.Census(head.format(r='guest', m=1) + row.format(i=1, a=0, o='proxy', x=0) + row.format(i=9, a=1, o='proxy', x=1)
                      + 'obj c-1 key=00000001 alive=1 hp=1 ai=1 own=avatar owner=1 pos=(2 0 0)\n')
    pr = cl.census_problems(h, g)
    check(any(p.startswith('c1: alive=1') for p in pr), f'census: dead on the guest, alive on the host ({pr})')
    check(any(p.startswith('c3: the host has it') for p in pr) and not any(p.startswith(('c2', 'c4')) for p in pr),
          'census: a missing NPC near the guest counts, one 500 m away (not streamed) or a corpse does not')
    check(any(p.startswith('c9: the guest has it') for p in pr), 'census: a proxy the host does not have')
    other = census.Census(head.replace('AABBCCDD', '11223344').format(r='guest', m=1))
    check(len(cl.census_problems(h, other)) == 1, 'census: another world is one problem, not a list')
    ps = cl.Persist(2)
    check(ps.update(['a', 'b']) == [] and ps.update(['a']) == ['a'] and ps.update(['a']) == []
          and ps.update(['b']) == [] and ps.update(['b']) == ['b'],
          'persist: confirmed once on the second check in a row, a gap resets it')
    folder = os.path.join(TMP, 'run')
    t = [1000.0]
    f = cl.Findings(folder, clock=lambda: t[0])
    t[0] += 61
    check(f.add('desync', 'x', 'Bob: kind n out of sync', 'creature/play', ['Alice', 'Bob'], ['log a']), 'a new finding')
    check(not f.add('desync', 'x', 'again', 'creature/play', ['Bob']), 'the same key again only counts')
    f.attach('desync', 'x', ['shot b'])
    md = open(os.path.join(folder, 'findings.md')).read()
    jl = open(os.path.join(folder, 'findings.jsonl')).read().splitlines()
    check('Bob: kind n out of sync' in md and '* log a' in md and 'shot b' in md and '(+61 s)' in md
          and 'stage creature/play' in md, 'findings.md has the text, the time, the stage and the evidence')
    check(len(jl) == 1 and f.items[0]['repeats'] == 1 and f.summary() == {'desync': 1}, 'one json line, one repeat')
    # the name's time is Wine's local time (an hour off here): the file time decides
    names = [('crash-20261029-010000-i13.txt', 5000), ('crash-20261029-030000-i13.txt', 900),
             ('report-20261029-020000-i27.txt', 1001), ('crash-20261029-030000-i3.txt', 5000),
             ('sporekit.13.log', 5000), ('SporeCoop-report-20261029-020000-i27.zip', 5000)]
    got = cl.new_reports(names, 1000, {13, 14, 27, 37})
    check(got == ['crash-20261029-010000-i13.txt', 'report-20261029-020000-i27.txt'],
          f'new reports: only the instances of the run, only since the start ({got})')


CHECKS = {'noenv': test_noenv, 'docslint': test_docslint, 'kill': test_kill, 'fixture': test_fixture, 'headless': test_headless, 'tickets': test_tickets,
          'tickets_check': test_tickets_check, 'relint': test_relint,
          'release': test_release, 'triage': test_triage, 'campaign': test_campaign}

if __name__ == '__main__':
    if sys.argv[1:2] == ['--kill-inside']:
        TMP = sys.argv[2]
        kill_inside()
        sys.exit(1 if fails else 0)
    want = sys.argv[1:] or list(CHECKS)
    for c in want:
        TMP = tempfile.mkdtemp(prefix=f'sk-selftest-{c}-')
        try:
            CHECKS[c]()
        finally:
            shutil.rmtree(TMP, ignore_errors=True)
    print(f'tools_selftest: {len(want)} check(s), {len(fails)} failure(s)')
    sys.exit(1 if fails else 0)
