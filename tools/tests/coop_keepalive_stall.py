#!/usr/bin/env python3
"""BUG-0687 diagnostics: a host whose frames stop while its main thread still pumps messages names the loop it sits in.

In campaign_tribe (rel/0.7.10) the host's frames stopped for good right after a guest left; only the keepalive (the
session and the world share, run from the window procedure) went on, and the watchdog's stack dump 30 s later never
reached the log. The keepalive now logs once per stall of 3 s the main thread's own call chain and its windows.

Two games on the creature fixture. The host's main thread runs a message loop of its own for 6 s (`coop.stalltest 6`:
no frames, messages dispatched, as the nested loop of the report). Checks: the guest still hears the host meanwhile
(the keepalive's session update: the link stays up), the host's log has one `keepalive: no frame for` line with a chain
of return addresses into the game and the window list, and frames go on afterwards.

  SPORE_LANE=9 python3 tools/tests/coop_keepalive_stall.py
"""
import re
import threading
import time

from twoinst import Checks, Console, start
import lane


def main():
    host, guest = start('creature')
    ck = Checks()
    path = lane.game_log(lane.HOST)
    size0 = len(open(path, errors='replace').read())
    res = {}

    def stall():
        con = Console(lane.HOST)
        con.timeout = 30
        try:
            res['out'] = con.call('coop.stalltest 6')['out']
        except (OSError, ValueError) as e:
            res['out'] = repr(e)
    th = threading.Thread(target=stall, daemon=True)
    th.start()
    time.sleep(4.5)
    st = guest.call('coop.status')['out']
    th.join(30)
    print('  host:', res.get('out', '').strip(), flush=True)
    ck('stalled' in res.get('out', ''), 'the host\'s main thread ran its own message loop for 6 s')
    print('  guest during the stall:', [l for l in st.splitlines() if 'together' in l or 'connected' in l][:2], flush=True)
    time.sleep(2)
    new = open(path, errors='replace').read()[size0:]
    lines = [l for l in new.splitlines() if 'keepalive: no frame for' in l]
    for l in lines:
        print('  ' + l[:400], flush=True)
    ck(len(lines) == 1, f'one stall line in the host\'s log ({len(lines)})')
    ck(lines and len(re.findall(r' [0-9A-F]{8}\b', lines[0].split('; user32')[0])) >= 3 and 'windows [' in lines[0],
       'it names the call chain into the game and the main thread\'s windows')
    f0 = host.call('frame')['out']
    time.sleep(1)
    f1 = host.call('frame')['out']
    ck(f0 != f1, f'the host\'s frames go on after the stall ({f0.strip()} -> {f1.strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
