#!/usr/bin/env python3
"""The session report and the session settings end with their session (BUG-0770), a busy guest answers the next
Ask (BUG-0783). Two games, Cell (0.7.12 F3 / F4, docs/REPORTING.md#reporting-session).

  A. BUG-0783: the guest's upload stops halfway (test hook `stall`), the host gives the round up after 5 s
     (`coop.sreport stallwait 5`) and asks again: the guest (still "sending") answers the new round at once and its
     report arrives: the host lists "Bob: sent" within 25 s. Before: the guest ignored the Ask, the host waited 120 s.
  B. BUG-0770, the guest: while its upload hangs it leaves (coop.leave) and joins again: its game no longer says
     "sending" or "asks for your report" (its old upload would go on to the new host).
  C. BUG-0770, the host: it asks, the guest is sending; the host leaves: no round left (round=none); hosting again,
     its "Ask the players" is not "Already asking".
  D. BUG-0770, the settings: the host forces all.dna_pace to a value the guest does not have; the guest gets it,
     leaves and joins a host that sends no table (`coop.corefix settings_in 0`: a host older than 0.7.12): the
     guest plays with its own value, `coop.settings` does not say "the host's table".
  E. BUG-0780: the host's disk is slow (`coop.sreport writedelay 3000`: 3 s per MB of the zip): the host's game keeps
     answering (every console answer under 1 s) while the session zip is written.
SPORE_FIX_OFF=core.sreport_reask fails A; SPORE_FIX_OFF=core.session_state_end fails B, C and D;
SPORE_FIX_OFF=core.sreport_write_worker fails E.

  SPORE_LANE=4 python3 tools/tests/coop_sreport_leave.py
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def st(con):
    return con.call('coop.sreport status')['out']


def field(out, key):
    m = re.search(r'\b' + key + r'=(\S+)', out)
    return m.group(1) if m else ''


def hostline(out):
    m = re.search(r"^hostline '(.*)'$", out, re.M)
    return m.group(1) if m else ''


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def stalled(guest):
    """the guest answered Send and its upload stopped halfway (hook `stall`)"""
    return wait(lambda: field(st(guest), 'guest') == 'sending' and 'Sending your report' in st(guest), 40)


def ask_and_stall(ck, host, guest, label):
    guest.call('coop.sreport test stall')
    ck('Asking' in host.call('coop.sreport ask')['out'], f'[{label}] the host asks for the reports')
    ok = wait(lambda: field(st(guest), 'guest') == 'asked', 15)
    ck(ok, f'[{label}] the guest is asked ({field(st(guest), "guest")})')
    guest.call('coop.sreport answer send')
    ok = stalled(guest)
    ck(ok, f'[{label}] the guest sends, its upload hangs halfway (guest={field(st(guest), "guest")})')
    time.sleep(2)
    return ok


def rejoin(host, guest):
    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    return wait_guest_in(host, guest, 120)


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    print('  ' + st(host).splitlines()[0], flush=True)

    # A. BUG-0783
    host.call('coop.sreport stallwait 5')
    if ask_and_stall(ck, host, guest, 'A'):
        r1 = wait(lambda: field(st(host), 'round') == 'none', 30)
        ck(r1 and 'Bob: timed out' in hostline(st(host)), f'[A] the host gives the hanging round up ({hostline(st(host))!r})')
        t0 = time.time()
        host.call('coop.sreport ask')
        done = wait(lambda: field(st(host), 'round') == 'none', 25)
        line = hostline(st(host))
        print(f'  [A] second round after {time.time() - t0:.0f} s: {line!r}; guest: {st(guest).splitlines()[0]}', flush=True)
        ck(done and 'Bob: sent' in line, f'[A] the busy guest answers the next Ask: its report arrives within 25 s ({line!r})')
    host.call('coop.sreport stallwait 60')

    # B. BUG-0770, the guest leaves while sending
    if ask_and_stall(ck, host, guest, 'B'):
        ck(rejoin(host, guest), '[B] the guest leaves and joins again')
        time.sleep(3)
        g = st(guest)
        print(f'  [B] guest after the rejoin: {g.splitlines()[0]} / {g.splitlines()[2]}', flush=True)
        ck(field(g, 'guest') != 'sending', f'[B] the guest\'s old upload ended with its session (guest={field(g, "guest")})')
        ck("guestline 1" not in g, '[B] the guest is not asked by the old session')
    # the host's round of B ends (the guest left it): wait until it is written
    wait(lambda: field(st(host), 'round') == 'none', 20)

    # C. BUG-0770, the host leaves while asking
    if ask_and_stall(ck, host, guest, 'C'):
        host.call('coop.leave')
        time.sleep(2)
        h = st(host)
        ck(field(h, 'round') == 'none', f'[C] the host\'s round ended with its session (round={field(h, "round")})')
        host.call(lane.HOST_CMD)
        time.sleep(1)
        out = host.call('coop.sreport ask')['out'].strip()
        ck('Already asking' not in out, f'[C] hosting again, "Ask the players" is not "Already asking" ({out!r})')
        guest.call('coop.leave')
        time.sleep(2)
        guest.call(lane.JOIN)
        ck(wait_guest_in(host, guest, 120), '[C] the guest joins the new session')
        wait(lambda: field(st(host), 'round') == 'none', 10)
    guest.call('coop.sreport test none')

    # D. BUG-0770, the host's settings table
    def pace(c):
        m = re.search(r'all\.dna_pace=(-?\d+)', c.call('coop.settings get all.dna_pace')['out'])
        return int(m.group(1)) if m else None
    own = pace(guest)
    other = 0 if own else 1
    out = host.call(f'coop.settings force all.dna_pace {other}')['out'].strip()
    got = wait(lambda: pace(guest) == other, 10)
    ck(got, f'[D] the guest plays with the host\'s all.dna_pace={other} (own {own}; host: {out!r})')
    guest.call('coop.corefix settings_in 0')
    ok = rejoin(host, guest)
    ck(ok, '[D] the guest leaves and joins a host that sends no settings table')
    lst = guest.call('coop.settings list')['out']
    now = pace(guest)
    print(f'  [D] guest: {lst.splitlines()[0] if lst else ""}; all.dna_pace={now}', flush=True)
    ck(now == own and "the host's table" not in lst,
       f'[D] the old host\'s table ended with its session: the guest plays with its own all.dna_pace={own} ({now})')
    guest.call('coop.corefix settings_in 1')
    host.call(f'coop.settings force all.dna_pace {own}')

    # E. BUG-0780: the session zip on a slow disk (test knob: 3 s per MB written) does not stop the host's game
    m = re.search(r'last_ms=(\d+)', st(host))
    print(f'  [E] the last write (normal disk) took {m and m.group(1)} ms', flush=True)
    host.call('coop.sreport writedelay 3000')
    host.call('coop.sreport ask')
    if wait(lambda: field(st(guest), 'guest') == 'asked', 15):
        guest.call('coop.sreport answer send')
        t0, worst, done = time.time(), 0.0, False
        while time.time() - t0 < 60:
            a = time.time()
            out = st(host)
            worst = max(worst, time.time() - a)
            if field(out, 'round') == 'none' and 'Bob: sent' in hostline(out):
                done = True
                break
            time.sleep(0.1)
        m = re.search(r'last_ms=(\d+)', st(host))
        print(f'  [E] written in {m and m.group(1)} ms; the host\'s slowest answer meanwhile {worst:.2f} s', flush=True)
        ck(done, f'[E] the round is written ({hostline(st(host))!r})')
        ck(worst < 1.0, f'[E] the host\'s game kept running while the zip was written (slowest answer {worst:.2f} s)')
    host.call('coop.sreport writedelay 0')

    ck('total faults: 0' in guest.call('exc')['out'], 'guest: no fault')
    ck('total faults: 0' in host.call('exc')['out'], 'host: no fault')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
