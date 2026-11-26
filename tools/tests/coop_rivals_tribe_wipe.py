#!/usr/bin/env python3
"""Tribe Rivals: full tribe wipe recovery, PopDestroyed Game Over block, baby creation, and 5-min PvP immunity (BUG-1571).

1. Start two games on `tribe` fixture, enable Tribe Rivals + War between host (0) and guest (1).
   Set wipe timing to 3 s respawn / 300 s (5 min) PvP immunity.
2. Phase 1 (guest wiped with 0 food): drain guest food to 0 and kill all guest tribe members + chieftain
   (`coop.tribe rivals wipe_all 0 1`).
   - Neither host nor guest hits Game Over or input lock.
   - Guest gets 5-minute (~300s) PvP immunity immediately (synced to guest too).
   - After ~3 s guest's tribe automatically respawns (>= 2 live adults including chieftain) with >= 15 food.
   - While guest is immune, host PvP citizen damage, hut damage, and food raid are ALL blocked (`immune_blocked >= 3`).
   - Guest can immediately buy a baby (`coop.tribe rivals baby 1`), increasing member count and spending 10 food.
3. Phase 2 (anti-exploit + host full wipe):
   - Guest attacks host (`hit_member 1 0 1`): breaks guest's own PvP shield early (`immune_broken >= 1`, guest immune=0).
   - Guest kills all host tribe members + chieftain (`wipe_all 1 0`).
   - Native `TRG_PRE_PopDestroyed` Game Over is blocked on host (`pop_go_blocked >= 1`), host stays in Tribe without
     input lock/Game Over caption, and guest is NOT paused by `tribe.gameover_wait`.
   - Host gets 5-minute PvP shield and automatically respawns (>= 2 live adults, `recovered >= 2`), and guest attacks
     against shielded host are blocked.
PASS: 1..3 with zero faults.
SPORE_FIX_OFF=rivals.tribe_wipe_recovery (`rivals_tribe_wipe_off`): wiped tribes do not auto-respawn or gain
PvP immunity, and host wipe triggers Game Over instead of blocking PopDestroyed: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_tribe_wipe.py
"""
import re
import time

from coop_rivals_tribe import parse_assign, parse_int, rv_out, tr_out, wait_for
from harness import num
import twoinst
from twoinst import Checks, start


def parse_live(text, pid):
    m = re.search(rf'assign pid={pid} .*?\blive=(-?\d+)', text)
    return int(m.group(1)) if m else -1


def parse_immune(text, pid):
    m = re.search(rf'assign pid={pid} .*?\bimmune=(-?\d+)', text)
    return int(m.group(1)) if m else -1


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(2.0)

    # 1. Enable Tribe Rivals + War between host (0) and guest (1)
    host.call('coop.rivals fasttimers 1')
    host.call('coop.rivals mode tribe rivals')
    ok_assign = wait_for(lambda: parse_assign(tr_out(host), 1) is not None and parse_int(tr_out(guest), 'swapped') == 1,
                         25.0)
    ck(bool(ok_assign), f'Tribe Rivals assigned guest tribe and swapped perspective ({tr_out(host).splitlines()[0]})')
    if not ok_assign:
        return ck.result()
    host.call('coop.rivals war 1')
    war_ok = wait_for(lambda: '0-1=War' in rv_out(host) and '0-1=War' in rv_out(guest), 8.0)
    ck(bool(war_ok), 'players transitioned to War')
    if not war_ok:
        return ck.result()

    # Fast respawn (3 s) with full 5-minute (300 s) PvP shield
    print('  wipe_timing: ' + (host.call('coop.tribe rivals wipe_timing 3 300').get('out') or '').strip(), flush=True)

    # 2. Phase 1: Drain guest food to 0, then host wipes out ALL guest members + chieftain
    g_food0 = parse_assign(tr_out(host), 1)['food']
    if g_food0 > 0:
        host.call(f'coop.tribe rivals addfood 1 -{g_food0}')
    ck(parse_assign(tr_out(host), 1)['food'] == 0,
       f'guest starts Phase 1 with 0 food ({parse_assign(tr_out(host), 1)["food"]})')

    w1 = (host.call('coop.tribe rivals wipe_all 0 1').get('out') or '').strip()
    print(f'  Phase 1 wipe_all 0->1: {w1}', flush=True)
    ck('live_after=0' in w1, f'host wiped all guest tribe members + chieftain ({w1})')

    # Guest gets 5-min PvP shield immediately and auto-respawns with >= 2 live adults & >= 15 food
    imm_started = wait_for(lambda: parse_immune(tr_out(host), 1) >= 250, 5.0)
    ck(bool(imm_started), f'wiped guest immediately got ~5-min PvP shield (immune={parse_immune(tr_out(host), 1)}s)')

    respawned1 = wait_for(lambda: parse_live(tr_out(host), 1) >= 2 and parse_int(tr_out(host), 'recovered') >= 1, 15.0)
    # Wait 2.5 s after respawn to prove respawned adults stay alive on the village surface (no planet-core death loop)
    if respawned1:
        time.sleep(2.5)
    st_h1 = tr_out(host)
    a1 = parse_assign(st_h1, 1)
    g_imm_local = wait_for(lambda: parse_immune(tr_out(guest), 1) >= 200, 8.0)
    print(f'  Phase 1 after respawn (+2.5s): host={st_h1.splitlines()[0]} | p1={a1} live={parse_live(st_h1, 1)} '
          f'immune_h={parse_immune(st_h1, 1)} immune_g={parse_immune(tr_out(guest), 1)}', flush=True)
    ck(bool(respawned1) and parse_live(st_h1, 1) >= 2 and parse_int(st_h1, 'recovered') == 1,
       f'guest tribe auto-respawned once and stayed alive with >= 2 adults (live={parse_live(st_h1, 1)}, recovered={parse_int(st_h1, "recovered")})')
    ck(a1 is not None and a1['food'] >= 15, f'guest respawned with >= 15 recovery food (food={a1["food"] if a1 else -1})')
    ck(bool(g_imm_local), f'5-min PvP shield synced to guest (guest immune={parse_immune(tr_out(guest), 1)}s)')

    # PvP shield blocks citizen damage, hut damage, and food raids while active
    ib0 = parse_int(tr_out(host), 'immune_blocked')
    hm_out = (host.call('coop.tribe rivals hit_member 0 1 50').get('out') or '').strip()
    hh_out = (host.call('coop.tribe rivals hit_hut 0 1 50').get('out') or '').strip()
    f1_pre = parse_assign(tr_out(host), 1)['food']
    rd_out = (host.call('coop.tribe rivals raid 1 15').get('out') or '').strip()
    f1_post = parse_assign(tr_out(host), 1)['food']
    ib1 = parse_int(tr_out(host), 'immune_blocked')
    print(f'  Phase 1 shield checks: hit_member=[{hm_out}] hit_hut=[{hh_out}] raid=[{rd_out}] food={f1_pre}->{f1_post} ib={ib0}->{ib1}', flush=True)
    ck(ib1 >= ib0 + 3 and f1_post == f1_pre,
       f'5-min PvP shield blocked citizen hit, hut hit, and food raid (immune_blocked {ib0}->{ib1}, food {f1_pre}->{f1_post})')

    # Baby creation works right after wipe recovery
    baby_out = (host.call('coop.tribe rivals baby 1').get('out') or '').strip()
    print(f'  Phase 1 guest baby: {baby_out}', flush=True)
    ck(baby_out.startswith('ok:'), f'guest can buy a baby immediately after wipe recovery ({baby_out})')

    # 3. Phase 2: Guest attacking host breaks guest's own shield early, then guest kills HOST members one by one
    # (1.5 s apart so each fallen member leaves cTribe +0x354 up-list and the final fall fires native 0x5C80207 PopDestroyed)
    brk_out = (host.call('coop.tribe rivals hit_member 1 0 1').get('out') or '').strip()
    ib_brk = parse_int(tr_out(host), 'immune_broken')
    g_imm_after_atk = parse_immune(tr_out(host), 1)
    print(f'  Phase 2 anti-exploit: hit_member 1->0=[{brk_out}] immune_broken={ib_brk} guest_immune={g_imm_after_atk}', flush=True)
    ck(ib_brk >= 1 and g_imm_after_atk == 0,
       f'attacking a rival broke shielded player own PvP immunity early (immune_broken={ib_brk}, immune={g_imm_after_atk})')

    last_kill = ''
    for step in range(10):
        if parse_live(tr_out(host), 0) <= 0:
            break
        last_kill = (host.call('coop.tribe rivals kill_one 1 0').get('out') or '').strip()
        print(f'  Phase 2 kill_one #{step + 1} 1->0: {last_kill}', flush=True)
        if 'live=1->0' in last_kill or parse_live(tr_out(host), 0) <= 0:
            break
        time.sleep(1.5)
    ck(parse_live(tr_out(host), 0) == 0, f'guest wiped all host tribe members + chieftain ({last_kill})')

    respawned2 = wait_for(lambda: parse_live(tr_out(host), 0) >= 2 and parse_int(tr_out(host), 'recovered') >= 2, 15.0)
    if respawned2:
        time.sleep(2.5)
    st_h2 = tr_out(host)
    pop_blk = parse_int(st_h2, 'pop_go_blocked')
    h_mode = (host.call('game.mode').get('out') or '').strip()
    g_go = (guest.call('coop.tribe').get('out') or '')
    g_paused = num(g_go, 'paused') or 0
    print(f'  Phase 2 after host respawn (+2.5s): host={st_h2.splitlines()[0]} mode={h_mode} live0={parse_live(st_h2, 0)} '
          f'imm0={parse_immune(st_h2, 0)} pop_go_blocked={pop_blk} guest_go_paused={g_paused}', flush=True)
    ck(pop_blk >= 1,  # counter: confirms TribeGame_GoalListener blocked native PopDestroyed (0x5C80207); mode & respawn checked below
       f'host blocked native TRG_PRE_PopDestroyed Game Over (pop_go_blocked={pop_blk})')
    ck('Tribe' in h_mode and g_paused == 0,
       f'host stayed in Tribe without Game Over and guest was not paused (mode={h_mode}, guest_paused={g_paused})')
    ck(bool(respawned2) and parse_live(st_h2, 0) >= 2 and parse_int(st_h2, 'recovered') == 2 and parse_immune(st_h2, 0) >= 250,
       f'host tribe auto-respawned once, stayed alive with >= 2 adults and 5-min PvP shield (live={parse_live(st_h2, 0)}, recovered={parse_int(st_h2, "recovered")}, immune={parse_immune(st_h2, 0)}s)')

    # Guest attack against shielded host is blocked
    ib2_before = parse_int(tr_out(host), 'immune_blocked')
    hm2_out = (host.call('coop.tribe rivals hit_member 1 0 50').get('out') or '').strip()
    ib2_after = parse_int(tr_out(host), 'immune_blocked')
    ck(ib2_after > ib2_before,
       f'host 5-min PvP shield blocked guest attack ({hm2_out}, immune_blocked {ib2_before}->{ib2_after})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
