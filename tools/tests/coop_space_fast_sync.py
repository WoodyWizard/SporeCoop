#!/usr/bin/env python3
"""Space: the sync self-check keeps the copy of a fast host ship that brakes and turns in sync (BUG-0547).

The host's ship drawn on the guest is where the host's ship was a moment before. The sync-check
allowed that moment times the ship's speed at the read (BUG-0378); a ship that slows down or turns
within the moment (a rescue flight, play_space's bot), or whose copy comes later than the average
round trip (jitter, a latency spike), is still drawn on the path it flew, but further back than that
speed allows: MISMATCH p0 46.2 in space_rescue, p0 50.0 in the 0.7.10 final check's play_space.

Both players land on the home planet over a player's network (SPORE_NET, unless the lab sets one:
jitter and 300 ms latency spikes), the host checks every second (`coop.diag sync 1`). The host flies
full-thrust pulses with turns for 100 s, then a forced offset is checked, then the guest's ship is
disabled by a pirate's relayed hits and the host flies play_space's rescue flight to it. Each moment
the copy was off the place its speed allows is logged by the host with its distance from the host's
path (sync.own_path on or off). Checks:
* the flight made such moments, and in each the guest drew the ship within 10 m of the host's path;
* the sync-check took none of them for a difference, and no MISMATCH of p0 in either log
  (SPORE_FIX_OFF=sync.own_path fails: each moment is a difference);
* a real offset is still a MISMATCH: `coop.diag sync force shift` (the host's own position 100 m off
  in its digest) within 15 s.

  SPORE_LANE=9 python3 tools/tests/coop_space_fast_sync.py [--seconds 100]
"""
import os
import re
import sys
import time

# a player's network (docs/06-testing.md#testing-degraded) unless the lab gives another: the copy of a ship is drawn
# later than the average round trip whenever a state comes late (jitter, a latency spike), still on its path
os.environ.setdefault('SPORE_NET', 'delay=40,jitter=20,spike_every=10,spike_s=6,spike_ms=300')
import fixtures  # noqa: E402
import lane  # noqa: E402
from play_space import Player  # noqa: E402
from twoinst import Checks, lab_mark, start  # noqa: E402


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'([\d.]+)', text)
    return float(m.group(1)) if m else -1


def p0_mismatch(lines):
    return [ln for ln in lines if 'sync-check: MISMATCH' in ln and re.search(r'\bp0 [\d.]+', ln)]


def main():
    secs = float(sys.argv[sys.argv.index('--seconds') + 1]) if '--seconds' in sys.argv else 100.0
    host, guest = start('space')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    home = fixtures.FIXTURES['space']['home']
    for p in (ph, pg):
        p.land_retry(home)  # BUG-0182: retried while still in the system
    ck(ph.ctx() == 'planet' and ph.place()[0] == pg.place()[0], f'both on the home planet ({ph.place()}, {pg.place()})')
    print('  ' + host.call('coop.diag sync 1')['out'].splitlines()[-1], flush=True)
    time.sleep(12)  # the first check of a newcomer comes 10 s after the join; the planet settles
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        # a pulse at full thrust with a turn, then coasting (2.5 s of W takes the fixture's ship to its top speed,
        # ~300 m/s, ~550 m: BUG-0193); the turns keep it near
        ph.key_hold(['0x57'], 1.5)
        ph.key_hold(['0x57', '0x41' if (n // 2) % 2 else '0x44'], 1.0)
        time.sleep(0.6 if n % 3 == 2 else 0.2)
        n += 1
        if ph.ctx() != 'planet':
            break
    time.sleep(5)  # the last recheck
    hl, gl = log_since(lane.HOST, h0), log_since(lane.GUEST, g0)
    # a real offset is still a difference
    h1 = log_size(lane.HOST)
    lab_mark('sync-force', 'on')  # the lab's sync-check gate leaves this MISMATCH out
    host.call('coop.diag sync force shift')
    t1 = time.time()
    while time.time() - t1 < 15 and not p0_mismatch(log_since(lane.HOST, h1)):
        time.sleep(1)
    host.call('coop.diag sync force off')
    time.sleep(4)  # the recheck of the forced difference
    lab_mark('sync-force', 'off')
    found = p0_mismatch(log_since(lane.HOST, h1))
    ck(found, f'a ship 100 m off (force shift) is still a MISMATCH ({found[0].split("coop: ", 1)[-1] if found else "none"})')
    # both sightings had the guest's ship disabled while the host flew (space_rescue's rescue flight, play_space's
    # guest at 8 HP): a pirate of the host disables the guest (the relayed hits of coop_space_rescue.py; the guest's
    # repair range 0 so the flight does not repair it), the host flies the same pulses for the 20 s it stays down
    rescued, downs = 0, 0
    for rnd in range(1):
        if rnd:
            time.sleep(62)  # the disabled state's 60 s cooldown
        print('  ' + sp(guest, 'repairdist 0'), flush=True)
        host.call('coop.fleet spawn 2')
        time.sleep(1)
        ids = re.findall(r'ship #(\d+) \S+ type=2', host.call('coop.fleet list')['out'])
        down = False
        if ids:
            hp = field(sp(guest, 'hp'), 'hp')
            host.call(f'coop.fleet hurt {ids[-1]} 1 {hp - 5:.2f}')
            t1 = time.time()
            while time.time() - t1 < 15 and not down:
                host.call(f'coop.fleet hurt {ids[-1]} 1')
                time.sleep(0.5)
                down = field(sp(guest, 'hp'), 'down') == 1
        print(f'  round {rnd + 1}: the guest is disabled: {down} ({sp(guest, "hp")})', flush=True)
        if not down:
            continue
        downs += 1
        h2, g2 = log_size(lane.HOST), log_size(lane.GUEST)
        t1 = time.time()
        while time.time() - t1 < 5:  # away from it first, then the rescue flight of play_space's bot
            ph.key_hold(['0x57'], 1.5)
            time.sleep(0.4)
            n += 1
        print('  ' + sp(guest, 'repairdist 60'), flush=True)
        r0 = field(sp(guest), 'rescues')
        ph.rescue()
        t1 = time.time()
        while time.time() - t1 < 10 and field(sp(guest), 'rescues') == r0:
            time.sleep(1)
        rescued += field(sp(guest), 'rescues') > r0
        time.sleep(4)
        hl += log_since(lane.HOST, h2)
        gl += log_since(lane.GUEST, g2)
    print(f'  disabled {downs} times, rescued {rescued}', flush=True)
    down = downs > 0
    moments = [ln.split('coop: ', 1)[-1] for ln in hl if "from the host's ship" in ln]
    for ln in moments[:12]:
        print('   ', ln, flush=True)
    print('  ' + host.call('coop.diag sync')['out'].splitlines()[-1], flush=True)
    ck(n >= 15, f'the host flew {n} pulses on the planet')
    ck(down, 'the guest\'s ship was disabled while the host flew')
    ck(moments, f'the copy of the host\'s ship was off the place its speed allows {len(moments)} times')
    # what the guest saw: the copy on the host's path (within the 10 m of a player), a moment back
    near = [float(m.group(1)) for m in (re.search(r', ([\d.]+) m from its path', ln) for ln in moments) if m]
    ck(near and max(near) <= 10.0, f'each time the guest drew the host\'s ship on the host\'s path (within {max(near or [0]):.1f} m)')
    # and the sync-check did not call that a difference (a first look a recheck would follow; SPORE_FIX_OFF=sync.own_path)
    wrong = [ln for ln in moments if 'no difference (BUG-0547)' not in ln]
    ck(not wrong, f'the sync-check took none of them for a difference ({len(wrong)} of {len(moments)} taken)')
    mm = p0_mismatch(hl) + p0_mismatch(gl)
    for ln in mm[:4]:
        print('   ', ln, flush=True)
    ck(not mm, f'no sync-check MISMATCH of the host\'s ship while it flew pulses ({len(mm)})')
    host.call('coop.diag sync 30')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
