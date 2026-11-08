#!/usr/bin/env python3
"""Tribe: an NPC tribe member killed on the host lies where the host's lies on the guest too (BUG-0488).

coop_tribe_member_place.py --attach over play_tribe (lab 20261106-070213 / its rerun): the detector's
`Tribe:unmatched:citizen` rows were host citizens that stood still for 10-20 s with no member the mod tracks
near them (no t<id> within 40 m): ? the bodies of killed members, which leave their tribe's member list while the
game keeps the body, so the host's snapshot drops them and the guest removes its mirror at once.

1. Two games on the tribe fixture.
2. The host kills one NPC tribe member near the guest (`coop.tribe pincer <victim> <attacker> 1000`).
3. For 25 s: the game's citizens round the victim's place on both games (`coop.census all`, worlddiff rows).
PASS: whenever the host's game has a citizen there, the guest's has one within 15 m too.
SPORE_FIX_OFF=tribe.dead_body: must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_dead_member.py [--all: every member of that tribe] [--player: a member of
  the host's own tribe, BUG-0620; SPORE_FIX_OFF=tribe.member_body must FAIL] [--rejoin: BUG-0664, below]
  [--join-body: --rejoin, and the guest's game has a citizen within 3 m of the host's body all the time it lies there
  (BUG-0665: a guest spawned no mirror for a body entry; SPORE_FIX_OFF=tribe.join_body must FAIL)]

--rejoin (BUG-0664): the guest leaves and joins again; once the host has saved the world for it, the host's own
member is killed, so the guest loads a world where that member is a body out of its tribe's list on the host. The
host's sync-check is asked again and again while the body lies there. PASS: no MISMATCH (long_tribe on rel/0.7.9:
"t: 1 missing on the guest" twice, after the restart and after the rejoin). SPORE_FIX_OFF=tribe.sync_bodies must FAIL.

--eat (BUG-0667): before the host's own member is killed, the guest's mirror of it starts eating at its food mat in the
guest's own game (`coop.tribe localbt` + `coop.tribe eat`: its tree runs as while the host is away, long_tribe's
editor visit), so the eat node 0x0461300F is active on the mirror when its body is destroyed. PASS: the body goes on the
guest with no caught fault and the guest keeps drawing frames. SPORE_FIX_OFF=tribe.body_teardown must FAIL (the fault
at 0xAC9C7D, data 0x39C; long_tribe's guest froze after it).
--eat --menu (BUG-0667, long_tribe_short 20261107-043030): as --eat, but once the guest has taken the body out of its
tribe (bodies_detached), the guest leaves to the main menu through the pause menu (Exit to Main Menu, Don't Save x2)
while the body still lies: the world's teardown destroys the tribes first and the body after them. PASS: no caught
fault, the guest reaches the main menu and runs frames. SPORE_FIX_OFF=tribe.body_teardown must FAIL.
"""
import math
import re
import sys
import time

import lane

import worlddiff
from coop_tribe import members
from twoinst import Checks, start, wait_guest_in
RE_TEARDOWN = r"(?:teardown_restored|slot_null_skipped)=\d+"  # (Python 3.10 f-strings take no backslash)


def citizens_at(con, p, r=20.0):
    w = worlddiff.dump(con, 60, p)
    return [x['pos'] for x in w.rows if x['kind'] == 'g' and worlddiff.noun_name(x['noun']) == 'citizen'
            and not worlddiff._origin(x['pos']) and math.dist(x['pos'], p) <= r]


ALL = '--all' in sys.argv
# BUG-0620: the victim is a member of the player tribe (the host's own): a player's member that dies leaves its
# tribe's member list while the game keeps its body (an NPC tribe's stays in its tribe, knocked out)
PLAYER = '--player' in sys.argv or '--rejoin' in sys.argv or '--join-body' in sys.argv
REJOIN = '--rejoin' in sys.argv or '--join-body' in sys.argv  # BUG-0664
JOIN_BODY = '--join-body' in sys.argv  # BUG-0665: the joined guest's game shows the body too
EAT = '--eat' in sys.argv  # BUG-0667
MENU = '--menu' in sys.argv  # BUG-0667: the guest leaves to the main menu while the body lies
if EAT:
    PLAYER = True
EAT_NODE = 0x0461300F


def guest_node(con, i):
    m = re.search(r'node=([0-9A-Fa-f]{8})', con.call(f'coop.tribe ai {i}')['out'])
    return int(m.group(1), 16) if m else None


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def menu_exit(guest, ck):
    """the guest leaves its world to the main menu as a player does (coop_long_session's menu event)"""
    import uifind
    if 'open=1' in guest.call('coop.uistate').get('out', ''):
        guest.call('key f9')
    for _ in range(3):
        guest.call('key esc')
        time.sleep(1.2)
        if uifind.shown(guest, 'Resume Game'):
            break
    ok = uifind.press(guest, 'Exit to Main Menu', 'Do you want to save your game?') \
        and uifind.press(guest, "Don't Save", 'Are you sure?') and uifind.press(guest, "Don't Save")
    ck(ok, 'the guest chose Exit to Main Menu, Don\'t Save')
    t0, seen = time.time(), False
    while ok and time.time() - t0 < 60 and not seen:
        seen = 'GalaxyGameEntry' in guest.call('game.mode').get('out', '')
        time.sleep(1)
    ck(seen, f'the guest is in the main menu ({time.time() - t0:.1f} s)')
    f0 = frame(guest)
    time.sleep(5)
    f1 = frame(guest)
    ck(f1 > f0 + 30, f'the guest runs frames 5 s later (frame {f0} -> {f1})')


def eat_then_die(host, guest, ck, victim, attacker):
    """BUG-0667: the mirror's eat node is active when the guest destroys the body"""
    print('  guest:', guest.call(f'coop.tribe localbt {victim} 1')['out'].strip(), flush=True)
    node, t0 = None, time.time()
    while time.time() - t0 < 30 and node != EAT_NODE:
        if int(time.time() - t0) % 6 == 0:
            print('  guest:', guest.call(f'coop.tribe eat {victim}')['out'].strip(), flush=True)
        time.sleep(1)
        node = guest_node(guest, victim)
    print('  guest: ' + guest.call(f'coop.tribe localbt {victim} 0')['out'].strip() + f'; node {node and hex(node)} after '
          f'{time.time() - t0:.1f} s: {guest.call(f"coop.tribe ai {victim}")["out"].strip()[:160]}', flush=True)
    ck(node == EAT_NODE, f'the guest\'s mirror of #{victim} is in the eat node (node {node and hex(node)})')
    if node != EAT_NODE:
        return
    print('  host:', host.call(f'coop.tribe pincer {victim} {attacker} 1000')['out'].strip(), flush=True)
    t1, gone = time.time(), None
    while time.time() - t1 < 75 and gone is None:
        time.sleep(3)
        st = next((x for x in guest.call('coop.tribe')['out'].splitlines() if 'tribe(guest)' in x), '')
        det = re.search(r'bodies_detached=(\d+)', st)
        in_g = victim in members(guest) or guest_node(guest, victim) is not None
        print(f'    {time.time() - t1:4.1f} s: guest has the mirror {in_g}, '
              f'{det.group(0) if det else ""} {" ".join(re.findall(RE_TEARDOWN, st))}',
              flush=True)
        if MENU and det and int(det.group(1)) > 0 and in_g:  # the body lies, out of its tribe: leave the world
            print('  guest: the body is out of its tribe; to the main menu', flush=True)
            return menu_exit(guest, ck)
        if not in_g and det and int(det.group(1)) > 0:
            gone = time.time() - t1
    ck(gone is not None, f'the body went on the guest ({gone and round(gone, 1)} s after the kill)')
    f0 = frame(guest)
    time.sleep(5)
    f1 = frame(guest)
    ck(f1 > f0 + 30, f'the guest runs frames 5 s later (frame {f0} -> {f1})')


TRIBE_MODE = 0x1654C02


def guest_mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def sync_counts(con):
    m = re.search(r'checks (\d+) ok (\d+) differs (\d+) mismatch (\d+)', con.call('coop.diag sync')['out'])
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0, 0)


def bodies_sent(con):
    m = re.search(r'bodies_sent=(\d+)', con.call('coop.tribe')['out'])
    return int(m.group(1)) if m else 0


def sync_keys(con):
    return {ln.split()[0] for ln in con.call('coop.sync')['out'].splitlines() if ln.split() and ln.split()[0][:1] == 't'}


def rejoin_with_body(host, guest, ck, victim, attacker):
    """BUG-0664: the guest joins a world in which the host's member died (its body lies, out of the member list)"""
    host.call('coop.diag sync 600')  # only the checks asked below
    c0 = sync_counts(host)
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    t0 = time.time()
    # the host saves the world for the guest first (its frames stop under the saved box): kill after that, so the
    # guest's world still has the member alive and only the host's snapshot names its body
    while time.time() - t0 < 60 and guest_mode(guest) == TRIBE_MODE:
        time.sleep(0.3)
    print(f'  guest left its world after {time.time() - t0:.1f} s', flush=True)
    p = members(host).get(victim, (0, 0, None))[2]
    print('  host:', host.call(f'coop.tribe pincer {victim} {attacker} 1000')['out'].strip(), flush=True)
    together = wait_guest_in(host, guest, 150)
    ck(together, f'the guest is in the host\'s world again ({time.time() - t0:.1f} s)')
    body_checks, missing, shown, lone_at = 0, [], 0, []
    t1, sent = time.time(), bodies_sent(host)
    while time.time() - t1 < 24:
        in_list = victim in members(host)
        hk, gk = sync_keys(host), sync_keys(guest)
        key = f't{victim}'
        # the host's game keeps the body: its snapshot still sends one (tribe.member_body, bodies_sent grows)
        now_sent = bodies_sent(host)
        body = not in_list and now_sent > sent
        sent = now_sent
        print(f'    {time.time() - t1:4.1f} s: victim in its tribe {in_list}, body sent {body}, host lists {key} '
              f'{key in hk}, guest {key in gk}; host t {len(hk)} guest t {len(gk)}', flush=True)
        if body:
            body_checks += 1
            if key in hk and key not in gk:
                missing.append(round(time.time() - t1, 1))
            if JOIN_BODY and p:  # BUG-0665: the guest's game has a citizen where the host's body lies
                hc, gc = citizens_at(host, p, 6), citizens_at(guest, p, 21)
                lone = [q for q in hc if not any(math.dist(q, g) <= 3 for g in gc)]
                print(f'      host citizens within 6 m of the body {len(hc)}, guest within 21 m {len(gc)}, '
                      f'host without a partner {len(lone)}', flush=True)
                shown += bool(hc) and not lone
                if lone:
                    lone_at.append(round(time.time() - t1, 1))
            host.call('coop.diag sync now')
        time.sleep(4)
    time.sleep(5)
    c1 = sync_counts(host)
    print(f'  host sync-checks: {c1[0] - c0[0]}, ok {c1[1] - c0[1]}, differs {c1[2] - c0[2]}, mismatch {c1[3] - c0[3]}; '
          f'samples with the body: {body_checks}, guest without its key: {missing[:6]}', flush=True)
    host.call('coop.diag sync on')
    ck(body_checks > 0, f'the host\'s body lay out of its member list after the guest joined ({body_checks} samples)')
    ck(c1[0] > c0[0], f'the host ran sync-checks ({c1[0] - c0[0]})')
    ck(c1[3] == c0[3], f'no sync-check MISMATCH while the body lay there ({c1[3] - c0[3]})')
    if JOIN_BODY:
        ck(p is not None, 'the victim\'s place on the host before the kill')
        ck(shown > 0 and not lone_at, f'the joined guest\'s game has a citizen where the host\'s body lies '
           f'({shown} samples; host body without a partner: {lone_at[:6]})')


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    hm = members(host)
    npc = {i: m for i, m in hm.items() if m[3] != 'player'}
    by_tribe = {}
    for i, m in npc.items():
        by_tribe.setdefault(m[3], []).append(i)
    pair = next(((v[0], v[1]) for v in by_tribe.values() if len(v) >= 2), None)
    ck(pair, f'an NPC tribe with two members ({ {k: len(v) for k, v in by_tribe.items()} })')
    if not pair:
        return ck.result()
    victim, attacker = pair
    if PLAYER:
        own = [i for i, m in hm.items() if m[3] == 'player']
        ck(len(own) >= 2, f'the player tribe has two members ({len(own)})')
        if len(own) < 2:
            return ck.result()
        victim = own[-1]
        p0 = hm[victim][2]
        attacker = min(npc, key=lambda i: math.dist(npc[i][2], p0))
        npc = {**npc, victim: hm[victim]}
    if EAT:
        eat_then_die(host, guest, ck, victim, attacker)
        ck.faults(host=host, guest=guest)
        return ck.result()
    if REJOIN:
        rejoin_with_body(host, guest, ck, victim, attacker)
        ck.faults(host=host, guest=guest)
        return ck.result()
    p = npc[victim][2]
    print(f'  victim #{victim} at {worlddiff._fmt(p)}; host citizens there {len(citizens_at(host, p))}, guest '
          f'{len(citizens_at(guest, p))}', flush=True)
    tribe = npc[victim][3]
    if ALL:  # every member of that tribe: the tribe is beaten (its village raided to the end)
        # the attacker: a member of another tribe (the player's), so every one of the tribe can be hit
        att = next((i for i, m in hm.items() if m[3] != tribe), attacker)
        for v in by_tribe[tribe]:
            print(f'  host: #{v}', host.call(f'coop.tribe pincer {v} {att} 1000')['out'].strip(), flush=True)
    else:
        print('  host:', host.call(f'coop.tribe pincer {victim} {attacker} 1000')['out'].strip(), flush=True)
    t0, bad, host_body, in_guest_list = time.time(), [], 0, []
    while time.time() - t0 < (60 if ALL else 25):
        t = time.time() - t0
        hc, gc = citizens_at(host, p, 6), citizens_at(guest, p, 21)
        # BUG-0620 (--player): a body lies still, its mirror where it is (3 m); the village's other members are near
        lone = [q for q in hc if not any(math.dist(q, g) <= (3 if PLAYER else 15) for g in gc)]
        in_list = victim in members(host)
        if PLAYER and t > 3 and victim in members(guest):  # BUG-0620: the guest's body is no member (not selectable)
            in_guest_list.append(round(t, 1))
        if hc and not in_list:
            host_body += 1
        print(f'    {t:4.1f} s: host citizens within 6 m {len(hc)} (victim in its tribe: {in_list}), guest within 21 m '
              f'{len(gc)}, host without a partner {len(lone)}', flush=True)
        if lone:
            bad.append(t)
        time.sleep(2)
    print(f'  host samples with a body out of the member list: {host_body}', flush=True)
    print('  host: ' + next((x for x in host.call('coop.tribe')['out'].splitlines() if 'tribe(host)' in x), ''), flush=True)
    if PLAYER:
        ck(host_body > 0, f'the host\'s game kept the body of its member out of the member list ({host_body} samples)')
        ck(not in_guest_list, f'the body is no member of the guest\'s tribe either ({in_guest_list[:4]})')
    ck(not bad, f'every host citizen at the victim\'s place has a partner on the guest ({len(bad)} samples not: {bad[:5]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
