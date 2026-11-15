#!/usr/bin/env python3
"""Worktrees and branches that are done (BUG-1127, A13 of docs/plan-0.7.15.md): lists what can go and how much of
build/ and out/ that frees; deletes only with --apply.

  tools/wt_gc.py                      dry run: what would be deleted, what is kept and why
  tools/wt_gc.py --keep '*/w51-*'     more branch globs to keep (rel/* is always kept)
  tools/wt_gc.py --min-age 48         a tree touched in the last 48 hours stays (default)
  tools/wt_gc.py --apply              git worktree remove (never --force), git branch -d, git worktree prune

A worktree goes only when ALL of these hold:
  * its branch is merged into main (--into), it is not main, not rel/*, not a --keep glob; a detached HEAD stays;
  * git status shows nothing (no change, no untracked file; build/ and out/ are ignored), no merge / rebase / bisect
    in progress, the worktree is not locked;
  * nothing uses it: no process has its cwd, a PWD, an open file or a command-line path in it (an agent, a lab run,
    a game started from its tools), no lab lane lock (.lab-run-laneN.lock) is held by a process in it, no game
    console port is held by a process started from it;
  * nothing in it changed for --min-age hours (its git HEAD / index, its top folder, out/, out/lab/*, build/).
A merged branch without a worktree goes with `git branch -d` (git refuses an unmerged one). Unmerged branches are
listed for a person to decide. The main checkout and the tree this runs from are never touched."""
import argparse
import fnmatch
import glob
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_PORTS = range(47800, 48000)  # SporeKit's console ports (47800 + instance - 1)


def git(*a, cwd=None, check=True):
    r = subprocess.run(['git', *a], cwd=cwd or ROOT, capture_output=True, text=True)
    if check and r.returncode:
        raise RuntimeError(f'git {" ".join(a)}: {r.stderr.strip()}')
    return r.stdout


def worktrees(repo):
    """[{path, head, branch (or None: detached), locked, prunable}] of `git worktree list --porcelain`"""
    out = []
    for block in git('worktree', 'list', '--porcelain', cwd=repo).strip().split('\n\n'):
        w = {'path': None, 'head': None, 'branch': None, 'locked': False, 'prunable': False}
        for line in block.split('\n'):
            k, _, v = line.partition(' ')
            if k == 'worktree':
                w['path'] = v
            elif k == 'HEAD':
                w['head'] = v
            elif k == 'branch':
                w['branch'] = v[len('refs/heads/'):] if v.startswith('refs/heads/') else v
            elif k == 'locked':
                w['locked'] = True
            elif k == 'prunable':
                w['prunable'] = True
        if w['path']:
            out.append(w)
    return out


def under(path, root):
    root = root.rstrip('/')
    return path == root or path.startswith(root + '/')


def process_uses():
    """{pid: set of paths} every process of this user points at: cwd, exe, PWD / OLDPWD, open files, absolute
    command-line arguments (unreadable processes are skipped)"""
    uses = {}
    for d in glob.glob('/proc/[0-9]*'):
        pid = int(os.path.basename(d))
        if pid == os.getpid():
            continue
        s = set()
        for link in ('cwd', 'exe'):
            try:
                s.add(os.readlink(os.path.join(d, link)))
            except OSError:
                pass
        try:
            env = open(os.path.join(d, 'environ'), 'rb').read().split(b'\0')
            for e in env:
                if e.startswith((b'PWD=', b'OLDPWD=')):
                    s.add(e.split(b'=', 1)[1].decode('utf-8', 'replace'))
        except OSError:
            pass
        try:
            for a in open(os.path.join(d, 'cmdline'), 'rb').read().split(b'\0'):
                a = a.decode('utf-8', 'replace')
                for part in a.split('='):  # --branch=/x, PATH=... style arguments
                    if part.startswith('/'):
                        s.add(part)
        except OSError:
            pass
        try:
            for fd in os.listdir(os.path.join(d, 'fd')):
                try:
                    t = os.readlink(os.path.join(d, 'fd', fd))
                    if t.startswith('/'):
                        s.add(t)
                except OSError:
                    pass
        except OSError:
            pass
        if s:
            uses[pid] = s
    return uses


def held_lane_locks(repo):
    """[(lock file, holder line)] of the lab's lane locks another process holds now"""
    import fcntl
    common = git('rev-parse', '--path-format=absolute', '--git-common-dir', cwd=repo).strip()
    game = os.path.dirname(os.path.dirname(common))  # the main checkout's parent: the lane-1 game folder
    held = []
    for f in glob.glob(os.path.join(os.path.dirname(game), '.lab-run-lane*.lock')) + \
            glob.glob(os.path.join(os.path.dirname(game), 'SPORE-lab', '.lab-run-lane*.lock')):
        try:
            with open(f) as fh:
                try:
                    fcntl.flock(fh, fcntl.LOCK_SH | fcntl.LOCK_NB)
                    fcntl.flock(fh, fcntl.LOCK_UN)
                except OSError:
                    held.append((f, fh.read().strip()))
        except OSError:
            pass
    return held


def port_pids():
    """{pid: port} of processes listening on a game console port (TCP, /proc/net/tcp)"""
    inodes = {}
    for name in ('/proc/net/tcp', '/proc/net/tcp6'):
        try:
            for line in open(name).read().splitlines()[1:]:
                f = line.split()
                port = int(f[1].rsplit(':', 1)[1], 16)
                if f[3] == '0A' and port in GAME_PORTS:  # LISTEN
                    inodes[f[9]] = port
        except (OSError, IndexError, ValueError):
            pass
    out = {}
    if not inodes:
        return out
    for d in glob.glob('/proc/[0-9]*/fd'):
        try:
            for fd in os.listdir(d):
                t = os.readlink(os.path.join(d, fd))
                if t.startswith('socket:[') and t[8:-1] in inodes:
                    out[int(d.split('/')[2])] = inodes[t[8:-1]]
        except OSError:
            pass
    return out


def last_change(path, gitdir):
    """the newest mtime of the places a tree's user touches (no deep walk: 400 trees with build/ and out/)"""
    cand = [path, os.path.join(path, 'out'), os.path.join(path, 'build'), os.path.join(path, 'out', 'lab')]
    if gitdir:
        cand += [os.path.join(gitdir, n) for n in ('HEAD', 'index', 'logs/HEAD')]
    try:
        cand += [e.path for e in os.scandir(path)]
    except OSError:
        pass
    try:
        cand += [e.path for e in os.scandir(os.path.join(path, 'out', 'lab'))]
    except OSError:
        pass
    t = 0.0
    for c in cand:
        try:
            t = max(t, os.lstat(c).st_mtime)
        except OSError:
            pass
    return t


def disk_bytes(paths):
    """bytes on disk of the paths that exist (du; a shared reflink extent counts in each)"""
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        return 0
    r = subprocess.run(['du', '-s', '-B1', '-c', *paths], capture_output=True, text=True)
    lines = r.stdout.strip().splitlines()
    return int(lines[-1].split()[0]) if lines else 0


def gb(n):
    return f'{n / 1e9:.1f} GB' if n >= 1e8 else f'{n / 1e6:.1f} MB'


def plan(repo, into='main', keep=(), min_age_h=48.0, now=None, sizes=True):
    """(remove: [tree], branches: [name], kept: [(what, reason)], unmerged: [name]) without changing anything"""
    now = now or time.time()
    keep = ['rel/*', into, *keep]
    here = os.path.realpath(os.getcwd())
    trees = worktrees(repo)
    main = trees[0]['path']
    merged = set(git('branch', '--format=%(refname:short)', '--merged', into, cwd=repo).split())
    every = set(git('branch', '--format=%(refname:short)', cwd=repo).split())
    uses = process_uses()
    locks = held_lane_locks(repo)
    ports = port_pids()
    remove, kept = [], []
    checked_out = set()
    for w in trees[1:]:
        p, b = w['path'], w['branch']
        if b:
            checked_out.add(b)
        why = None
        if w['prunable'] or not os.path.isdir(p):
            why = 'missing folder (git worktree prune removes the entry)'
        elif not b:
            why = 'detached HEAD'
        elif any(fnmatch.fnmatch(b, k) for k in keep):
            why = 'kept branch (' + next(k for k in keep if fnmatch.fnmatch(b, k)) + ')'
        elif b not in merged:
            why = f'not merged into {into}'
        elif w['locked']:
            why = 'locked'
        elif under(here, p):
            why = 'the tree this runs from'
        if why is None:
            st = subprocess.run(['git', '-C', p, 'status', '--porcelain'], capture_output=True, text=True)
            gitdir = subprocess.run(['git', '-C', p, 'rev-parse', '--path-format=absolute', '--git-dir'],
                                    capture_output=True, text=True).stdout.strip()
            if st.returncode:
                why = 'git status failed: ' + st.stderr.strip()[:80]
            elif st.stdout.strip():
                n = len(st.stdout.strip().splitlines())
                why = f'uncommitted work ({n} path(s))'
            elif any(os.path.exists(os.path.join(gitdir, n))
                     for n in ('MERGE_HEAD', 'REBASE_HEAD', 'rebase-merge', 'rebase-apply', 'BISECT_LOG',
                               'CHERRY_PICK_HEAD')):
                why = 'a merge / rebase / bisect in progress'
            else:
                users = sorted(pid for pid, s in uses.items() if any(under(x, p) for x in s))
                lock = [f'{os.path.basename(f)} ({h})' for f, h in locks
                        if any(under(x, p) for pid in _pids(h) for x in uses.get(pid, ()))]
                port = [f'port {port} (pid {pid})' for pid, port in ports.items()
                        if any(under(x, p) for x in uses.get(pid, ()))]
                age_h = (now - last_change(p, gitdir)) / 3600
                if lock:
                    why = 'a lab run holds ' + ', '.join(lock)
                elif port:
                    why = 'a game started from it runs: ' + ', '.join(port)
                elif users:
                    why = f'in use by process(es) {", ".join(map(str, users[:5]))}'
                elif min_age_h > 0 and age_h < min_age_h:  # --min-age 0: no age rule (a fresh mtime may lie a tick ahead)
                    why = f'changed {age_h:.1f} h ago (< {min_age_h:g} h)'
        if why:
            kept.append((f'{p} [{b or "detached"}]', why))
        else:
            remove.append(dict(w))
    gone = {w['branch'] for w in remove}
    branches = sorted(b for b in merged if not any(fnmatch.fnmatch(b, k) for k in keep)
                      and (b not in checked_out or b in gone))
    unmerged = sorted(every - merged - {into})
    if sizes:
        with ThreadPoolExecutor(8) as ex:
            for w, n in zip(remove, ex.map(lambda w: disk_bytes([os.path.join(w['path'], 'build'),
                                                                  os.path.join(w['path'], 'out')]), remove)):
                w['bytes'] = n
    return dict(main=main, remove=remove, branches=branches, kept=kept, unmerged=unmerged)


def _pids(holder):
    """the pid of a lane lock's holder line (`run <id> pid <pid> since <time>`)"""
    f = holder.split()
    return [int(f[i + 1]) for i in range(len(f) - 1) if f[i] == 'pid' and f[i + 1].isdigit()]


def apply(repo, p):
    done, failed = [], []
    for w in p['remove']:
        r = subprocess.run(['git', '-C', repo, 'worktree', 'remove', w['path']], capture_output=True, text=True)
        (done if r.returncode == 0 else failed).append((w['path'], r.stderr.strip()))
    for b in p['branches']:
        # from the main checkout: `branch -d` checks the merge against the HEAD it runs in (main there)
        r = subprocess.run(['git', '-C', p['main'], 'branch', '-d', b], capture_output=True, text=True)
        (done if r.returncode == 0 else failed).append(('branch ' + b, r.stderr.strip()))
    subprocess.run(['git', '-C', repo, 'worktree', 'prune'], capture_output=True)
    return done, failed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default=ROOT, help='any checkout of the repository (default: this one)')
    ap.add_argument('--into', default='main', help='the branch a tree must be merged into')
    ap.add_argument('--keep', action='append', default=[], help='a branch glob to keep (repeatable)')
    ap.add_argument('--min-age', type=float, default=48.0, help='hours without a change before a tree may go')
    ap.add_argument('--apply', action='store_true', help='delete (otherwise only print)')
    ap.add_argument('--no-sizes', action='store_true', help='skip du of build/ and out/')
    ap.add_argument('-v', '--verbose', action='store_true', help='every kept tree with its reason')
    a = ap.parse_args()
    p = plan(a.repo, a.into, a.keep, a.min_age, sizes=not a.no_sizes)
    total = sum(w.get('bytes', 0) for w in p['remove'])
    for w in p['remove']:
        print(f'remove   {w["path"]} [{w["branch"]}]' + (f'  build/+out/ {gb(w["bytes"])}' if 'bytes' in w else ''))
    for b in p['branches']:
        print(f'branch   {b} (merged into {a.into}: git branch -d)')
    reasons = {}
    for what, why in p['kept']:
        key = ('changed in the last --min-age hours' if why.startswith('changed') else
               'a lab run holds a lane lock' if why.startswith('a lab') else
               why.split(':')[0] if why.startswith(('a game', 'git status')) else
               'in use by a process' if why.startswith('in use') else
               'uncommitted work' if why.startswith('uncommitted') else why)
        reasons.setdefault(key, []).append(what)
        if a.verbose:
            print(f'keep     {what}: {why}')
    print(f'\nwt_gc: {len(p["remove"])} worktree(s) to remove (build/ and out/: {gb(total)}), '
          f'{len(p["branches"])} merged branch(es) to delete, {len(p["kept"])} worktree(s) kept, '
          f'{len(p["unmerged"])} unmerged branch(es) left for a person')
    for key, v in sorted(reasons.items(), key=lambda x: -len(x[1])):
        print(f'  kept {len(v):4}  {key}')
    if p['unmerged']:
        print('  unmerged: ' + ' '.join(p['unmerged'][:40]) + (' ...' if len(p['unmerged']) > 40 else ''))
    if not a.apply:
        print('dry run: nothing changed (--apply deletes)')
        return 0
    done, failed = apply(a.repo, p)
    for what, err in failed:
        print(f'FAILED   {what}: {err}')
    print(f'wt_gc --apply: {len(done)} done, {len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
