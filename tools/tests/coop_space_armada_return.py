#!/usr/bin/env python3
"""Space: an armada wave survives the host leaving the planet (BUG-0046).

The armada contract's waves are the game's own pirate raids. The host leaves the colony during a
wave and lands again: the old code started a second raid on the planet while the first still ran
(the end of either decided the wave, the ships of both got their HP raised). Now the wave keeps
its raid mission and resumes it. Checked in the host's game: the raid missions on the colony
(`coop.space contracts raids`, the game's mission list, state 2 or 3) after the return: one.

BUG-0227, in the same run:
 * HP: wave 1's ships have their max HP raised. For the resumed wave `contracts tough_reuse 1`
   simulates a ship at an address that was raised before (the pointer goes into the "raised once"
   set first); its ships must still get the raised max HP (`coop.fleet list`), as the set is keyed
   by object serial. (Without contracts.wave_tough they keep the base HP.)
 * no ships: a second leave and return with `contracts hideships 1` (the resumed raid's ships never
   come; the hook also ignores that raid's end state): the wave must not be won without a fight,
   and one raid is on the colony (the old one aborted when it still runs, or a new one). (Without contracts.wave_resume the wave advanced 30 s later.)

  SPORE_LANE=6 python3 tools/tests/coop_space_armada_return.py [--without-fix]
  (SPORE_FIX_OFF=contracts.wave_resume: the no-ships part FAILS; contracts.wave_tough: the HP part)
"""
import re
import sys
import time

from coop_space_contracts import captions, click_row, sp, st, wait
from coop_ui import Ui
from twoinst import Checks, start


def raids(con):
    return re.findall(r'^raid (\S+) state (\d)', sp(con, 'contracts raids'), re.M)


def ship_hp(con):
    """{pointer: max hp} of the game's UFO list"""
    return {m.group(1): float(m.group(2)) for m in
            re.finditer(r'ship #-?\d+ (\S+) type=\d+ pol=\w+ hp=[\d.]+/([\d.]+)', con.call('coop.fleet list')['out'])}


def armada(con):
    return re.findall(r'^armada (\S+)', sp(con, 'contracts ships'), re.M)


def wave(con):
    m = re.search(r'wave (\d+)/\d+ on=(\d) ships=(\d) resumed=(\d) .* aborted=(\d+)', sp(con, 'contracts wave'))
    return dict(n=int(m.group(1)), on=int(m.group(2)), ships=int(m.group(3)), resumed=int(m.group(4)),
                aborted=int(m.group(5))) if m else {}


def leave_and_return(host, uh, planet, ck):
    for _ in range(10):  # (the wheel did nothing during the wave: the raid's captions / the HUD)
        if not sp(host, 'star').startswith('place 4'):
            break
        captions(host, uh)
        print('  takeoff:', sp(host, 'takeoff'))
        time.sleep(2)
    ck(not sp(host, 'star').startswith('place 4'), f'the host left the planet ({sp(host, "star")})')
    time.sleep(6)
    print('  raids while away:', raids(host))
    for _ in range(10):
        captions(host, uh)
        r = sp(host, 'land')
        if wait(lambda: sp(host, 'star').startswith('place 4'), 20):
            break
        print('  land:', r)
    ck(sp(host, 'star').split()[1] == planet, f'the host is back on the colony ({sp(host, "star")})')


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    if without:
        print(' ', sp(host, 'contracts wavefix 0'))
    sp(host, 'contracts period 0')
    sp(host, 'contracts limit 0')
    hstar = re.search(r'star (\d+)', sp(host, 'star')).group(1)
    if re.search(r'star (\d+)', sp(guest, 'star')).group(1) != hstar:
        print('  guest to the host\'s star:', sp(guest, 'jumpto', hstar))
        time.sleep(12)
    uh, ug = Ui(host, 'host'), Ui(guest, 'guest')
    print('  ', sp(host, 'contracts offer armada here'))
    for c in (host, guest):
        sp(c, 'contracts accept')
    ck(wait(lambda: st(host)['state'] == 'active', 15), 'the armada contract is active')
    for ui, c in ((uh, host), (ug, guest)):
        ck(click_row(ui, c, 'Armada at', 'Land'), f'{ui.who}: a click on "Land"')
    ok = wait(lambda: sp(host, 'star').startswith('place 4') and sp(guest, 'star').split()[1] == sp(host, 'star').split()[1], 30)
    ck(ok, f'both on the colony ({sp(host, "star")} / {sp(guest, "star")})')
    planet = sp(host, 'star').split()[1]
    t0 = time.time()
    while time.time() - t0 < 60 and int(st(host)['ships_up']) == 0:
        captions(host, uh)
        captions(guest, ug)
        time.sleep(1)
    ck(int(st(host)['ships_up']) > 0, f'wave 1 came ({st(host)["ships_up"]} ships)')
    r0 = raids(host)
    print('  raids on the colony:', r0)
    hp1 = ship_hp(host)
    tough = sorted({hp1[p] for p in armada(host) if p in hp1})
    print('  wave 1 max HP:', tough, '|', sp(host, 'contracts wave'))

    # the host leaves the planet for the system view and lands again
    print(' ', sp(host, 'contracts tough_reuse 1'))
    leave_and_return(host, uh, planet, ck)
    t0 = time.time()
    seen = {}
    while time.time() - t0 < 20 or (not seen and time.time() - t0 < 28):
        captions(host, uh)
        hp = ship_hp(host)
        for p in armada(host):
            if p in hp:
                seen[p] = hp[p]
        time.sleep(1)
    sp(host, 'contracts tough_reuse 0')
    print('  resumed wave ships (max HP):', seen, '|', sp(host, 'contracts wave'))
    if seen and tough:
        ck(all(v in tough for v in seen.values()),
           f'every ship of the resumed wave has a raised max HP as in wave 1 ({tough}): {seen}')
    else:
        print('  (no ships of the resumed wave in 28 s: the HP part is not checked in this run)')
    r1 = raids(host)
    print('  raids after the return:', r1, '| host:', sp(host, 'contracts'))
    ck(len(r1) == 1, f'one raid on the colony after the return ({len(r1)}: {r1})')
    ck(st(host)['state'] == 'active', f'the contract goes on ({st(host)["state"]})')

    # BUG-0227: a resumed raid whose ships never come is not a wave won without a fight
    w0 = wave(host)
    if not w0.get('on'):  # the wave may be between raids: wait for the next one to run
        wait(lambda: wave(host).get('on'), 40)
        w0 = wave(host)
    print('  before the second leave:', sp(host, 'contracts wave').strip())
    print(' ', sp(host, 'contracts hideships 1'))
    leave_and_return(host, uh, planet, ck)
    t0 = time.time()
    while time.time() - t0 < 45:
        captions(host, uh)
        time.sleep(1)
    w1 = wave(host)
    r2 = raids(host)
    print('  45 s after the return:', sp(host, 'contracts wave').strip(), '| raids', r2)
    sp(host, 'contracts hideships 0')
    ck(w1.get('n') == w0.get('n'), f'the wave is not won without its ships (wave {w0.get("n")} -> {w1.get("n")})')
    print(f'  (the old raid was aborted by the contract: {w1.get("aborted", 0) > w0.get("aborted", 0)})')
    ck(len(r2) == 1, f'one raid on the colony again ({len(r2)}: {r2})')
    ck(st(host)['state'] == 'active', f'the contract goes on ({st(host)["state"]})')
    sp(host, 'contracts cancel')
    ck.faults(host=host, guest=guest)
    if without:
        sp(host, 'contracts wavefix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
