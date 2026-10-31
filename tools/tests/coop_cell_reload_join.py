#!/usr/bin/env python3
"""Cell stage: after a world reload the guest's cell is next to the host (BUG-0352).

A loaded Cell world starts the player's cell at the pool's start. Only an own-cell rebuild moved a
guest next to the host afterwards, so a reload without one (the host's evolution, a manual share:
every load after the first of a session) left the guest where the pool starts, up to 130 m from the
host and outside its tiles (player report 2026-10-31).

1. Both in the cell fixture; the host's cell moves 80 m away from the guest (`coop.pool tp`).
2. The host saves and shares its world again; the guest loads it.
3. In the guest's own game, within 20 s of the load: its cell (the avatar) is within 15 m of the
   host's cell as the guest's game draws it (the host's puppet), and the host's game has the
   guest's puppet within 15 m of its own cell.

SPORE_FIX_OFF=cell.join_after_load: the run that must FAIL (the guest stays at the start).
"""
import re
import time

from twoinst import Checks, start
import twoinst


def own_pos(con):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'], re.MULTILINE)
    return (float(m.group(1)), float(m.group(2))) if m else None


def puppet_pos(con, rid):
    lines = con.call('coop.status')['out'].splitlines()
    for i, l in enumerate(lines):
        if l.startswith(f'remote {rid} ') and i + 1 < len(lines) and 'puppet pos=(' in lines[i + 1]:
            v = lines[i + 1].split('puppet pos=(')[1].split(')')[0].split()
            return float(v[0]), float(v[1])
    return None


def dist(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5 if a and b else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: puppet_pos(host, 1) and puppet_pos(guest, 0), 30), 'both show the other player')
    time.sleep(8.0)  # the guest's own join after its first load of this test is over
    d0 = None
    for _ in range(3):
        print('  host:', host.call('coop.pool tp 80 0')['out'].strip())
        time.sleep(3.0)
        d0 = dist(own_pos(host), own_pos(guest))
        if d0 and d0 > 50:
            break
    print(f'  before the reload: host {own_pos(host)}, guest {own_pos(guest)}, {d0} m apart')
    ck(d0 is not None and d0 > 50, f'the host is far from the guest before the reload ({d0} m)')

    loads0 = guest.call('log 400')['out'].count('world: loading ')
    out = host.call('coop.save')['out']
    ck('saved' in out, f'the host saved ({out.strip()})')
    host.call('coop.share')
    loaded = wait(lambda: guest.call('log 400')['out'].count('world: loading ') > loads0, 120, 1)
    ck(loaded and twoinst.wait_guest_in(host, guest), 'the guest loaded the host\'s world again')
    t_in = time.time()
    first = own_pos(guest)
    print(f'  guest just in: own {first}, host {own_pos(host)}')

    def close():
        g, hp_g = own_pos(guest), puppet_pos(guest, 0)
        return g and hp_g and dist(g, hp_g) < 15.0
    ok = wait(close, 20)
    g, hp_g, h, gp_h = own_pos(guest), puppet_pos(guest, 0), own_pos(host), puppet_pos(host, 1)
    print(f'  {time.time() - t_in:.1f} s after the load: guest own {g}, the host as the guest draws it {hp_g} '
          f'({dist(g, hp_g)} m); host own {h}, the guest as the host draws it {gp_h} ({dist(h, gp_h)} m)')
    print('  guest:', ' '.join(l for l in guest.call('coop.pool')['out'].splitlines() if l.startswith('cell_guest_fx')))
    ck(ok, f'in the guest\'s game its cell is next to the host after the load ({dist(g, hp_g)} m)')
    ck(wait(lambda: dist(own_pos(host), puppet_pos(host, 1)) is not None and dist(own_pos(host), puppet_pos(host, 1)) < 15.0, 10),
       f'in the host\'s game the guest is next to it ({dist(own_pos(host), puppet_pos(host, 1))} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
