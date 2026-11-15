#!/usr/bin/env python3
"""A reliable message bigger than one packet (BUG-0034): the guest sends a 1500-character chat line,
then a short one. Both must reach the host's chat (what its chat window shows), and the guest's
connection must not be stuck (pending messages) or flood the host with empty packets.

  coop_chat_long.py [--without-fix]   (--without-fix: coop.fix net.big_reliable off on the guest, the
                                       code before the fix, so the test must FAIL)"""
import re
import sys
import time

from twoinst import Checks, start


def conn(con):
    """(sent, pending) of the first connection line of coop.status"""
    m = re.search(r'sent=(\d+) .*pending=(\d+)', con.call('coop.status')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', guest.call('coop.fix net.big_reliable off')['out'].strip().replace('\n', ' '))
    long_text = 'L' + 'x' * 1498 + 'Z'
    print(' ', guest.call('coop.say ' + long_text)['out'].strip())
    print(' ', guest.call('coop.say after-the-long-one')['out'].strip())
    s0, _ = conn(guest)
    t0 = time.time()
    got_long = got_short = False
    while time.time() - t0 < 15 and not (got_long and got_short):
        hs = host.call('coop.status')['out']
        got_long = ('L' + 'x' * 1498 + 'Z') in hs
        got_short = 'after-the-long-one' in hs
        time.sleep(1)
    time.sleep(3)
    s1, pending = conn(guest)
    t1 = time.time()
    rate = (s1 - s0) / (t1 - t0) if s0 is not None and s1 is not None else -1
    print(f'  host chat: long={got_long} short={got_short}; guest packets to the host {rate:.0f}/s, pending={pending}')
    ck(got_long, 'the host shows the 1500-character line')
    ck(got_short, 'the host shows the line sent after it')
    ck(pending is not None and pending < 5, f'nothing stays queued on the guest ({pending})')
    ck(0 <= rate < 300, f'no flood of empty packets ({rate:.0f} packets/s; a stuck message made ~5000/s on a LAN)')
    if without:
        guest.call('coop.fix net.big_reliable on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
