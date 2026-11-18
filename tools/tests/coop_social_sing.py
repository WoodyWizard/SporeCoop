#!/usr/bin/env python3
"""BUG-1241 (players' video): one player sings with its creature (the social stance, inside a social game with an
animal); the other player's creature must NOT sing with it, nor keep a song or its rings while it runs about, on
either game.

Creature fixture. The other player stands 3 m from the singer, next to a Kook. Judged on what the games play
(`coop.npc cur`: the game's own current animation of the own avatar, `cur avatar`, and of each partner's puppet,
`cur p<id>`, with its pack / family bits), on the pool effects of every creature (`coop.effects probe`) and on the
pictures (key frames):

  1. the singer engages the Kook (a click on it in the social stance) and presses the sing key until its creature
     sings (no song in 20 s: INCONCLUSIVE)
  2. while it sings: the other player's own creature plays no social animation, and its puppet on the singer's game
     only animations the other's own game plays
  3. the other player runs (W held); 4. 20 s later: the same, and no puppet keeps a pool effect its player's own
     creature does not have

  coop_social_sing.py [--reverse] [--own] [--flock] [--walk] [--to-partner]
  --reverse     the guest sings (to a Kook mirror, the guest's own social path), the host is the other player
  --own         the guest plays its own creation from My Creations (a players' default, docs/09-coop-design.md 9.4.1)
  --flock       the host summons its flock first (its pack: the game's Summon Flock), the pack stands by
  --walk        the other player walks next to the singer while it sings
  --to-partner  the host clicks the guest's creature instead of a Kook
"""
import re
import sys
import time

import creations_state
from harness import avatar_pos, waiter
from coop_creature_partner_pass import aim
from coop_creature_social import engage
from twoinst import Checks, keyframe, set_stance, shot, start

KOOK = '065CB8AB'  # the fixture's Kook nest: a species that takes part in a social game
FLAG = lambda k: k in sys.argv
TO_PARTNER, REVERSE, OWN, FLOCK, WALK = (FLAG(k) for k in ('--to-partner', '--reverse', '--own', '--flock', '--walk'))
IDLE = {'02481DE5', '027844AD'}  # the fixture creature's stand / idle animations

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


def kooks(con):
    return [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                             con.call('coop.npc list')['out']) if float(m.group(2)) > 0]


def avatar_key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    st0 = creations_state.save(guest) if OWN else None
    try:
        return run(host, guest, ck, gid)
    finally:
        if st0:
            creations_state.restore(guest, st0)


def run(host, guest, ck, gid):
    if OWN:  # the guest's own creature, as a player that made one in the Creature Creator
        k = creations_state.choose_own(guest)
        hk = avatar_key(host)
        own = wait(lambda: (lambda g: g if g and g != hk else None)(avatar_key(guest)), 40)
        print(f'  guest chose its own creation {k}: avatar {own}, host {hk}', flush=True)
        ck(own, 'the guest plays its own creature (another species than the host\'s)')
        time.sleep(6)  # the swap settles (the puppet on the host's game is rebuilt)
    singer, other = (guest, host) if REVERSE else (host, guest)
    sname, oname = ('guest', 'host') if REVERSE else ('host', 'guest')
    s_pup_of_o = 'p0' if REVERSE else 'p' + gid  # the other's puppet on the singer's game
    o_pup_of_s = 'p' + gid if REVERSE else 'p0'
    hk = kooks(host)
    ck(hk, f'the host has Kooks around ({hk[:5]})')
    kook = hk[0] if hk else None
    if kook:  # both players next to the Kook (the host's own Kook; the guest's mirror of it)
        print('  host: ' + host.call(f'coop.npc gonpc {kook} 3.0')['out'].strip(), flush=True)
        time.sleep(1.5)
    hp = avatar_pos(host)
    if hp:
        guest.call('coop.abilities goto %.1f %.1f %.1f' % (hp[0] + 3, hp[1], hp[2]))
    time.sleep(4)
    if FLOCK:
        print('  host: ' + host.call('coop.drama start 8')['out'].strip()[:160], flush=True)  # the game's Summon Flock
        time.sleep(15)
        print('  host: ' + host.call('coop.drama')['out'].strip().replace('\n', ' | ')[:300], flush=True)
    for c in (host, guest):
        c.call('coop.effects log on')
    print("  host's puppet of the guest: " + host.call('coop.npc cur')['out'].replace('\n', ' | ')[:300], flush=True)
    ck(set_stance(singer, 'social'), f'the {sname} is in the social stance')
    social = social_anims(host) | social_anims(guest)
    print(f'  social animations: {sorted(social)}')
    probes = {'other_avatar': (other, 'avatar'), 'singer_puppet_of_other': (singer, s_pup_of_o),
              'other_puppet_of_singer': (other, o_pup_of_s), 'singer_avatar': (singer, 'avatar')}
    before = {}
    sample(2.0, probes, before)
    print('  before:', {k: sorted(set(v)) for k, v in before.items()})
    base = set().union(*[set(v) for v in before.values()]) | IDLE

    # 1. the singer engages (a click in the social stance) and sings
    target = f'p{gid}' if TO_PARTNER else f'n{kook}'
    if REVERSE:
        engaged = bool(kook) and engage(guest, f'n{kook}')
    else:
        engaged = False
        for _ in range(6):
            o = aim(host, target) if kook or TO_PARTNER else None
            if o:
                host.call(f'click {o[1]} {o[2]}')
                time.sleep(1.0)
                engaged = True
                break
    ck(engaged, f'the {sname} engaged {target} in the social stance')
    if WALK:
        other.call('key w down')
    sung = {}
    t_sing = time.time()
    for _ in range(24):  # the social game starts a moment after the click (the Kook's answer): a player presses again
        hold(singer, 0x31)
        sample(1.5, probes, sung)
        if social & set(sung.get('singer_avatar', [])):
            break
    keyframe('sings', f'the {sname} sings; the {oname}\'s creature does NOT sing on either game', settle=0)
    sample(4.0, probes, sung)
    if WALK:
        other.call('key w up')
    sang = bool(social & set(sung.get('singer_avatar', [])))
    print(f'  (the {sname} sang: {sang}, after {time.time() - t_sing:.1f} s)')
    print('  while singing:', {k: sorted(set(v)) for k, v in sung.items()})
    print(f'  the {sname}\'s game: ' + singer.call('coop.npc cur')['out'].replace('\n', ' | ')[:300], flush=True)
    if not sang:
        ck.faults(host=host, guest=guest)
        return ck.inconclusive(f'the {sname}\'s creature never sang in 40 s (the social game did not start)')
    print(f'  (the {oname}\'s game shows the {sname} singing: {bool(social & set(sung.get("other_puppet_of_singer", [])))}; BUG-1244)')
    ck(not (social & set(sung.get('other_avatar', []))),
       f"the {oname}'s own creature does not sing with the {sname} ({sorted(social & set(sung.get('other_avatar', [])))})")
    ck(not (social & set(sung.get('singer_puppet_of_other', []))),
       f"the {sname}'s game does not show the {oname} singing ({sorted(social & set(sung.get('singer_puppet_of_other', [])))})")
    extra = set(sung.get('singer_puppet_of_other', [])) - set(sung.get('other_avatar', [])) - base
    ck(not extra, f"the {sname}'s game shows the {oname}'s creature only with animations its own game plays ({sorted(extra)})")

    # 2. the other player runs off
    ran = {}
    other.call('key w down')
    sample(2.0, probes, {})
    sample(4.0, probes, ran)
    keyframe('other_runs', f"the {oname} runs off: its creature runs on both games, no sing pose, no rings", settle=0)
    other.call('key w up')
    print('  while the other runs:', {k: sorted(set(v)) for k, v in ran.items()})
    ck(not (social & set(ran.get('singer_puppet_of_other', []))),
       f"the {sname}'s game shows the {oname} running, not singing ({sorted(set(ran.get('singer_puppet_of_other', [])))})")
    ck(not (social & set(ran.get('other_avatar', []))), f"the {oname}'s own creature runs, not sings")
    own_all = set(ran.get('other_avatar', [])) | set(sung.get('other_avatar', []))  # (a walk start variant may fall between samples)
    extra = set(ran.get('singer_puppet_of_other', [])) - own_all - base
    ck(not extra, f"while the {oname} runs the {sname}'s game shows it with its own animations only ({sorted(extra)})")

    # 3. later
    time.sleep(15)
    later = {}
    sample(4.0, probes, later)
    print('  20 s later:', {k: sorted(set(v)) for k, v in later.items()})
    ck(not (social & set(later.get('singer_puppet_of_other', []))), f"20 s later the {sname}'s game shows no singing {oname}")
    ck(not (social & set(later.get('other_avatar', []))), f"20 s later the {oname}'s creature does not sing")
    from coop_brain_rings import mirrored, state
    st = state(host, guest, gid)
    print('  the social win effect 02C4C076 now:', st, flush=True)
    ck(mirrored(st), f"no puppet keeps the social win's effect its player's own creature no longer has ({st})")
    keyframe('later', f'later: nobody sings, no rings on the {oname}\'s creature on either game', settle=0)
    shot(host, 'social_sing_host.png')
    shot(guest, 'social_sing_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
