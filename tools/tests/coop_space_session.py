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
   cCivData of the guest's political id) with its own two cities (BUG-1151: spice, size, turrets, buildings and names
   as `coop.space allies citytest` made them before the save); the badges it had earned are earned again; Rivals mode is on in both games,
   the guest's money is mg again (not the host's save money) and the two are at war.
PASS: 1 to 3. FAIL twins (each fails its own check of step 3):
  SPORE_FIX_OFF=store.space_colonies  the colony is not the guest's
  SPORE_FIX_OFF=space.colony_cities   the colony comes back with one generated city (BUG-1151)
  SPORE_FIX_OFF=store.space_badges    the guest has the host's badges
  SPORE_FIX_OFF=store.space_rivals    Allies mode, the guest's budget gone, no war

--npc (BUG-1151): instead, the guest's game takes a star of an NPC empire near it (`coop.space allies npcstar`: the
game's own TransferStarOwnership, as when a system is bought or conquered), so the guest's colonies there are on a star
the NPC owns in the host's world. Next session: the star and its colony are the guest's again in the guest's game.
  SPORE_FIX_OFF=space.npc_star_colonies  the restore refuses the NPC's star: the colony is not the guest's (FAIL)

  SPORE_LANE=9 python3 tools/tests/coop_space_session.py [--npc]
"""
import re
import sys
import time

from coop_space_badges_rejoin import badges, earned
from coop_space_colony_rejoin import planet_line
from coop_space_map import no_movie, rosters, sp, wait
from coop_space_rival_merge import money, rivals
import keyviews
import lane
from play_space import Player
from twoinst import Checks, keyframe, session_restart, start



def close_reward(con, tries=3):
    """a badge's "Reward" panel (the restored badges give one in the next session) waits for its green check and takes
    the clicks (BUG-0268, coop_campaign.py REWARD_CHECK): closed like a player"""
    import uifind
    for _ in range(tries):
        r = uifind.shown(con, 'Reward')
        if not r or not 400 < r[0] < 1000:
            return
        con.call('move 932 844')
        time.sleep(0.4)
        con.call('click 932 844')
        time.sleep(1.5)


def land_colony(con, planet, who):
    """coop_space_map.land_on for a planet `coop.targets` lists as a colony of the own empire (key 'C<PlanetID>', tech 5)"""
    from coop_space_map import context, place, targets
    for key in ('C' + planet, 'P' + planet):
        for _ in range(6):
            t = targets(con)
            if key not in t:
                con.call('wheel 720 540 -4')
                time.sleep(1.5)
                continue
            _, x, y = t[key]
            con.call(f'click {x} {y}')
            time.sleep(1.5)
            if place(con)[2] == planet:
                wait(lambda: targets(con).get(key, (1e9,))[0] < 40, 30)
                print(f'  {who} land ({key}):', sp(con, 'land'), flush=True)
                return wait(lambda: context(con) == 'planet', 30) is not None
    print(f'  {who}: could not pick planet {planet} (C or P)', flush=True)
    return False


def colony_frame(host, guest, planet, tag, land=True):
    """BUG-1220: a key frame of the restored colony on both games: both land on it (each game's own ship; a caption or a
    badge's reward box closed first), the game's own camera over the ship (`coop.census all` lists no city in Space, so
    there is no point to aim at). Never fails the test: what it saw is printed."""
    if not planet:
        return
    want = 0x40000000 | int(planet, 16)
    end = time.time() + 400
    seen = {}
    try:
        for role, inst, pid in (('host', lane.HOST, 0), ('guest', lane.GUEST, 1)):
            no_movie(host if role == 'host' else guest, 20)  # a caption (it holds the clicks)
            close_reward(host if role == 'host' else guest)  # a badge's reward box
            p = Player(role, inst, pid, 7 + pid, end, {})
            if not land:  # (--npc: the colony is at another star than the ship: no flight there, the game's own view)
                seen[role] = f'{p.place()[0]:08X} (not flown to)'
                continue
            for _ in range(3):
                p.land_retry(planet, seconds=100)
                if p.place()[0] == want:
                    break
            if p.place()[0] != want:  # (a colony of the player's own empire is a 'C' target, not a 'P')
                land_colony(host if role == 'host' else guest, planet, role)
            seen[role] = f'{p.place()[0]:08X}'
        time.sleep(5)
        for role, con in (('host', host), ('guest', guest)):
            no_movie(con, 20)
            close_reward(con)
        print(f'  colony key frame: {seen}', flush=True)
        time.sleep(1.5)
        if land:
            note = (f'space, next session: each game tried to land on the guest\'s restored colony {planet} (its place in '
                    'the extra: 4xxxxxxx landed, the game\'s own camera over the ship with the colony\'s buildings; '
                    '8xxxxxxx its system view, the colony planet in orbit)')
        else:
            note = (f'space, next session: both games in their own view after the restore (the guest\'s colony {planet} '
                    'is at the NPC\'s star, not flown to): no reward box or caption left over')
        keyframe(tag, note, extra=[f'{k}: {v}' for k, v in seen.items()])
    except Exception as e:  # (a frame never fails the test; Player raises Stop at its deadline)
        print(f'  (colony key frame not taken: {e!r}; {seen})', flush=True)
    finally:
        keyviews.view_off(host, guest)


def city_lines(con, planet):
    """the guest's own cities on a planet as its game's record has them (`allies colonycities`): one line per city"""
    out = sp(con, 'allies colonycities', planet)
    return [re.sub(r'^\s+city \d+ ', '', ln).strip() for ln in out.splitlines() if ln.strip().startswith('city ')]


def relation(con, pid):
    m = re.search(r'\brel((?: \d+=\w+)*)', sp(con, 'rivalry'))
    return dict(re.findall(r' (\d+)=(\w+)', m.group(1))).get(str(pid)) if m else None


def my_id(con):
    m = re.search(r'client id=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else None


def star_owner(con, star):
    m = re.search(r"star %s '[^']*' emp=([0-9A-F]+)" % star, sp(con, 'allies star', star))
    return m.group(1) if m else None


def npc_main():
    host, guest = start('space_empire')  # (contact with another empire: its stars are known)
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    out = sp(guest, 'allies npcstar')
    if out.startswith('no NPC star'):  # the fixture's NPCs have one star each: their home star, as a conquest takes it
        out = sp(guest, 'allies npcstar force')
    print('  guest: ' + out.replace('\n', ' | ')[:400], flush=True)
    m = re.match(r'npcstar (\d+) owner ([0-9A-F]+) -> ([0-9A-F]+) colonies((?: [0-9A-F]+(?:\(new\))?)+)', out)
    ck(m is not None, 'the guest\'s game took a star of an NPC empire with a colony there')
    if not m:
        return ck.result()
    star, npc, mine = m.group(1), m.group(2), m.group(3)
    planet = m.group(4).split()[0].replace('(new)', '')
    gid = my_id(guest)
    ck(mine != npc and star_owner(guest, star) == mine, f'in the guest\'s game star {star} is its own now ({star_owner(guest, star)})')
    ck(star_owner(host, star) == npc, f'in the host\'s game star {star} is still the NPC\'s ({star_owner(host, star)})')

    def host_has():
        return [c for c in rosters(host).get(gid, {}).get('colonies', []) if c[0] == planet and c[2]]
    ck(wait(host_has, 30) is not None, f'the host\'s roster of the guest has colony {planet} as its own')
    time.sleep(3)
    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    time.sleep(3)
    no_movie(guest)

    def back():
        t, c = planet_line(guest, planet)
        return (t, c) if t == 5 and mine in c and star_owner(guest, star) == mine else None
    r = wait(back, 60, 2)
    tech, civs = planet_line(guest, planet)
    print(f'  next session: guest planet {planet} tech={tech} civs {civs} star owner {star_owner(guest, star)} | '
          + sp(guest, 'allies').splitlines()[0][-80:], flush=True)
    ck(r is not None, f'next session: the NPC\'s star {star} and colony {planet} are the guest\'s again in its game')
    ck(star_owner(host, star) == npc, f'the host\'s game keeps the NPC\'s star ({star_owner(host, star)})')
    colony_frame(host, guest, planet, 'npc_colony', land=False)
    ck.faults(host=host, guest=guest)
    return ck.result()


def main():
    if '--npc' in sys.argv:
        return npc_main()
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
    # BUG-1151: the colony's own cities as a player's are after a while (two cities, spice, buildings, turrets)
    out = sp(guest, 'allies citytest', planet) if planet else ''
    print('  guest: ' + out.replace('\n', ' | ')[:500], flush=True)
    ck(out.startswith('citytest ok'), 'the guest\'s colony has two cities with spice and buildings')
    cities0 = city_lines(guest, planet) if planet else []
    ck(wait(lambda: re.search(r'cities: fix=1 kept=[1-9]', sp(host, 'map kept')), 30) is not None,
       'the host keeps the cities of the guest\'s colony (K_COLONY)')

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
    c2 = wait(lambda: city_lines(guest, planet) == cities0 and cities0, 30, 2) if planet else None
    print('  next session: guest cities ' + ' | '.join(city_lines(guest, planet)) + ' (before: ' + ' | '.join(cities0) + ') | '
          + next((ln.strip() for ln in sp(guest, 'map kept').splitlines() if 'cities:' in ln), '-'), flush=True)
    ck(c2 is not None, f'next session: the colony has its own cities again (spice, size, turrets, buildings, names)')

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
    colony_frame(host, guest, planet, 'colony')
    ck.faults(host=host, guest=guest)
    if rivals(host):
        sp(host, 'rivalry mode allies')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
