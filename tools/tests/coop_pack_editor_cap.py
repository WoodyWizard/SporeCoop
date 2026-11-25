#!/usr/bin/env python3
"""BUG-1560: guest pack recruit followed by Creature Editor / save must not leak companion to host or break 3-member cap.

Creature fixture, two players:
  1. Advance brain level so guest has pack capacity >= 1, find a live mirrored NPC of the host's own species
     (at the home nest), and have the guest recruit it via the real social flow.
  2. Host enters Creature Editor (`coop.vote evolve`), drags a part onto its creature so the host's species
     evolves (`S_old -> S_new`), and clicks Done (guest clicks Cancel).
  3. Verify right after Creature Editor:
     - Host `CreatureUI_AddPosseCard` blocked the guest pack member (`ui_leaked == 0`, `ui_blocked >= 1`).
     - Ownership rebound to the NPC's new net id (`total == 1`, `owner == 1`, `rebound >= 1` when species changed).
     - Host's own `local_count == 0`, `list == 1`, `temp == 1`, `true_temp == 1`.
  4. Host saves and both players reload (`session_restart(host, guest, save=True)`):
     - Guest pack member was stripped from the save (`list == 0`, `temp == 0`, `true_temp == 0`, `local_count == 0`).
  5. Host advances to brain 3 (`local_max == 3`) and attempts to recruit 4 distinct live NPCs (`coop.npc recruit`):
     - First 3 succeed (`list == 3`, `local_count == 3`, `temp == 0`, `true_temp == 0`).
     - 4th recruit is strictly refused (`list == 3`, `local_count == 3`).
  6. SPORE_FIX_OFF=npc.pack_editor_cap (`pack_editor_cap_off`):
     - After Creature Editor, `ui_leaked >= 1` and `total == 0` (orphaned in host's pack).
     - After save/reload, `list >= 1`, `temp >= 1`, `true_temp == 0` (+0xE68 lost while +0x1C serialized).
     - Host recruits past the 3-member cap (`list == 4`) -> FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_pack_editor_cap.py
"""
import re
import time

from harness import field_dotall as field, mode, waiter
import settings_lib as sl
from twoinst import Checks, session_restart, start

DONE = (1416, 1062)
wait = waiter(step=1.0)


def in_editor(con):
    return 'Editor' in mode(con)


def editor_ui(con):
    return con.call('coop.ui tree 4')['out'].count('Window') > 15


def is_busy(con):
    st = con.call('coop.status')['out']
    if re.search(r'cinematic [0-9A-F]{8} playing', st):
        return True
    m = re.search(r'busy:\s*(\S+)', st)
    if m and m.group(1) not in ('none', '-'):
        return True
    ui = con.call('coop.uistate')['out']
    il = re.search(r'input_lock=(-?\d+)', ui)
    return bool(il) and int(il.group(1)) > 0


def clear_cinematics(host, guest, max_secs=25.0):
    t0 = time.time()
    while time.time() - t0 < max_secs:
        any_b = False
        for c in (host, guest):
            if is_busy(c):
                any_b = True
                c.call('key enter')
        if not any_b:
            return
        time.sleep(1.0)


def pack_info(con):
    out = con.call('coop.social pack')['out']
    m = re.search(r'guest_pack: fix=(\d+) local_count=(-?\d+) local_max=(\d+) my_pack=(\d+) total=(\d+) '
                  r'recruited=(\d+) released=(\d+) dismissed=(\d+)', out)
    d = re.search(r'posse_diag: cap_fix=(\d+) list=(-?\d+) temp=(-?\d+) true_temp=(-?\d+) '
                  r'ui_blocked=(\d+) ui_leaked=(\d+) save_strips=(\d+) rebound=(\d+) '
                  r'orphans=(\d+) temp_fixed=(\d+) rebind_pending=(\d+)', out)
    members = [(int(x), int(y)) for x, y in re.findall(r'pack_member id=(\d+) owner=(\d+)', out)]
    if not m or not d:
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
        cap_fix=int(d.group(1)),
        list=int(d.group(2)),
        temp=int(d.group(3)),
        true_temp=int(d.group(4)),
        ui_blocked=int(d.group(5)),
        ui_leaked=int(d.group(6)),
        save_strips=int(d.group(7)),
        rebound=int(d.group(8)),
        orphans=int(d.group(9)),
        temp_fixed=int(d.group(10)),
        rebind_pending=int(d.group(11)),
        members=members,
        raw=out,
    )


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def find_host_species_npc(host, guest, host_sp):
    """Return a live mirrored NPC id of host_sp (or None if none mirrored yet)."""
    h_out = host.call('coop.social rel')['out']
    g_out = guest.call('coop.social rel')['out']
    g_ids = {int(m.group(1)) for m in re.finditer(r'^rel (\d+) ', g_out, re.M) if 'DEAD' not in m.group(0)}
    for line in h_out.splitlines():
        if 'DEAD' in line:
            continue
        m = re.match(r'^rel (\d+) species=([0-9A-F]+) ', line)
        if m and int(m.group(1)) in g_ids and m.group(2).upper() == host_sp.upper():
            return int(m.group(1))
    return None


def live_host_npc_ids(host):
    out = host.call('coop.social rel')['out']
    ids = []
    for line in out.splitlines():
        if 'DEAD' in line:
            continue
        m = re.match(r'^rel (\d+) species=([0-9A-F]+) ', line)
        if m:
            ids.append(int(m.group(1)))
    return ids


def social_recruit_npc(host, guest, npc_id, timeout=25.0):
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
            guest.call(f'coop.social goto {npc_id}')
            guest.call(f'coop.social act {npc_id} 0')
        time.sleep(1.0)
    return False


def add_part(con, y):
    con.call(f'move 86 {y}')
    time.sleep(0.3)
    con.call(f'button 86 {y} down')
    time.sleep(0.3)
    for x, yy in ((300, 250), (600, 400), (850, 500), (930, 560)):
        con.call(f'move {x} {yy}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 930 560 up')
    time.sleep(1)


def host_creature_editor(host, guest, ck):
    dna = lambda c: float(field(c.call('coop.dna')['out'], r'dna=(-?[\d.]+)') or 0)
    clear_cinematics(host, guest, 25)
    if dna(host) < 40:
        host.call('coop.dna add 40')
        time.sleep(2)
        clear_cinematics(host, guest, 20)
    print('  vote evolve:', host.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
    # With core.editor_now on (default), host enters Creature Editor immediately without pulling the guest in.
    ck(wait(lambda: in_editor(host), 60), 'host entered Creature Editor')
    ck(wait(lambda: editor_ui(host), 60, 2), 'host editor loaded UI')
    host.call('wait.frames 120 60000')
    time.sleep(2)
    d_in = dna(host)
    add_part(host, 780)
    if dna(host) == d_in:
        add_part(host, 170)
    time.sleep(1.5)
    print(f'  host editor DNA: {d_in} -> {dna(host)}', flush=True)
    host.call('click %d %d' % DONE)
    # Confirm any editor exit / growth cinematic caption until back in Creature stage
    t0 = time.time()
    while time.time() - t0 < 60:
        if 'Creature' in mode(host) and host.call('coop.avatar')['ok'] and not is_busy(host):
            break
        if in_editor(host):
            host.call('click %d %d' % DONE)
        else:
            host.call('key enter')
        time.sleep(1.5)
    ck('Creature' in mode(host) and host.call('coop.avatar')['ok'], 'host returned to Creature stage')
    # If evolution triggered a migration world share, wait for the guest to finish loading back into Creature stage.
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 45, 1.0),
       'guest is in Creature stage after host editor')
    clear_cinematics(host, guest, 20)


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(3)
        # 1. Advance brain so guest has pack cap >= 1
        print('  brain upgrade dna:', host.call('coop.dna add 250')['out'].strip(), flush=True)
        sl.wait(lambda: pack_info(guest).get('local_max', 0) >= 1, 20)
        clear_cinematics(host, guest, 15)
        time.sleep(1.5)

        h_sp0 = avatar_key(host)
        ck(bool(h_sp0), f'host avatar species key read ({h_sp0})')
        kin_id = sl.wait(lambda: find_host_species_npc(host, guest, h_sp0), 15) if h_sp0 else None
        ck(kin_id is not None, f'found live mirrored nest-mate of host species {h_sp0} (id={kin_id})')
        if kin_id is None:
            return ck.result()

        # 2. Guest recruits host-species NPC
        ok_rec = social_recruit_npc(host, guest, kin_id)
        hp0 = pack_info(host)
        gp0 = sl.wait(lambda: (lambda p: p if p.get('my_pack', 0) >= 1 else None)(pack_info(guest)), 8) or pack_info(guest)
        print(f'  before editor: ok={ok_rec}\n    host: {hp0["raw"].strip()}\n    guest: {gp0["raw"].strip()}', flush=True)
        ck(ok_rec and hp0['ok'] and hp0['total'] == 1 and hp0['list'] == 1 and hp0['temp'] == 1 and hp0['true_temp'] == 1
           and hp0['local_count'] == 0,
           f'guest recruited host-species NPC #{kin_id} cleanly before editor '
           f'(list={hp0.get("list")} temp={hp0.get("temp")} true={hp0.get("true_temp")} local={hp0.get("local_count")})')

        # 3. Host enters Creature Editor, evolves species, exits
        host_creature_editor(host, guest, ck)
        time.sleep(4)
        h_sp1 = avatar_key(host)
        hp1 = pack_info(host)
        gp1 = pack_info(guest)
        print(f'  after editor: species {h_sp0} -> {h_sp1}\n    host: {hp1["raw"].strip()}\n    guest: {gp1["raw"].strip()}',
              flush=True)

        ck(hp1['ok'] and hp1['ui_leaked'] == 0,
           f'host CreatureUI did not leak a portrait card for guest pack member on editor exit '
           f'(ui_leaked={hp1.get("ui_leaked")}, ui_blocked={hp1.get("ui_blocked")})')
        ck(gp1['ok'] and gp1['temp'] == 0 and gp1['local_count'] >= 0,
           f'guest posse temp count stayed clean after host editor / migration share '
           f'(guest temp={gp1.get("temp")} local_count={gp1.get("local_count")})')
        ck(hp1['total'] == 1 and any(ow == 1 for _, ow in hp1['members']) and hp1['local_count'] == 0
           and hp1['list'] == 1 and hp1['temp'] == 1 and hp1['true_temp'] == 1,
           f'guest pack member stayed owned by guest after Creature Editor and did not leak to host '
           f'(total={hp1.get("total")} members={hp1.get("members")} list={hp1.get("list")} '
           f'temp={hp1.get("temp")} true={hp1.get("true_temp")} local={hp1.get("local_count")} '
           f'rebound={hp1.get("rebound")})')
        if h_sp1 and h_sp0 and h_sp1 != h_sp0:
            ck(hp1['rebound'] >= 1, f'species evolution rebound guest pack member to new NPC id (rebound={hp1.get("rebound")})')

        # 4. Host saves and both players reload: guest pack member must NOT be saved into host's pack or leak mTempCount
        host, guest, rinfo = session_restart(host, guest, save=True)
        ck(rinfo['saved'], f'host saved world before reload ({rinfo})')
        time.sleep(3)
        hp2 = pack_info(host)
        print(f'  after save+reload:\n    host: {hp2["raw"].strip()}', flush=True)
        ck(hp2['ok'] and hp2['list'] == 0 and hp2['temp'] == 0 and hp2['true_temp'] == 0 and hp2['local_count'] == 0,
           f'save/reload kept guest pack member out of host posse and mNumTransientPosseMembers at 0 '
           f'(list={hp2.get("list")} temp={hp2.get("temp")} true={hp2.get("true_temp")} local={hp2.get("local_count")})')

        # 5. Enforce host 3-member pack cap: advance to brain 3 and attempt to recruit 4 distinct live NPCs
        if hp2.get('local_max', 0) < 3:
            host.call('coop.dna add 600')
            sl.wait(lambda: pack_info(host).get('local_max', 0) >= 3, 20)
            clear_cinematics(host, guest, 15)
            time.sleep(1.5)

        cur_sp = avatar_key(host) or h_sp1 or h_sp0
        cand = live_host_npc_ids(host)
        while len(cand) < 5 and cur_sp:
            host.call(f'coop.npc spawn {cur_sp}')
            time.sleep(0.5)
            cand = live_host_npc_ids(host)

        ck(len(cand) >= 4, f'have >= 4 live NPCs on host to test 3-member pack cap ({len(cand)} found)')
        rec_results = []
        for nid in cand[:5]:
            r = host.call(f'coop.npc recruit {nid}')
            ok = r.get('ok', False) and 'joined the pack' in r.get('out', '')
            rec_results.append((nid, ok, r.get('out', '').strip()))
            if len([x for x in rec_results if x[1]]) >= 4:
                break
        hp3 = pack_info(host)
        joined_n = len([x for x in rec_results if x[1]])
        print(f'  cap test results={rec_results}\n    host: {hp3["raw"].strip()}', flush=True)
        ck(hp3['ok'] and hp3['list'] == 3 and hp3['local_count'] == 3 and hp3['temp'] == 0 and hp3['true_temp'] == 0,
           f'host pack capped strictly at 3 members (list={hp3.get("list")} local_count={hp3.get("local_count")} '
           f'temp={hp3.get("temp")} true={hp3.get("true_temp")} joined_calls={joined_n})')
        ck(joined_n == 3, f'4th host pack recruit was refused (joined_calls={joined_n}, results={rec_results})')
    finally:
        pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
