#!/usr/bin/env python3
"""BUG-0543: a partner's new colony in a star system both players own is a colony in my game too.

The players start as one empire (the guest plays a copy of the host's save), so the home system is the
host's star in both games. The guest founds a colony on a free planet of that system after the join
(`coop.space allies colonize <PlanetID>`: the record-level colony of the guest's empire, as the game
makes one). Before the fix the host's game skipped it ("your own star system"): the planet stayed
free there and the host could found its own colony on it.

1. The guest's colony: the planet's record in the guest's game has tech 5 and a cCivData of its empire.
2. The host's game: the same planet's record has tech 5 and a cCivData of the host's empire (the star
   has one owner), within 30 s (the guest's roster).
3. As before a save (`allies remove`) the host's planet is free again (a save never holds it), and
   comes back after it (`allies apply`).

  coop_space_allies_own.py   (SPORE_FIX_OFF=allies.own_star: FAILS in 2)"""
import re
import time

from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def planet_line(con, pid):
    out = sp(con, 'allies planet', pid)
    for ln in out.splitlines():
        if ln.strip().startswith('planet ' + pid):
            return ln.strip()
    return ''


def tech(line):
    m = re.search(r'tech=(\d+)', line)
    return int(m.group(1)) if m else -1


def emp_pol(con):
    m = re.search(r'pol (?:0x)?([0-9A-Fa-f]{8})', con.call('coop.space map roster')['out'])
    return m.group(1).upper() if m else ''


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    st = re.search(r'star (\d+)', sp(guest, 'star'))
    star = st.group(1) if st else ''
    hs = sp(host, 'allies star', star)
    gs = sp(guest, 'allies star', star)
    print('  host:', hs.replace('\n', ' | '))
    print('  guest:', gs.replace('\n', ' | '))
    ck('(mine)' in hs.splitlines()[0] and '(mine)' in gs.splitlines()[0], f'star {star} is the players\' own in both games')
    host_active = re.search(r'active ([0-9A-F]+)', sp(host, 'star')).group(1)
    guest_active = re.search(r'active ([0-9A-F]+)', sp(guest, 'star')).group(1)
    free = [m.group(1) for m in re.finditer(r'planet ([0-9A-F]{8}) \'[^\']*\' tech=([0-3])\b', hs)
            if m.group(1) not in (host_active, guest_active)]
    ck(free, f'a free planet in that system (host on {host_active}, guest on {guest_active})')
    if not free:
        return ck.result()
    pid = free[0]
    print('  guest colonizes:', sp(guest, 'allies colonize', pid).replace('\n', ' | '))
    gl = planet_line(guest, pid)
    ck(tech(gl) == 5, f'the guest\'s game: {pid} is its colony ({gl})')
    gpol = re.search(r'civ\(pol=(?:0x)?([0-9A-Fa-f]+)', gl)
    hp = None
    ok = wait(lambda: tech(planet_line(host, pid)) == 5, 30)
    hl = planet_line(host, pid)
    print('  host planet:', hl)
    print('  host allies:', sp(host, 'allies list').replace('\n', ' | '))
    ck(ok, f'the host\'s game: {pid} is a colony too ({hl})')
    hpol = re.search(r'emp=(?:0x)?([0-9A-Fa-f]+)', hs)
    ck(hpol and ('civ(pol=' + hpol.group(1)) in hl.replace('0x', ''),
       f'of the host\'s empire, the star\'s owner ({hpol.group(1) if hpol else "?"})')
    print('  host star:', sp(host, 'allies star', star).replace('\n', ' | '))
    print('  remove:', sp(host, 'allies remove').replace('\n', ' | '))
    hl2 = planet_line(host, pid)
    ck(tech(hl2) < 5 and 'civ(' not in hl2, f'as before a save the host\'s planet is free again ({hl2})')
    print('  apply:', sp(host, 'allies apply').replace('\n', ' | '))
    print('  host allies:', sp(host, 'allies list').replace('\n', ' | '))
    hl3 = planet_line(host, pid)
    ck(tech(hl3) == 5, f'and a colony again after it ({hl3})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
