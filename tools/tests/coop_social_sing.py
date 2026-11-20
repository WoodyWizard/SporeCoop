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

BUG-1346 (2026-11-20, fix/w60-dna), why three variants never judged or failed:
* --to-partner was INCONCLUSIVE in every run since 0.7.15.1: the game starts no social game with a puppet (it runs
  no AI to answer the click's stimulus, BUG-1241) and since 0.7.15.2 the click in the social stance does not even
  pick the partner (it goes to the creature behind it, BUG-1250). The host's song is no precondition there: the
  variant judges what the guest's creature plays on both games after the host's click and sing keys (printed
  whether a song happened, e.g. with a Kook behind the partner).
* at 10 fps (social_sing_own_lowfps) the camera drags of `aim` take 17 of the game's frames (1.7 s) and the test
  read the screen point 0.5 s after each drag, while the camera still turned: the click hit the ground and the host
  walked off (lab 20261120-041304, -214823, 0.7.15.2 -144132 / -134852: the host 1.5 to 30 m away, no relation
  event, no song). The aim now waits for the game's frames after each drag and for the point to stand still, and
  the host clicks the Kook again when no song came after 8 presses (a player clicks again).
* the walk start 04866D8E (the start clip of the idle walk 04866DAB the game's own Creature_ChooseIdleAnim plays,
  docs/re/creature.md) on the puppet was taken for a foreign animation when the own game's samples caught only the
  loop: the start lasts less than a sample round (4 console reads) and fell into the unsampled 2 s after W went
  down. The run's first 2 s of the own avatar now count, and a start clip counts as the own game's when its loop is
  among them. Social animations stay foreign in any case.
"""
import re
import sys
import time

import creations_state
from harness import avatar_pos, waiter
from coop_creature_partner_pass import W, H, aim, targets
from coop_creature_social import engage
from twoinst import Checks, keyframe, set_stance, shot, start

KOOK = '065CB8AB'  # the fixture's Kook nest: a species that takes part in a social game
FLAG = lambda k: k in sys.argv
TO_PARTNER, REVERSE, OWN, FLOCK, WALK = (FLAG(k) for k in ('--to-partner', '--reverse', '--own', '--flock', '--walk'))
IDLE = {'02481DE5', '027844AD'}  # the fixture creature's stand / idle animations
# BUG-1346: a clip's start -> its loop (idle_walk_lookat, docs/re/creature.md: Creature_ChooseIdleAnim): the own game
# played the start when it played the loop; a sample round can miss the short start
CLIP_START = {'04866D8E': '04866DAB'}
OLD = '--old' in sys.argv  # BUG-1346: the test before its fix (aim without waiting, one click, the old judgement)

wait = waiter(step=0.5)


def foreign(shown, own, base):
    """animations a puppet showed that the player's own game did not play (BUG-1346: a start clip whose loop the own
    game played counts as played)"""
    own = set(own) | set(base)
    return {a for a in set(shown) - own if CLIP_START.get(a) not in own}


def frames(con, n):
    """wait n of the game's frames (a camera drag is 17 frames, a click 6: at 10 fps 1.7 s and 0.6 s)"""
    con.call(f'wait.frames {int(n)} {max(5000, int(n) * 1000)}')


def aim_settled(con, key):
    """aim (coop_creature_partner_pass) that waits for the game: every camera drag done (its frames) before the
    screen point is read, and the point standing still over 4 frames before it is used; (dist, x, y) or None"""
    if OLD:
        return aim(con, key)
    o = None
    for _ in range(20):
        o = targets(con).get(key)
        if o and abs(o[1] - W // 2) < 70 and 150 < o[2] < H - 220:
            frames(con, 4)
            o2 = targets(con).get(key)
            if o2 and abs(o2[1] - o[1]) <= 12 and abs(o2[2] - o[2]) <= 12:
                return o2
            continue  # (the camera or the creature still moves)
        dx = 380 if not o else max(-300, min(300, int((o[1] - W // 2) * 0.6)))
        con.call(f'drag {W // 2} {H // 2} {W // 2 + dx} {H // 2} right')
        frames(con, 20)
        time.sleep(0.2)
    return o if o and 20 < o[1] < W - 20 and 90 < o[2] < H - 170 else None


def click_target(host, target):
    """the host's click on target in the social stance (the camera turned onto it first); True when clicked"""
    for _ in range(6):
        o = aim_settled(host, target)
        if o:
            host.call(f'click {o[1]} {o[2]}')
            if not OLD:
                frames(host, 8)  # (the click is pressed and released over 6 frames)
            time.sleep(1.0)
            return True
    return False


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
        engaged = bool(kook or TO_PARTNER) and click_target(host, target)
    ck(engaged, f'the {sname} engaged {target} in the social stance')
    if WALK:
        other.call('key w down')
    sung = {}
    t_sing = time.time()
    for i in range(24):  # the social game starts a moment after the click (the Kook's answer): a player presses again
        if i in (8, 16) and not REVERSE and not OLD and engaged:
            # BUG-1346: no song after 8 presses: the click missed (or the Kook walked off): a player clicks again
            print(f'  (no song after {i} presses: the host clicks {target} again)', flush=True)
            click_target(host, target)
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
    if not sang and TO_PARTNER and not OLD:
        # BUG-1346: no social game with a partner (BUG-1241: a puppet runs no AI to answer; BUG-1250: the click in the
        # social stance goes to the creature behind the partner): the guest's side is judged without a song
        print('  (no social game with the partner, as the game and BUG-1250 have it: the guest\'s side judged without a song)')
    elif not sang:
        ck.faults(host=host, guest=guest)
        return ck.inconclusive(f'the {sname}\'s creature never sang in 40 s (the social game did not start)')
    print(f'  (the {oname}\'s game shows the {sname} singing: {bool(social & set(sung.get("other_puppet_of_singer", [])))}; BUG-1244)')
    ck(not (social & set(sung.get('other_avatar', []))),
       f"the {oname}'s own creature does not sing with the {sname} ({sorted(social & set(sung.get('other_avatar', [])))})")
    ck(not (social & set(sung.get('singer_puppet_of_other', []))),
       f"the {sname}'s game does not show the {oname} singing ({sorted(social & set(sung.get('singer_puppet_of_other', [])))})")
    if OLD:
        extra = set(sung.get('singer_puppet_of_other', [])) - set(sung.get('other_avatar', [])) - base
    else:
        extra = foreign(sung.get('singer_puppet_of_other', []), sung.get('other_avatar', []), base)
    ck(not extra, f"the {sname}'s game shows the {oname}'s creature only with animations its own game plays ({sorted(extra)})")

    # 2. the other player runs off
    ran, ran0 = {}, {}
    other.call('key w down')
    sample(2.0, probes, ran0)  # (the walk's start: the puppets follow; only the own avatars' samples count, below)
    sample(4.0, probes, ran)
    keyframe('other_runs', f"the {oname} runs off: its creature runs on both games, no sing pose, no rings", settle=0)
    other.call('key w up')
    print('  while the other runs:', {k: sorted(set(v)) for k, v in ran.items()})
    ck(not (social & set(ran.get('singer_puppet_of_other', []))),
       f"the {sname}'s game shows the {oname} running, not singing ({sorted(set(ran.get('singer_puppet_of_other', [])))})")
    ck(not (social & set(ran.get('other_avatar', []))), f"the {oname}'s own creature runs, not sings")
    own_all = set(ran.get('other_avatar', [])) | set(sung.get('other_avatar', []))  # (a walk start variant may fall between samples)
    if not OLD:  # BUG-1346: the own avatar's first 2 s of the run count too (the puppet shows them later)
        own_all |= set(ran0.get('other_avatar', []))
        extra = foreign(ran.get('singer_puppet_of_other', []), own_all, base)
    else:
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
