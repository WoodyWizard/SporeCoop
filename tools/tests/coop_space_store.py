#!/usr/bin/env python3
"""Space: spice sold to an NPC empire's store through its comm screen (BUG-0199).

The sale is the game's own (the comm screen's Trade tab); the co-op part is the shared money: the seller's game earns,
shared.cpp carries the change to the other game (a guest's earning goes to the host as MSG_RES_ADD).

1. Two games on space_empire (contact with the empire 80001FCC, home star 0046102D; 20 Red Spice in each cargo).
2. The seller jumps into that empire's system (`coop.space jumpto`), then with real clicks: the comm tab above the
   empire's card (288, 914), the first answer (720, 725: "Salutations! There is something I'd like to discuss"),
   Trade (420, 744), the spice in the cargo (327, 760), Sell Item (1063, 884): one unit at the store's price.
3. The guest sells first, then the host.
PASS: the seller's cargo has one unit less (its own game), the seller's money rose by the price, and the OTHER game's
shared money rose by the same price within 10 s (and the other game's cargo is unchanged); both stores paid the same
price for the same spice (BUG-0747: each game made its own random store).
SPORE_FIX_OFF=res.guest_send must FAIL (the guest's earning never reaches the host); SPORE_FIX_OFF=space.store_seed
must FAIL (the two prices differ: 9 of 10 times for a random factor, more when the store does not want the spice).

  SPORE_LANE=3 python3 tools/tests/coop_space_store.py
"""
import re
import time

from coop_space_jump import here, settle, sp
from twoinst import Checks, shot, start

STAR, POL = 0x0046102D, 0x80001FCC


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def spice(con):
    m = re.search(r'count (\d+)', sp(con, 'spice'))
    return int(m.group(1)) if m else None


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def sell(ck, seller, other, who):
    for _ in range(5):
        r = sp(seller, 'jumpto', hex(STAR))
        print(f'  {who} jumpto {STAR:08X}: {r}', flush=True)
        if r == 'jumping':
            break
        settle(seller, who, 10)
        time.sleep(2)
    arrived = wait(lambda: here(seller)[0] == POL, 30, 2)
    settle(seller, who)
    ck(bool(arrived), f'{who}: in the system of the empire {POL:08X} ({here(seller)})')
    c0, m0, o0, oc0 = spice(seller), money(seller), money(other), spice(other)
    print(f'  {who}: cargo {c0}, money {m0}; other game: money {o0}, cargo {oc0}', flush=True)
    seller.call('click 288 914')  # the comm tab above the empire's card
    time.sleep(4)
    seller.call('click 720 725')  # "Salutations! There is something I'd like to discuss..."
    time.sleep(3)
    seller.call('click 420 744')  # Trade
    time.sleep(3)
    seller.call('click 327 760')  # the spice in the cargo
    time.sleep(2)
    shot(seller, f'store_{who}_trade.png')
    seller.call('click 1063 884')  # Sell Item
    time.sleep(3)
    shot(seller, f'store_{who}_sold.png')
    c1, m1 = spice(seller), money(seller)
    price = (m1 - m0) if m1 is not None and m0 is not None else None
    print(f'  {who}: cargo {c0} -> {c1}, money {m0} -> {m1}', flush=True)
    ck(c0 is not None and c1 == c0 - 1, f'{who}: one unit of spice left the seller\'s cargo ({c0} -> {c1})')
    ck(price is not None and price > 0, f'{who}: the store paid {price} in the seller\'s game')
    got = wait(lambda: (lambda m: m if m is not None and o0 is not None and price and abs(m - (o0 + price)) < 1 else None)(
        money(other)), 10)
    time.sleep(3)
    o1 = money(other)
    print(f'  other game: money {o0} -> {o1}, cargo {oc0} -> {spice(other)}', flush=True)
    ck(got is not None and o1 is not None and abs(o1 - (o0 + price)) < 1,
       f'{who}: the other game\'s shared money rose by the sale ({o0} -> {o1}, price {price})')
    ck(spice(other) == oc0, 'the other game\'s cargo is unchanged')
    for _ in range(2):
        seller.call('click 424 882')  # Goodbye
        time.sleep(2)
    return price


def main():
    host, guest = start('space_empire')
    ck = Checks()
    for con, who in ((host, 'host'), (guest, 'guest')):
        settle(con, who, 20)
    pg = sell(ck, guest, host, 'guest')
    ph = sell(ck, host, guest, 'host')
    # BUG-0747: the same empire's store pays the same price in both games (its wants and price factors are seeded
    # from its planet: the log line "the NPC store of planet ... seeded")
    for con, who in ((host, 'host'), (guest, 'guest')):
        print(f'  {who}: ' + ' '.join(t for t in sp(con).split() if t.startswith('stores_seeded=')), flush=True)
    stores = {}
    for con, who in ((host, 'host'), (guest, 'guest')):
        lines = [ln for ln in sp(con, 'stores').splitlines() if ln.startswith('store planet')]
        print(f'  {who} stores: {lines}', flush=True)
        stores[who] = {ln.split()[2]: ln.split(' wants ')[1] for ln in lines}
    common = sorted(set(stores['host']) & set(stores['guest']))
    ck(common, f'both games made a store at the same planet ({common})')
    diff = [k for k in common if stores['host'][k] != stores['guest'][k]]
    ck(not diff, f'every store both games made wants the same spices at the same prices ({len(common) - len(diff)} of '
                 f'{len(common)}: {[(k, stores["host"][k], stores["guest"][k]) for k in diff]})')
    ck(pg is not None and ph is not None and abs(pg - ph) < 1,
       f'both games\' stores paid the same for one Red Spice (guest {pg}, host {ph})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
