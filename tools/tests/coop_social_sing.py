#!/usr/bin/env python3
"""BUG-1241 (players' video): the host sings with its creature (the social stance, key 1, nobody engaged) next to the
guest; the guest's creature must NOT sing with it, and once the guest runs off its creature must run, on both games.

Creature fixture. The guest stands 4 m from the host. Judged on what the games play (`coop.npc cur`: the game's own
current animation of the own avatar, `cur avatar`, and of each partner's puppet, `cur p<id>`) and on the pictures
(key frames):

  1. the host sings (a real key press, as the player): its puppet on the guest's game sings (the relay, expected);
     neither the guest's own avatar nor the host's puppet of the guest plays a social ability's animation
  2. the guest runs (W held for 6 s): the host's puppet of it does not keep a social animation while it runs, and the
     guest's own avatar runs
  3. 20 s later nothing on either game still plays a social animation of the guest

  coop_social_sing.py
"""
import re
import time

from harness import avatar_pos, waiter
from twoinst import Checks, keyframe, set_stance, shot, start

wait = waiter(step=0.5)


def hold(con, vk, t=0.3):
    con.call(f'key {vk} down')
    time.sleep(t)
    con.call(f'key {vk} up')


def social_anims(con):
    return {m.group(1) for m in re.finditer(r'^ab \d+ type=0x[0-9A-F]+ key=1 .*anim=([0-9A-F]{8})',
                                            con.call('coop.social abil -1')['out'], re.M)}


def cur(con, who):
    m = re.search(rf'^cur {who} anim=([0-9A-F]{{8}})', con.call('coop.npc cur')['out'], re.M)
    return m.group(1) if m else None


def sample(secs, probes, into):
    """probes: {name: (con, who)}; into: {name: [anim, ...]}"""
    t0 = time.time()
    while time.time() - t0 < secs:
        for name, (con, who) in probes.items():
            a = cur(con, who)
            if a:
                into.setdefault(name, []).append(a)
        time.sleep(0.15)


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    hp = avatar_pos(host)
    if hp:
        guest.call('coop.abilities goto %.1f %.1f %.1f' % (hp[0] + 4, hp[1], hp[2]))
    time.sleep(4)
    gp = avatar_pos(guest)
    d = sum((a - b) ** 2 for a, b in zip(hp, gp)) ** 0.5 if hp and gp else None
    print(f'  host {hp} guest {gp} d={d}')
    ck(d is not None and d < 12, f'the guest stands next to the host ({d and round(d, 1)} m)')
    ck(set_stance(host, 'social'), 'the host is in the social stance')
    sing_h, sing_g = social_anims(host), social_anims(guest)
    print(f'  social animations: host {sorted(sing_h)}, guest {sorted(sing_g)}')
    social = sing_h | sing_g
    ck(social, 'the creatures have social abilities')
    probes = {'guest_avatar': (guest, 'avatar'), 'host_puppet_of_guest': (host, 'p' + gid),
              'guest_puppet_of_host': (guest, 'p0'), 'host_avatar': (host, 'avatar')}
    before = {}
    sample(2.0, probes, before)
    print('  before:', {k: sorted(set(v)) for k, v in before.items()})

    # 1. the host sings
    sung = {}
    hold(host, 0x31)
    sample(1.5, probes, sung)
    keyframe('host_sings', 'the host sings (its puppet on the guest sings); the guest\'s creature does NOT sing on either game', settle=0)
    sample(4.0, probes, sung)
    print('  while the host sings:', {k: sorted(set(v)) for k, v in sung.items()})
    ck(social & set(sung.get('host_avatar', [])), "the host's own creature sings (the key press worked)")
    ck(social & set(sung.get('guest_puppet_of_host', [])), "the guest's game shows the host singing (its puppet)")
    ck(not (social & set(sung.get('guest_avatar', []))),
       f"the guest's own creature does not sing with the host ({sorted(social & set(sung.get('guest_avatar', [])))})")
    ck(not (social & set(sung.get('host_puppet_of_guest', []))),
       f"the host's game does not show the guest singing ({sorted(social & set(sung.get('host_puppet_of_guest', [])))})")
    print('  host effects:', host.call('coop.effects map')['out'].strip()[:600])
    print('  guest effects:', guest.call('coop.effects map')['out'].strip()[:600])

    # 2. the guest runs off
    ran = {}
    guest.call('key 87 down')  # W
    sample(2.0, probes, {})
    sample(4.0, probes, ran)
    keyframe('guest_runs', "the guest runs off: its creature runs on both games, no sing pose, no rings", settle=0)
    guest.call('key 87 up')
    print('  while the guest runs:', {k: sorted(set(v)) for k, v in ran.items()})
    gp2 = avatar_pos(guest)
    moved = sum((a - b) ** 2 for a, b in zip(gp, gp2)) ** 0.5 if gp and gp2 else 0
    ck(moved > 3, f'the guest ran ({moved:.1f} m)')
    ck(not (social & set(ran.get('host_puppet_of_guest', []))),
       f"the host's game shows the guest running, not singing ({sorted(set(ran.get('host_puppet_of_guest', [])))})")
    ck(not (social & set(ran.get('guest_avatar', []))), "the guest's own creature runs, not sings")

    # 3. later
    time.sleep(15)
    later = {}
    sample(4.0, probes, later)
    print('  20 s later:', {k: sorted(set(v)) for k, v in later.items()})
    ck(not (social & set(later.get('host_puppet_of_guest', []))), "20 s later the host's game shows no singing guest")
    ck(not (social & set(later.get('guest_avatar', []))), "20 s later the guest's creature does not sing")
    print('  host effects later:', host.call('coop.effects map')['out'].strip()[:600])
    keyframe('later', 'later: nobody sings, no rings effect on the guest\'s creature on either game', settle=0)
    shot(host, 'social_sing_host.png')
    shot(guest, 'social_sing_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
