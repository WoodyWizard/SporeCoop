#!/usr/bin/env python3
"""The native net tests pass when several copies run at the same time (BUG-0969).

The lab runs `unit` (make test) on several lanes at once, and the games of other lanes host on 127.0.0.1 in the LAN
discovery range (kDefaultPort .. +19). The discovery check hosted on the fixed port kDefaultPort + 19 and asked the
whole range: a second copy could not bind the port (`host.host(...)` failed, then `found.size() == 1`), and a lane's
game answered it (lab 20261112-154553: found[0] was another game). This test builds the net tests (the make test
command line) and runs COPIES of them at once, ROUNDS times: every copy must end with ALL OK.

  python3 tools/tests/unit_parallel.py [--copies 3] [--rounds 2]
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--copies', type=int, default=3)
    ap.add_argument('--rounds', type=int, default=2)
    a = ap.parse_args()
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'net_tests_par')
    cxx = os.environ.get('HOSTCXX', 'clang++')
    src = [os.path.join('mods/coop/net', f) for f in sorted(os.listdir(os.path.join(ROOT, 'mods/coop/net')))
           if f.endswith('.cpp')]
    cmd = [cxx, '-std=c++20', '-O1', '-g', '-Wall', '-Imods/coop', '-o', exe] + src + [
        'updater/pack.cpp', 'updater/sha256.cpp', 'tests/net_tests.cpp']
    if subprocess.call(cmd, cwd=ROOT) != 0:
        print('RESULT: FAIL (build)')
        return 1
    bad = 0
    for r in range(a.rounds):
        procs = [subprocess.Popen([exe], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                 for _ in range(a.copies)]
        for i, p in enumerate(procs):
            o, _ = p.communicate()
            fails = [ln.strip() for ln in o.splitlines() if 'FAIL' in ln]
            ok = p.returncode == 0 and 'ALL OK' in o
            print(f'  {"ok  " if ok else "FAIL"} round {r + 1} copy {i + 1}: {fails[:6] if fails else "ALL OK"}', flush=True)
            bad += 0 if ok else 1
    print(f'RESULT: {"OK" if not bad else "FAIL"} ({bad} of {a.copies * a.rounds} copies failed)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
