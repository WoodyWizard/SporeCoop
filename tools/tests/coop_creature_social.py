#!/usr/bin/env python3
"""Creature stage, guest side of the native game (docs/re/creature.md 8, docs/stability/creature.md):
1) a guest befriends a mirrored NPC through real input: social stance, a click on the NPC (the
   game's social stimulus), a social ability key; the host applies the native result (the species
   is impressed, DNA for the pack, the NPC impressed) and both machines show it (before
   2026-10-23 nothing happened);
2) the species relationship table (impressed / killed / ally / extinct per species) is the host's
   on the guest too: a guest's goal card said "The Kook species is extinct!" after mirrored deaths;
3) the partner's name tag is not drawn over the pause menu and the co-op window."""
import re
import time

from twoinst import Checks, shot, start
from uifind import press, shown

KOOK = '065CB8AB'  # the fixture's Kook nest: a species that can be impressed (tuning: 1 round, 3 for an ally)


def num(rx, text, default=None):
    m = re.search(rx, text)
    return float(m.group(1)) if m else default


def table(con):
    """{species instance: (relation, impressed, killed)}"""
    t = {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4)))
         for m in re.finditer(r'species ([0-9A-F]{8})/\S+ relation=(-?\d+) x=\d+ impressed=(\d+) killed=(\d+)',
                              con.call('coop.social species')['out'])}
    return {k: v for k, v in t.items() if v != (0, 0, 0)}  # an empty record is the same as none


def social(con):
    return con.call('coop.social')['out']


def objs(con):
    d = {}
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'obj':
            d[f[1]] = (float(f[2]), int(f[3]), int(f[4]))
    return d


def in_view(o):
    return o and 20 < o[1] < 1420 and 90 < o[2] < 900


def engage(guest, key, timeout=40):
    """social stance, the avatar steps next to the NPC (coop.social goto: walking there by clicks
    lost the target to the camera and to fleeing animals), then a click on it like a player: the
    click is the game's social stimulus to the NPC, the mod remembers the engaged mirror"""
    guest.call('click 690 998')  # the social stance button (W/2 - 30, H - 82)
    time.sleep(0.3)
    t0 = time.time()
    sweep = 0
    while time.time() - t0 < timeout:
        guest.call(f'coop.social goto {key[1:]}')
        time.sleep(0.5)
        o = objs(guest).get(key)
        if not in_view(o):
            guest.call('drag 720 540 950 540 right')  # look around
            sweep += 1
            if sweep % 8 == 0:
                guest.call('wheel 720 540 -3')
            time.sleep(0.4)
            continue
        guest.call(f'click {o[1]} {o[2]}')
        time.sleep(0.8)
        if re.search(r'engaged=' + key[1:] + r'\b', social(guest)):
            return True
    return False


def hold(con, vk, t=0.3):
    """a key press a player makes (the mod polls the key state: a bare tap can be missed)"""
    con.call(f'key {vk} down')
    time.sleep(t)
    con.call(f'key {vk} up')


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')  # a guest standing at a nest gets attacked now and then
    time.sleep(3)

    # ---- 2) the table is the host's
    ht, gt = table(host), table(guest)
    ck(ht and ht == gt, f'the species table is the host\'s on the guest ({len(ht)} species)')

    # ---- 1) befriend a mirrored Kook
    mirrored = set(re.findall(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' spawned', guest.call('coop.npc list')['out']))
    kooks = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                             host.call('coop.npc list')['out'])
             if m.group(1) in mirrored and float(m.group(2)) > 0]
    ck(kooks, f'the guest mirrors Kooks ({kooks})')
    imp0 = ht.get(KOOK, (0, 0, 0))[1]
    dna0 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'], 0)
    target = None
    for k in kooks[:3]:
        if engage(guest, 'n' + k):
            target = k
            break
    ck(target is not None, f'a click on a mirrored Kook in the social stance engages it (n{target})')
    if target:
        acts0 = num(r'acts_sent=(\d+)', social(guest), 0)
        shot(guest, 'social_engaged.png')
        hold(guest, 0x31)  # 1: the first social ability (sing)
        time.sleep(2.5)
        g = social(guest)
        ck(num(r'acts_sent=(\d+)', g, 0) > acts0, 'key 1 next to it: the guest sings and the action goes to the host')
        for _ in range(4):  # one round for a Kook, two for the alpha
            if num(r'results_in=(\d+)', social(guest), 0) > 0:
                break
            hold(guest, 0x31)
            time.sleep(3)
        g, h = social(guest), social(host)
        print('  host :', h.strip())
        print('  guest:', g.strip())
        ck(num(r'impressed=(\d+)', h, 0) >= 1, 'the host applied the native result (impressed)')
        time.sleep(2)
        ht, gt = table(host), table(guest)
        ck(ht.get(KOOK, (0, 0, 0))[1] == imp0 + 1, f'the Kooks count one more impressed creature: '
                                                     f'{imp0} -> {ht.get(KOOK)}')
        ck(ht == gt, f'the guest\'s table is the host\'s again ({gt.get(KOOK)})')
        dna1 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'], 0)
        ck(dna1 > dna0, f'the pack earned DNA: {dna0} -> {dna1}')
        ck(abs(num(r'dna=([\d.]+)', guest.call('coop.dna')['out'], 0) - dna1) < 0.5, 'the same DNA on the guest')
        ck(num(r'cards_refreshed=(\d+)', g, 0) >= 1, 'the guest\'s goal cards were told')
        shot(guest, 'social_guest.png')
        shot(host, 'social_host.png')
        # the impressed NPC stays impressed on the guest's mirror (NF_IMPRESSED) and refuses a second game
        res0 = num(r'refused=(\d+)', host.call('coop.social')['out'], 0)
        imp1 = num(r'impressed=(\d+)', host.call('coop.social')['out'], 0)
        for _ in range(5):  # the ability cools down for a few seconds
            if 'action sent' in guest.call(f'coop.social act {target} 0')['out']:
                break
            time.sleep(2)
        time.sleep(2)
        hs = host.call('coop.social')['out']
        ck(num(r'refused=(\d+)', hs, 0) > res0 and num(r'impressed=(\d+)', hs, 0) == imp1,
           'an impressed NPC is not impressed twice')

    # ---- 1b) more Kooks until the species is an ally (tuning: 3): the card's ally reward is paid
    # once, by the host, whichever machine's card saw it (both can have the card)
    if target:
        done = {target}
        dna_a = num(r'dna=([\d.]+)', host.call('coop.dna')['out'], 0)
        for k in kooks:
            if table(host).get(KOOK, (0, 0, 0))[0] == 6:
                break
            if k in done:
                continue
            done.add(k)
            # a game the host refuses (the Kook walked out of the social range while the guest
            # played: `refused` of the host's coop.social) is played again with the same Kook: it
            # is neither impressed nor dead. Moving on to the next one ran out of Kooks in 3 of 6
            # runs (BUG-0021)
            for attempt in range(3):
                if not engage(guest, 'n' + k, 45):
                    break
                i0 = num(r'impressed=(\d+)', social(host), 0)
                f0 = num(r'refused=(\d+)', social(host), 0)
                for _ in range(4):  # an alpha takes two rounds; the ability cools down in between
                    hold(guest, 0x31)
                    time.sleep(3)
                    hs = social(host)
                    if num(r'impressed=(\d+)', hs, 0) > i0 or num(r'refused=(\d+)', hs, 0) > f0:
                        break
                    engage(guest, 'n' + k, 15)
                if num(r'impressed=(\d+)', social(host), 0) > i0:
                    break
                print(f'  n{k}: no impression (attempt {attempt + 1}, refused {f0} -> '
                      f'{num(r"refused=(\d+)", social(host), 0)}): played again')
        time.sleep(3)
        ht, gt = table(host), table(guest)
        dna_b = num(r'dna=([\d.]+)', host.call('coop.dna')['out'], 0)
        hs = social(host)
        print(f'  ally: {ht.get(KOOK)} on the host, {gt.get(KOOK)} on the guest; DNA {dna_a} -> {dna_b}; {hs.strip()}')
        ck(ht.get(KOOK, (0,))[0] == 6 and gt.get(KOOK) == ht.get(KOOK), 'the Kooks are the pack\'s allies on both')
        ck(num(r'goal_dna_paid=(\d+)', hs, 0) == 1, 'the ally reward was paid once')
        ck(abs(num(r'dna=([\d.]+)', guest.call('coop.dna')['out'], 0) - dna_b) < 0.5, 'the same DNA on the guest')
        ck(shown(guest, 'allies') is not None, 'the guest\'s card says "The Kook are your allies!"')
        shot(guest, 'social_ally_guest.png')

    # ---- 2) mirrored deaths: the host kills an NPC of another species, the guest replays the death
    # (a mirror's death or removal must not count on the guest's own table: three counted kills
    # make a species "extinct" there); the host's counts arrive and win
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    others = [(m.group(1), m.group(2)) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                                           host.call('coop.npc list')['out'])
              if m.group(2) not in (KOOK, fam) and float(m.group(3)) > 0 and m.group(1) in mirrored]
    if others:
        nid, sp = others[0]
        k0 = table(host).get(sp, (0, 0, 0))[2]
        host.call(f'coop.npc hosthit {nid} 100000')
        time.sleep(3)
        ht, gt = table(host), table(guest)
        print('  guest:', social(guest).strip())
        ck(ht.get(sp, (0, 0, 0))[2] == k0 + 1 and gt.get(sp) == ht.get(sp),
           f'n{nid} died: one kill of {sp} on the host, the same on the guest ({ht.get(sp)} / {gt.get(sp)})')
        ck(ht == gt, 'the tables match after the death')
    ck(not shown(guest, 'species is extinct'), 'no "The ... species is extinct!" card on the guest')

    # ---- 3) name tags under menus
    lab = lambda c: guest.call('coop.status')['out']  # noqa: E731
    if shown(guest, 'You were killed'):  # the death caption waits for OK (docs/re/creature.md 7)
        guest.call('key enter')
        time.sleep(12)
    for _ in range(3):  # Esc opens the pause menu (a first Esc may close a card)
        guest.call('key esc')
        time.sleep(1.5)
        if shown(guest, 'Resume Game'):
            break
    time.sleep(0.6)
    s = lab(guest)
    h0 = num(r'hidden_frames=(\d+)', s, 0)
    time.sleep(1)
    s = lab(guest)
    ck('menu_up=1' in s and num(r'hidden_frames=(\d+)', s, 0) > h0,
       'the pause menu is up: the partner\'s name tag is not drawn')
    shot(guest, 'social_pause_menu.png')
    press(guest, 'Resume Game')
    time.sleep(1)
    s1 = lab(guest)
    h1 = num(r'hidden_frames=(\d+)', s1, 0)
    time.sleep(1)
    ck('menu_up=0' in lab(guest) and num(r'hidden_frames=(\d+)', lab(guest), 0) == h1,
       'menu closed: the name tags are back')
    hold(guest, 0x78)  # F9: the co-op window
    time.sleep(1)
    s = lab(guest)
    ck('window=1' in s, 'the co-op window is open: no name tags over it')
    hold(guest, 0x78)
    time.sleep(1)

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
