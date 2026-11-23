#!/usr/bin/env python3
"""Space Rivals: a guest that plays a Space save of its own keeps that save's money (BUG-1483, audit SPC-4), and an
offer's payment that reaches it there waits for the co-op world (BUG-1485, audit SPC-6).

A connected guest may load a Space save of its own (BUG-0709, BUG-0816). In a Rivals session its game still asked the
host for its budget (K_ASK) and took the answer (K_SET: the own save's money became the co-op budget), and reported the
own save's money as its Rivals budget. An offer's payment (K_OFFER_ACK, when the buyer clicks, up to 2 minutes later)
went into whatever empire the seller had then: the own save got the co-op world's sale.

1. Two games in Space, Rivals mode; the guest earns 8234: its budget mg (an odd value no save holds by chance).
2. The guest offers spice to the host's colony here (`coop.space trade sell`: in Rivals an offer, the spice held back).
   Both games keep an offer for 400 s (`trade offerlife`, the test's own load takes up to 150 s).
3. The guest, still connected, loads a copy of the share save under another name (own_world.load_own: space_other=1).
   Its money there m0 is the own save's: it must not be the co-op budget mg (SPORE_FIX_OFF=space.rival_own_save: the
   K_SET made it mg, FAIL).
4. The host accepts the offer (`trade accept <id>`, the page's Accept). Over 15 s the guest's own save money must stay
   m0 and the payment must be owed (`trade` pay_owed=1) (SPORE_FIX_OFF=trade.sale_pay_world: m0 + the price, FAIL;
   space.rival_own_save off: its K_SET may move it too).
   The host's copy of the guest's budget stays mg (no report of the own save's money).

  SPORE_LANE=2 python3 tools/tests/coop_space_own_rivals.py
"""
import re
import time

import own_world
from coop_space_kept_key import budget_on_host
from coop_space_rival_merge import rivals, sp, wait
from twoinst import Checks, keyframe, start


def money(con):
    m = re.search(r'money (-?\d+)', sp(con, 'trade money'))
    return int(m.group(1)) if m else None


def field(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con, 'trade'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 30000:
        host.call('coop.res add money 40000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)
    guest.call('coop.res add money 8234')
    time.sleep(4)
    gid = int(re.search(r'\bid=(\d+)', guest.call('coop.status')['out']).group(1))
    mg = money(guest)
    ck(mg and budget_on_host(host, gid) == mg, f'the host knows the guest\'s budget {mg}')
    for c in (host, guest):
        sp(c, 'trade offerlife 400')

    # 2. an offer of spice to the host's colony here
    print('  guest spice:', sp(guest, 'trade spice 0 10'), sp(guest, 'trade spice 1 10'), flush=True)
    deal = wait(lambda: (lambda d: d if d.startswith('deal ') else None)(sp(guest, 'trade deal')), 20)
    print('  guest deal:', deal or sp(guest, 'trade deal'), flush=True)
    dm = re.search(r' x(\d+) price (\d+)', deal or '')
    ck(dm is not None, 'the guest can offer spice to the host\'s colony here')
    if not dm:
        return ck.result()
    total = int(dm.group(1)) * int(dm.group(2))
    print('  guest sell:', sp(guest, 'trade sell'), flush=True)
    offer = wait(lambda: re.search(r'offer ([0-9A-F]{8}) from', sp(host, 'trade offers')), 15)
    ck(offer is not None and field(guest, 'pending') == 1, f'the offer waits on the host ({sp(host, "trade offers")})')
    if not offer:
        return ck.result()
    oid = offer.group(1)

    # 3. the guest's own Space save
    ok = own_world.load_own(guest, 'Madas.spo', 'Rivown', 'Space')
    ck(ok, 'the guest plays a Space save of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        guest.call('coop.worldfix autoload 1')
        return ck.result()
    time.sleep(6)  # K_ASK every 2 s before the fix
    m0 = money(guest)
    print(f'  own save: money {m0} (the co-op budget {mg}) | guest: ' + sp(guest, 'rivalry').splitlines()[0][:200], flush=True)
    ck(m0 is not None and m0 != mg, f'the own save keeps its own money, not the co-op budget ({m0} vs {mg})')
    ck(field(guest, 'pending') == 1, 'the offer is still on its way')

    # 4. the host accepts now
    print('  host accept:', sp(host, 'trade accept', oid), flush=True)
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 15:
        seen.add(money(guest))
        time.sleep(1)
    print(f'  guest after the accept: money seen {sorted(x for x in seen if x is not None)} | '
          + sp(guest, 'trade').strip()[-260:], flush=True)
    ck(field(guest, 'pending') == 0, 'the host\'s answer reached the guest')
    ck(seen == {m0}, f'the own save\'s money stays {m0} (seen {sorted(x for x in seen if x is not None)}; '
                     f'the payment {total} is not its)')
    ck(field(guest, 'pay_owed') == 1, 'the payment is owed for the co-op world')
    hb = budget_on_host(host, gid)
    ck(hb == mg, f'the host\'s copy of the guest\'s budget is still {mg} ({hb})')
    keyframe('own_rivals', f'the guest in a Space save of its own after the host bought its offer: its HUD money is '
             f'the own save\'s ({m0}), not the co-op budget and not +{total}', players={'host': host, 'guest': guest})
    guest.call('coop.worldfix autoload 1')
    for c in (host, guest):
        sp(c, 'trade offerlife 120')
    if rivals(host):
        sp(host, 'rivalry mode allies')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
