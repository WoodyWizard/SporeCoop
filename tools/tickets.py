#!/usr/bin/env python3
"""Project tickets: one Markdown file per bug report, crash or task in docs/tickets/ (CLAUDE.md 1).

  tickets.py new "<title>" --kind crash --stage Creature [--source ...] [--role guest]
                                         create the next BUG-NNNN file and print its path
  tickets.py list [--status open] [--kind crash] [--stage Creature]
  tickets.py show BUG-0012               the ticket file
  tickets.py find <text>                 tickets whose fields or text match (an address, a symptom, a test)
  tickets.py log BUG-0012                git commits that name the ticket ("BUG-0012" in the message)
  tickets.py index                       rewrite docs/tickets/README.md (the table of all tickets)
  tickets.py release 0.7.3               fixed tickets with fixed_in "unreleased" get that version
  tickets.py check [--all]               (in make test) fields valid, ids unique, index up to date, and the
                                         evidence of new fixes (BUG-0215); --all: every rule on every
                                         ticket, a report only (exit 0)

Private tickets (security problems not fixed yet, anything with player data) go to the MAIN
checkout's out/tickets/ (not in git, shared by every worktree) with --private; `list`/`find` read
both places. A new id is free on every local branch, in every worktree and in out/tickets/."""
import argparse
import os
import re
import subprocess
import sys
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC = os.path.join(ROOT, 'docs', 'tickets')


def main_checkout():
    """the main checkout (the common git dir's folder): shared by every worktree"""
    try:
        common = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                capture_output=True, text=True, check=True).stdout.strip()
        return os.path.dirname(common) if common else ROOT
    except (OSError, subprocess.CalledProcessError):
        return ROOT


# private tickets live in ONE place for all worktrees, the main checkout's out/tickets: a worktree's
# own out/ (ignored by git) is deleted with the worktree, and the main checkout never saw it (BUG-0062)
PRIVATE = os.path.join(main_checkout(), 'out', 'tickets')
INDEX = os.path.join(PUBLIC, 'README.md')
FIELDS = ['id', 'title', 'status', 'kind', 'stage', 'reported', 'source', 'role', 'versions', 'fixed_in',
          'branch', 'commits', 'tests', 'signature', 'docs', 'related']
# optional fields: `switch:` the coop.fix names of the fix's test switches (BUG-0214)
LAST_OLD_RELEASE = (0, 7, 3, 0)  # the evidence rules apply to fixes released after it (BUG-0215)
STATUS = ['open', 'fixed', 'not-reproduced', 'game-bug', 'wontfix', 'duplicate']
KIND = ['crash', 'freeze', 'desync', 'bug', 'feature', 'security', 'test', 'tooling']
ID_RE = re.compile(r'^BUG-(\d{4})$')

INDEX_HEAD = """# Tickets

Every player report, crash and bug found gets a ticket: one file `BUG-NNNN-<slug>.md` here, made with
`tools/tickets.py new`. The file says what was reported, the root cause, the fix (branch, commits,
version) and the test that catches it; its `## History` grows by dated lines. Commit messages of a
fix name the ticket (`BUG-0012`), so `tools/tickets.py log BUG-0012` finds them. Security problems
that are not fixed yet stay in `out/tickets/` (not in git) until the fix is released.

This table is written by `tools/tickets.py index` (checked by `make test`). Statuses: open, fixed,
not-reproduced, game-bug (Spore itself), wontfix, duplicate.

| Ticket | Status | Kind | Stage | Reported | Fixed in | Title |
|---|---|---|---|---|---|---|
"""

TEMPLATE = """---
id: {id}
title: {title}
status: open
kind: {kind}
stage: {stage}
reported: {today}
source: {source}
role: {role}
versions: {versions}
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- {today}: opened.
"""


def parse(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    m = re.match(r'---\n(.*?)\n---\n', text, re.S)
    meta = {}
    if m:
        for line in m.group(1).splitlines():
            if ':' in line:
                k, v = line.split(':', 1)
                meta[k.strip()] = v.strip()
    meta['_path'] = path
    meta['_text'] = text
    return meta


def all_tickets(private=True):
    out = []
    for d in [PUBLIC] + ([PRIVATE] if private else []):
        if not os.path.isdir(d):
            continue
        for n in sorted(os.listdir(d)):
            if re.match(r'^BUG-\d{4}-.*\.md$', n):
                t = parse(os.path.join(d, n))
                t['_private'] = d == PRIVATE
                out.append(t)
    return out


def slug(s):
    s = re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')
    return s[:48].rstrip('-') or 'ticket'


def worktree_ticket_dirs():
    """docs/tickets of every worktree of this repository (parallel agents) and the main checkout's
    out/tickets: a new id must be free in all of them."""
    dirs = {PUBLIC, PRIVATE}
    try:
        out = subprocess.run(['git', '-C', ROOT, 'worktree', 'list', '--porcelain'], capture_output=True,
                             text=True, check=True).stdout
        for line in out.splitlines():
            if line.startswith('worktree '):
                wt = line[len('worktree '):]
                dirs.add(os.path.join(wt, 'docs', 'tickets'))
                dirs.add(os.path.join(wt, 'out', 'tickets'))
    except (OSError, subprocess.CalledProcessError):
        pass
    return dirs


def branch_ticket_names():
    """docs/tickets file names committed on any local branch or remote-tracking ref: a branch whose
    worktree is gone (or was never checked out here) still owns its ids (BUG-0057)"""
    names = []
    try:
        refs = subprocess.run(['git', '-C', ROOT, 'for-each-ref', '--format=%(refname)', 'refs/heads', 'refs/remotes'],
                              capture_output=True, text=True, check=True).stdout.split()
        for ref in refs:
            out = subprocess.run(['git', '-C', ROOT, 'ls-tree', '--name-only', ref, 'docs/tickets/'],
                                 capture_output=True, text=True).stdout
            names += [os.path.basename(n) for n in out.split()]
    except (OSError, subprocess.CalledProcessError):
        pass
    return names


def next_id():
    names = branch_ticket_names()
    for d in worktree_ticket_dirs():
        if os.path.isdir(d):
            names += os.listdir(d)
    nums = [0] + [int(m.group(1)) for n in names if (m := re.match(r'^BUG-(\d{4})-', n))]
    return f'BUG-{max(nums) + 1:04d}'


def index_text():
    rows = []
    for t in all_tickets(private=False):
        name = os.path.basename(t['_path'])
        rows.append(f"| [{t.get('id', '?')}]({name}) | {t.get('status', '')} | {t.get('kind', '')} | "
                    f"{t.get('stage', '')} | {t.get('reported', '')} | {t.get('fixed_in', '')} | "
                    f"{t.get('title', '').replace('|', '/')} |")
    return INDEX_HEAD + '\n'.join(rows) + '\n'


def cmd_new(a):
    import fcntl
    d = PRIVATE if a.private else PUBLIC
    os.makedirs(d, exist_ok=True)
    # one lock for all worktrees: the common git dir is shared by them
    common = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                            capture_output=True, text=True).stdout.strip()
    lock = open(os.path.join(common, 'tickets.lock') if common else os.path.join(d, '.lock'), 'w')
    fcntl.flock(lock, fcntl.LOCK_EX)
    tid = next_id()
    path = os.path.join(d, f'{tid}-{slug(a.title)}.md')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(TEMPLATE.format(id=tid, title=a.title, kind=a.kind, stage=a.stage, today=date.today().isoformat(),
                                source=a.source, role=a.role, versions=a.versions))
    if not a.private:
        with open(INDEX, 'w', encoding='utf-8') as f:
            f.write(index_text())
    print(path)
    print('reminder: a fixed ticket lists its fix commits in `commits:` and its Verification names the FAIL and '
          'PASS lab run ids (or `no-fail-run: <reason>`); tickets.py check fails otherwise (BUG-0215)',
          file=sys.stderr)


def cmd_list(a):
    for t in all_tickets():
        if a.status and t.get('status') != a.status:
            continue
        if a.kind and t.get('kind') != a.kind:
            continue
        if a.stage and t.get('stage', '').lower() != a.stage.lower():
            continue
        p = ' (private)' if t['_private'] else ''
        print(f"{t.get('id')}  {t.get('status', ''):<14} {t.get('kind', ''):<8} {t.get('stage', ''):<12} "
              f"{t.get('fixed_in', '') or '-':<10} {t.get('title', '')}{p}")


def find_ticket(tid):
    for t in all_tickets():
        if t.get('id') == tid:
            return t
    sys.exit(f'no ticket {tid}')


def cmd_show(a):
    print(find_ticket(a.id)['_text'])


def cmd_find(a):
    rx = re.compile(re.escape(a.text), re.I)
    for t in all_tickets():
        if rx.search(t['_text']):
            hits = [l.strip() for l in t['_text'].splitlines() if rx.search(l)][:3]
            print(f"{t.get('id')}  {t.get('status')}  {t.get('title')}")
            for h in hits:
                print(f'    {h[:140]}')


def cmd_log(a):
    t = find_ticket(a.id)
    subprocess.run(['git', '-C', ROOT, 'log', '--all', '--format=%h %ad %s', '--date=short', f'--grep={a.id}'])
    for c in re.split(r'[,\s]+', t.get('commits', '')):
        if c:
            subprocess.run(['git', '-C', ROOT, 'log', '-1', '--format=%h %ad %s', '--date=short', c],
                           stderr=subprocess.DEVNULL)


def cmd_index(a):
    os.makedirs(PUBLIC, exist_ok=True)
    with open(INDEX, 'w', encoding='utf-8') as f:
        f.write(index_text())
    print(INDEX)


def release_blockers(tickets, ctx):
    """BUG-0215 D: fixed tickets about to get the version whose commits are missing or were all
    released before (BUG-0167 got 0.7.3.0 for a commit of 0.6.0)"""
    out = []
    for t in tickets:
        if t.get('status') != 'fixed' or t.get('fixed_in', '') not in ('', 'unreleased'):
            continue
        cs = commits_of(t)
        if not cs:
            out.append(f'{t.get("id")}: no commits listed')
        elif ctx.git and all(ctx.git.resolve(c) and ctx.git.first_tag(c) for c in cs):
            out.append(f'{t.get("id")}: every commit is in an earlier release ({", ".join(cs)})')
    return out


def cmd_release(a):
    blockers = release_blockers(all_tickets(), Ctx('all'))
    if blockers and not a.force:
        for b in blockers:
            print(b)
        print(f'release {a.version}: {len(blockers)} fixed ticket(s) without a commit of this release; fix them '
              f'(commits:, status) or run again with --force')
        return 1
    n = 0
    for t in all_tickets():
        if t.get('status') == 'fixed' and t.get('fixed_in', '') in ('', 'unreleased'):
            text = re.sub(r'^fixed_in:.*$', f'fixed_in: {a.version}', t['_text'], count=1, flags=re.M)
            text = text.rstrip('\n') + f'\n- {date.today().isoformat()}: released in {a.version}.\n'
            with open(t['_path'], 'w', encoding='utf-8') as f:
                f.write(text)
            n += 1
            print(t.get('id'), t.get('title'))
    cmd_index(a)
    print(f'{n} ticket(s) marked fixed in {a.version}')


# ---------------------------------------------------------------- check (BUG-0215)
# The rules are functions in CHECKS: fn(ticket, ctx) -> [problem, ...]. Hygiene rules apply to every
# ticket; evidence rules (scope "new") only to fixed tickets whose fixed_in is "unreleased" or a
# version after LAST_OLD_RELEASE. `check --all` runs every rule on every ticket and only reports.
# Other branches add rules to CHECKS (BUG-0214's `switch:` rule, the known-issues rules).

def version_tuple(v):
    return tuple(int(x) for x in v.split('.')) if re.match(r'^\d+(\.\d+)+$', v or '') else None


class Git:
    """cached git lookups; None from git_ctx() in a shallow clone or without .git"""

    def __init__(self, root):
        self.root = root
        self._resolve, self._anc, self._tag = {}, {}, {}

    def _run(self, *a):
        return subprocess.run(['git', '-C', self.root, *a], capture_output=True, text=True)

    def resolve(self, h):
        if h not in self._resolve:
            r = self._run('rev-parse', '--verify', '--quiet', h + '^{commit}')
            self._resolve[h] = r.stdout.strip() if r.returncode == 0 else None
        return self._resolve[h]

    def is_ancestor(self, h):
        if h not in self._anc:
            self._anc[h] = self._run('merge-base', '--is-ancestor', h, 'HEAD').returncode == 0
        return self._anc[h]

    def first_tag(self, h):
        """the earliest release tag vX.Y.Z that contains h, as a version tuple (None: in no release)"""
        if h not in self._tag:
            tags = [version_tuple(t[1:]) for t in self._run('tag', '--contains', h).stdout.split()
                    if t.startswith('v')]
            tags = [t for t in tags if t]
            self._tag[h] = min(tags) if tags else None
        return self._tag[h]

    def grep(self, tid):
        r = self._run('log', '--format=%h', f'--grep={tid}\\b', '-E', 'HEAD')
        return r.stdout.split()


def git_ctx(root):
    r = subprocess.run(['git', '-C', root, 'rev-parse', '--is-shallow-repository'], capture_output=True, text=True)
    if r.returncode != 0 or r.stdout.strip() != 'false':
        return None
    return Git(root)


class Ctx:
    def __init__(self, scope, root=ROOT):
        self.scope = scope  # "new" or "all"
        self.root = root
        self.git = git_ctx(root)
        self.notes = []
        suite = os.path.join(root, 'tools', 'tests', 'suite.py')
        text = open(suite, encoding='utf-8').read() if os.path.exists(suite) else ''
        self.suites = set(re.findall(r"^add\('([^']+)'", text, re.M))
        self.fix_names = set()
        cdir = os.path.join(root, 'mods', 'coop')
        if os.path.isdir(cdir):
            for n in os.listdir(cdir):
                if n.endswith('.cpp'):
                    src = open(os.path.join(cdir, n), encoding='utf-8', errors='replace').read()
                    self.fix_names |= set(re.findall(r'COOP_FIX(?:_UNSAFE)?\(\s*"([^"]+)"', src))


def in_scope(t, ctx):
    """the evidence rules: fixed tickets of the new fixes (every fixed ticket with --all)"""
    if t.get('status') != 'fixed':
        return False
    if ctx.scope == 'all':
        return True
    v = t.get('fixed_in', '')
    return v == 'unreleased' or (version_tuple(v) or (0,)) > LAST_OLD_RELEASE


def section(text, name):
    m = re.search(r'^## ' + re.escape(name) + r'\s*\n(.*?)(?=^## |\Z)', text, re.M | re.S)
    return re.sub(r'<!--.*?-->', '', m.group(1), flags=re.S) if m else ''


def commits_of(t):
    return [c for c in re.split(r'[,\s]+', t.get('commits', '')) if re.match(r'^[0-9a-f]{7,40}$', c)]


def rule_fields(t, ctx):
    p, name, tid = [], os.path.basename(t['_path']), t.get('id', '')
    if not ID_RE.match(tid) or not name.startswith(tid + '-'):
        p.append(f'id "{tid}" does not match the file name')
    for k in FIELDS:
        if k not in t:
            p.append(f'field "{k}" missing')
    if t.get('status') not in STATUS:
        p.append(f'status "{t.get("status")}" not one of {STATUS}')
    if t.get('kind') not in KIND:
        p.append(f'kind "{t.get("kind")}" not one of {KIND}')
    if not re.match(r'^\d{4}-\d{2}-\d{2}$', t.get('reported', '')):
        p.append('reported is not a date')
    if t.get('status') == 'fixed' and not (t.get('fixed_in') and t.get('tests')):
        p.append('a fixed ticket needs fixed_in (a version or "unreleased") and tests')
    if t.get('kind') == 'security' and t.get('status') == 'open' and not t['_private']:
        p.append('an open security ticket belongs in out/tickets/ (--private)')
    return p


# an absolute path of somebody's machine; the game's own folders may be named (a player's report)
OUTSIDE = re.compile(r'(?:/home/|/tmp/|/root/|/Users/|[A-Za-z]:\\Users\\)[^\s`\'")\]]*')


def rule_paths(t, ctx):
    """B5: a public ticket names no path outside the repository (logs belong in out/lab/<run>/)"""
    if t['_private']:
        return []
    bad = [m.group(0) for m in OUTSIDE.finditer(t['_text'])
           if not re.search(r'(SporebinEP1|Data)[/\\]?$', m.group(0))]
    return [f'names a path outside the repository ({bad[0][:60]}...): copy what matters into the ticket or '
            f'use out/lab/<run>/'] if bad else []


def rule_switch(t, ctx):
    """BUG-0214: `switch:` (when present) names coop.fix registry names (or kit.* SporeKit switches)"""
    names = [n for n in re.split(r'[,\s]+', t.get('switch', '')) if n]
    unknown = [n for n in names if n not in ctx.fix_names and not n.startswith('kit.') and n != 'none']
    return [f'switch: unknown coop.fix name(s) {", ".join(unknown)} (coop.fix list; names are <area>.<switch>)'] \
        if unknown and ctx.fix_names else []


def rule_commits(t, ctx):
    """B1, B2: commits listed, in history, and at least one not in a release before fixed_in"""
    if not in_scope(t, ctx):
        return []
    cs = commits_of(t)
    if not cs:
        return ['a fixed ticket lists its fix commits in `commits:`']
    if ctx.git is None:
        ctx.notes.append('git checks skipped (no history: shallow clone or no .git)')
        return []
    p = []
    for c in cs:
        if not ctx.git.resolve(c):
            p.append(f'commit {c} does not exist (update commits: after a rebase)')
        elif not ctx.git.is_ancestor(c):
            p.append(f'commit {c} is not in this branch\'s history')
    if p:
        return p
    v = t.get('fixed_in', '')
    target = None if v == 'unreleased' else version_tuple(v)
    firsts = [ctx.git.first_tag(c) for c in cs]
    if not any(f is None or (target is not None and f >= target) for f in firsts):
        rel = '.'.join(map(str, min(f for f in firsts if f)))
        p.append(f'fixed_in {v}: every commit was already released in {rel} (nothing was fixed in {v})')
    named = set(ctx.git.grep(t.get('id', ''))) if t.get('id') else set()
    listed = {ctx.git.resolve(c) for c in cs}
    extra = [h for h in named if ctx.git.resolve(h) not in listed]
    if extra:
        ctx.notes.append(f'{os.path.basename(t["_path"])}: commits naming {t.get("id")} not listed: {" ".join(extra[:5])}')
    return p


def rule_tests(t, ctx):
    """B3: tests: names something that exists (a lab suite name, a path, make test / unit)"""
    if not in_scope(t, ctx):
        return []
    text = t.get('tests', '')
    if re.search(r'\bmake test\b|\bunit\b', text):
        return []
    for tok in re.findall(r'[\w./-]+', text):
        if tok in ctx.suites or any(os.path.exists(os.path.join(ctx.root, d, tok)) for d in ('', 'tools/tests', 'tests')):
            return []
    return [f'tests: "{text[:60]}" names no lab suite, file or make test']


RUN_ID = re.compile(r'\b\d{8}-\d{6}\b|out/lab/')


def rule_verification(t, ctx):
    """B4: Verification names FAIL and PASS, with lab run ids for a lab test, or `no-fail-run: <reason>`"""
    if not in_scope(t, ctx):
        return []
    v = section(t['_text'], 'Verification')
    if re.search(r'^\s*[-*]?\s*no-fail-run:\s*\S', v, re.M):
        return []
    p = []
    if 'FAIL' not in v or 'PASS' not in v:
        p.append('## Verification names no FAIL run and PASS run (or a line `no-fail-run: <reason>`)')
    lab = [tok for tok in re.findall(r'[\w-]+', t.get('tests', '')) if tok in ctx.suites and tok != 'unit']
    if lab and not RUN_ID.search(v):
        p.append(f'## Verification: the lab test {lab[0]} needs its run ids (out/lab/YYYYMMDD-HHMMSS)')
    return p


CHECKS = [('fields', rule_fields), ('paths', rule_paths), ('switch', rule_switch), ('commits', rule_commits),
          ('tests', rule_tests), ('verification', rule_verification)]


def run_checks(tickets, ctx):
    problems, seen = [], {}
    for t in tickets:
        name = os.path.basename(t['_path'])
        tid = t.get('id', '')
        if tid in seen:
            problems.append(f'{name}: fields: id {tid} also in {seen[tid]}')
        seen[tid] = name
        for rule, fn in CHECKS:
            for msg in fn(t, ctx):
                # a private ticket's text never goes into public output beyond its id
                problems.append(f'{tid if t["_private"] else name}: {rule}: {msg}')
    return problems, seen


def cmd_check(a):
    ctx = Ctx('all' if getattr(a, 'all', False) else 'new')
    problems, seen = run_checks(all_tickets(), ctx)
    if os.path.isdir(PUBLIC):
        cur = open(INDEX, encoding='utf-8').read() if os.path.exists(INDEX) else ''
        if cur != index_text():
            problems.append('docs/tickets/README.md is not up to date: run tools/tickets.py index')
    for p in problems:
        print(p)
    for n in dict.fromkeys(ctx.notes):
        print('note: ' + n)
    print(f'tickets: {len(seen)} ticket(s), {len(problems)} problem(s)' + (' (--all: a report only)' if ctx.scope == 'all' else ''))
    return 1 if problems and ctx.scope == 'new' else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    n = sp.add_parser('new')
    n.add_argument('title')
    n.add_argument('--kind', default='bug', choices=KIND)
    n.add_argument('--stage', default='')
    n.add_argument('--source', default='')
    n.add_argument('--role', default='')
    n.add_argument('--versions', default='')
    n.add_argument('--private', action='store_true')
    l = sp.add_parser('list')
    l.add_argument('--status')
    l.add_argument('--kind')
    l.add_argument('--stage')
    for c in ('show', 'log'):
        sp.add_parser(c).add_argument('id')
    sp.add_parser('find').add_argument('text')
    sp.add_parser('index')
    rl = sp.add_parser('release')
    rl.add_argument('version')
    rl.add_argument('--force', action='store_true', help='release although tickets lack commits of this release')
    sp.add_parser('check').add_argument('--all', action='store_true')
    a = ap.parse_args()
    r = globals()['cmd_' + a.cmd](a)
    sys.exit(r or 0)


if __name__ == '__main__':
    main()
