#!/usr/bin/env python3
"""BUG-0791: what skg::readable (one VirtualQuery per call, a syscall) costs the frame in a co-op session.

A fork measured a 438-actor adventure on native Windows spending most of its main thread in NtQueryVirtualMemory called
from readable(). Measured here, on both games of a session on the fixture (--fixture, default creature):
1. `coop.perf readable` over --secs seconds: readable() calls per frame and the frame rate (base);
2. the same with every call making --extra more VirtualQuery calls (`coop.perf readable extra N`): the frame rate
   then;
3. the cost of one call, from the frame time difference ((1/fps2 - 1/fps1) / (calls per frame x extra)) and from the
   command's own loop of 2000 calls (`us_per_call=`), and so the share of the frame readable() takes now.
Fails when that share is 5 % or more of the frame on either game (then a scoped cache as in the fork pays off).

  SPORE_LANE=N python3 tools/tests/perf_readable.py [--fixture creature] [--secs 20] [--extra 20]
"""
import argparse
import re
import time

from twoinst import Checks, start


def perf(con):
    m = re.search(r'since=(\d+) frames=(\d+) secs=([\d.]+) per_frame=([\d.]+) fps=([\d.]+) us_per_call=([\d.]+)',
                  con.call('coop.perf readable')['out'])
    return {k: float(v) for k, v in zip(('calls', 'frames', 'secs', 'per_frame', 'fps', 'us'), m.groups())} if m else None


def window(cons, secs):
    for c in cons.values():
        perf(c)  # start of the window
    time.sleep(secs)
    return {n: perf(c) for n, c in cons.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--secs', type=float, default=20)
    ap.add_argument('--extra', type=int, default=20)
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    cons = {'host': host, 'guest': guest}
    time.sleep(10)
    base = window(cons, a.secs)
    for c in cons.values():
        c.call(f'coop.perf readable extra {a.extra}')
    more = window(cons, a.secs)
    for c in cons.values():
        c.call('coop.perf readable extra 0')
    for n in cons:
        b, m = base[n], more[n]
        if not b or not m:
            ck(False, f'{n}: coop.perf answers')
            continue
        ft1, ft2 = 1000.0 / b['fps'], 1000.0 / m['fps']
        per_call_ms = (ft2 - ft1) / (b['per_frame'] * a.extra) if b['per_frame'] > 0 else 0.0
        share_frame = per_call_ms * b['per_frame'] / ft1 * 100 if ft1 > 0 else 0.0
        share_loop = b['us'] / 1000.0 * b['per_frame'] / ft1 * 100 if ft1 > 0 else 0.0
        print(f'  {n}: {b["per_frame"]:.0f} readable() calls a frame at {b["fps"]:.1f} fps ({ft1:.2f} ms); with {a.extra} '
              f'more VirtualQuery each: {m["fps"]:.1f} fps ({ft2:.2f} ms); one call {per_call_ms * 1000:.2f} us by the frame '
              f'time, {b["us"]:.2f} us in the command\'s loop; share of the frame {share_frame:.2f} % (frame time), '
              f'{share_loop:.2f} % (loop)', flush=True)
        ck(max(share_frame, share_loop) < 5.0, f'{n}: readable() takes under 5 % of the frame '
           f'({max(share_frame, share_loop):.2f} %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
