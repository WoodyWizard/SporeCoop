#!/usr/bin/env python3
"""Space over two evenings: a guest's own colony, badges, Rivals budget and relation come back in the next session
(E1 of 0.7.15: the per-player store on the host's disk; BUG-1149, BUG-1152).

The shared world is the host's and is saved; what is the guest's own (a colony it founded, its badges, its Rivals
budget, its war or alliance with another player) lived only in the host's memory and was gone when the host's game
was closed. The host now keeps it in Games-sporecoop-store/<slot>/<planet>.txt next to its save (store.cpp), written
with every save of its game and read when the world opens; the next session gives it back the way a rejoin does.

1. Both play the `space` fixture. The host switches to Rivals mode; the guest earns 7000 (its own budget mg) and
   declares war on the host. The guest makes a colony of its empire in the home system (`coop.space allies
   colonize`, the record-level colony of coop_space_colony_rejoin.py) and gets badges by the game's own Update
   (counter 2 +60, `coop.space badges event`).
2. A new evening (twoinst.session_restart): the host saves its game, both leave and quit, both games start again, the
   host loads its save and hosts, the guest joins and loads the host's world.
3. In the guest's game: the planet is a colony of the guest's empire again (the game's planet record: tech 5 and a
   cCivData of the guest's political id); the badges it had earned are earned again; Rivals mode is on in both games,
   the guest's money is mg again (not the host's save money) and the two are at war.
PASS: 1 to 3. FAIL twins (each fails its own check of step 3):
  SPORE_FIX_OFF=store.space_colonies  the colony is not the guest's
  SPORE_FIX_OFF=store.space_badges    the guest has the host's badges
  SPORE_FIX_OFF=store.space_rivals    Allies mode, the guest's budget gone, no war

  SPORE_LANE=9 python3 tools/tests/coop_space_session.py
"""
import re
import time

from coop_space_badges_rejoin import badges, earned
from coop_space_colony_rejoin import planet_line
from coop_space_map import no_movie, rosters, sp, wait
from coop_space_rival_merge import money, rivals
from twoinst import Checks, session_restart, start


def relation(con, pid):
    m = re.search(r'\brel((?: \d+=\w+)*)', sp(con, 'rivalry'))
    return dict(re.findall(r' (\d+)=(\w+)', m.group(1))).get(str(pid)) if m else None


def my_id(con):
    m = re.search(r'client id=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else None


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)

    # 1a. Rivals: the guest's own budget and a war
    if (money(host) or 0) < 20000:
        host.call('coop.res add money 30000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'Rivals mode on both games')
    time.sleep(4)
    guest.call('coop.res add money 7000')
    print('  guest:', sp(guest, 'rivalry war 0'), flush=True)
    time.sleep(5)
    mh, mg = money(host), money(guest)
    gid = my_id(guest)
    ck(mh is not None and mg is not None and mg != mh, f'the guest has its own budget {mg} (host {mh})')
    ck(wait(lambda: relation(host, gid) == 'war', 10), f'the host and the guest are at war ({relation(host, gid)})')

    # 1b. a colony of the guest's empire
    out = sp(guest, 'allies colonize')
    print('  guest:', out.splitlines()[0] if out else '-', flush=True)
    m = re.match(r'colonized ([0-9A-F]+)', out)
    ck(m is not None, 'the guest made a colony of its empire in the home system')
    planet = m.group(1) if m else None
    tech, civs = planet_line(guest, planet) if planet else (None, [])
    gpol = civs[-1] if civs else None
    print(f'  guest: planet {planet} tech={tech} civ pols {civs}', flush=True)

    def host_has():
        return [c for c in rosters(host).get(gid, {}).get('colonies', []) if c[0] == planet and c[2]]
    ck(planet and wait(host_has, 30) is not None, f'the host\'s roster of the guest has colony {planet} as its own')

    # 1c. badges the guest's game gives by itself
    gr0, gb0, _ = badges(guest)
    print('  guest: ' + sp(guest, 'badges event 2 60'), flush=True)

    def got_new():
        no_movie(guest, 4)
        _, b, _ = badges(guest)
        return sorted(k for k, st in b.items() if st == 1 and gb0.get(k) != 1) or None
    new = wait(got_new, 40, 2)
    ck(new is not None, f'the guest\'s game gave it badges by itself ({new})')
    time.sleep(5)  # the tables reach the host (K_BADGES)
    no_movie(guest, 4)
    _, gb1, _ = badges(guest)
    _, hb1, _ = badges(host)
    print('  host: ' + sp(host, 'badges kept').replace('\n', ' | ')[:300], flush=True)
    print('  host: ' + sp(host, 'map kept').replace('\n', ' | ')[:300], flush=True)
    mg = money(guest)
    time.sleep(3)  # the last budget report (K_MONEY every 0.5-3 s)

    # 2. a new evening
    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    print('  host: ' + host.call('coop.store')['out'].strip().replace('\n', ' | ')[:400], flush=True)
    time.sleep(3)
    no_movie(guest)
    gid2 = my_id(guest)

    # 3. the guest's own things again, in the guest's game
    def colony_back():
        t, c = planet_line(guest, planet)
        return (t, c) if t == 5 and gpol in c else None
    r = wait(colony_back, 60, 2) if planet else None
    tech, civs = planet_line(guest, planet) if planet else (None, [])
    print(f'  next session: guest planet {planet} tech={tech} civ pols {civs} | ' + sp(guest, 'map kept').splitlines()[0],
          flush=True)
    ck(r is not None, f'next session: {planet} is a colony of the guest\'s empire again (tech {tech}, civs {civs}, its pol {gpol})')

    def badges_back():
        no_movie(guest, 4)
        _, b, _ = badges(guest)
        return b if earned(gb1) - earned(hb1) <= earned(b) else None
    b2 = wait(badges_back, 45, 2)
    _, gb2, _ = badges(guest)
    ck(b2 is not None, f'next session: the badges the guest earned are earned again (missing '
                       f'{sorted((earned(gb1) - earned(hb1)) - earned(gb2))})')

    ok = wait(lambda: rivals(host) and rivals(guest) and money(guest) == mg, 60, 2)
    print(f'  next session: rivals host {rivals(host)} guest {rivals(guest)}, guest money {money(guest)} (kept {mg}), '
          f'host money {money(host)} | ' + sp(host, 'rivalry').splitlines()[-1].strip(), flush=True)
    ck(ok, f'next session: Rivals mode goes on and the guest\'s budget is its own {mg} again (it has {money(guest)})')
    ck(wait(lambda: relation(host, gid2) == 'war', 15), f'next session: still at war ({relation(host, gid2)})')
    ck.faults(host=host, guest=guest)
    if rivals(host):
        sp(host, 'rivalry mode allies')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
