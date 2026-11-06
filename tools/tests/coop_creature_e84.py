#!/usr/bin/env python3
"""Creature stage: a save made while the partner's puppet existed, loaded again after a restart
(a players' report, 2026-10-25: "These guys appeared after I loaded save and crashed my game after I
killed them"; the host crashed 3 times at 0xD48634, CreatureStageEventHandler reading
(+0xE84)->+0x32C of a creature without archetype tuning).

1. Two games play together; the host saves (the puppet of the guest is in the world).
2. Both games are restarted (like after a crash); the host loads that save alone, with the removal
   of such creatures switched off (`coop.npc strays off`): the puppet is back as a plain game
   creature without tuning (+0xE84 = 0), in a herd like ours (no archetype, no nest).
3. The host attacks it: the game's attack message (0xD335362C {attacker, target, 0}, the one the
   creature attack vt 0xB8 posts) goes through the game's dispatcher to every creature-stage
   listener, CreatureStageEventHandler among them. Before the fix the host crashed at 0xD48634.
4. The removal on again (as in normal play): the creature is gone within seconds.
5. The guest joins: its puppet gets a herd of its species, reused after a rejoin (a new herd per
   spawn stayed in the world and in every save).
BUG-0385 (step 2): such a creature is no NPC of the host's world (`coop.npc list`): the host gave it an id
and sent it to the guests in the frames before its removal (SPORE_FIX_OFF=npc.stray_ids fails).

  SPORE_LANE=2 python3 tools/tests/coop_creature_e84.py
"""
import re
import time

import lane
import twoinst
from twoinst import Checks, Console, lane_state, sh, start, wait_guest_in, wait_up


def untuned(con):
    """(ours, other) creatures without tuning, the lines of the others"""
    out = con.call('coop.npc untuned')['out']
    m = re.search(r'untuned \d+ \(ours (\d+), other (\d+)\)', out)
    other = [l for l in out.splitlines() if 'untuned' in l and ' ours' not in l and l.startswith('  creature')]
    return (int(m.group(1)), int(m.group(2)), other) if m else (None, None, [])


def archless_herds(con):
    m = re.search(r'without an archetype (\d+)', con.call('coop.npc archless')['out'])
    return int(m.group(1)) if m else -1


def guest_species(host):
    m = re.search(r'puppet pos=.* species=([0-9A-F]+)', host.call('coop.status')['out'])
    return m.group(1) if m else None


def wait_puppet(host, t=40):
    t0 = time.time()
    while time.time() - t0 < t:
        sp = guest_species(host)
        if sp and f'species={sp}' in host.call(f'coop.npc creatures {sp}')['out']:
            return sp
        time.sleep(1)
    return None


def close_boxes(host, secs):
    """the "game saved" box stops the host's frames: close it like wait_guest_in does"""
    t0 = time.time()
    while time.time() - t0 < secs:
        f1 = twoinst.host_frame(host)
        time.sleep(0.7)
        if twoinst.host_frame(host) == f1:
            host.call('key enter')


def restart_and_load(slot, planet, before_load=()):
    """both games again (guest first, like twoinst.start), the host loads GameN/Planet"""
    for m in twoinst._monitors:
        m.stop = True
    sh('kill')
    time.sleep(2)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    for _ in range(180):
        if 'GalaxyGameEntry' in host.call('game.mode')['out']:
            break
        time.sleep(1)
    host.call('wait.frames 240 90000')
    for cmd in before_load:
        print('  ', host.call(cmd)['out'].strip(), flush=True)
    host.call(f'coop.load {slot} {planet}')
    t0 = time.time()
    while time.time() - t0 < 240 and not host.call('coop.avatar')['ok']:
        time.sleep(2)
    twoinst.watch(lane.HOST, lane.GUEST)
    # the world is up, the respawn cutscene of a fresh load (if any) ends
    for _ in range(30):
        if 'cinematic' not in host.call('coop.status')['out'].splitlines()[1]:
            break
        time.sleep(1)
    time.sleep(3)
    return host, guest


def main():
    host, guest = start()
    ck = Checks()
    st = lane_state()
    slot = 'Game0' if st.get('slot', 9) == 9 else f'Game{st["slot"]}'
    planet = (st.get('planet') or 'Game0/Scalinus').split('/')[1]
    sp = wait_puppet(host)
    ck(sp is not None, f'the host has the guest\'s puppet (species {sp})')
    # a save as before BUG-0020 (the puppet and its herd in it): what older saves hold
    host.call('coop.savestrip 0')
    out = host.call('coop.save')['out']
    close_boxes(host, 12)  # (the save is written a few frames after the request)
    host.call('coop.savestrip 1')
    ck('saved' in out, f'the host saved with the puppet in the world ({slot}/{planet}: {out.strip()})')

    # 2. after a restart the host loads that save alone, the removal switched off
    host, guest = restart_and_load(slot, planet, before_load=['coop.npc strays off'])
    ours, other, lines = untuned(host)
    for l in lines:
        print('   ', l.strip())
    ck(other and other >= 1, f'the save brought the puppet back as a game creature without tuning ({other})')
    host.call(lane.HOST_CMD)
    time.sleep(3)
    ptr = lines[0].split()[1] if lines else None

    def npc_id():
        for l in host.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) ([0-9A-F]+) ', l)
            if m and ptr and m.group(2) == ptr:
                return m.group(1)
        return None
    # BUG-0385: the host never makes it an NPC it would send to the guests (they cannot spawn its species:
    # "n missing on the guest"); SPORE_FIX_OFF=npc.stray_ids fails here
    nid = npc_id()
    ck(nid is None, f'[BUG-0385] the host does not track it as an NPC of its world (#{nid})')
    host.call('coop.fix npc.stray_ids off')  # the attack below needs it as an NPC (0.7.8 tracked it)
    time.sleep(2)
    nid, survived = npc_id(), False
    ck(nid is not None, f'the host simulates it as an NPC (#{nid}, npc.stray_ids off)')

    # 3. the host attacks it: the game's attack message reaches CreatureStageEventHandler
    if nid:
        try:
            out = host.call(f'coop.npc attackmsg {nid}')['out']
        except OSError as e:  # the game died inside the call
            out = f'no answer: {e}'
        time.sleep(3)
        survived = not out.startswith('no answer') and twoinst.alive(lane.HOST)
        ck(survived, f'the host survives an attack on it ({out.strip()})')
    if not nid or not survived:
        print('  (the host crashed: see the crash report in its game log)', flush=True)
        return ck.result()

    # 4. normal play: such creatures are removed
    host.call('coop.npc strays on')
    host.call('coop.fix npc.stray_ids on')
    time.sleep(3)
    ours, other, _ = untuned(host)
    rm = re.search(r'strays_removed=(\d+)', host.call('coop.npc')['out'])
    ck(other == 0 and rm and int(rm.group(1)) >= 1,
       f'with the removal on it is gone (other untuned {other}, removed {rm.group(1) if rm else "?"})')

    # 5. the guest joins: its puppet reuses the herd of its species
    h0 = archless_herds(host)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joins the loaded world')
    ck(wait_puppet(host) is not None, 'the host has the guest\'s puppet again')
    h1 = archless_herds(host)
    guest.call('coop.leave')
    time.sleep(4)
    guest.call(lane.JOIN)
    wait_guest_in(host, guest, 120)
    wait_puppet(host)
    h2 = archless_herds(host)
    # the herd the save brought back is of the species the game gave the unknown creature after the
    # restart (the guest's creation is registered only when it joins): the first spawn may add one
    ck(h1 <= h0 + 1 and h2 == h1, f'no new herd per puppet spawn (herds without an archetype {h0} -> {h1} -> {h2})')
    ours, other, _ = untuned(host)
    ck(other == 0, f'no creature without tuning but ours ({ours} ours, {other} other)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
