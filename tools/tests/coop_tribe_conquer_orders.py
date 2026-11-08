#!/usr/bin/env python3
"""BUG-0531: the members that raided a village act on new orders after the village falls.

play_tribe (out/lab/20261108-020506-2 of fix/w46-k4-bodies): the guest's members #1 and #9 raided an NPC tribe's
hut; the tribe fell (its tools at health 0, the tribe's citizens destroyed) and the two then stood for minutes
(speed 0, walk mode 1) in the node 0x04623E0C with the walk goal in that village, a third (#2) in the gather node
0x0569236E: every new order of the guest (walk, gather, hunt) changed their order record but not the node.

The test: the host's game forms an NPC tribe near the village (`coop.tribe npc_tribe`), the guest's members and
one of the host's raid its hut (`coop.tribe order <id> 15 <hut> 1E4DAAE`: the DoAction of a raid click, as
host_command gives a guest's), the hut's health is cut once they hit it (`coop.tribe huthp`) so the raid ends the
tribe, and then each raider gets a walk order 15 m off: it must walk (3 m in 10 s) and leave the raid's nodes.
`coop.tribe ai` shows the node, the walk, the tree's tick count (bt=) and the animation of every raider.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_conquer_orders.py [--dist 45] [--raiders 3] [--rounds 2]
"""
import argparse
import re
import time

from coop_tribe_gather_stall import ai, pos_of
from coop_tribe_tools import dist, members
from twoinst import Checks, start

HUT = '1E4DAAE'


def huts(con):
    r = []
    for line in con.call(f'coop.tribe objs {HUT}')['out'].splitlines():
        m = re.match(r'(\S+) alive=(\d) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(2) == '1':
            r.append((m.group(1), tuple(float(m.group(i)) for i in (3, 4, 5))))
    return r


def bt_of(out):
    m = re.search(r' bt=(\d+)', out)
    return int(m.group(1)) if m else None


def on_sphere(p, ref):
    k = dist(ref, (0, 0, 0)) / max(1.0, dist(p, (0, 0, 0)))
    return tuple(x * k for x in p)


def walk_check(host, guest, ids, toward, tag, secs=10.0):
    """a walk order 15 m toward `toward` for each id; {id: (moved, last ai line, bt ticks in the wait)}"""
    start_p, goals, bt0, gstart = {}, {}, {}, {}
    for mid in ids:
        p = pos_of(host, mid)
        if not p:
            continue
        d = [t - h for h, t in zip(p, toward)]
        l = sum(x * x for x in d) ** 0.5 or 1.0
        g = on_sphere(tuple(h + x / l * 15 for h, x in zip(p, d)), p)
        start_p[mid], goals[mid], gstart[mid] = p, g, pos_of(guest, mid)
        out, _, _ = ai(host, mid)
        bt0[mid] = bt_of(out)
        print(f'    {tag} #{mid} before: {out[:420]}', flush=True)
        print(f'    {tag} #{mid}: ' + host.call('coop.tribe order %d 4 %.2f %.2f %.2f' % ((mid,) + g))['out'].strip(), flush=True)
    res = {}
    t0 = time.time()
    while time.time() - t0 < secs:
        time.sleep(2)
        for mid, p0 in start_p.items():
            out, node, st = ai(host, mid)
            here = pos_of(host, mid)
            moved = dist(here, p0) if here else -1
            b = bt_of(out)
            gp = pos_of(guest, mid)  # what the guest's screen shows (its mirror)
            gmoved = dist(gp, gstart[mid]) if gp and gstart.get(mid) else -1
            res[mid] = (min(moved, gmoved), out, (b - bt0[mid]) if b is not None and bt0[mid] is not None else None)
            print(f'    {tag} {time.time() - t0:4.1f} s #{mid}: moved {moved:.1f} m (guest {gmoved:.1f} m); {out[:420]}', flush=True)
        if all(r[0] >= 3 for r in res.values()) and len(res) == len(start_p):
            break
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dist', type=float, default=45.0, help='m from the village for the NPC tribe')
    ap.add_argument('--raiders', type=int, default=3, help="the guest's members sent (plus one of the host's)")
    ap.add_argument('--rounds', type=int, default=2, help='walk orders after the fall')
    ap.add_argument('--hp', type=float, default=40.0, help="the hut's health once the raid hits it")
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)[:a.raiders]
    hown = sorted(i for i, o in mem.items() if o == 0)[-1:]
    ck(bool(own), f"the guest has members ({own}), the host's raider {hown}")
    if not own:
        return ck.result()
    vil = pos_of(host, own[0])
    before = {h for h, _ in huts(host)}
    out = host.call(f'coop.tribe npc_tribe 3 {a.dist:.0f}')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    ck(bool(m), 'the host formed an NPC tribe')
    if not m:
        ck.faults(host=host, guest=guest)
        return ck.result()
    tp = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(3)
    new = sorted((dist(p, tp), h, p) for h, p in huts(host) if h not in before)
    ck(bool(new), f'the new tribe has a hut ({len(new)})')
    if not new:
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, hut, hp = new[0]
    print(f'  hut {hut} at {hp}, {dist(hp, vil):.0f} m from the village', flush=True)
    raiders = own + hown
    for mid in raiders:
        print(f'  raid #{mid}: ' + host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))['out'].strip(),
              flush=True)
    # the raiders at the hut (the raid nodes), then its health cut: the next blows end the tribe
    t0, cut, fell = time.time(), False, None
    while time.time() - t0 < 90:
        time.sleep(2)
        hs = host.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        near = [mid for mid in raiders if (pos_of(host, mid) and dist(pos_of(host, mid), hp) < 14)]
        print(f'  {time.time() - t0:4.0f} s: {hs}; raiders near the hut {near}', flush=True)
        if hs.startswith('no hut') or re.search(r'health 0/', hs):
            fell = time.time() - t0
            break
        if not cut and len(near) >= max(1, len(raiders) // 2):
            print('  cut: ' + host.call('coop.tribe huthp %.2f %.2f %.2f %.0f' % (hp + (a.hp,)))['out'].strip(), flush=True)
            cut = True
    ck(fell is not None, f'the raid ended the hut ({fell and round(fell)} s)')
    for mid in raiders:
        out, _, _ = ai(host, mid)
        print(f'  after the fall #{mid}: {out[:480]}', flush=True)
    for who, con in (('host', host), ('guest', guest)):
        st = con.call('coop.status')['out']
        busy = re.findall(r'busy[=:] ?\S+|cinematic \S+ \S+', st)
        print(f'  {who}: {busy}; ' + con.call('coop.uistate')['out'].strip().splitlines()[0][:200], flush=True)
    # the conquest's cinematic and caption (TRG_TotemPole_Conquer) on the host: a player closes it, or with guests in
    # the session it closes itself (tribe.conquer_caption); the test presses nothing
    # (it starts some seconds after the hut's last blow: lab 20261108-101717, 23 s after the health 0 was read)
    t1, held, seen = time.time(), None, None
    while time.time() - t1 < 60:
        st = host.call('coop.status')['out']
        held = re.search(r'cinematic \S+ playing', st)
        if held and not seen:
            seen = held.group(0)
            print(f'  host after {time.time() - t1:.0f} s: {seen}', flush=True)
        if seen and (not held or time.time() - t1 > 40):
            break
        time.sleep(1)
    ck(bool(seen), f"the host's game played the conquest cinematic ({seen})")
    print(f'  host after {time.time() - t1:.0f} s: {held.group(0) if held else "no cinematic"}; '
          + host.call('coop.uistate')['out'].strip().splitlines()[0][:120], flush=True)
    others = sorted(i for i in members(guest) if i not in raiders)[:2]
    for mid in others:
        out, _, _ = ai(host, mid)
        print(f'  not a raider #{mid}: {out[:480]}', flush=True)
    time.sleep(4)
    stuck = {}
    for r in range(a.rounds):
        res = walk_check(host, guest, raiders, vil if r % 2 == 0 else hp, f'walk{r + 1}')
        for mid, (moved, out, ticks) in res.items():
            if moved < 3:
                stuck.setdefault(mid, []).append((r + 1, round(moved, 1), ticks))
        time.sleep(3)
    print(f'  stuck raiders (round, moved m, tree ticks in the wait): {stuck}', flush=True)
    ck(not stuck, f'every raider walks on a new order after the village fell ({stuck})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
