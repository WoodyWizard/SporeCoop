#!/usr/bin/env python3
"""A fault the game's own handler catches inside Present, in the overlay pass, or in a console command's task: the
co-op goes on (BUG-1024, BUG-0966).

Creature fixture (two games). Each case makes the fault on the host with a SporeKit test command; the game's handler
catches it (as it catches a fault of the driver, a mod or the game itself) and the game draws on.

  present  BUG-1024: `kit.test.presentfault present` faults inside the original Present (as a driver fault or a
           window message dispatched there would). Before the fix the Present depth stayed 1: no frame started again,
           the host's co-op stopped (the guest's age of the host grew for good) and outside-frame commands got no
           answer. PASS: the host's frames go on, an outside-frame command (`window`) answers, the guest's age of the
           host stays under 3 s. SPORE_FIX_OFF=kit.presentunwind fails.
  overlay  BUG-1024: three faults inside the overlay pass (an SK_EV_OVERLAY handler). Before the fix each left a
           reference of the back buffer held (a later device Reset fails while it is held). PASS: the back buffer's
           reference count is the one before, frames go on. SPORE_FIX_OFF=kit.presentunwind fails.
  task     BUG-0966: `kit.test.cmdfault`, a main-thread command whose own task faults inside a frame (as coop.puppet
           respawn did with a first version of BUG-0930's fix; a fault in a task the window procedure runs is not
           caught by the game: the game ends). Before the fix the command never answered (the client waited for ever). PASS:
           an error answer that names the cut-off within 20 s, the same connection answers the next command, frames
           go on. SPORE_FIX_OFF=kit.taskcut fails.

  SPORE_LANE=6 python3 tools/tests/kit_present_fault.py [--case present,overlay,task]
"""
import os
import re
import socket
import sys
import time

import lane
from twoinst import Checks, start
from sporectl import Console  # noqa: E402 (twoinst puts tools/ on the path)

CASES = ('present', 'overlay', 'task')


def call(con, line, timeout=None):
    """one console call; None when no answer came (a timeout or a closed connection)"""
    old = con.timeout
    if timeout:
        con.timeout = timeout
        if con.sock:
            con.sock.settimeout(timeout)
    try:
        return con.call(line)
    except (socket.timeout, OSError, ConnectionError):
        return None
    finally:
        con.timeout = old
        if con.sock:
            con.sock.settimeout(old)


def frame(con):
    r = call(con, 'frame', 10)
    m = re.search(r'frame=(\d+)', r['out']) if r else None
    return int(m.group(1)) if m else -1


def state(con):
    r = call(con, 'kit.test.presentfault', 15)
    return r['out'].strip() if r else 'no answer'


def num(text, key):
    m = re.search(key + r'=(\d+)', text)
    return int(m.group(1)) if m else -1


def host_age(guest):
    """the guest's age of the host's last state (coop.status: remote ... age=)"""
    r = call(guest, 'coop.status', 15)
    ages = [float(a) for a in re.findall(r'age=([\d.]+)s', r['out'])] if r else []
    return min(ages) if ages else None


def frames_go_on(ck, host, what):
    f0 = frame(host)
    time.sleep(5)
    f1 = frame(host)
    print(f'  {what}: host frames {f0} -> {f1}', flush=True)
    ck(f0 >= 0 and f1 > f0 + 20, f'{what}: the host\'s frames go on ({f0} -> {f1})')


def case_present(ck, host, guest):
    print('  host: ' + state(host), flush=True)
    a0 = host_age(guest)
    r = call(host, 'kit.test.presentfault present', 15)
    print('  host: ' + (r['out'].strip() if r else 'no answer'), flush=True)
    time.sleep(3)
    s = state(host)
    print('  host after: ' + s, flush=True)
    frames_go_on(ck, host, 'present')
    w = call(Console(lane.HOST), 'window', 20)  # an outside-frame command (SK_CMD_OUTSIDE_FRAME)
    print('  host window: ' + (w['out'].strip() if w else 'no answer in 20 s'), flush=True)
    ck(w is not None and w.get('ok'), 'present: an outside-frame command answers')
    time.sleep(2)
    a1 = host_age(guest)
    print(f'  guest: the host\'s state age {a0} -> {a1} s', flush=True)
    ck(a1 is not None and a1 < 3.0, f'present: the guest still gets the host\'s state (age {a1} s)')
    ck(num(s, 'presents_cut') >= 1 or 'depth=0' in s, 'present: the fault happened and the depth is back to 0')


def case_overlay(ck, host, guest):
    s0 = state(host)
    print('  host: ' + s0, flush=True)
    for i in range(3):
        r = call(host, 'kit.test.presentfault overlay', 15)
        print('  host: ' + (r['out'].strip() if r else 'no answer'), flush=True)
        time.sleep(1.5)
    time.sleep(1)
    s1 = state(host)
    print('  host after: ' + s1, flush=True)
    frames_go_on(ck, host, 'overlay')
    ck(num(s1, 'frames_cut') >= num(s0, 'frames_cut') + 3, 'overlay: three frames were cut off by the faults')
    ck(num(s1, 'bb_refs') == num(s0, 'bb_refs'),
       f'overlay: the back buffer\'s references are back ({num(s0, "bb_refs")} -> {num(s1, "bb_refs")})')
    a1 = host_age(guest)
    ck(a1 is not None and a1 < 3.0, f'overlay: the guest still gets the host\'s state (age {a1} s)')


def case_task(ck, host, guest):
    for cmd in ('kit.test.cmdfault', 'kit.test.cmdfault'):
        con = Console(lane.HOST)
        t0 = time.time()
        r = call(con, cmd, 20)
        dt = time.time() - t0
        print(f'  host {cmd}: ' + (repr(r['out'].strip()) if r else 'no answer in 20 s') + f' ({dt:.1f} s)', flush=True)
        ck(r is not None and 'cut off' in r['out'], f'task: {cmd} is answered (cut off by the fault)')
        if r is not None:
            n = call(con, 'frame', 15)
            ck(n is not None and 'frame=' in n['out'], f'task: the same connection answers the next command')
        frames_go_on(ck, host, cmd)
    a1 = host_age(guest)
    ck(a1 is not None and a1 < 3.0, f'task: the guest still gets the host\'s state (age {a1} s)')


def exc_print(con, name):
    """the faults the host logged (the test's own: a read of 0x10 in sporekit.dll), for the record"""
    r = call(con, 'exc', 15)
    lines = r['out'].splitlines() if r else ['no answer']
    for l in lines[:6]:
        print(f'  {name} exc: ' + l.strip(), flush=True)


def main():
    cases = list(CASES)
    if '--case' in sys.argv:
        cases = [c for c in sys.argv[sys.argv.index('--case') + 1].split(',') if c]
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    a = host_age(guest)
    print(f'  guest: the host\'s state age {a} s', flush=True)
    ck(a is not None and a < 3.0, 'the guest gets the host\'s state before')
    for c in cases:
        print(f'-- {c}', flush=True)
        {'present': case_present, 'overlay': case_overlay, 'task': case_task}[c](ck, host, guest)
    exc_print(Console(lane.HOST), 'host')
    ck(frame(guest) > 0, 'the guest\'s game answers')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
