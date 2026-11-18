#!/usr/bin/env python3
"""BUG-1241 (players' video, second look): after the guest befriended creatures by singing, the guest's creature in
the HOST's game kept the gold sing rings for about 3 minutes (while it ran, fought, stood with its mouth shut).

The guest plays its OWN social games, as a player does (creature fixture): the social stance, a click on a Kook, then
for every prompt of the Kook the sing key (1) until the Kook is impressed; `--games N` Kooks in a row, `--spam` the
sing key pressed again and again around the prompts (a player who keeps singing). Then the guest stands, runs (W)
and stands again for a minute. Read on both games every ~0.5 s:
  * `coop.npc cur`: the guest's own avatar (`cur avatar`) and the host's puppet of it (`cur p<id>`): the animation,
    the ability the creature performs now (+0xE8C, `ability=`, -1 none) and its animation;
  * `coop.effects map`: the pool effects of the guest's avatar and of the host's puppet, with `active=`.
Checked: once the guest's own creature performs nothing, the host's puppet performs no ability either (no stuck
ability, which keeps its sing state), plays no social animation, and keeps no active pool effect the guest's own
creature does not have; and the key frames (the rings must not hang over the guest's creature on the host's screen).
  coop_puppet_after_social.py [--games N] [--spam]
SPORE_NET / SPORE_FPS as in every test (loss, a slow game)."""
import re
import sys
import time

from coop_creature_social import KOOK, hold, num, social
from coop_creature_social_ally import engage, guest_list, host_list, pos
from twoinst import Checks, keyframe, shot, start

GAMES = int(sys.argv[sys.argv.index('--games') + 1]) if '--games' in sys.argv else 1
SPAM = '--spam' in sys.argv
SANG = []  # the first song is shot (key frame)


def cur(con, who):
    """(anim, ability index in use, its anim) of `who` (avatar | p<id>) in con's game"""
    m = re.search(rf'^cur {who} anim=([0-9A-F]{{8}}) .*?ability=(-?\d+) ability_anim=([0-9A-F]{{8}})',
                  con.call('coop.npc cur')['out'], re.M)
    return (m.group(1), int(m.group(2)), m.group(3)) if m else None


def pools(con, who):
    """{key: active} of the pool effects of `who` ('avatar' or the puppet's player id) in con's game"""
    out, sec = {}, None
    for line in con.call('coop.effects map')['out'].splitlines():
        m = re.match(r'^(avatar|puppet (\d+)) ', line)
        if m:
            sec = 'avatar' if m.group(1) == 'avatar' else m.group(2)
            continue
        m = re.match(r'^\s+b\d+ key=([0-9A-F]{8}) value=\S+ active=(\d)', line)
        if m and sec == who:
            out[m.group(1)] = m.group(2) == '1'
    return out


def social_anims(con):
    return {m.group(1) for m in re.finditer(r'^ab \d+ type=0x[0-9A-F]+ key=[1-4] .*anim=([0-9A-F]{8})',
                                            con.call('coop.social abil -1')['out'], re.M)}


def play_game(host, guest, nid):
    """one social game of the guest with Kook #nid, answered with the sing key: impressed on the host"""
    if not engage(guest, nid, 40):
        print(f'  #{nid}: not engaged', flush=True)
        return False
    s = social(guest)
    last_in = num(r'prompts_in=(\d+)', s, 0)
    t0 = time.time()
    presses = 0
    while time.time() - t0 < 70:
        if host_list(host).get(nid, (0, 0, 0, False))[3]:
            print(f'  #{nid}: impressed after {time.time() - t0:.0f} s, {presses} sing presses', flush=True)
            return True
        s = social(guest)
        pin = num(r'prompts_in=(\d+)', s, 0)
        if pin > last_in or SPAM:
            last_in = pin
            time.sleep(1.2)  # (the Kook performs its prompt)
            for _ in range(6 if SPAM else 4):
                a0 = num(r'acts_sent=(\d+)', social(guest), 0)
                hold(guest, 0x31)
                presses += 1
                time.sleep(0.8)
                sent = num(r'acts_sent=(\d+)', social(guest), 0) > a0
                if sent and not SANG:
                    SANG.append(nid)
                    keyframe('sings', 'the guest sings to a Kook: its creature sings with the song\'s rings on both screens '
                             '(the guest\'s own and the host\'s puppet of it)', settle=0)
                if sent and not SPAM:
                    break
        if num(r'engaged=(-?\d+)', s, -1) != nid and time.time() - t0 > 5:
            engage(guest, nid, 15)
        time.sleep(0.4)
    print(f'  #{nid}: not impressed in 70 s ({presses} presses); guest {social(guest).strip()[:300]}', flush=True)
    return False


def watch(host, guest, gid, secs, rows, label):
    t0 = time.time()
    while time.time() - t0 < secs:
        g, h = cur(guest, 'avatar'), cur(host, 'p' + gid)
        gp, hp = pools(guest, 'avatar'), pools(host, gid)
        rows.append((label, round(time.time() - t0, 1), g, h, {k for k, v in gp.items() if v}, {k for k, v in hp.items() if v}))
        time.sleep(0.4)


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
        c.call('coop.effects log on')  # (every effect the creatures make, in the game logs: which one is the rings)
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    time.sleep(2)
    gl = guest_list(guest)
    kooks = [i for i, v in host_list(host).items() if v[0] == KOOK and not v[2] and v[1] > 0 and not v[3] and gl.get(i, (False,))[0]]
    ck(kooks, f'the guest mirrors Kooks it can befriend ({kooks})')
    if not kooks:
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.npc gonpc {kooks[0]} 8')
    time.sleep(1)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 3, hp[1], hp[2]))
    time.sleep(3)
    sing = social_anims(guest) | social_anims(host)
    print(f'  social animations {sorted(sing)}; puppet before: {cur(host, "p" + gid)}', flush=True)

    won = []
    for nid in kooks:
        if len(won) >= GAMES:
            break
        if play_game(host, guest, nid):
            won.append(nid)
    ck(won, f'the guest befriended {len(won)} Kook(s) by singing ({won})')
    t_end = time.time()
    keyframe('won', 'the guest just befriended a Kook by singing: on the host\'s screen the guest\'s creature may show the '
             'song\'s rings now (its own game shows them too)', settle=0)
    rows = []
    watch(host, guest, gid, 12, rows, 'stand')
    keyframe('stand', 'the guest stands silent after its song: NO gold rings over the guest\'s creature on the host\'s '
             'screen, nor on the guest\'s own', settle=0)
    guest.call('key w down')
    watch(host, guest, gid, 8, rows, 'run')
    keyframe('run', 'the guest runs off: the guest\'s creature runs on both screens, no rings, no sing pose', settle=0)
    guest.call('key w up')
    watch(host, guest, gid, 40, rows, 'later')
    keyframe('later', 'a minute after the song: no rings over the guest\'s creature on either screen', settle=0)
    shot(host, 'after_social_host.png')
    shot(guest, 'after_social_guest.png')

    # what the host's puppet does once the guest's own creature performs nothing (8 s grace after the song)
    late = [r for r in rows if time.time() - t_end > 0 and (r[0] != 'stand' or r[1] > 8)]
    stuck = [(r[0], r[1], r[3]) for r in late if r[2] and r[3] and r[2][1] < 0 and r[3][1] >= 0]
    sung = [(r[0], r[1], r[3][0]) for r in late if r[3] and r[3][0] in sing and not (r[2] and r[2][0] in sing)]
    extra = {}
    for r in late:
        for k in r[5] - r[4]:
            extra.setdefault(k, []).append((r[0], r[1]))
    for r in rows[::5]:
        print(f'    {r[0]:5} {r[1]:5.1f} guest {r[2]} pools {sorted(r[4])} | host puppet {r[3]} pools {sorted(r[5])}', flush=True)
    print(f'  puppet: ability stuck {len(stuck)} of {len(late)} samples {stuck[:3]}; social anim {len(sung)} {sung[:3]}; '
          f'pool keys only on the puppet {({k: (len(v), v[:2]) for k, v in extra.items()})}', flush=True)
    print(f'  host: {social(host).strip()[:300]}', flush=True)
    print('  host: ' + host.call('coop.effects')['out'].strip().splitlines()[0], flush=True)
    print('  guest: ' + guest.call('coop.effects')['out'].strip().splitlines()[0], flush=True)
    if won:
        ck(not stuck, f'the host\'s puppet of the guest performs no ability once the guest\'s own creature performs none '
           f'({len(stuck)} of {len(late)} samples: {stuck[:3]})')
        ck(not sung, f'the host\'s puppet plays no social animation the guest\'s own creature does not ({sung[:3]})')
        lasting = {k: v for k, v in extra.items() if len(v) >= 10}
        ck(not lasting, f'the host\'s puppet keeps no active pool effect the guest\'s own creature does not have ({lasting})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
