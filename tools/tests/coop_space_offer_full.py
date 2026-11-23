#!/usr/bin/env python3
"""Space Rivals: an offer the partner can not hold comes back at once (BUG-1491, audit SPC-17).

In Rivals mode a sale to a partner's colony is an offer: the seller's spice is held back until the partner answers. A
partner whose list was full (16 offers waiting) dropped a new offer without an answer, and the seller's Withdraw then
found nothing to answer either: the spice stayed held until the partner left or the session ended.

1. Two games in Space, Rivals mode. The host's game holds no partner offers (`trade offerlimit 0`: its list is full
   at once, as with 16 waiting).
2. The guest offers spice to the host's colony here (`trade sell`): the spice is held back.
3. Within 10 s the host's decline must reach the guest: its game's cargo has the spice again (the game's cargo count)
   and nothing of it is on its way (`trade` pending=0); the host counts the decline (declined_full).
   0.7.16.1 (SPORE_FIX_OFF=trade.offer_answer): no answer, the spice stays held (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_offer_full.py
"""
import re
import time

from coop_space_rival_merge import rivals, sp, wait
from coop_space_trade import cargo
from twoinst import Checks, keyframe, start


def field(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con, 'trade'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(3)
    print('  host:', sp(host, 'trade offerlimit 0'), flush=True)
    print('  guest spice:', sp(guest, 'trade spice 0 10'), sp(guest, 'trade spice 1 10'), flush=True)
    deal = wait(lambda: (lambda d: d if d.startswith('deal ') else None)(sp(guest, 'trade deal')), 20)
    print('  guest deal:', deal or sp(guest, 'trade deal'), flush=True)
    dm = re.search(r'spice (\S+) .* x(\d+) price (\d+)', deal or '')
    ck(dm is not None, 'the guest can offer spice to the host\'s colony here')
    if not dm:
        return ck.result()
    inst, n = dm.group(1), int(dm.group(2))
    c0, sent0 = cargo(guest, inst), field(guest, 'offers_sent')
    print('  guest sell:', sp(guest, 'trade sell'), flush=True)
    held = wait(lambda: field(guest, 'offers_sent') > sent0 or None, 5, 0.2)  # (the decline may be back at once)
    print(f'  guest cargo {c0} -> {cargo(guest, inst)}, pending {field(guest, "pending")} ({n} offered)', flush=True)
    back = wait(lambda: field(guest, 'pending') == 0 and cargo(guest, inst) == c0, 10, 0.5)
    print(f'  after: guest cargo {cargo(guest, inst)} pending {field(guest, "pending")} | host declined_full='
          f'{field(host, "declined_full")} offers={field(host, "offers")}', flush=True)
    ck(bool(held), 'the offer went out')
    ck(bool(back), f'the spice is back in the guest\'s cargo within 10 s ({cargo(guest, inst)} == {c0})')
    ck(field(host, 'declined_full') >= 1, 'the host\'s game declined the offer it could not hold')
    keyframe('offer_full', 'the guest after its offer to a full list: the spice is in its cargo again, a "declined" '
             'notice', players={'host': host, 'guest': guest})
    sp(host, 'trade offerlimit 16')
    if rivals(host):
        sp(host, 'rivalry mode allies')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
