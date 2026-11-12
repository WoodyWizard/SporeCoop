#!/usr/bin/env python3
"""BUG-0829: DNA a guest earned while the host was away moves the guest's evolution bar once, like the host's.

While the host is away (main menu, an editor, a load) the guest's own gains are applied in its game (its bar moves)
and kept; at the away end they go to the host (MSG_DNA_ADD), whose total comes back. A host total sent before the host
added them (its own change, the 2 s resend) arrived after the away end: the guest's DNA went down to it
(SetEvolutionPoints, the bar stays) and up again with the next total (AddEvolutionPoints: the bar moved twice).

Run over a network with latency (the suite: SPORE_NET="delay=250"), so a host total is in flight then.
1. Two games on the creature fixture; both games' DNA and evolution bar (`coop.dna bar`).
2. The host leaves for the main menu; once the guest counts it away, the guest earns 40 DNA (the game's
   AddEvolutionPoints, `coop.dna add`).
3. The host loads its world again; around the guest's away end the host's own DNA changes every 0.1 s (`coop.dna add 1`
   on the host: a total goes out each time).
PASS: 10 s after the return the guest's bar grew by as much as the host's (within 1), the DNA is the same. FAIL
(SPORE_FIX_OFF=core.solo_await): the guest's bar is ahead by the 40.

  SPORE_NET="delay=250" SPORE_LANE=7 python3 tools/tests/coop_dna_away_bar.py
"""
import re
import time

from twoinst import Checks, lane_state, start


def read(con):
    out = con.call('coop.dna bar')['out'] or ''
    d = re.search(r'dna=(-?[\d.]+)', out)
    b = re.search(r'bar=(-?[\d.]+)', out)
    return (float(d.group(1)) if d else None, float(b.group(1)) if b else None)


def away(guest):
    m = re.search(r'host_away=(\d)', guest.call('coop.status')['out'])
    return m and m.group(1) == '1'


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    h0, g0 = read(host), read(guest)
    print(f'  before: host dna/bar {h0}, guest {g0}', flush=True)
    print(' ', host.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 60 and not away(guest):
        time.sleep(0.5)
    ck(away(guest), 'the guest counts the host away')
    print(' ', guest.call('coop.dna add 40')['out'].strip(), flush=True)
    time.sleep(1)
    gs = read(guest)
    print(f'  guest while the host is away: {gs}', flush=True)
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 150 and not host.call('coop.avatar')['ok']:
        time.sleep(0.5)
    adds = 0
    t0 = time.time()  # the host's DNA changes until well after the guest's away end
    while time.time() - t0 < 30:
        host.call('coop.dna add 1')
        adds += 1
        time.sleep(0.1)
        if not away(guest) and time.time() - t0 > 3:
            for _ in range(15):
                host.call('coop.dna add 1')
                adds += 1
                time.sleep(0.1)
            break
    ck(not away(guest), 'the host is back for the guest')
    time.sleep(10)
    h1, g1 = read(host), read(guest)
    print(f'  after: host dna/bar {h1}, guest {g1} (the host added {adds} of its own)', flush=True)
    print('  guest:', [ln for ln in guest.call('coop.status')['out'].splitlines() if 'host_away' in ln or ln.startswith('dna=')],
          flush=True)
    ok = None not in (h0[1], g0[1], h1[1], g1[1])
    ck(ok, 'both games have an evolution bar')
    if ok:
        dh, dg = h1[1] - h0[1], g1[1] - g0[1]
        ck(abs(dh - dg) <= 1.0, f"the guest's evolution bar grew as much as the host's (host +{dh:.1f}, guest +{dg:.1f})")
        ck(abs(h1[0] - g1[0]) <= 0.6, f'both games have the same DNA ({h1[0]}, {g1[0]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
