#!/usr/bin/env python3
"""Proof of a refactoring that must not change behaviour (B2 of 0.7.15: pure moves; B1 / C8 reuse it).
docs/10-dev-pipeline.md#pipeline-refactor-proof.

  refactor_dump.py capture            a lab test (`lab.py run refactor_dump --lane N --no-build`, suite.py): two
                                      games in a Creature session (twoinst.start), then the registries of both
                                      games and the source tables of the tree, printed as `=== <section>` blocks
                                      into the test's log (out/lab/<run>/refactor_dump.log)
  refactor_dump.py compare A B        A, B: two such logs (or run dirs): every section identical? exit 1 if not
  refactor_dump.py source [REV]       the source tables only (REV: a git revision, default the working tree)
  refactor_dump.py moves REV1 REV2    what a move commit changed besides moving lines: every line of
                                      mods/coop/*.cpp|h whose count differs between the revisions (+ added,
                                      - removed; blank lines ignored). A pure move prints only the includes,
                                      declarations and namespace lines it needed

Sections of `capture` (per game: host, guest):
  fix        `coop.fix list`: the count line, then the switches sorted by name (registration order follows the
             link order of the files, which a move changes; nothing depends on it)
  lifecycle  `coop.lifecycle` without the held counts, in table order (the order of the resets)
  players    `coop.players`: how many modules keep per-player state
  guards     `coop.guard`: the call guards' names, sorted (registration order: link order)
  hooks      SporeKit's `hooks`: mod, target, on/off, symbol, in the order they were made
  commands   `help`: every console command and its help text
Source sections (the tree the build came from):
  msgtable   the message dispatch of the session's custom-message callback (`g_sess.cb.custom`), one line per
             `MSG_` test or `on_message` call, in order, and the count of every `net::MSG_` name over mods/coop
  manifest   the rows of net/messages.def and net/space_kinds.def (B3: the dispatch routes, classes, directions and
             families come from them; the description strings left out)
  callbacks  the session callbacks wired (`g_sess.cb.<name> =`, in order, each with a hash of its lambda's body) and
             the frame / event subscriptions
"""
import collections
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COOP = 'mods/coop'


# ---- source tables -------------------------------------------------------------------------------------------

def _files(rev=None):
    """{path: text} of mods/coop/*.cpp|h (not net/) at `rev` (None: the working tree)"""
    out = {}
    if rev is None:
        d = os.path.join(ROOT, COOP)
        for f in sorted(os.listdir(d)):
            if f.endswith(('.cpp', '.h')):
                out[f'{COOP}/{f}'] = open(os.path.join(d, f), encoding='utf-8', errors='replace').read()
        return out
    names = subprocess.run(['git', '-C', ROOT, 'ls-tree', '--name-only', rev, COOP + '/'], check=True,
                           capture_output=True, text=True).stdout.split()
    for n in sorted(names):
        if n.endswith(('.cpp', '.h')):
            out[n] = subprocess.run(['git', '-C', ROOT, 'show', f'{rev}:{n}'], check=True, capture_output=True,
                                    text=True, errors='replace').stdout
    return out


def _body(text, start):
    """the text of the brace block that opens after `start` (an index into text)"""
    i = text.index('{', start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if not depth:
                return text[i:j + 1]
    return text[i:]


def source_tables(rev=None):
    files = _files(rev)
    sec = collections.OrderedDict()
    disp = []
    for path, text in files.items():
        m = re.search(r'g_sess\.cb\.custom\s*=\s*\[', text)
        if m:
            for ln in _body(text, m.end()).splitlines():
                if 'MSG_' in ln or 'on_message(' in ln or 'Route::' in ln:
                    disp.append(re.sub(r'\s+', ' ', ln.split('//')[0]).strip())
    counts = collections.Counter()
    for path, text in files.items():
        if path.endswith('.cpp'):
            counts.update(re.findall(r'net::(MSG_[A-Z0-9_]+)', text))
    sec['msgtable'] = ['dispatch:'] + disp + ['counts:'] + [f'{k} {v}' for k, v in sorted(counts.items())]
    # B3 (BUG-1136): the manifest rows the dispatch and the classes come from (net/messages.def, net/space_kinds.def)
    rows = []
    for rel in ('mods/coop/net/messages.def', 'mods/coop/net/space_kinds.def'):
        try:
            text = open(os.path.join(ROOT, rel), encoding='utf-8').read() if rev is None else subprocess.run(
                ['git', '-C', ROOT, 'show', f'{rev}:{rel}'], check=True, capture_output=True, text=True).stdout
        except (OSError, subprocess.CalledProcessError):
            continue
        rows += [re.sub(r'\s+', ' ', ln.split('"')[0]).strip() for ln in text.splitlines()
                 if ln.startswith(('MSG(', 'FAMILY(', 'KIND(', 'FEATURE('))]
    if rows:
        sec['manifest'] = rows
    cbs, subs = [], []
    for path, text in files.items():
        if not path.endswith('.cpp'):
            continue
        if 'g_sess.cb.custom' in text or 'sporekit_mod_main' in text:
            for m in re.finditer(r'g_sess\.cb\.(\w+)\s*=\s*(\[?)', text):
                # the callback's body (whitespace folded): a moved callback keeps its hash
                h = hashlib.sha256(re.sub(r'\s+', ' ', _body(text, m.end())).encode()).hexdigest()[:12] \
                    if m.group(2) else '-'
                cbs.append(f'{m.group(1)} {h}')
            subs += [re.sub(r'\s+', ' ', s) for s in re.findall(r'H->subscribe\([^;]*\);', text)]
    sec['callbacks'] = ['session:'] + cbs + ['subscriptions:'] + sorted(subs)
    return sec


# ---- the games' registries -----------------------------------------------------------------------------------

def game_sections(con):
    sec = collections.OrderedDict()
    out = con.call('coop.fix list').get('out', '')
    lines = [ln for ln in out.splitlines() if ln.strip()]
    sec['fix'] = lines[:1] + sorted(lines[1:])
    out = con.call('coop.lifecycle').get('out', '')
    sec['lifecycle'] = [re.sub(r' held=\d+', '', ln) for ln in out.splitlines() if ln.strip()]
    sec['players'] = [ln for ln in con.call('coop.players').get('out', '').splitlines() if ln.strip()]
    out = con.call('coop.guard').get('out', '')
    g = out.splitlines()
    sec['guards'] = [re.sub(r' cleared=\d+| last=.*', '', g[0])] if g else []
    sec['guards'] += sorted(ln.split()[0] for ln in g[1:] if ln.strip())
    sec['hooks'] = [ln.rstrip() for ln in con.call('hooks').get('out', '').splitlines() if ln.strip()]
    sec['commands'] = [ln.rstrip() for ln in con.call('help').get('out', '').splitlines() if ln.strip()]
    return sec


def emit(name, lines):
    print(f'=== {name} ({len(lines)} lines)')
    for ln in lines:
        print('  ' + ln)
    print(f'=== end {name}', flush=True)


def capture():
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'tests'))
    import twoinst  # noqa: E402
    host, guest = twoinst.start('creature')
    twoinst.wait_guest_in(host, guest)
    print('git: ' + subprocess.run(['git', '-C', ROOT, 'describe', '--always', '--dirty'], capture_output=True,
                                   text=True).stdout.strip(), flush=True)
    empty = []
    for who, con in (('host', host), ('guest', guest)):
        for k, v in game_sections(con).items():
            emit(f'{who}.{k}', v)
            if not v:
                empty.append(f'{who}.{k}')
    for k, v in source_tables().items():
        emit(f'source.{k}', v)
    if empty:
        print(f'RESULT: FAIL (empty dumps: {", ".join(empty)})', flush=True)
        return 1
    print('RESULT: PASS (dumps taken; compare two runs with tools/refactor_dump.py compare)', flush=True)
    return 0


# ---- compare ---------------------------------------------------------------------------------------------------

def parse(path):
    if os.path.isdir(path):
        path = os.path.join(path, 'refactor_dump.log')
    sec, cur = collections.OrderedDict(), None
    for ln in open(path, encoding='utf-8', errors='replace'):
        ln = ln.rstrip('\n')
        m = re.match(r'=== (?!end )(\S+) \(\d+ lines\)$', ln)
        if m:
            cur = sec.setdefault(m.group(1), [])
            continue
        if ln.startswith('=== end '):
            cur = None
            continue
        if cur is not None and ln.startswith('  '):
            cur.append(ln[2:])
    return sec


def compare(a, b):
    import difflib
    sa, sb = parse(a), parse(b)
    bad = 0
    for k in list(sa) + [k for k in sb if k not in sa]:
        x, y = sa.get(k), sb.get(k)
        if x == y:
            print(f'{k}: identical ({len(x)} lines)')
            continue
        bad += 1
        print(f'{k}: DIFFERS')
        for d in difflib.unified_diff(x or [], y or [], 'A', 'B', lineterm='', n=1):
            print('    ' + d)
    if not sa:
        print(f'no sections in {a}')
        bad += 1
    print('RESULT: ' + ('identical' if not bad else f'{bad} section(s) differ'))
    return 1 if bad else 0


def moves(rev1, rev2):
    def count(rev):
        c = collections.Counter()
        for text in _files(rev).values():
            c.update(ln.rstrip() for ln in text.splitlines() if ln.strip())
        return c
    c1, c2 = count(rev1), count(rev2 if rev2 != '.' else None)
    added = sorted((c2 - c1).items())
    removed = sorted((c1 - c2).items())
    for ln, n in removed:
        print(f'- {ln}' + (f'  (x{n})' if n > 1 else ''))
    for ln, n in added:
        print(f'+ {ln}' + (f'  (x{n})' if n > 1 else ''))
    print(f'{sum(n for _, n in removed)} line(s) removed, {sum(n for _, n in added)} added besides the moved ones')
    return 0


def main(argv):
    if len(argv) >= 1 and argv[0] == 'capture':
        return capture()
    if len(argv) == 3 and argv[0] == 'compare':
        return compare(argv[1], argv[2])
    if len(argv) in (1, 2) and argv[0] == 'source':
        for k, v in source_tables(argv[1] if len(argv) > 1 else None).items():
            emit(f'source.{k}', v)
        return 0
    if len(argv) == 3 and argv[0] == 'moves':
        return moves(argv[1], argv[2])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
