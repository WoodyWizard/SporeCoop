#!/usr/bin/env python3
"""Test impact selection (D5, TST-7; docs/06-testing.md#testing-impact): the tests a change can break, for the runs in
between (a fix branch, the integration run after merges). The final check and the release rule stay full.

  lab.py impact [--base <rev>] [--why]     print the selection and why each test is in it
  lab.py run impact [--base <rev>] ...     run it (the other `run` options as usual)

The change: `git diff <merge-base of base and HEAD>` (committed and uncommitted) plus untracked files. Default base: of
`main` and the local `rel/*` branches the one with the nearest merge base (a branch of rel/0.7.15 gets rel/0.7.15).

How a changed file maps to tests (map(), in this order):
  docs, *.md, .claude/, CHANGELOG         nothing beyond the always set
  tools/tests/suite.py                    the entries whose add(...) lines changed
  a test script or helper (tools/tests/*.py, tools/*.py)
                                          every suite entry whose script is it or imports it (transitively, by its
                                          `import x` / `from x import` lines); twoinst / lane / sporectl are imported by
                                          nearly every test, so a change there selects nearly all of them
  the lab's own tools (LAB_TOOLS)         `unit` (their self-tests run in make test)
  tests/*.cpp, tests/*                    `unit`
  a stage module (STAGE_FILES)            every `full` test of that stage, every `full` test without a known stage
                                          (cross-stage: about a fifth of full), the tests that name a switch, message or
                                          symbol the change touches (below)
  data/symbols/*.sym                      the stages of the files that ask for the changed names (a core file among
                                          them: everything)
  anything else (coop.cpp, net/, game.cpp, world.cpp, sporekit/, sdk/, loader/, updater/, Makefile, an unknown
  file)                                   core: every `full` test
  always                                  the `smoke` suite and `unit`
Changed lines of C++ files add: the tests that name a fix switch registered (COOP_FIX) by a name or variable on those
lines (`SPORE_FIX_OFF=<switch>` in suite.py, or the switch's name in the test's script), and the stages of the stage
modules that handle a MSG_* named on those lines.

The stage of a test: its fixture (tools/tests/fixtures.py), else `--fixture X` in its command, else a stage word in its
name (cell / creature / tribe / civ / space, play_tribe ...); without one it is cross-stage.

Limits: the map knows files, not behaviour. A change in a stage module can break another stage through shared state
(a cross-stage regression then shows in the final check); a core file selects all of full (coop.cpp is in most
merges: splitting it, B2, is what makes the selection smaller); a test that reaches code only through a console
command defined elsewhere is found only by its stage; tests outside `full` (nightly-only, players, extended, campaign)
are selected only when their own script or entry changed.
"""
import fnmatch
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STAGES = ('Cell', 'Creature', 'Tribe', 'Civ', 'Space')

STAGE_FILES = {
    'Cell': ['mods/coop/cellsync.*', 'mods/coop/cell_logic.h'],
    'Creature': ['mods/coop/npc.*', 'mods/coop/npc_ids.h', 'mods/coop/nest.*', 'mods/coop/creature_hp.*',
                 'mods/coop/hunt_hits.h', 'mods/coop/drama.*', 'mods/coop/social.*', 'mods/coop/teamplay.*',
                 'mods/coop/parts.*', 'mods/coop/hud_portrait.*'],
    'Tribe': ['mods/coop/tribe.*'],
    'Civ': ['mods/coop/civ.*'],
    'Space': ['mods/coop/space*', 'mods/coop/squadron.*'],
}
NOTHING = ['docs/*', '*.md', '.claude/*', '.github/ISSUE_TEMPLATE/*', 'LICENSE', '.gitattributes', '.gitignore']
LAB_TOOLS = ['tools/lab.py', 'tools/labcov.py', 'tools/labverdict.py', 'tools/labimpact.py', 'tools/loggates.py',
             'tools/tickets.py', 'tools/docs_lint.py', 'tools/re_lint.py', 'tools/glyph_lint.py', 'tools/wt_gc.py',
             'tools/tests/*_selftest.py', 'tools/tests/test_lint.py', 'tools/tests/state_fields_check.py',
             'tools/tests/shards.py', 'tools/tests/fakepeer_check.py', 'tools/crash_triage.py',
             'tools/detector_report.py', 'tools/frames_review.py']
UNIT_FILES = ['tests/*']
WORD_STAGE = {'cell': 'Cell', 'creature': 'Creature', 'tribe': 'Tribe', 'civ': 'Civ', 'space': 'Space'}


def git(*a, cwd=None):
    return subprocess.run(['git', *a], cwd=cwd or ROOT, capture_output=True, text=True, check=True).stdout


def default_base(cwd=None):
    """of main and the local rel/* branches, the one whose merge base with HEAD is the fewest commits back"""
    best = None
    for b in ['main'] + git('for-each-ref', '--format=%(refname:short)', 'refs/heads/rel', cwd=cwd).split():
        try:
            mb = git('merge-base', 'HEAD', b, cwd=cwd).strip()
            n = int(git('rev-list', '--count', f'{mb}..HEAD', cwd=cwd).strip())
        except (subprocess.CalledProcessError, ValueError):
            continue
        if best is None or n < best[0]:
            best = (n, b)
    return best[1] if best else 'main'


def changes(base, cwd=None):
    """({path: [changed lines, + and -]}) between the merge base of base and HEAD and the working tree, untracked
    files included (their whole text)"""
    mb = git('merge-base', base, 'HEAD', cwd=cwd).strip()
    out, cur = {}, None
    for ln in git('diff', '-U0', '--no-color', '--no-renames', mb, cwd=cwd).splitlines():
        if ln.startswith('diff --git '):
            cur = ln.split(' b/', 1)[1]
            out.setdefault(cur, [])
        elif cur and (ln.startswith('+') or ln.startswith('-')) and not ln.startswith(('+++', '---')):
            out[cur].append(ln[1:])
    for p in git('ls-files', '--others', '--exclude-standard', cwd=cwd).split('\n'):
        if p and p not in out:
            try:
                out[p] = open(os.path.join(cwd or ROOT, p), errors='replace').read().splitlines()
            except OSError:
                out[p] = []
    return out


def _match(path, pats):
    return any(fnmatch.fnmatch(path, p) for p in pats)


def file_stage(path):
    for st, pats in STAGE_FILES.items():
        if _match(path, pats):
            return st
    return None


def test_stage(t, fixtures=None):
    """the stage of a suite entry, or None (cross-stage)"""
    if fixtures is None:
        sys.path.insert(0, os.path.join(HERE, 'tests'))
        import fixtures as fx
        fixtures = fx.FIXTURES
    fx = t.get('fixture')
    if not fx:
        m = re.search(r'--fixture[ =](\w+)', t.get('cmd', ''))
        fx = m.group(1) if m else None
    if fx and fx in fixtures:
        return fixtures[fx]['stage']
    for w in re.split(r'[_\W]+', t['name']):
        if w in WORD_STAGE:
            return WORD_STAGE[w]
    return None


_IMPORT = re.compile(r'^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w., ]+))', re.M)


def import_graph(tree):
    """{module: {modules it imports}} of tools/tests/*.py and tools/*.py (names only, the files of the two folders)"""
    mods, graph = {}, {}
    for d in ('tools', os.path.join('tools', 'tests')):
        for f in os.listdir(os.path.join(tree, d)):
            if f.endswith('.py'):
                mods[f[:-3]] = os.path.join(d, f)
    for m, rel in mods.items():
        try:
            text = open(os.path.join(tree, rel), errors='replace').read()
        except OSError:
            continue
        deps = set()
        for a, b in _IMPORT.findall(text):
            for name in ([a] if a else [x.strip().split(' ')[0] for x in b.split(',')]):
                name = name.split('.')[0]
                if name in mods and name != m:
                    deps.add(name)
        graph[m] = deps
    return mods, graph


def users(graph, changed):
    """the modules that are a changed module or import one, transitively"""
    rev = {}
    for m, deps in graph.items():
        for d in deps:
            rev.setdefault(d, set()).add(m)
    seen, todo = set(changed), list(changed)
    while todo:
        for u in rev.get(todo.pop(), ()):
            if u not in seen:
                seen.add(u)
                todo.append(u)
    return seen


def test_scripts(t):
    """the module names of the scripts a suite entry runs"""
    return {os.path.basename(m)[:-3] for m in re.findall(r'tools/(?:tests/)?[\w]+\.py', t.get('cmd', ''))}


_SWITCH_REG = re.compile(r'COOP_FIX(?:_UNSAFE)?\(\s*"([\w.]+)"\s*,\s*"[^"]*"\s*,\s*([\w:.]+)')
_MSG = re.compile(r'\bMSG_[A-Z0-9_]+\b')


def switch_table(tree):
    """{switch: variable} of every COOP_FIX registration in mods/coop"""
    out = {}
    base = os.path.join(tree, 'mods', 'coop')
    for dp, _, fs in os.walk(base):
        for f in fs:
            if f.endswith(('.cpp', '.h')):
                try:
                    text = open(os.path.join(dp, f), errors='replace').read()
                except OSError:
                    continue
                for name, var in _SWITCH_REG.findall(text):
                    out[name] = var.split('.')[-1].split('::')[-1]
    return out


def sources(tree):
    """{repo path: text} of mods/coop's C++ files"""
    out = {}
    base = os.path.join(tree, 'mods', 'coop')
    for dp, _, fs in os.walk(base):
        for f in fs:
            if f.endswith(('.cpp', '.h')):
                p = os.path.join(dp, f)
                try:
                    out[os.path.relpath(p, tree)] = open(p, errors='replace').read()
                except OSError:
                    pass
    return out


def select(tests, changed, tree=ROOT, fixtures=None, claims=None):
    """[(test entry, [why])] of the tests the change can break, in suite order; changed = changes()"""
    mods, graph = import_graph(tree)
    by_name = {t['name']: t for t in tests}
    why = {}

    def add(t, reason):
        why.setdefault(t['name'], [])
        if reason not in why[t['name']]:
            why[t['name']].append(reason)

    full = [t for t in tests if 'full' in t['suites']]
    for t in tests:
        if 'smoke' in t['suites'] or t['name'] == 'unit':
            add(t, 'always (smoke, unit)')
    core, stages, changed_mods, lines = [], set(), set(), []
    src = None
    for path, ch in sorted(changed.items()):
        if _match(path, NOTHING):
            continue
        if path == 'tools/tests/suite.py':
            for ln in ch:
                m = re.match(r"\s*add\('(\w+)'", ln)
                if m and m.group(1) in by_name:
                    add(by_name[m.group(1)], 'its suite entry changed')
            continue
        if _match(path, LAB_TOOLS):
            if 'unit' in by_name:
                add(by_name['unit'], f'lab tool {path}')
            continue
        if path.endswith('.py') and path.startswith('tools/'):
            changed_mods.add(os.path.basename(path)[:-3])
            continue
        if _match(path, UNIT_FILES):
            if 'unit' in by_name:
                add(by_name['unit'], f'unit tests {path}')
            continue
        if path.startswith('data/symbols/') and path.endswith('.sym'):
            names = {m.group(1) for ln in ch for m in [re.match(r'\s*([\w:~<>]+)=', ln)] if m}
            src = src if src is not None else sources(tree)
            for f, text in src.items():
                if any(f'"{n}"' in text for n in names):
                    st = file_stage(f)
                    if st:
                        stages.add((st, f'symbol used by {f}'))
                    else:
                        core.append(f'{path} (a symbol used by {f})')
            continue
        st = file_stage(path)
        if st:
            stages.add((st, path))
            lines += ch
        else:
            core.append(path)
            if path.endswith(('.cpp', '.h')):
                lines += ch
    # test scripts and the helpers they import
    if changed_mods:
        hit = users(graph, changed_mods)
        for t in tests:
            s = test_scripts(t) & hit
            if s:
                add(t, f'its script imports a changed file ({", ".join(sorted(s))})' if not test_scripts(t) & changed_mods
                    else 'its script changed')
    # switches and messages on the changed C++ lines
    if lines:
        table = switch_table(tree)
        text = '\n'.join(lines)
        hit = {sw for sw, var in table.items() if f'"{sw}"' in text or re.search(rf'\b{re.escape(var)}\b', text)}
        if hit:
            if claims is None:
                sys.path.insert(0, HERE)
                import labcov
                claims = labcov.claims(os.path.join(tree, 'tools', 'tests', 'suite.py'))
            named = {}
            for test, sw, _ in claims:
                named.setdefault(sw, set()).add(test)
            for sw in sorted(hit):
                for n in sorted(named.get(sw, ())):
                    if n in by_name:
                        add(by_name[n], f'names switch {sw}')
                for t in tests:
                    for m in test_scripts(t):
                        p = mods.get(m)
                        if p and sw in _read(tree, p):
                            add(t, f'its script names switch {sw}')
        msgs = set(_MSG.findall(text))
        if msgs:
            src = src if src is not None else sources(tree)
            for f, body in src.items():
                st = file_stage(f)
                if st and any(re.search(rf'\b{m}\b', body) for m in msgs):
                    stages.add((st, f'message handled in {f}'))
    if core:
        for t in full:
            add(t, f'core change ({", ".join(core[:3])}{" ..." if len(core) > 3 else ""})')
    for st, src_why in sorted(stages):
        for t in full:
            ts = test_stage(t, fixtures)
            if ts == st:
                add(t, f'{st} stage ({src_why})')
            elif ts is None:
                add(t, f'cross-stage, {st} changed ({src_why})')
    return [(t, why[t['name']]) for t in tests if t['name'] in why]


_cache = {}


def _read(tree, rel):
    k = (tree, rel)
    if k not in _cache:
        try:
            _cache[k] = open(os.path.join(tree, rel), errors='replace').read()
        except OSError:
            _cache[k] = ''
    return _cache[k]


def impact(base=None, tree=ROOT):
    """(base, [(test, [why])]) for the tree's change against base (default: default_base())"""
    sys.path.insert(0, os.path.join(tree, 'tools', 'tests'))
    import suite
    base = base or default_base(tree)
    return base, select(suite.T, changes(base, tree), tree)


def cmd_impact(a, tree=ROOT):
    base, sel = impact(getattr(a, 'base', None), tree)
    print(f'# impact against {base}: {len(sel)} tests')
    for t, why in sel:
        print(f'  {t["name"]}' + (f'  <- {"; ".join(why)[:200]}' if getattr(a, 'why', False) else ''))
    return 0
