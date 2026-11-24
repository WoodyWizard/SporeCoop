#!/usr/bin/env python3
"""WP-A (BUG-1474): host-authoritative Galactic Adventures creatures/enemies (adventure.host_actors,
adventure.hit_forward, adventure.guest_extras, adventure.body_side) in tutorial Act 1 (Skex & Thorns).

1. Host and guest join at the galaxy menu, enter the tutorial adventure (forced place id 0ADF0E01), start play (state 3),
   and advance to Act 1 (Skex and Thorns).
2. Partner body side (adventure.body_side): host's partner captain puppet receives the host captain's political ID
   (+0x30) so host enemy AI attacks partner captains too. SPORE_FIX_OFF=adventure.body_side -> FAIL.
3. Host-authoritative actor pairing & kinetic drive (adventure.host_actors): guest keeps native scenario objects, pairs
   living creatures by (marker, class) within 12 m, suppresses NPCTickAI on paired creatures only, and tracks host
   position within 4 m. SPORE_FIX_OFF=adventure.host_actors -> FAIL.
4. Guest hit forwarding (adventure.hit_forward): guest hits a paired enemy (`coop.adventure hit <host_key> 5`); host's
   creature HP drops by ~5 on the host's own game. SPORE_FIX_OFF=adventure.hit_forward -> FAIL.
5. Host Die mirroring (adventure.host_actors): host kills the enemy (`coop.adventure kill <host_key>`); guest's paired
   creature transitions to dead=1.
6. Unpaired leftover 5 s removal (adventure.guest_extras): guest spawns an extra unpaired scenario creature 20 m away
   (`coop.adventure spawn_extra`); it is destroyed automatically after 5 s while the host actor stream runs.
   SPORE_FIX_OFF=adventure.guest_extras -> FAIL.

  SPORE_LANE=N python3 tools/tests/coop_adventure_enemies.py
"""
import math
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
from coop_adventure_shared import adv_bodies, adv_state_act, boot_into_tutorial, mode_name, safe_call  # noqa: E402
from harness import wait_until  # noqa: E402
from twoinst import Checks, keyframe  # noqa: E402

ACTOR_RE = re.compile(
    r'actor serial=(\d+) marker=([0-9A-F]+) class=(-?\d+) pos=\(([^ ]+) ([^ ]+) ([^)]+)\) '
    r'hp=([0-9.]+)/([0-9.]+) dead=(\d+) host_key=(\d+) ai_skip=(\d+) unp=([-\d.]+)'
)


def parse_actors(c):
    out = []
    for m in ACTOR_RE.finditer(safe_call(c, 'coop.adventure actors')):
        out.append({
            'serial': int(m.group(1)),
            'marker': m.group(2),
            'cls': int(m.group(3)),
            'pos': (float(m.group(4)), float(m.group(5)), float(m.group(6))),
            'hp': float(m.group(7)),
            'max_hp': float(m.group(8)),
            'dead': int(m.group(9)) != 0,
            'host_key': int(m.group(10)),
            'ai_skip': int(m.group(11)) != 0,
            'unp': float(m.group(12)),
        })
    return out


def dist3(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    ck = Checks()
    h, g = boot_into_tutorial(ck)
    if mode_name(h) != 'Scenario' or mode_name(g) != 'Scenario':
        lane.sh('kill')
        return ck.result()

    # Advance to Act 1 (Skex & Thorns)
    print('  host act 1: ' + safe_call(h, 'coop.adventure act 1').replace('\n', ' | '), flush=True)
    wait_until(lambda: (adv_state_act(h) or (0, -1))[1] == 1, 20, step=1.0, what='host act 1')
    if (adv_state_act(g) or (0, -1))[1] != 1:
        wait_until(lambda: (adv_state_act(g) or (0, -1))[1] == 1, 15, step=1.0, what='guest act 1')
    if (adv_state_act(g) or (0, -1))[1] != 1:
        safe_call(g, 'coop.adventure act 1')
        wait_until(lambda: (adv_state_act(g) or (0, -1))[1] == 1, 20, step=1.0, what='guest manual act 1')
    ck((adv_state_act(h) or (0, -1))[1] == 1 and (adv_state_act(g) or (0, -1))[1] == 1,
       f'1. both games in act 1 (host={adv_state_act(h)}, guest={adv_state_act(g)})')

    # 2. Partner body political ID (+0x30) on host
    wait_until(lambda: adv_bodies(h)[1] is not None, 20, step=1.0, what='host partner puppet')
    time.sleep(2)
    hav, hpup = adv_bodies(h)
    print(f'  host bodies: av={hav} pup={hpup}', flush=True)
    ck(hav is not None and hpup is not None and hav[1] != 'FFFFFFFF' and hpup[2] == hav[1],
       f'2. host partner captain puppet has captain political ID (av={hav and hav[1]}, pup={hpup and hpup[2]})')

    # 3. Wait for Act 1 scenario creatures to pair on guest
    def paired_live():
        ga = [a for a in parse_actors(g) if not a['dead'] and a['host_key'] != 0 and a['ai_skip']]
        return ga if ga else None

    ga_paired = wait_until(paired_live, 30, step=1.0, what='paired adventure actors on guest')
    ha_all = parse_actors(h)
    ga_all = parse_actors(g)
    print(f'  host actors: {len(ha_all)}, guest actors: {len(ga_all)}, guest paired+ai_skip: {len(ga_paired or [])}',
          flush=True)
    ck(bool(ga_paired), f'3. guest paired living scenario creatures with NPCTickAI skipped ({len(ga_paired or [])})')
    keyframe('enemies_act1', 'WP-A Act 1 paired adventure creatures on host and guest', {'host': h, 'guest': g}, settle=3)

    if ga_paired:
        target = ga_paired[0]
        hk = target['host_key']
        h_match = next((a for a in ha_all if a['serial'] == hk and not a['dead']), None)
        ck(h_match is not None, f'3b. host has living actor serial={hk}')
        if h_match:
            d = dist3(target['pos'], h_match['pos'])
            print(f'  paired target host_key={hk}: guest_pos={target["pos"]} host_pos={h_match["pos"]} dist={d:.2f} m',
                  flush=True)
            ck(d < 4.0, f'3c. paired guest creature tracks host position within 4 m ({d:.2f} m)')

            # 4. Guest hit forwarding -> host HP drops
            hp_before = h_match['hp']
            print(f'  guest hit {hk} 5: ' + safe_call(g, f'coop.adventure hit {hk} 5').replace('\n', ' | '), flush=True)
            def host_hp_dropped():
                cur = next((a for a in parse_actors(h) if a['serial'] == hk), None)
                return cur if (cur and cur['hp'] <= hp_before - 3.0) else None
            after_hit = wait_until(host_hp_dropped, 10, step=0.5, what='host HP drop from guest hit')
            print(f'  host target hp before={hp_before} after={after_hit and after_hit["hp"]}', flush=True)
            ck(after_hit is not None,
               f'4. guest hit forwarded and lowered host creature HP ({hp_before:.1f} -> {after_hit and after_hit["hp"]})')

            # 5. Host Die mirroring -> guest paired creature dead=1
            print(f'  host kill {hk}: ' + safe_call(h, f'coop.adventure kill {hk}').replace('\n', ' | '), flush=True)
            def guest_dead():
                cur = next((a for a in parse_actors(g) if a['serial'] == target['serial']), None)
                return cur is None or cur['dead']
            died = wait_until(guest_dead, 12, step=0.5, what='guest mirrors host Die')
            ck(bool(died), f'5. guest mirrored host Die for paired creature serial={target["serial"]} (host_key={hk})')

    # 6. Unpaired guest leftover removed after 5 s
    spawn_out = safe_call(g, 'coop.adventure spawn_extra')
    print('  guest spawn_extra: ' + spawn_out.replace('\n', ' | '), flush=True)
    m_ex = re.search(r'spawned extra serial=(\d+)', spawn_out)
    ck(m_ex is not None, f'6a. spawned unpaired test creature on guest ({spawn_out})')
    if m_ex:
        ex_ser = int(m_ex.group(1))
        removed = wait_until(
            lambda: all(a['serial'] != ex_ser for a in parse_actors(g)),
            15, step=1.0, what='unpaired guest creature 5 s removal',
        )
        ck(bool(removed), f'6b. unpaired guest leftover creature serial={ex_ser} removed after 5 s')

    alive = {k: c for k, c in (('host', h), ('guest', g)) if mode_name(c) != 'gone'}
    ck(len(alive) == 2, f'7. both games still running ({", ".join(alive) or "none"})')
    ck.faults(**alive)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
