#!/usr/bin/env python3
"""BUG-1236: Civilization, the cities' passive income is no "<host> earned N money (shared)" card on the guest.

The game's own 6 s city and spice tick and the trade deliveries (cCivilization::AddMoney from the return addresses of
docs/re/civ.md#civ-income-addmoney) passed the cause threshold (25) and went into the host's MSG_RES cause: every guest
got the card every 6 s through the whole stage (698 lines in the guests' logs of lab 20261117-154911).

1. Two games on the civ fixture, the shared money (civ.own_money off). The host idles 24 s (no command): the guest's
   screen (`coop.status` toasts, polled every 0.4 s) shows no "<host> earned N money (shared)" card, while the
   host's money did grow by the cities' income (the game's +0x98, the shared total the guest shows).
2. A purchase of the host (`coop.res add money -300`, an active change) still gives one "<host> spent 300 money
   (shared)" card on the guest within 4 s.
Twin: SPORE_FIX_OFF=civ.income_cause: 1. sees 3 or more cards (one every 6 s): must FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_civ_income_cause.py
"""
import re
import time

from harness import shared_money as money
from twoinst import Checks, keyframe, start


def card_texts(con, pat):
    return {ln.split("'", 1)[-1] for ln in con.call('coop.status')['out'].splitlines()
            if ln.startswith('toast ') and re.search(pat, ln)}


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    host_name = 'Alice'
    earned = rf'{host_name} earned \d+ money \(shared\)'
    m0, seen, cards, prev = money(host), [], 0, set()
    t0 = time.time()
    while time.time() - t0 < 24:
        now = card_texts(guest, earned)
        cards += len(now - prev)  # a card that came up since the last look
        seen += sorted(now - prev)
        prev = now
        time.sleep(0.4)
    m1 = money(host)
    print(f'  24 s idle: host money {m0} -> {m1}; "earned" cards on the guest: {cards} {seen[:6]}', flush=True)
    print('  host:', ' '.join(w for w in host.call('coop.res')['out'].split() if w.startswith(('passive=', 'causes='))),
          '|', next((ln for ln in host.call('coop.civ')['out'].splitlines() if ln.startswith('money:')), ''), flush=True)
    ck(m0 is not None and m1 is not None and m1 > m0, f"the host's cities earned money meanwhile ({m0} -> {m1})")
    ck(cards == 0, f'no "{host_name} earned N money (shared)" card for the cities\' income ({cards} in 24 s)')
    keyframe('idle', "the guest's screen: no 'earned N money (shared)' card at the right edge")
    print(' ', host.call('coop.res add money -300')['out'].strip().replace('\n', ' | '), flush=True)
    t = None
    t0 = time.time()
    while time.time() - t0 < 4 and not t:
        t = next(iter(card_texts(guest, rf'{host_name} spent 3\d\d money \(shared\)')), None)
        time.sleep(0.2)
    print('  guest card:', t, flush=True)
    ck(t is not None, f'the host\'s purchase still says "{host_name} spent 300 money (shared)" on the guest')
    keyframe('spent', "the guest's screen shows the card 'Alice spent 300 money (shared)'")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
