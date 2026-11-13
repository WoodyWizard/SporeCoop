#!/usr/bin/env python3
"""BUG-0825: a creature the game built where a copy waiting for the end of a cinematic was is never destroyed with it.

`game::destroy_creature` keeps creatures of ours until the running cinematic is over (BUG-0018) and destroyed the
queued address when it ended if a creature was listed there. The game may free the queued creature meanwhile and
give its block to the next creature it makes (an NPC of the world); that NPC then went at the cinematic's end, and
counted as a puppet (no AI) while queued.

Host on the creature fixture plays CRG_ClaimNest (its caption keeps it running) and the guest picks another creation
(`coop.creations next creature`): the host builds a new puppet and queues the old one's destroy. Then the host's game
destroys the queued puppet itself (`coop.npc reusehunt <old> 150 x`: DestroyInstance, then once a frame a creature
of its species next to the avatar until the game gives one the freed block). When the cinematic is over the new
creature at that address must still live (the game's object list, its serial).
SPORE_FIX_OFF=npc.defer_identity must FAIL.

The reuse is the game's: in about 1 of 8 rounds with the misses destroyed at once the block did not come back (half of
the runs had no reuse in 6 rounds: 20261112-031338, 20261112-041619 #2). The hunt keeps its misses alive until it
ends (`k`: their blocks do not go back to the game before the queued puppet's), and a round without a reuse is
inconclusive: the test plays another one, up to --tries rounds within --minutes.

The game frees the destroyed puppet when its last reference goes, 0.3 s to over 30 s after the destroy: with 300-frame
hunts the block was still in use when the hunt ended in most rounds of the 0.7.13 final check (0 reuses in 16 rounds,
20261112-175447 / 20261112-205503 #1; lab 20261112-212318: `chunk ... freed_at=-1` after rounds 1-2, freed 600 frames after
round 3's hunt, and in round 4 the creature made in the frame the chunk was seen free got the block). So the hunt
(`f20`) watches the target's heap chunk (the dlmalloc-style header before it: size 0x16A8 | 1; free when the next
chunk's bit 0 is clear) for up to --frames frames and makes creatures only from the frame it is free (20 at most).

  SPORE_LANE=6 python3 tools/tests/coop_defer_identity.py [--tries 16] [--minutes 9]"""
import argparse
import os
import re
import time

from coop_cinematic_puppet import CLAIM, cinematic, eax, puppet, wait
from twoinst import Checks, monitor_flush, start


def hunt(con):
    out = con.call('coop.npc reusehunt')['out']
    m = re.search(r'left=(-?\d+) tries=(\d+) .*?hit=([0-9A-Fa-f]+)', out)
    a = re.search(r'hit alive=(\d)', out)
    return ({'left': int(m.group(1)), 'tries': int(m.group(2)), 'hit': int(m.group(3), 16)} if m else
            {'left': 0, 'tries': 0, 'hit': 0}) | {'alive': bool(a and a.group(1) == '1')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=int(os.environ.get('SPORE_DEFER_TRIES', '16')))
    ap.add_argument('--minutes', type=float, default=9)
    ap.add_argument('--flags', default=os.environ.get('SPORE_DEFER_FLAGS', 'xf20'))
    ap.add_argument('--frames', type=int, default=900)
    ap.add_argument('--staleref', action='store_true', default=os.environ.get('SPORE_DEFER_STALEREF') == '1', help='RE: who still points into the destroyed puppet 10 s after it')
    a = ap.parse_args()
    t_end = time.time() + a.minutes * 60
    host, guest = start()
    ck = Checks()
    reused, gone = 0, 0
    if a.staleref:
        host.call('coop.staleref on')
    for k in range(a.tries):
        if time.time() > t_end:
            print(f'  {k} rounds in {a.minutes} min without a reuse: no more rounds', flush=True)
            break
        old = wait(lambda: puppet(host), 20)
        cm = eax(host, 'Simulator::cCinematicManager::Get cdecl')
        host.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')
        if not wait(lambda: cinematic(host) == CLAIM, 10):
            print('  the cinematic did not start', flush=True)
            continue
        guest.call('coop.creations next creature')
        new = wait(lambda: (lambda p: p if p and p != old else 0)(puppet(host)), 30)
        st = re.search(r'deferred=(\d+) done_after=(\d+) pending=(\d+)', host.call('coop.npc')['out'])
        print(f'  round {k + 1}: old puppet {old and hex(old)}, new {new and hex(new)}, host {st and st.group(0)}', flush=True)
        if new and st and int(st.group(3)) > 0 and cinematic(host) == CLAIM:
            print('  host: ' + host.call(f'coop.npc reusehunt {old:X} {a.frames} {a.flags}')['out'].strip().splitlines()[0], flush=True)
            h = hunt(host)
            t1 = time.time()
            if a.staleref:
                time.sleep(10)
                print('  host staleref: ' + '\n    '.join(host.call(f'coop.staleref {old:X}')['out'].strip().splitlines()[:40]), flush=True)
            while time.time() - t1 < a.frames / 25 + 5 and not h['hit'] and h['left'] > 0 and cinematic(host) == CLAIM:
                time.sleep(0.25)
                h = hunt(host)
            playing = cinematic(host) == CLAIM
            print(f'  round {k + 1}: reuse {h} while the cinematic plays: {playing}', flush=True)
            print('  host: ' + ' | '.join(host.call('coop.npc reusehunt')['out'].strip().splitlines()[:2]), flush=True)
            if h['hit'] and playing:
                reused += 1
                t0 = time.time()
                while time.time() - t0 < 40 and cinematic(host):
                    host.call('key enter')
                    time.sleep(2)
                time.sleep(2)
                h = hunt(host)
                print(f'  round {k + 1}: after the cinematic the creature at the freed block lives: {h["alive"]}', flush=True)
                gone += not h['alive']
        t0 = time.time()
        while time.time() - t0 < 40 and cinematic(host):
            host.call('key enter')
            time.sleep(2)
        print('  host after the round: ' + ' | '.join(host.call('coop.npc reusehunt')['out'].strip().splitlines()[:2]), flush=True)
        if reused >= 1:
            break
    if not reused:
        # The game reuses the block in about 1 round of 3 (docs/06-testing.md#testing-ftribe-w48): a run without a
        # reuse proves nothing either way, so it is a skip (not a pass), still failing on faults.
        print(f'  the game gave no queued puppet\'s block to a new creature during the cinematic ({reused} times)')
        ck.faults(host=host, guest=guest)
        if not ck.ok:
            return ck.result()
        monitor_flush()
        print('RESULT: SKIP (the condition was not reached: no block reuse in the time)')
        return 0
    ck(reused >= 1, f'the game gave a queued puppet\'s block to a new creature during the cinematic ({reused} times)')
    ck(not gone, f'that creature still lives when the cinematic is over (destroyed with the queue: {gone})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
