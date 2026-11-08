#!/usr/bin/env python3
"""Space: the sync self-check keeps the host's copy of a fast guest ship that brakes and turns in sync (BUG-0758).

The mirror of coop_space_fast_sync.py (BUG-0547, the host's ship): a guest's own ship (key p<guest>) is compared
between the host's copy of it (drawn a state late) and the guest's real ship. Only the speed slack at the read
(BUG-0378) covered it, so a guest flying key pulses (accelerate, brake, turn within the moment, the campaign's rescue
rounds) was a MISMATCH p1 79.8 / p3 30.2 in the 0.7.11 final check. The host now keeps each guest ship's path from
the states it received and takes the difference back when its copy and the guest's ship lie on it.

Both players land on the home planet over a player's network (SPORE_NET, unless the lab sets one: jitter and 300 ms
latency spikes), the host checks every second (`coop.diag sync 1`). The GUEST flies full-thrust pulses with turns.
Then the guest's ship is disabled while it flies at full speed (the campaign's rescue rounds), checks asked for at
once. Checks:
* every moment the host's copy was off the place the guest's speed allows (the host's detail line "from the guest's
  ship") that lay on the guest's path was no difference (none came in 4 runs: BUG-0758 not reproduced);
* no MISMATCH of p1 in either log;
* a real offset is still a MISMATCH: `coop.diag sync force shift` on the guest (its own position 100 m off in its
  digest) within 15 s.

  SPORE_LANE=9 python3 tools/tests/coop_space_fast_sync_guest.py [--seconds 100]
"""
import os
import re
import sys
import time

os.environ.setdefault('SPORE_NET', 'delay=40,jitter=20,spike_every=10,spike_s=6,spike_ms=300')
# a slow host PC (docs/06-testing.md SPORE_FPS): the host's copy is drawn a host frame earlier than the digest is read
# (at its next frame start); at 30 fps with 2 players the copy matched the ship in every check (0 differences in 79 and
# in 104 checks, lab 20261108-142942 / 20261108-143539)
os.environ.setdefault('SPORE_FPS', 'host:12')
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


def p1_mismatch(lines):
    return [ln for ln in lines if 'sync-check: MISMATCH' in ln and re.search(r'\bp1 [\d.]+', ln)]


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
    ck(pg.ctx() == 'planet' and ph.place()[0] == pg.place()[0], f'both on the home planet ({ph.place()}, {pg.place()})')
    print('  ' + host.call('coop.diag sync 1')['out'].splitlines()[-1], flush=True)
    time.sleep(12)  # the first check of a newcomer comes 10 s after the join; the planet settles
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        # the guest's pulses: full speed one way (W: up on the screen), then the other way at once (S): the ship turns
        # back within a moment (a rescue flight's stop, play_space's bot). The host's check is asked for in that moment
        # (a check every second hits it now and then: 0 differences in 79 checks of plain pulses, lab
        # 20261108-142942), at a different point of the turn each pulse
        a, b = ('0x57', '0x53') if n % 2 == 0 else ('0x53', '0x57')
        pg.key_hold([a], 2.0)
        pg.act(f'key {b} down')
        try:
            time.sleep((0.05, 0.15, 0.25, 0.4, 0.6)[n % 5])
            host.call('coop.diag sync now')
            time.sleep(0.8)
        finally:
            pg.cmd(f'key {b} up')
        pg.key_hold([b, '0x41' if (n // 2) % 2 else '0x44'], 0.4)
        time.sleep(0.3)
        n += 1
        if pg.ctx() != 'planet':
            break
    # the campaign's sightings came in its rescue rounds: the guest's ship disabled while it flies (a pirate of the host
    # hits it through the relayed hits of coop_space_rescue.py), the check asked for right after
    downs = 0
    for rnd in range(1):  # a second round within the 60 s cooldown kills the ship instead
        print('  ' + sp(guest, 'repairdist 0'), flush=True)
        host.call('coop.fleet spawn 2')
        time.sleep(1)
        ids = re.findall(r'ship #(\d+) \S+ type=2', host.call('coop.fleet list')['out'])
        if not ids:
            continue
        hp = field(sp(guest, 'hp'), 'hp')
        host.call(f'coop.fleet hurt {ids[-1]} 1 {hp - 5:.2f}')
        pg.act('key 0x57 down')
        try:
            time.sleep(2.0)
            down = False
            t1 = time.time()
            while time.time() - t1 < 10 and not down:
                host.call(f'coop.fleet hurt {ids[-1]} 1')
                time.sleep(0.15)
                host.call('coop.diag sync now')
                down = field(sp(guest, 'hp'), 'down') == 1
            for d in (0.05, 0.3, 0.6):
                time.sleep(d)
                host.call('coop.diag sync now')
        finally:
            pg.cmd('key 0x57 up')
        downs += down
        print(f'  round {rnd + 1}: the guest is disabled while it flies: {down} ({sp(guest, "hp")})', flush=True)
        print('  ' + sp(guest, 'repairdist 60'), flush=True)
    time.sleep(5)  # the last recheck
    hl, gl = log_since(lane.HOST, h0), log_since(lane.GUEST, g0)
    moments = [ln.split('coop: ', 1)[-1] for ln in hl if "from the guest's ship" in ln]
    for ln in moments[:12]:
        print('   ', ln, flush=True)
    print('  ' + host.call('coop.diag sync')['out'].splitlines()[-1], flush=True)
    # a real offset is still a difference
    h1 = log_size(lane.HOST)
    lab_mark('sync-force', 'on')  # the lab's sync-check gate leaves this MISMATCH out
    guest.call('coop.diag sync force shift')
    t1 = time.time()
    while time.time() - t1 < 15 and not p1_mismatch(log_since(lane.HOST, h1)):
        time.sleep(1)
    guest.call('coop.diag sync force off')
    time.sleep(4)  # the recheck of the forced difference
    lab_mark('sync-force', 'off')
    found = p1_mismatch(log_since(lane.HOST, h1))
    ck(found, f'a guest ship 100 m off (force shift) is still a MISMATCH ({found[0].split("coop: ", 1)[-1] if found else "none"})')
    ck(n >= 15, f'the guest flew {n} pulses on the planet')
    ck(downs, f'the guest\'s ship was disabled while it flew ({downs} times)')
    # the moments are not required: in 4 runs (about 550 checks with reverse turns, a disabled ship, a 12 fps host,
    # latency spikes) the host's copy of a guest's ship never was off the place its speed allows (BUG-0758 closed
    # not reproduced); any that come must lie on the guest's path and are then no difference
    print(f"  the host's copy of the guest's ship was off the place its speed allows {len(moments)} times", flush=True)

    def path_m(ln):
        m = re.search(r"the host's copy ([\d.]+) m from its path", ln)
        return float(m.group(1)) if m else 1e9
    on = [ln for ln in moments if 'on its path' in ln]
    print(f'  {len(on)} on the guest\'s path, {len(moments) - len(on)} off it '
          f'(copy distances {sorted(round(path_m(x), 1) for x in moments)[:20]})', flush=True)
    # the sync-check did not call those a difference (SPORE_FIX_OFF=sync.own_path_guests: each is one)
    wrong = [ln for ln in on if 'no difference (BUG-0758)' not in ln]
    ck(not wrong, f'the sync-check took none of them for a difference ({len(wrong)} of {len(on)} taken)')
    mm = p1_mismatch(hl) + p1_mismatch(gl)
    for ln in mm[:4]:
        print('   ', ln, flush=True)
    ck(not mm, f"no sync-check MISMATCH of the guest's ship while it flew pulses ({len(mm)})")
    host.call('coop.diag sync 30')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
