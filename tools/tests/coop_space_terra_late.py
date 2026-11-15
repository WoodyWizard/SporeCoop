#!/usr/bin/env python3
"""Space: a partner's terraforming reaches a game that was outside Space or had not generated that star yet, and a
player who enters that star later (BUG-1155).

A terraforming partner sends its planet record values (K_TERRA_REC, BUG-0883) while they change. A receiver outside
Space, or one whose game has not generated that star's planets (BUG-1042's checks), skipped them for good, and the
host did not keep them: the terraforming never reached that game (nor the host's save, when the host was the one
that skipped). Now a skipped record waits until the planet exists here in Space, and the host keeps the latest
values of each planet and sends the ones of a star system to a partner who enters it.

Fixture space_free. The values are sent the way terraforming sends them (`coop.space terrarec`, the K_TERRA_REC of
the BUG-1042 test).
  1. outside Space: the guest acts as in an editor (`coop.space away 1`), the host sends values for 01460035, then the
     guest comes back (`away 0`): its record of 01460035 has them.
  2. a star the host never generated (`coop.space nogen` on the host): the guest sends values for planet 0 of it;
     the host jumps there (its game generates the planets): the host's record of that planet has them.
  3. the host kept them: the guest (whose own record of that planet never had them) jumps to that star too: the
     host sends what it kept and the guest's record has them.
PASS: all three records are the sent values. SPORE_FIX_OFF=space.terra_rec_late must FAIL (all skipped).

  SPORE_LANE=6 python3 tools/tests/coop_space_terra_late.py
"""
import re
import time

from twoinst import Checks, start

HOME = '01460035'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def rec(con, planet):
    m = re.search(r'b0=([\d.-]+) b4=([\d.-]+) b8=([\d.-]+)', sp(con, 'planet', planet))
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def same(r, want):
    return r is not None and abs(r[0] - want[0]) < 0.01 and abs(r[1] - want[1]) < 0.01


def star_now(con):
    m = re.search(r'star (\d+)', sp(con, 'star'))
    return int(m.group(1)) if m else -1


def jump(con, who, star, ck):
    for _ in range(3):
        print(f'  {who} jumps to {star:06X}:', sp(con, 'jumpto', f'0x{star:X}', 'force'), flush=True)
        t_end = time.time() + 60
        while time.time() < t_end and star_now(con) != star:
            time.sleep(2)
        if star_now(con) == star:
            break
    time.sleep(5)
    ck(star_now(con) == star, f'the {who} is in the star system {star:06X} ({sp(con, "star")})')


def main():
    host, guest = start('space_free')
    ck = Checks()
    time.sleep(3)
    # 1. outside Space
    r0 = rec(guest, HOME)
    want1 = (0.27, 0.71, 0.33) if not r0 or abs(r0[0] - 0.27) > 0.05 else (0.62, 0.31, 0.2)
    print('  guest:', sp(guest, 'away 1'), flush=True)
    time.sleep(1)
    print(f'  host sends {want1} for {HOME}:', sp(host, 'terrarec', '4' + HOME[1:], *want1), flush=True)
    time.sleep(3)
    print('  guest:', sp(guest, 'away 0'), flush=True)
    r1 = None
    t_end = time.time() + 10
    while time.time() < t_end:
        time.sleep(2)
        r1 = rec(guest, HOME)
        if same(r1, want1):
            break
    print(f'  guest record of {HOME}: {r0} -> {r1}', flush=True)
    ck(same(r1, want1), f"1. the guest's record takes the values sent while it was outside Space ({r1} / {want1})")
    # 2. a star the host never generated
    ng = sp(host, 'nogen')
    m = re.search(r'nogen ([0-9A-F]{8})', ng)
    star = int(m.group(1), 16) if m else 0
    ck(star, f"a star without generated planets in the host's game ({ng})")
    if star:
        planet = f'{star:08X}'  # planet index 0
        want2 = (0.44, 0.56, 0.5)
        print(f'  guest sends {want2} for {planet}:', sp(guest, 'terrarec', f'{0x40000000 | star:08X}', *want2), flush=True)
        time.sleep(3)
        print('  host:', sp(host).split('terra_rec_got', 1)[-1][:160], flush=True)
        jump(host, 'host', star, ck)
        r2 = None
        t_end = time.time() + 15
        while time.time() < t_end:
            r2 = rec(host, planet)
            if same(r2, want2):
                break
            time.sleep(2)
        print(f"  host record of {planet}: {r2}", flush=True)
        ck(same(r2, want2), f"2. the host's record of a planet it generated later has the guest's values ({r2})")
        # 3. the host kept them for a partner who enters that star
        jump(guest, 'guest', star, ck)
        r3 = None
        t_end = time.time() + 15
        while time.time() < t_end:
            r3 = rec(guest, planet)
            if same(r3, want2):
                break
            time.sleep(2)
        print(f"  guest record of {planet}: {r3}", flush=True)
        ck(same(r3, want2), f"3. the host sends what it kept to a partner who enters the star ({r3})")
    print('  host: ' + re.sub(r'.*?(terra_rec_got.*?terra_resent=\d+).*', r'\1', sp(host).replace('\n', ' ')), flush=True)
    print('  guest: ' + re.sub(r'.*?(terra_rec_got.*?terra_resent=\d+).*', r'\1', sp(guest).replace('\n', ' ')), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
