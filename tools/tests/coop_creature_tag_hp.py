#!/usr/bin/env python3
"""BUG-1526: each player sees the partner's health in Creature (its name tag carries it).

Player report (0.7.15.5): "I don't see the host's HP, the host doesn't see the guest's HP". The game shows a
creature's health in its rollover, and the hover passes through a partner's creature (puppets.cpp h_HoverFilter: a
click goes to the animal behind it), so neither player could see the other's health.

1. Creature fixture; the guest walks next to the host (`coop.team come`).
2. The guest's game sets its creature's health to half its maximum (`coop.team hp`: the game's own health), the host's
   to a third of its own.
3. Within 3 s the name tag each game draws over the partner (`coop.find labels`, kind=player) ends with the partner's
   health as the partner's own game has it (`coop.avatar` hp=<health>/<max> on the other machine): ceil(health) /
   round(max). Key frame of both games (the note: the tag over the partner with the numbers).

  SPORE_FIX_OFF=creature.tag_hp   FAILS (the tag is the name alone)
"""
import math
import re
import time

from twoinst import Checks, keyframe, start


def own_hp(con):
    m = re.search(r'hp=([\d.]+)/([\d.]+)', con.call('coop.avatar')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def tag_of(con, pid):
    for m in re.finditer(r'label kind=player id=(\d+) .*text=(.*)', con.call('coop.find labels').get('out') or ''):
        if int(m.group(1)) == pid:
            return m.group(2).strip()
    return None


def want_of(subject):
    hp = own_hp(subject)
    return hp, (f'{math.ceil(hp[0])}/{round(hp[1])}' if hp else '?')


def wait_tag(viewer, subject, pid, seconds=3.0):
    t0 = time.time()
    got = hp = want = None
    while time.time() - t0 < seconds:
        hp, want = want_of(subject)
        got = tag_of(viewer, pid)
        if got and got.endswith(want):
            break
        time.sleep(0.25)
    return got, hp, want


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    for c in (host, guest):  # no pack regen meanwhile (the numbers stay put)
        c.call('coop.team off')
    print('  guest: ' + guest.call('coop.team come')['out'].strip(), flush=True)
    time.sleep(2)
    for con, share in ((guest, 0.5), (host, 1 / 3)):
        hp = own_hp(con)
        if hp:
            con.call('coop.team hp %.2f' % max(1.0, round(hp[1] * share, 1)))
    time.sleep(1.5)
    for viewer, subject, pid, who in ((host, guest, 1, "the guest's"), (guest, host, 0, "the host's")):
        got, hp, want = wait_tag(viewer, subject, pid)
        print(f'  {who} game: hp {hp}; the tag over it on the other game: {got!r}', flush=True)
        ck(got is not None, f"the other game draws a name tag over {who} creature")
        ck(bool(got) and got.endswith(want), f"the tag over {who} creature shows its health {want} ({got!r})")
    keyframe('tag_hp', "each game: the partner's name tag over its creature with its health (current/maximum): the guest "
             "at half, the host at a third", settle=0)
    for con in (host, guest):
        hp = own_hp(con)
        if hp:
            con.call('coop.team hp %.2f' % hp[1])
        con.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
