#!/usr/bin/env python3
"""Space with three players, two of them named Bob (BUG-0152, #110, #120): names, rescue, gifts and
rivals relations between guests, checked on every machine.

Alice hosts; Bob and a second guest that joins under the name "Bob" too (tools/tests/party.py,
instance 20+N, called Carol here).

1. names   the host makes the second "Bob" unique (BUG-0141): two different session names, each
           guest's own game knows its own. SPORE_FIX_OFF=core.unique_names must FAIL here.
2. gifts   tool gifts guest to guest: Alice gives Bob 5 Antimatter Bomb charges, Bob gives Carol 3
           (`coop.space trade give`): Carol's game has the tool with 3 charges, Bob's has 2 left.
3. rivals  rivals mode for three (`coop.space rivalry mode rivals`): every machine has its own
           budget; Bob declares war on Carol: Bob and Carol see war between them, Alice is neutral
           with both; Bob proposes peace, Carol accepts: neutral again; back to allies mode.
4. rescue  (#110, BUG-0131) Bob's ship takes a lethal hit next to the others: disabled (a partner can
           repair it); then Carol's: disabled too (Alice can still help); every machine counts two
           partners down. Then Alice's ship takes a lethal hit: both partners are disabled, so it
           is destroyed (the game's UFO state 2), not disabled. SPORE_FIX_OFF=space.helper must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_party_space.py
"""
import re
import time

from party import Party
from twoinst import Checks

BOMB = '144DF50A'  # Antimatter Bomb (uses ammo)


def sp(p, *a):
    return p.call('coop.space ' + ' '.join(str(x) for x in a)).strip()


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'(-?[\d.]+)', text)
    return float(m.group(1)) if m else -1


def ammo(p):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % BOMB, sp(p, 'trade all'))
    return int(m.group(1)) if m else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def relations(p):
    """{other id: relation} as this machine's rivalry status shows it"""
    out = sp(p, 'rivalry')
    m = re.search(r'\brel((?: \d+=\w+)*)', out)
    return {int(a): b for a, b in re.findall(r' (\d+)=(\w+)', m.group(1))} if m else {}, out


def main():
    ck = Checks()
    p = Party.start('space', 3, join_names={'Carol': 'Bob'})
    try:
        ids = p.ids()
        alice, bob, carol = p.host, p.guests[0], p.guests[1]
        names = sorted(n for n in ids if n not in ('Alice', 'Carol'))
        print(f'  roster {ids}; Bob={bob.pid} ({bob.netname}), second Bob={carol.pid} ({carol.netname})', flush=True)
        # 1. the second Bob got another name
        ck(bob.pid is not None and carol.pid is not None and bob.pid != carol.pid, '[names] two players, two ids')
        ck(bob.netname != carol.netname and carol.netname not in (None, 'Bob'),
           f'[names] the second "Bob" has its own name in the session ({bob.netname!r}, {carol.netname!r}; {names})')
        if not p.check_together(ck, 'join'):
            return ck.result()
        ck(wait(lambda: all('partners_here=2' in sp(x) for x in p.all), 60), 'all three ships in the same place')

        # 2. tool gifts guest to guest
        print('  [gift] Alice:', sp(alice, 'tool add', '0x' + BOMB), '|', sp(alice, 'trade ammo', BOMB, 12), flush=True)
        print('  [gift] Alice -> Bob:', sp(alice, 'trade give', BOMB, 5, bob.pid), flush=True)
        ck(wait(lambda: ammo(bob) == 5, 15), f'[gift] Bob has 5 charges ({ammo(bob)})')
        print('  [gift] Bob -> second Bob:', sp(bob, 'trade give', BOMB, 3, carol.pid), flush=True)
        ck(wait(lambda: ammo(carol) == 3, 15), f'[gift] the second Bob\'s game has the tool with 3 charges ({ammo(carol)})')
        ck(ammo(bob) == 2, f'[gift] Bob has 2 left ({ammo(bob)})')

        # 3. rivals among three
        print('  [rivals]', sp(alice, 'rivalry mode rivals'), flush=True)
        ck(wait(lambda: all('rivals=1' in sp(x, 'rivalry') for x in p.all), 20), '[rivals] every machine is in rivals mode')
        print('  [rivals] Bob war:', sp(bob, 'rivalry war', carol.pid), flush=True)
        ok = wait(lambda: relations(bob)[0].get(carol.pid) == 'war' and relations(carol)[0].get(bob.pid) == 'war', 15)
        ck(ok, f'[rivals] Bob and the second Bob are at war on both machines ({relations(bob)[1][:120]} | '
               f'{relations(carol)[1][:120]})')
        ra = relations(alice)[0]
        ck(ra.get(bob.pid) != 'war' and ra.get(carol.pid) != 'war', f'[rivals] Alice is at war with nobody ({ra})')
        print('  [rivals] peace:', sp(bob, 'rivalry peace', carol.pid), sp(carol, 'rivalry accept', bob.pid), flush=True)
        ck(wait(lambda: relations(bob)[0].get(carol.pid) != 'war' and relations(carol)[0].get(bob.pid) != 'war', 15),
           '[rivals] peace between them on both machines')
        print('  [rivals]', sp(alice, 'rivalry mode allies'), flush=True)
        ck(wait(lambda: all('rivals=0' in sp(x, 'rivalry') for x in p.all), 20), '[rivals] back to allies on every machine')
        time.sleep(2)

        # 4. rescue: two partners down, then the host's lethal hit destroys its ship
        g = sp(bob, 'hit', field(sp(bob, 'hp'), 'hp') + 50)
        ck(wait(lambda: field(sp(bob, 'hp'), 'down') == 1, 5), f'[rescue] Bob\'s ship is disabled ({g})')
        c = sp(carol, 'hit', field(sp(carol, 'hp'), 'hp') + 50)
        ck(wait(lambda: field(sp(carol, 'hp'), 'down') == 1, 5), f'[rescue] the second Bob\'s ship is disabled too ({c})')
        ck(wait(lambda: 'partners_down=2' in sp(alice), 5), '[rescue] the host counts two partners down')
        ck(wait(lambda: 'partners_down=1' in sp(bob) and 'partners_down=1' in sp(carol), 5),
           '[rescue] each guest counts the other one down')
        h = sp(alice, 'hit', field(sp(alice, 'hp'), 'hp') + 50)
        time.sleep(1.5)
        st = sp(alice, 'hp')
        print('  [rescue] host after:', st, flush=True)
        ck(field(h, 'down') == 0 and field(st, 'state') == 2,
           f'[rescue] the host\'s lethal hit destroys its ship: both partners are disabled ({st})')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
