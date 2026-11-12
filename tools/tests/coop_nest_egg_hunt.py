#!/usr/bin/env python3
"""BUG-0896: a small creature's body (a hatchling) and "+4 DNA from Player's hunt" at the place of an NPC nest egg a
guest ate or hit (the owner, watching a player's recording 2026-11-10).

The host's NPC egg hatches in the host's game only (cEgg update 0xC6B1E0 from cHerd::Update: state 3, the hatchling
made at the egg's place, docs/re/creature.md#creature-nest-eggs). The guest shows the host's eggs as mirrors that
follow the host's state from the egg snapshot (MSG_NEST, 1 Hz, nest.cpp host_tick), while the hatchling comes to the
guest as a new NPC proxy (npc.cpp spawn). A hunt notice on the host needs a lethal hit of the guest on a host NPC
(MSG_DAMAGE_REQ, npc.cpp: the kill credited to the host's avatar, dna_ledger::g_credit): an egg alone never gives
one. Suspected (inferred): for up to a second the guest sees the intact egg with the host's hatchling standing in
it; a click or bite at the egg then lands on the hatchling (half health, one bite).

1. Two games on the creature fixture; both avatars at the nearest nest of another species; its herd misses three
   members (`coop.nest target` / `respawn`): the game's AI lays three eggs (as coop_nest_eggs.py).
2. Eat: the guest eats one mirror (`coop.nest eat`: cEgg::SetState(4), what the game's eat node does). On the
   HOST: the egg is eaten (state 4) or gone, no new member of the herd within 5 m of it, no member died there,
   the DNA total unchanged, no `dna: ... cause=hunt` line in the host's log and no "'s hunt" notice.
3. Eat at the hatch moment: the host's next egg hatches in 1 s (`coop.nest hatch`), the guest eats its mirror
   0.5 s later. Both games end in the same state: eaten on both and no hatchling at the place on the host, or
   hatched on both (the guest's mirror state 3); never eaten on the guest while the host's egg hatched, and never
   eaten AND hatched on the host (SetState(4) on a hatching egg).
4. Hatch seen at once: the host's third egg hatches in 0.3 s; the guest's game is polled every 0.1 s. FAIL when the
   guest shows a spawned proxy within 1.5 m of the egg while its mirror of that egg is still intact (state 5 / 6)
   for longer than 0.4 s (the window where a click at the egg hits the hatchling).

SPORE_FIX_OFF=nest.hatch_now is the twin switch of the fix (the egg's hatch reaches the guest at the next 1 Hz
snapshot only, as in 0.7.12); step 4 is expected to fail with it.

  coop_nest_egg_hunt.py [--steps 2,3,4]"""
import argparse
import math
import re
import time

import lane
from twoinst import Checks, shot, start
import coop_nest_eggs as ne

PLACE = 5.0       # m around the egg: a new or dead member there is the egg's
INSIDE = 1.5      # m: a proxy this close to an intact egg mirror stands in it
WINDOW = 0.4      # s: the longest time the guest may show both (polling 0.1 s, one frame of lag)

MEMBER = re.compile(r'member (\S+) hp=(\S+) dead=(\d) to_lay=-?\d+ pos=\((\S+) (\S+) (\S+)\)')
PROXY = re.compile(r'#(\d+) species=\S+ spawned hp=(\S+) .*? at=\((\S+) (\S+) (\S+)\)')


def members(con, herd):
    """{ptr: (hp, dead, pos)} of the herd's members in the host's game (the game's own herd vector)"""
    return {m.group(1): (float(m.group(2)), int(m.group(3)), tuple(map(float, m.group(4, 5, 6))))
            for m in MEMBER.finditer(con.call(f'coop.nest herd {herd}')['out'])}


def near(ms, pos, r=PLACE):
    return {p: v for p, v in ms.items() if math.dist(v[2], pos) < r}


def proxies(con):
    """[(id, hp, pos)] of the guest's spawned NPC proxies"""
    return [(m.group(1), float(m.group(2)), tuple(map(float, m.group(3, 4, 5))))
            for m in PROXY.finditer(con.call('coop.npc list')['out'])]


def dna(con):
    m = re.search(r'dna=([\d.]+)', con.call('coop.dna')['out'])
    return float(m.group(1)) if m else None


def size(path):
    try:
        with open(path, 'rb') as f:
            return f.seek(0, 2)
    except OSError:
        return 0


def hunts_since(path, since):
    try:
        with open(path, 'rb') as f:
            f.seek(since)
            return re.findall(r'dna: \S+ -> \S+ cause=(\S*hunt\S*)', f.read().decode('utf-8', 'replace'))
    except OSError:
        return []


def hunt_toasts(con):
    return [t for t in re.findall(r"^\s*toast \S* \([^)]*\) '(.*?)'(?: \||$)", con.call('coop.status')['out'], re.M)
            if "'s hunt" in t]


def state_of(con, home, serial, mirror=False):
    for e in ne.eggs(con, home):
        if (e['of'] if mirror else e['serial']) == serial:
            return e['state']
    return 'gone'


def intact(es):
    return [e for e in es if e['state'] in (5, 6)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--steps', default='2,3,4')
    a = ap.parse_args()
    steps = {int(s) for s in a.steps.split(',') if s}
    host, guest = start('creature')
    ck = Checks()
    hlog = lane.game_log(lane.HOST)
    time.sleep(3)
    ns = ne.nest_herds(host)
    print('  nests of other species (dist, herd, members, home):', ns[:4], flush=True)
    if not ck(ns, 'a nest of another species with members on the host'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, herd, nmem, home = ns[0]
    hx = '%.2f %.2f %.2f' % home
    print('  host:', host.call(f'coop.nest look {hx} 7')['out'].strip(), flush=True)
    print('  guest:', guest.call(f'coop.nest look {hx} 9')['out'].strip(), flush=True)
    time.sleep(3)
    host.call(f'coop.nest respawn {herd} 2000')
    host.call(f'coop.nest target {herd} {nmem + 3}')
    t0 = time.time()
    ho = []
    while time.time() - t0 < 120:
        ho = intact(ne.eggs(host, home))
        lay = re.search(r'eggs_to_lay=(-?\d+)', host.call(f'coop.nest herd {herd}')['out'])
        if len(ho) >= 3 and lay and int(lay.group(1)) <= 0:
            break
        time.sleep(2)
    time.sleep(6)  # the last egg settles, the next snapshot
    # the herd lays no more while the test eats and hatches (a new egg is not the hatchling we look for)
    host.call(f'coop.nest respawn {herd} 99999999')
    ho, go = intact(ne.eggs(host, home)), ne.eggs(guest, home)
    print('  host eggs:', ne.short(ho), '/ guest:', ne.short(go), flush=True)
    if not ck(len(ho) >= len(steps), f'the host\'s herd laid {len(steps)} eggs ({len(ho)})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    mirrors = {g['of']: g for g in go if g['of']}
    eggs = [e for e in ho if e['serial'] in mirrors]
    ck(len(eggs) >= len(steps), f'the guest mirrors the host\'s eggs ({len(eggs)} of {len(ho)})')

    def take():
        return eggs.pop(0) if eggs else None

    # ---- 2. the guest eats an egg: nothing but an eaten egg on the host ----
    if 2 in steps and (e := take()):
        m0, d0, l0 = members(host, herd), dna(host), size(hlog)
        print('  guest:', guest.call(f'coop.nest eat {mirrors[e["serial"]]["p"]}')['out'].strip(), f'(host egg {e["serial"]})', flush=True)
        st = None
        for _ in range(10):
            time.sleep(1)
            st = state_of(host, home, e['serial'])
            if st in (4, 'gone'):
                break
        time.sleep(4)  # a hatchling made by the eat would be out by now, a kill paid
        m1 = members(host, herd)
        new = near({p: v for p, v in m1.items() if p not in m0}, e['pos'])
        died = near({p: v for p, v in m1.items() if v[1] and not m0.get(p, (0, 0))[1]}, e['pos'])
        print(f'  host egg state {st}; new members near {list(new)}; died near {list(died)}; dna {d0} -> {dna(host)}', flush=True)
        ck(st in (4, 'gone'), f'2: the egg the guest ate is eaten on the host (state {st})')
        ck(not new, f'2: no creature came out of the eaten egg on the host ({new})')
        ck(not died, f'2: no body at the eaten egg on the host ({died})')
        ck(dna(host) == d0, f'2: the eat paid no DNA on the host ({d0} -> {dna(host)})')
        h = hunts_since(hlog, l0)
        ck(not h and not hunt_toasts(host), f'2: no hunt credited on the host ({h}, {hunt_toasts(host)})')
        shot(host, 'egg_hunt_eat_host')

    # ---- 3. the guest eats an egg at its hatch moment: one outcome on both games ----
    if 3 in steps and (e := take()):
        m0 = members(host, herd)
        print('  host:', host.call(f'coop.nest hatch {e["p"]} 1000')['out'].strip(), flush=True)
        time.sleep(0.5)
        print('  guest:', guest.call(f'coop.nest eat {mirrors[e["serial"]]["p"]}')['out'].strip(), flush=True)
        time.sleep(6)
        hs = state_of(host, home, e['serial'])
        gs = state_of(guest, home, e['serial'], mirror=True)
        new = near({p: v for p, v in members(host, herd).items() if p not in m0}, e['pos'])
        print(f'  host egg {hs}, guest mirror {gs}, new members at the egg on the host {list(new)}', flush=True)
        hatched = hs == 3 or bool(new)
        ck(not (hs == 4 and new), f'3: the host\'s egg is not both eaten and hatched (state {hs}, new {list(new)})')
        ck(not (hatched and gs == 4), f'3: the guest does not show eaten an egg that hatched on the host (host {hs}, guest {gs})')
        shot(host, 'egg_hunt_hatch_host')
        shot(guest, 'egg_hunt_hatch_guest')

    # ---- 4. the hatch reaches the guest with its hatchling ----
    if 4 in steps and (e := take()):
        print('  host:', host.call(f'coop.nest hatch {e["p"]} 300')['out'].strip(), flush=True)
        both, first_proxy, hatched_at, t0 = 0.0, None, None, time.time()
        last = t0
        while time.time() - t0 < 8:
            now = time.time()
            ps = [p for p in proxies(guest) if math.dist(p[2], e['pos']) < INSIDE]
            gs = state_of(guest, home, e['serial'], mirror=True)
            if ps and first_proxy is None:
                first_proxy = now - t0
            if gs in (3, 4, 'gone') and hatched_at is None:
                hatched_at = now - t0
            if ps and gs in (5, 6):
                both += now - last
            last = now
            if hatched_at is not None and first_proxy is not None and now - t0 > hatched_at + 1:
                break
            time.sleep(0.1)
        print(f'  guest: a proxy in the egg after {first_proxy}, the mirror hatched after {hatched_at}; '
              f'both shown {both:.2f} s', flush=True)
        shot(guest, 'egg_hunt_window_guest')
        ck(hatched_at is not None, '4: the guest\'s mirror shows the hatch')
        ck(both <= WINDOW, f'4: the guest never shows the hatchling inside an intact egg for more than {WINDOW} s ({both:.2f} s)')

    print('  guest:', guest.call('coop.nest')['out'].strip(), flush=True)
    print('  host:', host.call('coop.nest')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
