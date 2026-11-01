#!/usr/bin/env python3
"""A guest makes a nest of NPCs the pack's allies by social games, and both games show it (BUG-0021, BUG-0168
area; audit item 27).

coop_creature_social.py's ally phase was flaky (BUG-0021: 1 OK of 4; a hypothesis there: its engage() clicked
the social stance button before every Kook, a second click can leave the stance, the next key 1 bites).
This test plays the same way with the stance read from the game (`cCreatureGameData::GetAbilityMode`, 0 =
social) and clicked only when it is not social, and judges what each game shows:

1. the guest plays social games with Kooks (the fixture's nest that can be impressed: one round each, 3
   for an ally) by real input: stance, a click on the Kook, key 1; after each impression the HOST's game has
   that Kook impressed (the creature's own flag) and the guest's game shows its proxy impressed too;
2. ally: both games' species tables say ally (relation 6) for the Kooks, the guest's goal card says so;
3. recruitment: the host takes an allied Kook into the pack (`coop.npc recruit`, the game's AddMember as its
   own recruit does) and walks 20 m away; the host's game has it in the pack following it, and the guest's
   game shows the Kook next to the host's creature (where the host's game has it).

  coop_creature_social_ally.py
  SOCIAL_ALLY_PLANT=npcsync: the host stops its NPC sync before the recruitment (`coop.npc off`, a planted
  bug: the recruitment check must FAIL)"""
import math
import os
import re
import time

from coop_creature_social import KOOK, hold, num, objs, in_view, social, table
from twoinst import Checks, shot, start
from uifind import shown


def stance(con):
    """the game's ability mode: 0 social, 1 combat (None: unknown)"""
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cCreatureGameData::GetAbilityMode cdecl')['out'])
    return int(m.group(1), 16) if m else None


def hits_sent(guest):
    m = re.search(r'hits_sent=(\d+)', guest.call('coop.npc')['out'])
    return int(m.group(1)) if m else -1


def social_stance(con):
    for _ in range(3):
        if stance(con) == 0:
            return True
        con.call('click 690 998')  # the social stance button (W/2 - 30, H - 82)
        time.sleep(0.6)
    return stance(con) == 0


def engage(guest, nid, timeout=40):
    key = 'n%d' % nid
    social_stance(guest)
    t0 = time.time()
    sweep = 0
    while time.time() - t0 < timeout:
        guest.call(f'coop.social goto {nid}')
        time.sleep(0.5)
        o = objs(guest).get(key)
        if not in_view(o):
            guest.call('drag 720 540 950 540 right')
            sweep += 1
            if sweep % 8 == 0:
                guest.call('wheel 720 540 -3')
            time.sleep(0.4)
            continue
        social_stance(guest)
        k0 = hits_sent(guest)
        guest.call(f'click {o[1]} {o[2]}')
        time.sleep(0.8)
        if hits_sent(guest) > k0:
            print(f'  #{nid}: the click sent a HIT (stance {stance(guest)})', flush=True)
        if re.search(r'engaged=%d\b' % nid, social(guest)):
            return True
    return False


def host_list(host):
    """{id: (species, hp, dead, impressed, pack, pos)}"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=[\d.]+( DEAD)? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)(.*)$',
                         host.call('coop.npc list')['out'], re.M):
        rest = m.group(8)
        imp = re.search(r'impressed=(\d)', rest)
        out[int(m.group(1))] = (m.group(2), float(m.group(3)), bool(m.group(4)), bool(imp and imp.group(1) == '1'),
                                ' pack' in rest, tuple(float(m.group(i)) for i in (5, 6, 7)))
    return out


def guest_list(guest):
    """{id: (spawned, impressed, at)} of the guest's proxies (impressed: the game's flag of the proxy)"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) species=\S+ (spawned|pending) .* at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)(.*)$',
                         guest.call('coop.npc list')['out'], re.M):
        imp = re.search(r'impressed=(-?\d)', m.group(6))
        out[int(m.group(1))] = (m.group(2) == 'spawned', bool(imp and imp.group(1) == '1'),
                                tuple(float(m.group(i)) for i in (3, 4, 5)))
    return out


def pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(2)
    gl = guest_list(guest)
    kooks = [i for i, v in host_list(host).items() if v[0] == KOOK and not v[2] and v[1] > 0 and gl.get(i, (False,))[0]]
    ck(len(kooks) >= 3, f'the guest mirrors the Kooks ({kooks})')
    if not kooks:
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.npc gonpc {kooks[0]} 10')
    time.sleep(1)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 2, hp[1], hp[2]))
    time.sleep(3)
    imp0 = table(host).get(KOOK, (0, 0, 0))[1]
    print(f'  Kooks impressed before: {imp0}; stance {stance(guest)}', flush=True)

    # ---- 1. social games by the guest until the Kooks are allies
    impressed = []
    for nid in kooks:
        if table(host).get(KOOK, (0,))[0] == 6:
            break
        for attempt in range(3):
            if not engage(guest, nid, 40):
                print(f'  #{nid}: not engaged (attempt {attempt + 1})', flush=True)
                continue
            i0, f0 = num(r'impressed=(\d+)', social(host), 0), num(r'refused=(\d+)', social(host), 0)
            for _ in range(4):
                s0, k0 = stance(guest), hits_sent(guest)
                hold(guest, 0x31)  # key 1: the first social ability (sing)
                time.sleep(3)
                if hits_sent(guest) > k0:  # BUG-0021: the guest's own hit on a Kook (it dies, its impression is lost)
                    print(f'  #{nid}: key 1 sent a HIT (stance before the key {s0}, now {stance(guest)})', flush=True)
                hs = social(host)
                if num(r'impressed=(\d+)', hs, 0) > i0 or num(r'refused=(\d+)', hs, 0) > f0:
                    break
                print(f'  #{nid}: no answer yet (stance before the key: {s0})', flush=True)
                engage(guest, nid, 15)
            if num(r'impressed=(\d+)', social(host), 0) > i0:
                impressed.append(nid)
                break
            print(f'  #{nid}: no impression (attempt {attempt + 1}; host {social(host).strip()})', flush=True)
        if nid in impressed:
            time.sleep(1.5)
            h, g = host_list(host).get(nid), guest_list(guest).get(nid)
            print(f'  #{nid} impressed: host flag {h and h[3]}, guest proxy flag {g and g[1]}; host table {table(host).get(KOOK)}',
                  flush=True)
            ck(h is not None and h[3], f'#{nid}: the host\'s game has the Kook impressed (the creature\'s flag)')
            ck(g is not None and g[1], f'#{nid}: the guest\'s game shows its Kook impressed')
    time.sleep(3)
    ht, gt = table(host), table(guest)
    print(f'  ally: host {ht.get(KOOK)}, guest {gt.get(KOOK)}; impressed {impressed}; {social(host).strip()}', flush=True)
    ck(ht.get(KOOK, (0,))[0] == 6, f'the guest\'s games made the Kooks the pack\'s allies in the host\'s game ({ht.get(KOOK)})')
    ck(gt.get(KOOK) == ht.get(KOOK), f'the guest\'s game has the same ally record ({gt.get(KOOK)})')
    ck(shown(guest, 'allies') is not None, 'the guest\'s card says "The Kook are your allies!"')
    shot(guest, 'social_ally_guest')
    shot(host, 'social_ally_host')

    # ---- 3. recruitment: an allied Kook joins the host's pack and follows it; the guest sees it follow
    hl = host_list(host)
    rec = next((i for i in impressed if i in hl and not hl[i][2]), None) or \
        next((i for i, v in hl.items() if v[0] == KOOK and not v[2] and v[1] > 0), None)
    if rec is None:
        ck(False, 'recruitment: a living Kook')
    else:
        if os.environ.get('SOCIAL_ALLY_PLANT') == 'npcsync':
            print('  PLANT: ' + host.call('coop.npc off')['out'].strip().splitlines()[0], flush=True)
        out = host.call(f'coop.npc recruit {rec}')['out'].strip()
        print(f'  recruit #{rec}: {out}', flush=True)
        h0 = pos(host)
        from coop_npc_pooled import away
        to = away(h0, 20, 2.0)
        host.call('coop.abilities goto %.2f %.2f %.2f' % tuple(to))
        guest.call('coop.abilities goto %.2f %.2f %.2f' % tuple(away(to, 4, 0.5)))
        t0 = time.time()
        res = None
        while time.time() - t0 < 25:
            time.sleep(1)
            hl, gl, hp = host_list(host), guest_list(guest), pos(host)
            h, g = hl.get(rec), gl.get(rec)
            if h and g and hp:
                res = (h[4], math.dist(h[5], hp), g[0], math.dist(g[2], hp), math.dist(g[2], h[5]))
                if res[0] and res[1] < 10 and res[2] and res[3] < 10 and res[4] < 2.5:
                    break
        print(f'  recruit #{rec} after {time.time() - t0:.0f} s: {res and dict(zip(("pack", "host_m", "guest_has", "guest_m", "apart"), res))}',
              flush=True)
        ck(res is not None and res[0] and res[1] < 10, f'recruitment: the host\'s game has #{rec} in the pack, following it '
           f'({res and round(res[1], 1)} m)')
        ck(res is not None and res[2] and res[3] < 10 and res[4] < 2.5,
           f'recruitment: the guest\'s game shows the recruit next to the host ({res and round(res[3], 1)} m from it, '
           f'{res and round(res[4], 1)} m from the host\'s own Kook)')
        shot(guest, 'social_recruit_guest')
        if os.environ.get('SOCIAL_ALLY_PLANT') == 'npcsync':
            host.call('coop.npc on')
    for c in (host, guest):
        m = re.search(r'^own: hp=[-\d.]+/([\d.]+)', c.call('coop.hp')['out'], re.M)
        if m:
            c.call(f'coop.team hp {m.group(1)}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
