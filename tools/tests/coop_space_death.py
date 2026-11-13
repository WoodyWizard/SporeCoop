#!/usr/bin/env python3
"""Space: a destroyed ship comes back (docs/re/space.md 7.2). Both native deaths end in the death
caption "...you return to your homeworld, a new spaceship at the ready" with an OK button: the game
respawns the ship (cGameDataUFO::Respawn) only after the player confirms it (Enter or the check
button), then plays the respawn caption "We've built you a new ship..." (cinematic CVG2SPG_short).
Checks:
1. rescue timeout: a lethal hit next to the partner disables the ship, nobody repairs it in 20 s ->
   the native death, the caption waits for the player, OK -> the ship is back at a colony, full HP;
2. no jump to another star while the respawn caption plays (leaving the planet then made the end of
   that caption fault in the game and the next death never respawned);
3. lone death: the ship alone in another star system dies at once (no disabling), OK -> back;
4. the old jump (forced during the respawn caption), then a lone death: the end of the respawn
   caption runs without a planet (the game's null [cSimulatorSpaceGame+0x54], guarded in
   gamefixes.cpp), OK still respawns the ship (natively or by the watchdog, message 0x67B2A95).
Any fault caught by either game during the test fails it."""
import re
import time

from twoinst import Checks, shot, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def stat(con, key):
    m = re.search(r'space co-op:.*?\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def field(hp, key):
    m = re.search(r'\b' + key + r'=(\d+)', hp)
    return int(m.group(1)) if m else -1


def star_of(con):
    m = re.search(r'place ([0-9A-F]+) star (\d+)', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2))) if m else (0, 0)


def ok_until(guest, cond, timeout):
    """press the captions' OK (Enter) like a player until cond(hp line) holds"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        hp = sp(guest, 'hp')
        if cond(hp):
            return hp, time.time() - t0
        guest.call('key enter')
        time.sleep(2)
    return sp(guest, 'hp'), time.time() - t0


def respawn(ck, guest, what, home):
    """the death caption is up and waits: confirm it like a player, the ship respawns at a colony"""
    hp = sp(guest, 'hp')
    ck(field(hp, 'state') == 2 and field(hp, 'cine') == 1, f'{what}: the ship is destroyed, death caption up ({hp})')
    time.sleep(6)
    hp = sp(guest, 'hp')
    print('  6 s later, nobody pressed OK:', hp)
    ck(field(hp, 'state') == 2, f'{what}: the caption waits for the player (no respawn on its own)')
    shot(guest, f'space_death_{what}_caption.png')
    hp, t = ok_until(guest, lambda h: field(h, 'state') == 0, 30)
    m = re.search(r'hp ([\d.]+) max ([\d.]+)', hp)
    print(f'  after OK ({t:.0f} s): {hp}')
    ck(field(hp, 'state') == 0 and field(hp, 'cine') == 0 and m and m.group(1) == m.group(2),
       f'{what}: OK respawns the ship with full HP')
    time.sleep(4)  # the travel to the colony and the landing
    place, star = star_of(guest)
    print(f'  guest place {place:08X} star {star}')
    ck(place & 0x40000000 and star == home, f'{what}: the ship is back on a colony planet of its home star ({star})')
    shot(guest, f'space_death_{what}_respawned.png')


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    _, home = star_of(host)
    print('  host', sp(host, 'star'), '| guest', sp(guest, 'star'), '| dist', sp(guest, 'dist'))
    ck(stat(guest, 'partners_here') >= 1, 'both ships are in the same star system')
    # per-process counters: a warm run (lab, w49-warm) starts from the values the games already have
    lost0, rescues0 = max(0, stat(guest, 'lost')), max(0, stat(guest, 'rescues'))

    # 1. disabled next to the partner, nobody comes: the native death after 20 s
    # (the ships start a few metres apart and a test can not fly the player's ship away: the repair
    # range goes to 0 instead, so the partner next to it can not repair it)
    print(' ', sp(guest, 'repairdist', 0))
    print('  hit:', sp(guest, 'hit', 100000))
    ck(field(sp(guest, 'hp'), 'down') == 1, 'a lethal hit next to a partner disables the ship')
    t0 = time.time()
    while time.time() - t0 < 30 and field(sp(guest, 'hp'), 'state') != 2:
        time.sleep(1)
    print(f'  destroyed after {time.time() - t0:.0f} s:', sp(guest, 'hp'))
    ck(stat(guest, 'lost') - lost0 == 1 and stat(guest, 'rescues') == rescues0, 'not repaired in 20 s: the ship is lost')
    print(' ', sp(guest, 'repairdist', 60))
    respawn(ck, guest, 'timeout', home)

    # 2. the respawn caption plays: no jump away until it is over
    hp = sp(guest, 'hp')
    j = sp(guest, 'jumpto', home + 1)
    print(f'  jump during the respawn caption ({hp}): {j}')
    ck(field(hp, 'movie') == 1 and 'cinematic' in j, 'no jump while the respawn caption plays')
    hp, t = ok_until(guest, lambda h: field(h, 'movie') == 0, 60)
    print(f'  respawn caption over after {t:.0f} s: {hp}')
    ck(field(hp, 'movie') == 0, 'the respawn caption ends with OK')

    # 3. alone in another system: no disabling, the native death at once
    print('  guest away:', sp(guest, 'jumpto', home + 1))
    time.sleep(15)
    _, away = star_of(guest)
    ck(away and away != home, f'the guest travelled to another star ({away})')
    hp = sp(guest, 'hit', 100000)
    print('  hit alone:', hp)
    ck(field(hp, 'down') == 0, 'alone the ship is not disabled (the native death)')
    respawn(ck, guest, 'lone', home)
    ck.faults(host=host, guest=guest)

    # 4. what the jump did before: leave the planet while the respawn caption plays, then die alone.
    # The end of that caption used to fault in the game (its handler clears a set of
    # [cSimulatorSpaceGame+0x54], null without the planet: AV at 0x7CFCC0) and the ship never got
    # the end of its death caption. Now gamefixes skips that cleanup and the ship respawns.
    hp = sp(guest, 'hp')
    print(f'  forced jump during the respawn caption ({hp}):', sp(guest, 'jumpto', home + 1, 'force'))
    time.sleep(15)
    print('  guest', sp(guest, 'star'), '| hit:', sp(guest, 'hit', 100000))
    hp, t = ok_until(guest, lambda h: field(h, 'state') == 0, 45)
    exc = guest.call('exc')['out'].splitlines()
    print(f'  after OK ({t:.0f} s): {hp} | respawns_forced={stat(guest, "respawns_forced")} | guest',
          ' '.join(exc[:2]))
    ck(field(hp, 'state') == 0, 'the ship respawns after a death that ended the respawn caption off the planet')
    m = re.search(r'respawn caption without a planet (\d+)', guest.call('coop.status')['out'])
    print('  guest game fixes:', m.group(0) if m else '(no counter)')
    ck(m and int(m.group(1)) >= 1, 'the end of the respawn caption off the planet is skipped by the guard')
    ok_until(guest, lambda h: field(h, 'movie') == 0, 60)

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
