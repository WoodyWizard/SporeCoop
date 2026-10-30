#!/usr/bin/env python3
"""Cell stage with three players (BUG-0152, #110): knockouts between guests, checked in each game.

Alice hosts, Bob and Carol are guests (tools/tests/party.py). Bob and Carol swim 300 units away
from Alice and stay side by side (`coop.pool tp <dx> <dy> <id>`: next to that player's cell).

1. KO relayed: Carol takes a lethal bite (the host's `coop.pool hurt`): her game knocks her out
   (a partner can help). The host AND the other guest draw her translucent (`ko[<Carol>]=1`, the
   game's opacity of her puppet < 0.6).
2. Revive between guests: Bob stays right next to her: her game revives her (`revives` +1, up
   again), and Alice's and Bob's games draw her opaque again.
3. Two knocked out at once (#110, BUG-0131): Carol is knocked out again, then Bob: both lie side by
   side, Alice is far. Neither may revive the other (a knocked-out cell is no helper): within 4 s
   no revive on either game, both still knocked out on all three machines.
   SPORE_FIX_OFF=pool.rescue must FAIL here (they revive each other).

  SPORE_LANE=6 python3 tools/tests/coop_party_cell.py
"""
import re
import time

from party import Party
from twoinst import Checks


def pool(p):
    return p.call('coop.pool')


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def ko(out, pid):
    """(state, opacity) of player pid as this game draws it"""
    m = re.search(rf'^ko\[{pid}\]=(\d) .*?op=(-?[\d.]+)', out, re.M)
    return (int(m.group(1)), float(m.group(2))) if m else (None, None)


def wait(cond, secs, step=0.25):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    ck = Checks()
    p = Party.start('cell', 3)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        alice, bob, carol = p.host, p.guests[0], p.guests[1]
        ids = p.ids()
        bid, cid = ids['Bob'], ids['Carol']
        ck(wait(lambda: (num(pool(carol), 'mirrors') or 0) > 5, 30), 'Carol mirrors the host pool')
        print('  Bob:', bob.call('coop.pool tp 300 0 0'), '| Carol:', carol.call(f'coop.pool tp 300 4 0'), flush=True)
        time.sleep(1.5)
        print('  Carol:', carol.call(f'coop.pool tp 2 0 {bid}'), flush=True)
        time.sleep(1.5)
        r0 = num(pool(carol), 'revives') or 0

        # 1. Carol knocked out: everybody draws her translucent
        alice.call(f'coop.pool hurt {cid} 50')
        ck(wait(lambda: num(pool(carol), 'downed') == 1, 5), '[ko] Carol\'s game knocks her out')
        for v in (alice, bob):
            st = wait(lambda: (lambda k: k if k[0] == 1 and k[1] is not None and 0 <= k[1] < 0.6 else None)(ko(pool(v), cid)), 4)
            ck(st, f'[ko] {v.name}\'s game draws Carol knocked out (translucent): {ko(pool(v), cid)}')

        # 2. Bob next to her revives her
        bob.call(f'coop.pool tp 2 0 {cid}')
        ok = wait(lambda: num(pool(carol), 'downed') == 0, 6)
        ck(ok and (num(pool(carol), 'revives') or 0) == r0 + 1,
           f'[revive] Bob next to her revives Carol in her game (revives {r0} -> {num(pool(carol), "revives")})')
        for v in (alice, bob):
            st = wait(lambda: (lambda k: k[1] is not None and k[1] > 0.9 and k[0] != 1)(ko(pool(v), cid)), 5)
            ck(st, f'[revive] {v.name}\'s game draws Carol up again: {ko(pool(v), cid)}')
        time.sleep(4)  # (invulnerable for a moment after a revive)

        # 3. both guests knocked out side by side: nobody revives anybody
        rb, rc = num(pool(bob), 'revives') or 0, num(pool(carol), 'revives') or 0
        carol.call(f'coop.pool tp 2 0 {bid}')
        time.sleep(1)
        alice.call(f'coop.pool hurt {cid} 50')
        ck(wait(lambda: num(pool(carol), 'downed') == 1, 5), '[two] Carol is knocked out')
        alice.call(f'coop.pool hurt {bid} 50')
        ck(wait(lambda: num(pool(bob), 'downed') == 1, 5), '[two] Bob is knocked out next to her (Alice can still help)')
        time.sleep(4)
        b, c = pool(bob), pool(carol)
        print(f'  [two] Bob downed={num(b, "downed")} revives={num(b, "revives")}; Carol downed={num(c, "downed")} '
              f'revives={num(c, "revives")}', flush=True)
        ck(num(b, 'revives') == rb and num(c, 'revives') == rc,
           f'[two] two knocked-out guests side by side do not revive each other (Bob {rb} -> {num(b, "revives")}, '
           f'Carol {rc} -> {num(c, "revives")})')
        ck(num(b, 'downed') == 1 and num(c, 'downed') == 1, '[two] both are still knocked out in their own games')
        h = pool(alice)
        ck(ko(h, bid)[0] == 1 and ko(h, cid)[0] == 1, f'[two] the host draws both knocked out ({ko(h, bid)}, {ko(h, cid)})')
        ck(ko(pool(bob), cid)[0] == 1, f'[two] Bob\'s game draws Carol knocked out ({ko(pool(bob), cid)})')
        # after kDownTime both come back next to Alice (a respawn, not a revive)
        ck(wait(lambda: num(pool(bob), 'downed') == 0 and num(pool(carol), 'downed') == 0, 15),
           '[two] both respawn after the knockout time')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
