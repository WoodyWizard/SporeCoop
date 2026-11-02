#!/usr/bin/env python3
"""The pack's regeneration and rescue after real NPC damage, as the other player sees them (audit item 26).

coop_team.py and creature_partner_hp make the damage with test commands (`coop.team hit`, `coop.npc hurt`).
Here the damage is the game's own: a player hits a host NPC lightly, the NPC fights back (its AI's
retaliation, docs/re/creature.md: TakeDamage posts 0x400 with the attacker as sender) and its bites land
on the player through the game's TakeDamage (on the host; for a guest the host forwards the hits on the
guest's puppet, MSG_DAMAGE). Per victim (the guest, then the host), both players next to each other:

1. hurt: the victim's own health falls by real hits; the OTHER game's creature of the victim shows the
   lower health (`coop.hp`: the puppet's game health, what its health bar draws);
2. regen: the attacker is killed; next to the partner the victim's health grows (2 %/s) in its game and
   the other game's creature of it shows the rise;
3. rescue: the victim's health is set just above nothing (`coop.team hp`), the NPC is provoked again: the
   next real hit would kill, the pack saves it (40 % health). The victim's game has it alive, the other
   game's creature of it is alive with that health, and the other player is told ("... saved ...").

  coop_team_real_damage.py [--victim guest|host|both]
  (SPORE_FIX_OFF=creature.partner_hp fails: the other game's creature stays at full health)"""
import argparse
import math
import re
import time

from coop_npc_pooled import away
from twoinst import Checks, shot, start


def own_hp(con):
    m = re.search(r'^own: hp=([-\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def puppet_hp(con, pid):
    m = re.search(rf'^puppet {pid}: hp=([-\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def npcs(host):
    """{id: (hp, dist from the host, dead, species)} of the host's NPCs"""
    return {int(m.group(1)): (float(m.group(3)), float(m.group(4)), bool(m.group(5)), m.group(2))
            for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=([\d.]+)( DEAD)?', host.call('coop.npc list')['out'], re.M)}


def mirrored(guest):
    return set(int(x) for x in re.findall(r'#(\d+) species=\S+ spawned', guest.call('coop.npc list')['out']))


def beside(partner, victim, d):
    """the partner d m from the victim, on the ground"""
    p = pos(victim)
    q = away(p, d, 0.7)
    partner.call('coop.abilities goto %.2f %.2f %.2f' % tuple(q))
    time.sleep(1.5)
    r = pos(partner)
    return math.dist(p, r) if p and r else None


def provoke(host, guest, victim, tried, tries=8, wait=10.0):
    """the victim walks up to a host NPC and hits it lightly until one fights back (its bites lower the
    victim's own health): returns (npc id, the first bite)"""
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    vcon, pcon = (guest, host) if victim == 'guest' else (host, guest)
    for _ in range(tries):
        ns, ms = npcs(host), mirrored(guest)
        cand = sorted((d, i) for i, (hp, d, dead, sp) in ns.items()
                      if not dead and hp > 1 and sp != fam and i in ms and i not in tried and d < 90)
        if not cand:
            return None
        nid = cand[0][1]
        tried.add(nid)
        if victim == 'guest':
            host.call(f'coop.npc gonpc {nid} 12')  # the host's NPCs live around it
            time.sleep(0.8)
            guest.call(f'coop.social goto {nid}')
        else:
            host.call(f'coop.npc gonpc {nid} 3')
        time.sleep(0.8)
        h0, mx = own_hp(vcon)
        if victim == 'guest':
            guest.call(f'coop.npc hit {nid} 0.3')
        else:
            host.call(f'coop.npc hosthit {nid} 0.3')
        t0 = time.time()
        while time.time() - t0 < wait:
            time.sleep(0.3)
            h1, _ = own_hp(vcon)
            if h1 is not None and h0 is not None and h1 < h0 - 0.05:
                print(f'  #{nid} ({ns[nid][3]}) fights back: {victim} {h0:.2f} -> {h1:.2f} of {mx}', flush=True)
                return nid, h0 - h1
        print(f'  #{nid} ({ns[nid][3]}) did not fight back in {wait:.0f} s', flush=True)
    return None


def run(ck, host, guest, victim, shots):
    vcon, ocon = (guest, host) if victim == 'guest' else (host, guest)
    vid = 1 if victim == 'guest' else 0
    other = 'host' if victim == 'guest' else 'guest'
    tol = lambda mx: max(0.6, 0.08 * mx)  # noqa: E731 (a state is 0.1 s old; bites go on)
    _, mx = own_hp(vcon)
    # 1. hurt: the partner 25 m away (no regen), the victim at 60 % (fewer bites to wait for)
    vcon.call('coop.team hp %.2f' % (0.6 * mx))
    tried = set()
    got = provoke(host, guest, victim, tried)
    h_set = 0.6 * mx
    ck(got is not None, f'{victim}: a host NPC fights back and hurts it (the game\'s own hits)')
    if not got:
        return
    nid, bite = got
    print(f'  the {other} {beside(ocon, vcon, 25):.0f} m away (no regen)', flush=True)
    time.sleep(4)
    own, _ = own_hp(vcon)
    best = None
    t0 = time.time()
    while time.time() - t0 < 3:
        own, omx = own_hp(vcon)
        php, pmx = puppet_hp(ocon, vid)
        if php is not None and own is not None:
            best = (php, pmx, own, omx)
            if abs(php - own) <= tol(mx):
                break
        time.sleep(0.2)
    host.call(f'coop.npc hosthit {nid} 100000')  # the attacker dies: its bites end
    print(f'  {victim} hurt: own {best and best[2]}/{best and best[3]}, the {other}\'s game shows '
          f'{best and best[0]}/{best and best[1]}', flush=True)
    ck(best and best[2] < h_set - 0.3, f'{victim}: its own game took the NPC\'s hits ({best and best[2]} < {h_set:.2f} - 0.3)')
    ck(best and abs(best[0] - best[2]) <= tol(mx) and best[0] < 0.97 * mx,
       f'{victim}: the {other}\'s game shows it hurt ({best and best[0]} / own {best and best[2]})')
    # 2. regen next to the partner
    time.sleep(1.0)
    print(f'  the {other} next to it: {beside(ocon, vcon, 2.5):.1f} m', flush=True)
    a_own, _ = own_hp(vcon)
    a_pup, _ = puppet_hp(ocon, vid)
    time.sleep(5)
    b_own, _ = own_hp(vcon)
    b_pup, _ = puppet_hp(ocon, vid)
    print(f'  {victim} regen 5 s: own {a_own} -> {b_own}, the {other}\'s game {a_pup} -> {b_pup}', flush=True)
    ck(b_own is not None and a_own is not None and b_own > a_own + 0.04 * mx,
       f'{victim}: regenerates next to the partner in its game ({a_own} -> {b_own})')
    ck(b_pup is not None and a_pup is not None and b_pup > a_pup + 0.04 * mx,
       f'{victim}: the {other}\'s game shows the regeneration ({a_pup} -> {b_pup})')
    if shots:
        shot(ocon, f'team_real_{other}_sees_{victim}_regen')
    # 3. rescue: an NPC bites; the victim one bite from death, the partner next to it
    vcon.call('coop.team hp %.2f' % mx)
    st0 = ocon.call('coop.status')['out'].count('saved')
    got = provoke(host, guest, victim, tried, tries=8, wait=10.0)
    if not got:
        ck(False, f'{victim}: a second host NPC fights back (for the rescue)')
        return
    nid2, bite = got
    print(f'  the {other} next to it: {beside(ocon, vcon, 3):.1f} m', flush=True)
    # one bite from death until a bite lands: the regen next to the partner is undone each second, and the victim
    # hits the NPC lightly every 3 s (an NPC that lost interest fights back again)
    target = max(0.05, 0.4 * bite)
    vcon.call('coop.team hp %.2f' % target)
    t0 = last_set = last_hit = time.time()
    own = None
    while time.time() - t0 < 25:
        own, _ = own_hp(vcon)
        if own is not None and (own <= 0 or own >= 0.3 * mx):
            break
        now = time.time()
        if own is not None and own > target + 0.01 and now - last_set >= 1.0:
            vcon.call('coop.team hp %.2f' % target)
            last_set = now
        if now - last_hit >= 3.0:
            if victim == 'guest':
                guest.call(f'coop.npc hit {nid2} 0.1')
            else:
                host.call(f'coop.npc hosthit {nid2} 0.1')
            last_hit = now
        time.sleep(0.2)
    print(f'  {victim}: {time.time() - t0:.0f} s until the lethal bite', flush=True)
    host.call(f'coop.npc hosthit {nid2} 100000')
    time.sleep(1.0)
    own, omx = own_hp(vcon)
    php = None
    t0 = time.time()
    while time.time() - t0 < 3:
        php, pmx = puppet_hp(ocon, vid)
        if php is not None and own is not None and abs(php - own) <= tol(mx):
            break
        time.sleep(0.2)
        own, omx = own_hp(vcon)
    told = ocon.call('coop.status')['out'].count('saved') > st0
    print(f'  {victim} rescue: own {own}/{omx}, the {other}\'s game {php}; told: {told}', flush=True)
    ck(own is not None and own >= 0.25 * mx, f'{victim}: the pack saved it from the NPC\'s lethal bite ({own} of {mx})')
    ck(php is not None and php >= 0.25 * mx and own is not None and abs(php - own) <= tol(mx),
       f'{victim}: the {other}\'s game shows it alive with that health ({php} / own {own})')
    ck(told, f'{victim}: the {other} is told that the pack saved it')
    if shots:
        shot(ocon, f'team_real_{other}_sees_{victim}_rescued')
    if own is not None and own <= 0:
        vcon.call('key enter')  # the death caption
        time.sleep(15)
    vcon.call('coop.team hp %.2f' % mx)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--victim', default='both', choices=('guest', 'host', 'both'))
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and not mirrored(guest):
        time.sleep(1)
    time.sleep(2)
    print('  host:', host.call('coop.hp')['out'].strip().replace('\n', ' | '), flush=True)
    print('  guest:', guest.call('coop.hp')['out'].strip().replace('\n', ' | '), flush=True)
    for v in (('guest', 'host') if a.victim == 'both' else (a.victim,)):
        run(ck, host, guest, v, True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
