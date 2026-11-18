#!/usr/bin/env python3
"""A guest befriends an animal whose social game asks for charm, dance or pose, as a player does it (BUG-1242).

Players' video (before 0.7.15): the second player could not befriend the creatures. A creature ability's +0x10 is its
social KIND (1 sing, 2 charm, 3 dance, 4 pose, 0 not social; docs/re/creature.md#creature-social-kinds); the guest's game
took only kind 1 as a social ability (the bar's hook, the keys 1-4), so a prompt to charm, dance or pose could only be
answered by singing (a tenth of its points) and the game failed. The `creature` fixture's nests prompt only to sing:
every older social test passed.

The campaign's Creature world (fixture `creature_cadian`) has nests whose species prompt to the other kinds. The test
reads the guest's social abilities, finds the nearest nest whose prompts it can all answer with at least one other kind
than sing (`coop.social herds`), takes both players there and plays as a player on the guest: the social stance, a click
on an animal, then for each prompt of the animal (the host plays it on its NPC, the guest's mirror performs it) a CLICK
on the ability bar's button of the same kind (`--keys`: the key of that kind instead), until the game ends. An animal
whose prompts were all sing does not count: the next one is tried. Checked on both games: each answer of another kind is
performed (the guest's avatar and the host's puppet of it play its animation), the animal is impressed (the host's NPC's
own flag, the guest's mirror's), the species table counts it on both.
SPORE_FIX_OFF=social.kinds must FAIL (the other kinds' answers do nothing, the game fails).
docs/06-testing.md#testing-social-kinds"""
import math
import re
import sys
import time

from coop_creature_social import hold, num, social, table
from coop_creature_social_ally import engage, guest_list, host_list, social_stance, stance
from harness import avatar_pos as pos
from twoinst import Checks, keyframe, shot, start

KEYS = '--keys' in sys.argv
KIND_NAME = {1: 'sing', 2: 'charm', 3: 'dance', 4: 'pose'}


def herds(host):
    """[(addr, species, members, home, dist, [(kind, type, points)])] of the host's game"""
    out = []
    for m in re.finditer(r'^herd (\S+) species=(\S+) members=(-?\d+) home=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) dist=(\d+) kinds=(\S*)$',
                         host.call('coop.social herds')['out'], re.M):
        kinds = [tuple(int(x, 16) if i == 1 else int(float(x)) for i, x in enumerate(k.split(':')))
                 for k in m.group(8).split(',') if k and k != '?']
        out.append((m.group(1), m.group(2), int(m.group(3)), tuple(float(m.group(i)) for i in (4, 5, 6)), int(m.group(7)), kinds))
    return out


def bar(guest):
    """{social kind: (x, y)} of the bar's social buttons on the guest's screen (social stance): the kinds of the
    abilities in the bar's slots (`coop.social bar`: AbilityBar_ptr) and where their button windows are"""
    slots = {}
    for m in re.finditer(r'^slot row=(\d+) col=(\d+) idx=\d+ type=\S+ kind=(\d+) win=([0-9A-F]{8})', guest.call('coop.social bar')['out'], re.M):
        if 1 <= int(m.group(3)) <= 4 and m.group(4) != '00000000':
            slots[int(m.group(3))] = int(m.group(4), 16)
    if not slots:
        return {}, {}
    o = guest.call('coop.ui where ' + ' '.join('%X' % v for v in slots.values()))['out']
    out = {}
    for k, wid in slots.items():
        m = re.search(r'^0*%X (\d+) (\d+) (\d+) (\d+)' % wid, o, re.M)
        if m:
            out[k] = ((int(m.group(1)) + int(m.group(3))) // 2, (int(m.group(2)) + int(m.group(4))) // 2)
    return out, slots


def modes(host):
    """{npc id: (species, social mode)} as the host's game sees them"""
    return {int(m.group(1)): (m.group(2), int(m.group(3)))
            for m in re.finditer(r'^rel (\d+) species=(\S+) .*? mode=(-?\d+)', host.call('coop.social rel')['out'], re.M)}


def kind_acts(guest):
    m = re.search(r'kind_acts=(\d+),(\d+),(\d+),(\d+),(\d+)', social(guest))
    return [int(x) for x in m.groups()] if m else [0] * 5


def answer(guest, kind, buttons):
    """the player's answer: a click on the bar's button of that kind (or its key)"""
    if stance(guest) != 0:
        social_stance(guest)  # back to the social stance
    if KEYS:
        hold(guest, 0x30 + kind)
        return
    if kind not in buttons:
        buttons.update(bar(guest)[0])
    if kind not in buttons:
        print(f'  (no {KIND_NAME[kind]} button on the bar: {sorted(buttons)})', flush=True)
        return
    x, y = buttons[kind]
    guest.call(f'move {x} {y}')
    time.sleep(0.3)
    guest.call(f'click {x} {y}')


def own_social(guest):
    """{social kind: (type, points, animation)} of the guest's avatar (the best of a kind)"""
    out = {}
    for m in re.finditer(r'^ab \d+ type=0x(\S+) key=(\d+) points=([\d.]+) .*anim=([0-9A-F]{8})',
                         guest.call('coop.social abil -1')['out'], re.M):
        k, pts = int(m.group(2)), float(m.group(3))
        if 1 <= k <= 4 and (k not in out or pts > out[k][1]):
            out[k] = (int(m.group(1), 16), pts, m.group(4))
    return out


def performed(host, guest, gid, anim, secs=2.5):
    """(the guest's avatar, the host's puppet of it) played `anim` within secs"""
    g = h = False
    t0 = time.time()
    while time.time() - t0 < secs and not (g and h):
        a = re.search(r'^cur avatar anim=([0-9A-F]{8})', guest.call('coop.npc cur')['out'], re.M)
        b = re.search(r'^cur p' + gid + r' anim=([0-9A-F]{8})', host.call('coop.npc cur')['out'], re.M)
        g |= bool(a and a.group(1) == anim)
        h |= bool(b and b.group(1) == anim)
        time.sleep(0.1)
    return g, h


PERFORMED = []  # (kind, on the guest, on the host) of every answer of another kind than sing


def play(host, guest, nid, buttons, ck, anims=None, gid='1'):
    """one social game with NPC #nid: (prompt kinds seen, impressed on the host, how it ended)"""
    s = social(guest)
    seen = []
    last_in = num(r'prompts_in=(\d+)', s, 0)
    f0, r0 = num(r'prompt_fails=(\d+)', social(host), 0), num(r'results_in=(\d+)', s, 0)
    pending, prompt_t, tries = None, 0, 0
    t0 = time.time()
    while time.time() - t0 < 90:
        s = social(guest)
        if not seen and time.time() - t0 > 15:
            return seen, False, 'no prompt in 15 s'  # (the host started no game with it)
        if host_list(host).get(nid, (0, 0, 0, False))[3]:
            return seen, True, 'impressed'
        if num(r'prompt_fails=(\d+)', social(host), 0) > f0:
            return seen, False, 'failed (the prompt list is through)'
        if num(r'results_in=(\d+)', s, 0) > r0 and num(r'engaged=(-?\d+)', s, -1) != nid:
            return seen, False, 'refused'
        pin, pnpc = num(r'prompts_in=(\d+)', s, 0), num(r'prompt_npc=(-?\d+)', s, -1)
        if pin > last_in:
            last_in = pin
            if pnpc == nid:
                kind = int(num(r'prompt_kind=(\d+)', s, 0))
                seen.append(kind)
                pending, prompt_t, tries = kind, time.time(), 0
                print(f'  #{nid}: prompt {len(seen)} kind {kind} ({KIND_NAME.get(kind, "?")}) type '
                      f'{re.search(r"prompt_type=(\S+)", s).group(1)} anim {re.search(r"prompt_anim=(\S+)", s).group(1)}', flush=True)
                if len(seen) == 1:
                    keyframe('prompt', f'the guest is in a social game with animal #{nid}: on the guest\'s screen its mirror '
                             'performs the prompt and the social meter is up; on the host\'s the animal performs it next to the guest')
        if pending is not None and time.time() - prompt_t > 1.5:
            k = pending if pending in buttons or KEYS or not buttons else 1  # a kind the bar lacks: the player sings
            a0 = kind_acts(guest)
            answer(guest, k, buttons)
            if k != 1 and anims and anims.get(k):  # what both players see: the guest's creature performs it
                pg, ph = performed(host, guest, gid, anims[k])
                PERFORMED.append((k, pg, ph))
                print(f'  #{nid}: the {KIND_NAME[k]} answer {anims[k]} played on the guest\'s avatar {pg}, on the host\'s '
                      f'puppet {ph}', flush=True)
            else:
                time.sleep(0.8)
            a1 = kind_acts(guest)
            tries += 1
            if a1[k] > a0[k]:
                print(f'  #{nid}: answered {KIND_NAME[k]} ({"key" if KEYS else "click"}), sent', flush=True)
                pending = None
            elif tries >= 4:
                print(f'  #{nid}: the {KIND_NAME[k]} answer ({"key" if KEYS else "click"}) sent nothing in {tries} tries '
                      f'(stance {stance(guest)})', flush=True)
                pending = None
            else:
                time.sleep(1.0)  # (cooling down)
        time.sleep(0.3)
    return seen, False, 'no end in 90 s'


def setup():
    """start the campaign's world; the players' creature differs between starts (the world's species: seen with sing
    only, sing + dance, sing + charm + pose, all four): started again (at most 3 starts) until the guest has another
    social kind than sing and a nest prompts it. (host, guest, the guest's kinds, the nests)"""
    for n in range(3):
        host, guest = start('creature_cadian')
        for c in (host, guest):
            c.call('coop.team hp 1000')
        time.sleep(2)
        # the guest's social abilities: a nest whose prompts it can all answer (its kinds, points at least the prompt's),
        # at least one of them another kind than sing; the nearest one away from the players' own family
        own, t0, t_change = own_social(guest), time.time(), time.time()
        while time.time() - t0 < 40 and time.time() - t_change < 8:  # (the ability list may still grow after the load)
            time.sleep(1)
            o = own_social(guest)
            if o != own:
                own, t_change = o, time.time()
        print(f'  start {n + 1}: the guest\'s social abilities {own} (settled after {t_change - t0:.0f} s)', flush=True)
        fam = [tuple(float(x) for x in m.groups()) for m in re.finditer(
            r'^\s*#\d+ \S+ species=\S+ .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).* family', host.call('coop.npc list')['out'], re.M)]
        hs = [h for h in herds(host) if 80 <= h[4] <= 600 and any(k != 1 for k, _, _ in h[5])
              and all(k in own and own[k][1] >= p for k, _, p in h[5]) and all(math.dist(h[3], f) > 30 for f in fam)]
        hs.sort(key=lambda h: (round(sum(k == 1 for k, _, _ in h[5]) / len(h[5]), 1), h[4]))  # the least sing first
        print(f'  nests prompting the guest\'s other social kinds: {[(h[0], h[4], h[5]) for h in hs[:5]]}', flush=True)
        if hs:
            break
    return host, guest, own, hs


def main():
    host, guest, own, hs = setup()
    ck = Checks()
    ck(hs, 'the world has a nest whose species prompts to another social kind than sing that the guest has (coop.social herds)')
    if not hs:
        ck.faults(host=host, guest=guest)
        return ck.result()
    ids, species, herd = [], None, None
    for herd in hs[:4]:  # (a nest whose animals cannot be socialized now, social mode 0: the next one)
        home = herd[3]
        host.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 6, home[1], home[2]))
        time.sleep(2)
        hp = pos(host)
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 2, hp[1], hp[2]))
        # the nest's animals come (the game spawns a herd's members near a player) and the guest mirrors them
        t0 = time.time()
        seen = {}
        while time.time() - t0 < 30 and not ids:
            time.sleep(2)
            h = next((x for x in herds(host) if x[0] == herd[0]), None)
            species = h[1] if h and h[2] > 0 and h[1] != '00000000' else None
            if not species:
                continue
            md, gl = modes(host), guest_list(guest)
            hl = host_list(host)
            seen = {i: (v[1], v[2], v[3], md.get(i, ('', None))[1], gl.get(i, (False,))[0]) for i, v in hl.items() if v[0] == species}
            ids = [i for i, (hp_, dead, imp, mode, mir) in seen.items() if hp_ > 0 and not dead and not imp and mode and mir]
        print(f'  nest {herd[0]} at {herd[4]} m, species {species}: animals {ids} after {time.time() - t0:.0f} s '
              f'(id: hp, dead, impressed, social mode, mirrored: {seen})', flush=True)
        if ids:
            break
    ck(ids, f'the guest mirrors animals of that nest ({species}: {ids})')
    if not ids:
        ck.faults(host=host, guest=guest)
        return ck.result()
    buttons, slots = {}, {}
    for _ in range(8):  # the bar's social row is up in the social stance (every slot's button laid out)
        social_stance(guest)
        time.sleep(0.6)
        buttons, slots = bar(guest)
        if buttons and set(buttons) == set(slots):
            break
    print(f'  the guest\'s social row: {buttons} (bar slots {({k: hex(v) for k, v in slots.items()})}); stance {stance(guest)}', flush=True)
    want = {k for k, _, _ in herd[5]}
    ck(want <= set(buttons) or KEYS, f'the guest\'s ability bar has a button for every kind the nest asks ({sorted(want)}: {sorted(buttons)})')
    t_before = table(host).get(species, (0, 0, 0))[1]
    anims = {k: v[2] for k, v in own.items()}
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    print(f'  the guest\'s social animations {anims}, client id {gid}', flush=True)

    won, other_kind_game = None, None
    for nid in ids[:6]:
        if won is not None:
            break
        t_before = table(host).get(species, (0, 0, 0))[1]
        if not engage(guest, nid, 40):
            print(f'  #{nid}: not engaged', flush=True)
            continue
        seen, ok, how = play(host, guest, nid, buttons, ck, anims, gid)
        other = any(k != 1 for k in seen)
        print(f'  #{nid}: prompts {[KIND_NAME.get(k, k) for k in seen]} -> {how}; guest {social(guest).strip()}', flush=True)
        print(f'  #{nid}: host {social(host).strip()}', flush=True)
        if other:
            other_kind_game = (nid, ok, how)
            won = nid if ok else won
            break  # the game this test is about was played
        if ok:
            continue  # a game of sing prompts only: try another animal for charm / pose
    ck(other_kind_game is not None, 'the guest played a social game with a prompt to charm, dance or pose')
    ck(PERFORMED and all(g and h for _, g, h in PERFORMED),
       f'every charm / dance / pose answer of the guest is performed on both games (the guest\'s avatar, the host\'s puppet): '
       f'{[(KIND_NAME[k], g, h) for k, g, h in PERFORMED]}')
    if other_kind_game:
        nid, ok, how = other_kind_game
        time.sleep(1.5)
        h, g = host_list(host).get(nid), guest_list(guest).get(nid)
        keyframe('result', f'the social game with #{nid} ended ({how}): on both screens the animal is impressed (green face '
                 'over it on the guest\'s screen), the meter closed')
        ck(h is not None and h[3], f'#{nid}: the host\'s game has the animal impressed after the guest answered its charm / '
           f'dance / pose prompts ({how})')
        ck(g is not None and g[1], f'#{nid}: the guest\'s game shows the animal impressed')
        ht, gt = table(host).get(species), table(guest).get(species)
        print(f'  species {species}: host {ht}, guest {gt} (impressed before {t_before})', flush=True)
        ck(ht is not None and ht[1] > t_before, f'the host\'s species table counts the impression ({t_before} -> {ht})')
        ck(gt == ht, f'the guest\'s species table is the host\'s ({gt})')
    shot(guest, 'social_kinds_guest')
    shot(host, 'social_kinds_host')
    for c in (host, guest):
        m = re.search(r'^own: hp=[-\d.]+/([\d.]+)', c.call('coop.hp')['out'], re.M)
        if m:
            c.call(f'coop.team hp {m.group(1)}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
