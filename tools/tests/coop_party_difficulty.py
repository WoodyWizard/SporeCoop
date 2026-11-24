#!/usr/bin/env python3
"""The difficulty for 3-4 players (WP-D1 of docs/plan-0.7.17.md item 5, BUG-1473): the session setting
all.party_difficulty scales the fights between players and NPCs on the host (party_difficulty_logic.h). Three games in
the Creature stage: the host and Bob from lane A, Carol from lane B (coop_multi's extra instance; SPORE_LANE_B, default
the same lane).

Measured twice, at 100 % and at the value under test (--value: 150 % by default; `auto` with three players is 125 %).
Every number is the GAME's health field on the machine named, never a counter of the mod:
  npc hits   an animal of the host's world hits each player once (`coop.hp npchit <id> 20`: the game's TakeDamage of
             the host's game, forwarded to a guest as a real bite is): the drop of that player's own creature's health
             on its own game (`coop.hp` own:), and on the host's game for a guest (its puppet's health, what the host's
             health bar draws). The drop at the value / the drop at 100 % = percent / 100 (within 10 %).
  npc kills  one NPC mirrored on both guests, hit in turn by Bob, Carol (`coop.npc hit`, MSG_DAMAGE_REQ) and the host
             (`coop.npc hosthit`) with a quarter of its health each: the hits until the host's game has it dead are
             party::hits_to_kill (4 at 100 %, 6 at 150 %, 5 at 125 %, +-1); after the first hit its health on the host's
             game and on Bob's (the proxy's health, the guest's health bar) dropped by damage x 100 / percent.
The joint hunt bonus, the pack's healing and its rescue are off for the run (they change hits too: creature.joint_hunt,
creature.pack_heal, creature.pack_rescue). Key frames after the hits.

Twin (suite party_difficulty_off): SPORE_FIX_OFF=core.party_tuning: the numbers at 150 % are those at 100 %: must FAIL.

  SPORE_LANE=2 SPORE_LANE_B=3 python3 tools/tests/coop_party_difficulty.py [--value 150|125|200|auto] [--players 3]
"""
import argparse
import math
import re
import subprocess
import sys
import time

import lane
import settings_lib
import twoinst
from coop_multi import join_cmd, lane_b, start_extra, wait_all
from coop_team_real_damage import own_hp, puppet_hp
from twoinst import Checks, keyframe, start

KEY = 'all.party_difficulty'
VALUES = {'100': 0, '125': 1, '150': 2, '200': 3, 'auto': 4}
OFF_KEYS = ('creature.joint_hunt', 'creature.pack_heal', 'creature.pack_rescue')
# Keep below Spore's per-hit player damage cap (~52% of 10 max HP), so 100% (3.0), 125% (3.75), and 150% (4.5) are not clamped.
HIT = 3.0


def percent(value, players):
    """party::percent (party_difficulty_logic.h)"""
    return {0: 100, 1: 125, 2: 150, 3: 200}.get(value, min(200, 100 + 25 * max(0, players - 2)))


def hits_to_kill(hp, dmg, pct):
    d = dmg * 100.0 / pct
    return math.ceil(hp / d - 1e-4) if hp > 0 and d > 0 else 0


def host_npcs(host):
    """{id: (hp, dead, species)} of the host's NPCs: the game's health of each (`coop.npc list`)"""
    return {int(m.group(1)): (float(m.group(3)), bool(m.group(4)), m.group(2))
            for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=[\d.]+( DEAD)?',
                                 host.call('coop.npc list')['out'] or '', re.M)}


def proxy_hp(guest, nid):
    """the guest's proxy of host NPC nid: its game health (`coop.npc list` of a guest)"""
    m = re.search(rf'^\s*#{nid} species=\S+ .*?hp=([\d.]+)', guest.call('coop.npc list')['out'] or '', re.M)
    return float(m.group(1)) if m else None


def mirrored(guest):
    return set(int(x) for x in re.findall(r'#(\d+) species=\S+ spawned', guest.call('coop.npc list')['out'] or ''))


def family(host):
    m = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'] or '')
    return m.group(1).zfill(8) if m else ''


def set_value(ck, host, guests, value):
    ok = settings_lib.force(host, guests, KEY, str(value))
    ck(ok, f'every guest plays {KEY} {value}')
    time.sleep(1)
    print('  host:', (host.call('coop.party')['out'] or '').strip().replace('\n', ' |'), flush=True)


def npc_hit_drops(ck, host, players):
    """{name: (drop on its own game, drop on the host's game or None)} of one animal hit of HIT on each player"""
    out = {}
    for name, (pid, con) in players.items():
        _, mx = own_hp(con)
        if mx is None:
            ck(False, f'{name}: its own creature\'s health is readable')
            continue
        con.call('coop.team hp %.2f' % mx)
        time.sleep(2.5)  # the host's puppet mirrors the full health
        own0 = own_hp(con)[0]
        pup0 = puppet_hp(host, pid)[0] if pid else None
        r = (host.call(f'coop.hp npchit {pid} {HIT}')['out'] or '').strip()
        if not ck('hit' in r and 'no puppet' not in r, f'{name}: an animal of the host\'s world hits it ({r})'):
            continue
        # Poll the minimum health right after the hit before passive Creature regeneration heals it back.
        own1, pup1 = own0, pup0
        t0 = time.time()
        while time.time() - t0 < 2.0:
            h_now = own_hp(con)[0]
            if h_now is not None and (own1 is None or h_now < own1):
                own1 = h_now
            if pid:
                p_now = puppet_hp(host, pid)[0]
                if p_now is not None and (pup1 is None or p_now < pup1):
                    pup1 = p_now
            time.sleep(0.15)
        d_own = own0 - own1 if own0 is not None and own1 is not None else None
        d_pup = pup0 - pup1 if pup0 is not None and pup1 is not None else None
        print(f'  {name}: own {own0} -> {own1}' + (f', on the host {pup0} -> {pup1}' if pid else ''), flush=True)
        out[name] = (d_own, d_pup)
        con.call('coop.team hp %.2f' % mx)
    return out


def kill_count(ck, host, bob, carol, pct, tag):
    """one NPC mirrored on both guests, hit in turn by Bob, Carol and the host with a quarter of its health: (hits,
    expected, first drop on the host, first drop on Bob's proxy, the hit's damage)"""
    fam = family(host)
    both = mirrored(bob) & mirrored(carol)
    cands = [(nid, v) for nid, v in host_npcs(host).items() if nid in both and not v[1] and v[0] > 4 and v[2] != fam]
    if not ck(cands, f'{tag}: an NPC of the host mirrored on both guests to hunt ({len(cands)} found)'):
        return None
    nid, (hp0, _, _) = max(cands, key=lambda kv: kv[1][0])
    dmg = hp0 / 4.0
    want = hits_to_kill(hp0, dmg, pct)
    print(f'  {tag}: NPC #{nid} health {hp0:.1f}, hits of {dmg:.2f}, {want} expected at {pct} %', flush=True)
    b0 = proxy_hp(bob, nid)
    order = [('Bob', lambda: bob.call(f'coop.npc hit {nid} {dmg:.3f}')),
             ('Carol', lambda: carol.call(f'coop.npc hit {nid} {dmg:.3f}')),
             ('host', lambda: host.call(f'coop.npc hosthit {nid} {dmg:.3f}'))]
    hits, first_host, first_bob = 0, None, None
    for i in range(want + 4):
        order[i % 3][1]()
        hits += 1
        time.sleep(1.2)
        v = host_npcs(host).get(nid)
        if i == 0:
            if v:
                first_host = hp0 - v[0]
            time.sleep(1.5)  # the next snapshot to Bob
            b1 = proxy_hp(bob, nid)
            first_bob = b0 - b1 if b0 is not None and b1 is not None else None
        if v is None or v[1] or v[0] <= 0:
            break
    print(f'  {tag}: dead after {hits} hits (first hit: -{first_host} on the host, -{first_bob} on Bob\'s proxy)', flush=True)
    return hits, want, first_host, first_bob, dmg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--value', default='150', choices=tuple(VALUES))
    ap.add_argument('--players', type=int, default=3, choices=(3,))  # (the lab's memory gate counts the games by it)
    a = ap.parse_args()
    value = VALUES[a.value]
    twoinst.forget_settings()
    host, bob = start('creature')
    ck = Checks()
    extra = start_extra(1)
    (cname, (carol, _)), = extra.items()
    carol.call(join_cmd(cname))
    ck(wait_all(host, 2), 'both guests are in the host\'s world')
    guests = [bob, carol]
    t0 = time.time()
    while time.time() - t0 < 40 and not (mirrored(bob) and mirrored(carol)):
        time.sleep(1)
    for k in OFF_KEYS:
        ck(settings_lib.force(host, guests, k, 'off'), f'{k} off for the measurement')
    players = {'host': (0, host), 'Bob': (1, bob), cname: (2, carol)}
    cons = {'host': host, 'guest': bob, 'carol': carol}
    pct = percent(value, 3)
    try:
        set_value(ck, host, guests, 0)
        base = npc_hit_drops(ck, host, players)
        kill0 = kill_count(ck, host, bob, carol, 100, '100 %')
        set_value(ck, host, guests, value)
        st = host.call('coop.party')['out'] or ''
        ck(f'percent={pct}' in st, f'host: the percentage in force is {pct} ({st.strip().splitlines()[0] if st.strip() else st})')
        now = npc_hit_drops(ck, host, players)
        keyframe('npc_hits', f'all three: each creature\'s health bar lower after one animal bite ({pct} % of a solo bite)',
                 players=cons, settle=0)
        for name in players:
            b, n = base.get(name, (None, None)), now.get(name, (None, None))
            if b[0] and n[0]:
                r = n[0] / b[0]
                ck(abs(r - pct / 100) <= 0.1 * pct / 100,
                   f'{name}: an NPC\'s hit costs {pct} % of the 100 % one on its own game ({b[0]:.2f} -> {n[0]:.2f}, x{r:.2f})')
            else:
                ck(False, f'{name}: both hits measured on its own game ({b[0]}, {n[0]})')
            if players[name][0] and b[1] and n[1]:
                ck(abs(n[1] - n[0]) <= max(1.0, 0.1 * n[0]),
                   f'{name}: the host\'s game shows the same drop ({n[1]:.2f} there, {n[0]:.2f} on its own game)')
        kill1 = kill_count(ck, host, bob, carol, pct, f'{pct} %')
        keyframe('npc_kill', f'all three: the hunted NPC lies dead after {kill1[0] if kill1 else "?"} hits ({pct} %)',
                 players=cons, settle=0)
        if kill0:
            ck(abs(kill0[0] - kill0[1]) <= 1, f'100 %: the NPC fell after {kill0[0]} hits (the game\'s {kill0[1]})')
        if kill1:
            hits, want, fh, fb, dmg = kill1
            ck(abs(hits - want) <= 1, f'{pct} %: the NPC fell after {hits} hits (expected {want}, x{pct / 100:.2f} the hits)')
            exp = dmg * 100 / pct
            ck(fh is not None and abs(fh - exp) <= max(0.5, 0.1 * exp),
               f'{pct} %: the first hit took {fh} of its health on the host\'s game (expected {exp:.2f})')
            ck(fb is not None and abs(fb - exp) <= max(1.0, 0.15 * exp),
               f'{pct} %: Bob\'s game shows the same health drop on its copy ({fb}, expected {exp:.2f})')
        if kill0 and kill1 and value != 0:
            ck(kill1[0] > kill0[0], f'the NPC takes more hits at {pct} % ({kill1[0]}) than at 100 % ({kill0[0]})')
    finally:
        settings_lib.reset([host, bob, carol], (KEY,) + OFF_KEYS)
    ck.faults(host=host, guest=bob, carol=carol)
    subprocess.run([lane.SPORE, 'kill'], env=lane_b()[1], capture_output=True)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
