#!/usr/bin/env python3
"""Civilization: a guest's specialty answer is taken once, no other city of the guest's game gets it (BUG-1479).

Audit CIV-10. The answer to the specialty box (message 0x40FAFDF) reaches every city's listener with one data pointer.
The guest's hook took it at the first listener and closed the question (g_spec_open = -1); its guard against the same
answer at the next listeners sat inside `g_spec_open >= 0` and never ran, so every other city's listener passed the
answer to the game's. The game's listener acts for the player civilization's last captured city (+0x44C; on a guest
whatever the loaded save named): SetSpecialty with that city's capture type (+0x29C) and a second Resume of the box's
pause 0x4BF38A7.

1. Two games on civ_npc. Test setup on the guest (the state a loaded save can hold): its player civilization's last
   captured city (+0x44C) is city X, whose capture type (+0x29C) differs from its specialty. X is chosen among the
   cities whose listener of 0x40FAFDF is not the first (`coop.msglisteners 40FAFDF`: the middle one).
2. The guest buys the NPC city nearest the capital whose specialty is not Economic (`coop.civ buycity`): the game
   asks the buyer; the guest answers Economic like a player (a click on the box's button).
3. PASS: X keeps its specialty in the guest's game, the same as in the host's (the game's own +0x540 field, `spec=` of
   `coop.civ`); the bought city is Economic in both; the guest's game time is not left standing.
   SPORE_FIX_OFF=civ.specialty_once: X's listener applies the answer: X has the capture type set up in step 1 on the
   guest only: FAIL.
   INCONCLUSIVE when X's listener took the answer itself (`took_at=` of the guest's `coop.civ tables` specialty line):
   then no other listener had it.

  SPORE_LANE=6 python3 tools/tests/coop_civ_specialty_once.py
"""
import re
import struct
import time

from coop_civ import dist
from coop_civ_specialty_runs import box_up, near, pauses, state
from play_civ import answer_specialty
from twoinst import Checks, keyframe, start

ANSWER_MSG = '40FAFDF'
CAPTURE_TYPE, LAST_CAPTURED = 0x29C, 0x44C
CITY = re.compile(r'^city (-?\d+) (?:0x)?([0-9A-Fa-f]+) pol=\w+.*? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*? spec=(-?\d+)',
                  re.M)


def addrs(con):
    """{city address: {i, pos, spec}} of this game's cities"""
    return {int(m.group(2), 16): dict(i=int(m.group(1)), pos=tuple(float(m.group(k)) for k in (3, 4, 5)),
                                      spec=int(m.group(6)))
            for m in CITY.finditer(con.call('coop.civ')['out'])}


def listener_cities(con, cities):
    """the city addresses in the order of the game's listener list of the answer message (listener = city +0x20C)"""
    out = []
    for m in re.finditer(r'obj=([0-9A-Fa-f]{8})', con.call(f'coop.msglisteners {ANSWER_MSG}')['out']):
        a = int(m.group(1), 16) - 0x20C
        if a in cities and a not in out:
            out.append(a)
    return out


def took_at(con):
    ln = next((l for l in con.call('coop.civ tables')['out'].splitlines() if l.startswith('specialty')), '')
    m = re.search(r'took_at=(?:0x)?([0-9A-Fa-f]+)', ln)
    return (int(m.group(1), 16) if m else None), ln


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = state(host)
    gc, _ = state(guest)
    cap = [c for c in hc if c['player']]
    npc = [c for c in hc if not c['player'] and c['spec'] != 2]
    if not ck(cap and npc, f'a player city and an NPC city that is not Economic ({len(npc)})'):
        return ck.result()
    tgt = min(npc, key=lambda c: dist(c['pos'], cap[0]['pos']))

    # 1. the guest's player civ names city X as its last captured one, with a capture type other than its specialty
    ga = addrs(guest)
    order = listener_cities(guest, ga)
    cand = [a for a in order if dist(ga[a]['pos'], tgt['pos']) >= 10.0]
    print(f'  guest: {len(ga)} cities, {len(order)} city listeners of {ANSWER_MSG}', flush=True)
    if not ck(len(cand) >= 2, f'the guest has city listeners of the answer besides the bought city ({len(cand)})'):
        return ck.result()
    xa = cand[len(cand) // 2]  # (not the first listener: the one that takes the answer)
    xg = ga[xa]
    xh = near(hc, xg['pos'])
    if not ck(xh, f"city X (guest {xa:08X} at {xg['pos']}) is in the host's game too"):
        return ck.result()
    m = re.search(r'civ=(?:0x)?([0-9A-Fa-f]+)', guest.call('coop.civ sw')['out'])
    civ = int(m.group(1), 16) if m else 0
    if not ck(civ, "the guest's player civilization"):
        return ck.result()
    xtype = next(t for t in (0, 1, 2) if t != xg['spec'])
    guest.call(f'mem.write 0x{civ + LAST_CAPTURED:08X} {struct.pack("<I", xa).hex()}')
    guest.call(f'mem.write 0x{xa + CAPTURE_TYPE:08X} {struct.pack("<i", xtype).hex()}')
    print(f'  guest setup: civ {civ:08X} +0x44C = city {xa:08X} (listener {order.index(xa)} of {len(order)}),'
          f' its +0x29C = {xtype} (specialty {xg["spec"]}; host {xh["spec"]})', flush=True)

    # 2. the purchase and the guest's answer
    print(f"  guest buys city {tgt['i']} (spec {tgt['spec']}):", guest.call(f"coop.civ buycity {tgt['i']} 500")['out'].strip(),
          flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not box_up(guest):
        time.sleep(0.5)
    if not ck(box_up(guest), "the specialty question is on the guest's screen"):
        return ck.result()
    gq = answer_specialty(guest, 5, prefer='Economic')
    time.sleep(3)

    # 3. city X as before in both games, the bought city Economic in both
    hc, _ = state(host)
    gc, _ = state(guest)
    took, ln = took_at(guest)
    gx, hx = near(gc, xg['pos']), near(hc, xg['pos'])
    h, g = near(hc, tgt['pos']), near(gc, tgt['pos'])
    gp = pauses(guest)
    print(f'  answered {gq}: city X host {hx and hx["spec"]} guest {gx and gx["spec"]}; bought city host '
          f'{h and h["spec"]} guest {g and g["spec"]}; guest {gp}\n  guest {ln}', flush=True)
    keyframe('answered', "the guest's Civ screen after its Economic answer: the box closed, the game running")
    ck(gq == 'Economic', f'the guest answered Economic ({gq})')
    if took == xa:
        return ck.inconclusive("city X's own listener took the answer (no later listener had it)")
    ck(took is not None and took in ga, f'a city listener of the guest took the answer ({took and hex(took)})')
    ck(gx and hx and gx['spec'] == hx['spec'] == xh['spec'],
       f"city X keeps its specialty in the guest's game, the host's (host {hx and hx['spec']}, guest {gx and gx['spec']}, "
       f"the capture type set up {xtype})")
    ck(h and h['spec'] == 2 and g and g['spec'] == 2,
       f"the bought city is Economic in both games (host {h and h['spec']}, guest {g and g['spec']})")
    ck('paused=0' in gp, f"the guest's game time is not left standing after the answer ({gp})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
