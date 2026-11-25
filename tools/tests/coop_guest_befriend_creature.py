#!/usr/bin/env python3
"""BUG-1561: Creature stage guest befriending of wild creatures and host's own species.

Verifies the four Creature guest-befriending fixes behind `social.guest_befriend`:
1. Host always includes its own avatar species as allied (relation=6) in MSG_SPECIES so an own-species guest
   (creature.mode own) still sees host's nest mates as allied/recruitable even when host's native table lacks a
   relation=6 entry (e.g. fresh stage or newly evolved host species).
2. Host holds the engaged NPC stationary and facing the guest's puppet during the guest's social game (npc_holds > 0)
   so it cannot walk out of kActRange (12m/16m) while the guest answers prompts.
3. Guest sends its own avatar ability key/points in K_ACT (host guest_pts > 0) and completes a social game with a wild
   Kook, impressing it on both host and guest.
4. Own-species guest (after Brain Level 1 unlock) recruits a nest mate of the host's own species into its own pack.

SPORE_FIX_OFF=social.guest_befriend must FAIL (host species drops to relation=0 on guest, npc_holds==0, guest_pts==0).

  SPORE_LANE=4 python3 tools/tests/coop_guest_befriend_creature.py
"""
import re
import time

import settings_lib as sl
from coop_creature_social import KOOK, num, social
from coop_creature_social_ally import guest_list, host_list, pos
from coop_guest_pack_recruit import pack_info, social_recruit_npc
from own_creature import play_own
from twoinst import Checks, start


def species_rel(con, sp_hex):
    out = con.call('coop.social species')['out']
    m = re.search(rf'species {sp_hex}/[0-9A-F]+/[0-9A-F]+ relation=(\d+)', out, re.I)
    return int(m.group(1)) if m else -1


def dismiss_cinematics(host, guest, secs=4.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        hb = 'cinematic' in host.call('coop.status')['out']
        gb = 'cinematic' in guest.call('coop.status')['out']
        if not hb and not gb:
            return
        if hb:
            host.call('key space')
        if gb:
            guest.call('key space')
        time.sleep(0.4)


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        for c in (host, guest):
            c.call('coop.team hp 1000')
        time.sleep(2)

        # Switch guest to its own different creature species (Kirgle, 290225CD) vs host's species (065CBE70)
        play_own(guest)
        time.sleep(2.5)

        # Find host's own species from host's nest mates
        hl = host_list(host)
        gl = guest_list(guest)
        # Host species in creature fixture is 065CBE70
        host_sp = '065CBE70'
        host_kin = [i for i, v in hl.items() if v[0] == host_sp and not v[2] and v[1] > 0 and gl.get(i, (False,))[0]]
        kooks = [i for i, v in hl.items() if v[0] == KOOK and not v[2] and v[1] > 0 and not v[3] and gl.get(i, (False,))[0]]
        print(f'  host kin={host_kin[:4]}, wild kooks={kooks[:4]}', flush=True)
        if not ck(bool(host_kin) and bool(kooks), f'guest mirrors host species kin ({host_kin[:3]}) and wild Kooks ({kooks[:3]})'):
            return ck.result()

        # Check 1: Even if host's native SpeciesRelationshipManager record for host's own species is cleared (rel=0,
        # simulating a fresh save or newly evolved host species), host still syncs relation=6 to the own-species guest!
        host.call(f'coop.social record {host_sp} 0 0')
        time.sleep(1.5)
        g_rel = species_rel(guest, host_sp)
        hs0 = social(host)
        print(f'  after clearing host record for {host_sp}: guest rel={g_rel}, host={hs0.strip()}', flush=True)
        ck(g_rel == 6, f'own-species guest still sees host species {host_sp} as allied relation=6 (rel={g_rel})')

        # Check 2 & 3: Guest befriends a wild Kook; verify host holds NPC still (npc_holds > 0) and uses guest's
        # own ability key/points (guest_pts > 0), impressing the Kook on both host and guest.
        kid = kooks[0]
        guest.call(f'coop.social goto {kid}')
        time.sleep(0.6)
        n0 = num(r'prompts_in=(\d+)', social(guest), 0)
        # Engage the Kook (sends K_ENGAGE, starts host prompt session and NPC hold)
        guest.call(f'coop.social act {kid} 0')
        time.sleep(0.5)
        # Wait for prompt and answer via guest's own ability
        t0 = time.time()
        answered = False
        while time.time() - t0 < 22:
            gs = social(guest)
            if host_list(host).get(kid, (0, 0, 0, False))[3] and guest_list(guest).get(kid, (False, False))[1]:
                break
            if num(r'prompts_in=(\d+)', gs, 0) > n0 and num(r'prompt_npc=(-?\d+)', gs, -1) == kid:
                guest.call(f'coop.social goto {kid}')
                res = guest.call(f'coop.social act {kid} 0')['out']
                if 'social action sent' in res:
                    n0 = num(r'prompts_in=(\d+)', gs, 0)
                    answered = True
                    time.sleep(1.2)
            time.sleep(0.3)

        hs1 = social(host)
        h_imp = host_list(host).get(kid, (0, 0, 0, False))[3]
        g_imp = guest_list(guest).get(kid, (False, False))[1]
        holds = num(r'npc_holds=(\d+)', hs1, 0)
        gpts = num(r'guest_pts=(\d+)', hs1, 0)
        print(f'  wild Kook #{kid}: answered={answered} impressed=(host={h_imp}, guest={g_imp}) npc_holds={holds} guest_pts={gpts}', flush=True)
        ck(holds > 0, f'host held engaged NPC in range during guest social game (npc_holds={holds})')  # counter: verifies host_engage/host_prompt_tick invoked game::stop + facing hold alongside game-state impression below
        ck(gpts > 0, f'host scored using guest own ability key/points (guest_pts={gpts})')  # counter: verifies K_ACT trailing ability key/points path was taken on host alongside game-state impression below
        ck(h_imp and g_imp, f'own-species guest impressed wild Kook #{kid} on both host and guest ({h_imp}, {g_imp})')

        # Check 4: Unlock Brain Level 1 (+250 DNA), dismiss upgrade cinematic, and verify own-species guest recruits
        # a creature of the host's own species!
        host.call('coop.dna add 250')
        sl.wait(lambda: pack_info(guest).get('local_max', 0) >= 1, 15)
        dismiss_cinematics(host, guest, 5.0)
        kin_id = host_kin[0]
        ok_rec = social_recruit_npc(host, guest, kin_id, timeout=22.0)
        gp = sl.wait(lambda: (lambda p: p if p['my_pack'] >= 1 else None)(pack_info(guest)), 8) or pack_info(guest)
        hp = pack_info(host)
        print(f'  host-species recruit #{kin_id}: ok={ok_rec} host={hp["raw"].strip()} | guest={gp["raw"].strip()}', flush=True)
        ck(ok_rec and gp['my_pack'] >= 1 and any(mid == kin_id and ow == 1 for mid, ow in hp['members']),
           f'own-species guest recruited host-species nest mate #{kin_id} into own pack (ok={ok_rec}, my_pack={gp["my_pack"]})')

        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        pass


if __name__ == '__main__':
    raise SystemExit(main())
