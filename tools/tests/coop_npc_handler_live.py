#!/usr/bin/env python3
"""BUG-0805: a host message about an NPC proxy that the guest's own game has just removed never hands the removed
creature to the game (Creature, guest).

The message handlers took the proxy's stored creature: in the frame after the guest's game destroyed it (and in the
frames before the game freed the block and gave it to another creature) the host's "you were hit by NPC n"
(MSG_DAMAGE) went to TakeDamage with the destroyed creature as the attacker. TakeDamage posts an "attacked"
stimulus to the victim's herd naming the attacker (+0x10, no reference, BUG-0926): the herd-help node later calls
that creature, freed by then (a pure virtual call, R6025, the game ends). The social prompt played an animation on
it (CreaturePlayAnim).

Two games on the creature fixture. On the guest, for --n proxies near the guest's creature:
`coop.npc handlerkill <id> damage` (the game destroys the proxy's creature, DestroyInstance without our
bookkeeping, and in the same call the host's MSG_DAMAGE naming that NPC as the attacker is handled), then the
same with `prompt`. 1 s later `coop.staleref <creature>` lists every pointer into the destroyed creature's block
by where it sits. PASS: no creature's AI block holds a stimulus naming it, and no game faulted.
SPORE_FIX_OFF=npc.handler_live must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_npc_handler_live.py [--n 2]"""
import argparse
import re
import time

from twoinst import Checks, start


def stim_holders(out):
    """AI-block holders at a stimulus slot's attacker (+0x10) or sender (+0x2C): `ai noun=.. +0xOFF xN`"""
    n = 0
    for m in re.finditer(r'^\s+ai noun=\S+ \+0x([0-9A-F]+) x(\d+)', out, re.M):
        off = int(m.group(1), 16) - 8
        if off >= 0 and off % 0x38 in (0x10, 0x2C):
            n += int(m.group(2))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=2)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    def live_ids():
        """spawned proxies, nearest first, not of the host's family or pack (a hit by those is a party hit)"""
        out = []
        for line in guest.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) species=\S+ spawned hp=([\d.]+) dist=([\d.]+)', line)
            if m and float(m.group(2)) > 0 and ' DEAD' not in line and ' family' not in line and ' pack' not in line:
                out.append((float(m.group(3)), int(m.group(1))))
        return [i for _, i in sorted(out)]

    t0 = time.time()
    while time.time() - t0 < 30 and len(live_ids()) < 3:
        time.sleep(1)
    near = live_ids()
    ck(len(near) >= a.n, f'the guest has proxies of the host\'s animals ({len(near)})')
    print('  guest: ' + guest.call('coop.staleref on')['out'].strip(), flush=True)
    held, done, tried = 0, 0, set()
    for _ in range(a.n * 3):
        if done >= a.n:
            break
        cand = [i for i in live_ids() if i not in tried]
        if not cand:
            time.sleep(2)
            continue
        i = cand[0]
        tried.add(i)
        out = guest.call(f'coop.npc handlerkill {i} damage')['out'].strip()
        print(f'  proxy #{i}: {out}', flush=True)
        m = re.search(r"creature ([0-9A-Fa-f]+) destroyed", out)
        if not m:
            continue
        done += 1
        time.sleep(1.0)
        ref = guest.call(f'coop.staleref {m.group(1)}')['out']
        h = stim_holders(ref)
        held += h
        print(f'  proxy #{i}: {ref.splitlines()[0] if ref else "?"}; stimuli naming it: {h}', flush=True)
        time.sleep(3)  # (the scan stops the game for seconds: its proxies settle again)
    ck(done == a.n, f'the guest\'s game destroyed {done} proxies with a host hit handled in the same call')
    ck(held == 0, f'no NPC of the guest\'s game holds a stimulus naming a removed creature ({held})')
    for i in [i for i in live_ids() if i not in tried][:1]:
        print(f'  proxy #{i}: ' + guest.call(f'coop.npc handlerkill {i} prompt')['out'].strip(), flush=True)
    time.sleep(6)  # (a freed attacker in a stimulus: the herd-help node calls it within seconds)
    print('  guest: ' + guest.call('coop.staleref off')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
