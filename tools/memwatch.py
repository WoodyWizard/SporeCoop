#!/usr/bin/env python3
"""Memory watchdog for a lab run (BUG-0916, docs/06-testing.md#testing-lab-memory).

  memwatch.py --pid <lab.py pid> --out <file> [--tree <checkout>] [--max-gb 4] [--min-avail-gb 12] [--step 5]

Every --step seconds: one line with the system's MemAvailable / SwapFree, the RSS of every python3 that runs in
the checkout (its cwd is under --tree, default this checkout) and of every SporeApp.exe (with its lane). When a
python3 of the run exceeds --max-gb or MemAvailable drops below --min-avail-gb, the lab gets SIGTERM (it ends its
tests and games itself; a lab started with nohup ignores SIGINT), and 40 s later whatever python3 of the tree is
still over the limit gets SIGKILL. Ends when the lab's pid is gone. Processes are found by name (pgrep -x) and
/proc, never by a command line pattern.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def meminfo():
    d = {}
    with open('/proc/meminfo') as f:
        for ln in f:
            k, v = ln.split(':', 1)
            d[k] = int(v.split()[0])  # kB
    return d


def rss_kb(pid):
    try:
        with open(f'/proc/{pid}/status') as f:
            for ln in f:
                if ln.startswith('VmRSS:'):
                    return int(ln.split()[1])
    except OSError:
        pass
    return 0


def pids(name):
    try:
        return [int(p) for p in subprocess.run(['pgrep', '-x', name], capture_output=True, text=True).stdout.split()]
    except OSError:
        return []


def tree_pythons(tree):
    """{pid: (rss kB, short command)} of the python3 processes whose cwd is under the tree"""
    out = {}
    for pid in pids('python3'):
        try:
            cwd = os.readlink(f'/proc/{pid}/cwd')
            cmd = open(f'/proc/{pid}/cmdline', 'rb').read().replace(b'\0', b' ').decode('utf-8', 'replace').strip()
        except OSError:
            continue
        if pid != os.getpid() and (cwd == tree or cwd.startswith(tree + os.sep)):
            words = [os.path.basename(w) for w in cmd.split()[1:4]]
            out[pid] = (rss_kb(pid), ' '.join(words))
    return out


def games():
    """{pid: (rss kB, lane or '?')} of every SporeApp.exe (Wine): the lane from its working directory"""
    out = {}
    for pid in pids('SporeApp.exe'):
        try:
            cwd = os.readlink(f'/proc/{pid}/cwd')
        except OSError:
            cwd = ''
        m = re.search(r'/lane(\d+)/', cwd + '/')
        out[pid] = (rss_kb(pid), m.group(1) if m else '?')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--pid', type=int, required=True, help='the lab.py process to stop')
    ap.add_argument('--out', required=True)
    ap.add_argument('--tree', default=ROOT)
    ap.add_argument('--max-gb', type=float, default=4.0)
    ap.add_argument('--min-avail-gb', type=float, default=12.0)
    ap.add_argument('--step', type=float, default=5.0)
    a = ap.parse_args()
    tree = os.path.realpath(a.tree)
    t0, fired = time.time(), 0.0
    with open(a.out, 'a') as f:
        f.write(f'# memwatch pid {a.pid} tree {tree} max {a.max_gb} GB, min available {a.min_avail_gb} GB\n')
        while os.path.exists(f'/proc/{a.pid}'):
            mi = meminfo()
            py = tree_pythons(tree)
            gm = games()
            avail = mi['MemAvailable'] / 1048576
            line = (f'{time.strftime("%H:%M:%S")} t={time.time() - t0:.0f} avail={avail:.1f}G '
                    f'swapfree={mi.get("SwapFree", 0) / 1048576:.1f}G py=['
                    + ' '.join(f'{p}:{r // 1024}M:{c}' for p, (r, c) in sorted(py.items())) + '] games=['
                    + ' '.join(f'{p}:L{ln}:{r // 1024}M' for p, (r, ln) in sorted(gm.items())) + ']')
            f.write(line + '\n')
            f.flush()
            big = {p: r for p, (r, _) in py.items() if r > a.max_gb * 1048576}
            if not fired and (big or avail < a.min_avail_gb):
                fired = time.time()
                why = f'python3 over {a.max_gb} GB: {big}' if big else f'MemAvailable {avail:.1f} GB under {a.min_avail_gb} GB'
                f.write(f'# STOP: {why}: SIGTERM to {a.pid}\n')
                f.flush()
                print(f'memwatch: STOP: {why}', flush=True)
                try:
                    os.kill(a.pid, signal.SIGTERM)
                except OSError:
                    pass
            elif fired and time.time() - fired > 40:
                for p in big:
                    f.write(f'# still over the limit: SIGKILL {p}\n')
                    try:
                        os.kill(p, signal.SIGKILL)
                    except OSError:
                        pass
            time.sleep(a.step)
        f.write('# the lab ended\n')
    return 1 if fired else 0


if __name__ == '__main__':
    sys.exit(main())
