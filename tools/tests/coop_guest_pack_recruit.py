#!/usr/bin/env python3
"""BUG-1256: guest pack recruit in the Creature stage.

Creature fixture, two players:
  1. Find a species with at least 2 live NPCs on the host and make it allied (`coop.social record <sp> 6 3`).
  2. Guest steps next to allied NPC #1 (`coop.social goto`), engages and answers prompts until recruited.
  3. Check:
     - Host `coop.social pack` reports `pack_member id=<id1> owner=1` while host's own `local_count=0` (guest
       recruits do not consume the host's own pack limit).
     - Guest `coop.social pack` reports `my_pack=1`, `local_count=1`, `pack_member id=<id1> owner=1`.
  4. Brain limit: if guest's `local_max == 1`, attempting to recruit allied NPC #2 is refused and `my_pack` stays 1.
  5. Guest dismisses NPC #1 (`coop.social dismiss <id1>`): both guest and host drop it cleanly (`my_pack=0`,
     host `total=0`, `released >= 1`).
  6. Guest recruits NPC #1 again, then leaves (`coop.leave`): host releases NPC #1 automatically (`total=0`,
     `released >= 2`); guest rejoins cleanly.
  7. SPORE_FIX_OFF=social.guest_recruit: recruiting allied NPC #1 fails (`my_pack=0`, host `recruited=0`) -> FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_guest_pack_recruit.py
"""
import collections
import re
import time

import lane
import settings_lib as sl
from twoinst import Checks, start, wait_guest_in


def pack_info(con):
    out = con.call('coop.social pack')['out']
    m = re.search(r'guest_pack: fix=(\d+) local_count=(\d+) local_max=(\d+) my_pack=(\d+) total=(\d+) '
                  r'recruited=(\d+) released=(\d+) dismissed=(\d+)', out)
    members = [(int(x), int(y)) for x, y in re.findall(r'pack_member id=(\d+) owner=(\d+)', out)]
    if not m:
        return dict(ok=False, members=members, raw=out)
    return dict(
        ok=True,
        fix=int(m.group(1)),
        local_count=int(m.group(2)),
        local_max=int(m.group(3)),
        my_pack=int(m.group(4)),
        total=int(m.group(5)),
        recruited=int(m.group(6)),
        released=int(m.group(7)),
        dismissed=int(m.group(8)),
        members=members,
        raw=out,
    )


def find_species_pair(host, guest):
    """Return (species_hex, [id1, id2]) with at least 2 live NPCs mirrored on the guest."""
    h_out = host.call('coop.social rel')['out']
    g_out = guest.call('coop.social rel')['out']
    g_ids = {int(m.group(1)) for m in re.finditer(r'^rel (\d+) ', g_out, re.M) if 'DEAD' not in m.group(0)}
    by_sp = collections.defaultdict(list)
    for line in h_out.splitlines():
        if 'DEAD' in line:
            continue
        m = re.match(r'^rel (\d+) species=([0-9A-F]+) ', line)
        if m and int(m.group(1)) in g_ids:
            by_sp[m.group(2)].append(int(m.group(1)))
    for sp, ids in sorted(by_sp.items(), key=lambda kv: -len(kv[1])):
        if len(ids) >= 2:
            return sp, ids[:2]
    return None, []


def social_recruit_npc(host, guest, npc_id, timeout=22.0):
    """Guest steps next to npc_id, engages it, and answers its prompts until recruited or timeout."""
    guest.call(f'coop.social goto {npc_id}')
    time.sleep(0.5)
    guest.call(f'coop.social act {npc_id} 0')
    t0 = time.time()
    last_prompt_age = 999.0
    while time.time() - t0 < timeout:
        hp = pack_info(host)
        if any(mid == npc_id and ow == 1 for mid, ow in hp['members']):
            return True
        st = guest.call('coop.social')['out']
        mp = re.search(r'prompt_npc=(-?\d+).*?prompt_age=([\d.]+).*?prompt_type=([0-9A-Fa-f]+)', st)
        if mp and int(mp.group(1)) == npc_id:
            age = float(mp.group(2))
            ptype = mp.group(3)
            if age < 8.0 and age < last_prompt_age:
                guest.call(f'coop.social goto {npc_id}')
                guest.call(f'coop.social answer {npc_id} {ptype}')
            last_prompt_age = age
        else:
            # Fallback if no prompt list (or re-try act)
            guest.call(f'coop.social goto {npc_id}')
            guest.call(f'coop.social act {npc_id} 0')
        time.sleep(1.0)
    return False


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(3)
        sp, ids = sl.wait(lambda: (lambda r: r if r[0] else None)(find_species_pair(host, guest)), 15) or (None, [])
        ck(bool(sp) and len(ids) >= 2, f'found allied candidate species with >=2 mirrored NPCs ({sp}, {ids})')
        if not sp:
            return ck.result()

        # Make the species allied (relation=6, impressed=3) on the host and let table sync to guest
        print(f'  allying species {sp} (NPCs {ids}):', host.call(f'coop.social record {sp} 6 3')['out'].strip(), flush=True)
        # Creature fixture starts at brain level 0 (pack cap 0); advance brain to level 1+ so guest can recruit >= 1
        print('  brain upgrade dna:', host.call('coop.dna add 250')['out'].strip(), flush=True)
        sl.wait(lambda: pack_info(guest).get('local_max', 0) >= 1, 20)
        time.sleep(1.5)

        gp0 = pack_info(guest)
        hp0 = pack_info(host)
        print(f'  before: host={hp0["raw"].strip()} | guest={gp0["raw"].strip()}', flush=True)
        ck(gp0['ok'] and gp0['local_max'] >= 1, f'guest has pack capacity >= 1 (local_max={gp0.get("local_max")})')

        # 1. Guest recruits allied NPC #1
        ok1 = social_recruit_npc(host, guest, ids[0])
        gp1 = sl.wait(lambda: (lambda p: p if p['my_pack'] >= 1 else None)(pack_info(guest)), 8) or pack_info(guest)
        hp1 = pack_info(host)
        print(f'  after recruit #{ids[0]}: ok={ok1} host={hp1["raw"].strip()} | guest={gp1["raw"].strip()}', flush=True)
        ck(ok1 and any(mid == ids[0] and ow == 1 for mid, ow in hp1['members']),
           f'host recorded guest as pack owner of NPC #{ids[0]} ({hp1["members"]})')
        ck(hp1['local_count'] == 0, f'guest pack member does not consume host pack slots (host local_count={hp1["local_count"]})')
        ck(gp1['my_pack'] == 1 and gp1['local_count'] == 1 and any(mid == ids[0] and ow == 1 for mid, ow in gp1['members']),
           f'guest synced its own pack member and posse count (my_pack={gp1["my_pack"]}, local_count={gp1["local_count"]})')

        # 2. Brain limit enforcement (at brain 1, local_max=1, recruiting a second NPC must be refused)
        if gp1['local_max'] == 1:
            ok2 = social_recruit_npc(host, guest, ids[1], timeout=8.0)
            hp2 = pack_info(host)
            gp2 = pack_info(guest)
            print(f'  brain limit check #{ids[1]}: ok2={ok2} host={hp2["members"]} guest my_pack={gp2["my_pack"]}', flush=True)
            ck(not ok2 and gp2['my_pack'] == 1 and len(hp2['members']) == 1,
               f'brain limit enforced: second recruit refused at max=1 (ok2={ok2}, members={hp2["members"]})')

        # 3. Guest dismisses its pack member
        print('  guest dismiss:', guest.call(f'coop.social dismiss {ids[0]}')['out'].strip(), flush=True)
        hp3 = sl.wait(lambda: (lambda p: p if p['total'] == 0 else None)(pack_info(host)), 8) or pack_info(host)
        gp3 = sl.wait(lambda: (lambda p: p if p['my_pack'] == 0 else None)(pack_info(guest)), 8) or pack_info(guest)
        print(f'  after dismiss: host={hp3["raw"].strip()} | guest={gp3["raw"].strip()}', flush=True)
        ck(hp3['total'] == 0 and hp3['released'] >= 1 and gp3['my_pack'] == 0 and gp3['local_count'] == 0,
           f'dismiss cleared pack member on both host and guest (host total={hp3["total"]} rel={hp3["released"]}, '
           f'guest my_pack={gp3["my_pack"]} count={gp3["local_count"]})')

        # 4. Re-recruit and verify cleanup when guest leaves
        ok3 = social_recruit_npc(host, guest, ids[0])
        ck(ok3, f'guest re-recruited NPC #{ids[0]} before leave test')
        if ok3:
            guest.call('coop.leave')
            hp4 = sl.wait(lambda: (lambda p: p if p['total'] == 0 else None)(pack_info(host)), 10) or pack_info(host)
            print(f'  after guest leave: host={hp4["raw"].strip()}', flush=True)
            ck(hp4['total'] == 0 and hp4['released'] >= hp3['released'] + 1,
               f'host released guest pack member when guest left (total={hp4["total"]}, released={hp4["released"]})')
            guest.call(lane.JOIN)
            wait_guest_in(host, guest, 120)
    finally:
        pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
