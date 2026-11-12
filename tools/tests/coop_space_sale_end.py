#!/usr/bin/env python3
"""BUG-0832: a spice sale that arrived is paid when the seller was out of Space at the end of its session.

The guest sells spice to the host's colony here (`coop.space trade sell`, the Space page's "Sell"). The host's game
takes the spice but its answer is lost (`trade hold 2`: added, no answer; in play the session ends before the answer
is back). The guest then acts as outside Space (`trade away 1`: an editor or an adventure) and leaves the session:
the sale had arrived (BUG-0542: not given back), so it must be paid. Back in Space (`away 0`) the guest's game money
must have grown by the sale.

Before the fix the payment was made only when the seller was in Space at the session end: nothing.
SPORE_FIX_OFF=trade.sale_owed: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_sale_end.py
"""
import re
import time

from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def money(con):
    m = re.search(r'money (-?\d+)', sp(con, 'trade money'))
    return int(m.group(1)) if m else None


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(4)
    print('  guest spice:', sp(guest, 'trade spice 0 10'), sp(guest, 'trade spice 1 10'), flush=True)
    deal = wait(lambda: (lambda d: d if d.startswith('deal ') else None)(sp(guest, 'trade deal')), 20)
    print('  guest deal:', deal or sp(guest, 'trade deal'), flush=True)
    m = re.search(r' x(\d+) price (\d+)', deal or '')
    ck(m is not None, 'the guest can sell spice to the host\'s colony here')
    if not m:
        return ck.result()
    total = min(int(m.group(1)), 10) * int(m.group(2))
    print('  host:', sp(host, 'trade hold 2'), flush=True)
    print('  guest sell:', sp(guest, 'trade sell'), flush=True)
    ck(wait(lambda: 'delivered=1' in sp(guest, 'trade'), 10), 'the sale reached the host (no answer yet)')
    print('  guest:', sp(guest, 'trade away 1'), flush=True)
    print('  guest leaves:', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(4)
    m1 = money(guest)
    print(f'  guest money after the session end (away): {m1}; ' + sp(guest, 'trade').splitlines()[0][-80:], flush=True)
    print('  guest:', sp(guest, 'trade away 0'), flush=True)
    ok = wait(lambda: money(guest) == m1 + total, 10)
    print(f'  guest money back in Space: {money(guest)} (sale {total})', flush=True)
    ck(ok, f'back in Space the guest is paid for the sale: {m1} + {total} ({money(guest)})')
    time.sleep(5)
    ck(money(guest) == m1 + total, f'once ({money(guest)})')
    sp(host, 'trade hold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
