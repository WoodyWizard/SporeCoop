#!/usr/bin/env python3
"""Space with three players, two of them named Bob (BUG-0152, #110, #120): names, rescue, gifts and
rivals relations between guests, checked on every machine.

Alice hosts; Bob and a second guest that joins under the name "Bob" too (tools/tests/party.py,
instance 20+N, called Carol here).

1. names   the host makes the second "Bob" unique (BUG-0141): two different session names, each
           guest's own game knows its own. SPORE_FIX_OFF=core.unique_names must FAIL here.
2. gifts   tool gifts guest to guest: Alice gives Bob 5 Antimatter Bomb charges, Bob gives Carol 3
           (`coop.space trade give`): Carol's game has the tool with 3 charges, Bob's has 2 left.
3. crew    a contract crew without the host: Alice offers a 1-star survey and stays; Bob and the
           second Bob accept and jump there: done on every machine, the reward money arrives once on
           every machine, the reward tool on both guests.
   sale: the second Bob sells spice to Bob's new colony there (money on every machine, the cargo).
   allies: Bob colonizes a planet there, the second Bob at another free star: every other machine's
           galaxy shows each star owned by that player's empire (the two names that were both "Bob").
4. rivals  rivals mode for three (`coop.space rivalry mode rivals`): every machine has its own
           budget; Bob declares war on Carol: Bob and Carol see war between them, Alice is neutral
           with both; Bob proposes peace, Carol accepts: neutral again; back to allies mode.
5. rescue  (#110, BUG-0131) Bob's ship takes a lethal hit next to the others: disabled (a partner can
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


def cst(x):
    return dict(re.findall(r'(\w+)=(\S+)', sp(x, 'contracts')))


def cmoney(x):
    m = re.search(r'money (-?\d+)', sp(x, 'contracts money'))
    return int(m.group(1)) if m else -1


def crew_contract(ck, p):
    """the host offers a 1-star survey and stays home; the two guests accept and jump there: done on
    every machine, the reward money arrives once on every machine, the tool goes to the crew only"""
    alice, bob, carol = p.host, p.guests[0], p.guests[1]
    sp(alice, 'contracts period 0')
    sp(alice, 'contracts limit 0')
    for g in (bob, carol):
        print(f'  [crew] {g.name} drive:', sp(g, 'tool add interstellardrive'), flush=True)
    m0 = {x.name: cmoney(x) for x in p.all}
    print('  [crew]', sp(alice, 'contracts offer survey 1'), flush=True)
    targets = re.findall(r'target (\d+) \S+ (.+?) done', sp(alice, 'contracts targets'))
    reward = int(cst(alice).get('money', 0))
    ck(len(targets) == 1, f'[crew] a survey of one star near the host ({targets})')
    if not targets:
        return
    for g in (bob, carol):
        print(f'  [crew] {g.name}:', sp(g, 'contracts accept'), flush=True)
    ck(wait(lambda: cst(alice).get('state') == 'active', 10), f'[crew] two guests make the crew: active ({cst(alice)})')
    star = targets[0][0]
    for g in (bob, carol):
        print(f'  [crew] {g.name} jumpto {star}:', sp(g, 'jumpto', star), flush=True)
    ok = wait(lambda: all(cst(x).get('state') == 'done' for x in p.all), 90, 1)
    ck(ok, f'[crew] the survey is done on every machine ({[cst(x).get("state") for x in p.all]})')
    ok = wait(lambda: all(cmoney(x) == m0[x.name] + reward for x in p.all), 15)
    ck(ok, f'[crew] the reward arrived once on every machine (+{reward}: '
           f'{ {x.name: cmoney(x) - m0[x.name] for x in p.all} })')
    tool = cst(alice).get('tool', '00000000')
    if tool != '00000000':
        for g in (bob, carol):
            ck('have' in sp(g, 'contracts tool', tool), f'[crew] {g.name} got the reward tool ({tool})')
    return star


def no_movie(x):
    """press through a running cinematic (a jump is refused while one plays)"""
    for i in range(10):
        if 'movie=0' in sp(x, 'hp'):
            return True
        x.call('key esc' if i % 3 == 2 else 'key enter')
        time.sleep(2)
    return 'movie=0' in sp(x, 'hp')


def owner_of(x, star):
    m = re.search(r"owner='(.*?)'", sp(x, 'allies star', star))
    return m.group(1) if m else None


def cargo(x, inst):
    m = re.search(r'cargo %s \'[^\']*\' x(\d+)' % inst, sp(x, 'trade spices'))
    return int(m.group(1)) if m else 0


def tmoney(x):
    m = re.search(r'money (-?\d+)', sp(x, 'trade money'))
    return int(m.group(1)) if m else None


def spice_sale(ck, p):
    """a spice sale from guest to guest: the second Bob, at the star of Bob's new colony, sells spice
    to it (`trade sell`, the Space page's "Sell"): the spice leaves her cargo and arrives in Bob's,
    the shared money grows by the price on every machine"""
    bob, carol = p.guests[0], p.guests[1]
    print('  [sale] spice:', sp(carol, 'trade spice 0 10'), sp(carol, 'trade spice 1 10'), flush=True)
    wait(lambda: 'deal ' in sp(carol, 'trade deal'), 20)
    deal = sp(carol, 'trade deal')
    print('  [sale] deal:', deal, flush=True)
    dm = re.search(r'deal (.+?) planet \S+ spice (\S+) .* x(\d+) price (\d+)', deal)
    ck(dm and bob.netname == dm.group(1), f'[sale] the second Bob can sell spice to Bob\'s colony ({deal})')
    if not dm:
        return
    inst, n, price = dm.group(2), int(dm.group(3)), int(dm.group(4))
    m0 = {x.name: tmoney(x) for x in p.all}
    c0, b0 = cargo(carol, inst), cargo(bob, inst)
    print('  [sale] sell:', sp(carol, 'trade sell'), flush=True)
    total = n * price
    ok = wait(lambda: all(tmoney(x) == m0[x.name] + total for x in p.all), 20)
    ck(ok, f'[sale] the shared money grew by {total} on every machine ({ {x.name: (tmoney(x) or 0) - (m0[x.name] or 0) for x in p.all} })')
    ck(cargo(carol, inst) == c0 - n, f'[sale] the spice left the seller\'s cargo ({c0} -> {cargo(carol, inst)})')
    ck(wait(lambda: cargo(bob, inst) == b0 + n, 15), f'[sale] it arrived in Bob\'s cargo ({b0} -> {cargo(bob, inst)})')


def two_mirrors(ck, p, star1):
    """allies mirrors of two partners: Bob colonizes a planet at the survey star, the second Bob one
    at another free star; every other machine's galaxy shows each star owned by that player's
    empire (two names that were both "Bob", BUG-0141)"""
    alice, bob, carol = p.host, p.guests[0], p.guests[1]
    out = sp(bob, 'allies colonize')
    print(f'  [allies] Bob at {star1}:', out, flush=True)
    spice_sale(ck, p)
    free = [x for x in re.findall(r'free (\d+)', sp(carol, 'allies free')) if x != str(star1)]
    ck(free, f'[allies] another free star for the second Bob ({free[:4]})')
    if not free:
        return
    star2 = free[0]
    no_movie(carol)  # (the survey's or the colony's cinematic refuses a jump)
    print(f'  [allies] second Bob jumpto {star2}:', sp(carol, 'jumpto', star2), flush=True)
    wait(lambda: re.search(r'star (\d+)', sp(carol, 'star')) and re.search(r'star (\d+)', sp(carol, 'star')).group(1) == star2, 40)
    no_movie(carol)
    print('  [allies] second Bob:', sp(carol, 'allies colonize'), flush=True)
    # the owner of a partner's star: that player's empire, named after the player
    want = {star1: bob.netname, star2: carol.netname}
    ok_name = lambda o, n: o in (n, f"{n}'s empire")  # noqa: E731
    for x, stars in ((alice, (star1, star2)), (bob, (star2,)), (carol, (star1,))):
        for st in stars:
            got = wait(lambda: ok_name(owner_of(x, st), want[st]), 30)
            ck(got, f'[allies] {x.name}\'s galaxy: star {st} belongs to {want[st]!r}\'s empire ({owner_of(x, st)!r})')


def home(ck, p):
    alice = p.host
    for g in p.guests:  # back to the host for the rest
        no_movie(g)
        sp(g, 'jumpto', re.search(r'star (\d+)', sp(alice, 'star')).group(1))
    ck(wait(lambda: all('partners_here=2' in sp(x) for x in p.all), 60), 'everybody back at the host\'s star')


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

        # 3. a contract crew without the host: Bob and the second Bob take a 1-star survey and fly there
        star1 = crew_contract(ck, p)
        if star1:
            two_mirrors(ck, p, star1)
        home(ck, p)

        # 4. rivals among three
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

        # 5. rescue: two partners down, then the host's lethal hit destroys its ship
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
