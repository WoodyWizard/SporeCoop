#!/usr/bin/env python3
"""Creature stage: a sneaking player is shown sneaking on the partner's game (BUG-0463).

Player report (0.7.3.4, both players): "sneak is not synced". The sneak ability sets the creature's
+0xBB1 (sneaking), its noise radius +0xB80 to 0 (what the NPCs' hearing check 0xC0D6C0 reads) and the
creature's update eases its model to alpha 0.2 (+0xB7C, see-through). Only the sneaking player's game did it.

Both directions, read in the observer's game (the puppet's own fields, the state the game draws and its
NPCs' AI hear on the host):
1. The guest's avatar sneaks with the game's setter (CreatureSetSneaking without force: it needs the sneak
   ability, as the ability key does): within 4 s the host's puppet of the guest sneaks (+0xBB1 = 1), has
   noise radius 0 and is drawn with the alpha the guest's game draws its own creature with (0.2 after a
   forced sneak; the fixture's creatures with the ability keep 1.0). The guest stops: the puppet stops
   (+0xBB1 = 0, noise > 0, alpha back over 0.8). The guest's fixture creature has no sneak ability: it sneaks
   by the setter's force flag.
2. The same for the host and the guest's puppet of the host.

  coop_creature_sneak.py   (creature.sneak off failed (retired in 0.7.15, BUG-1138: FAIL run 20261103-151356:creature_sneak))"""
import re
import time

from twoinst import Checks, start

LINE = re.compile(r'^(own|puppet) (-?\d+): sneak=(\d) noise=(\S+) alpha=(\S+) energy=(\S+) model_alpha=(\S+)', re.M)


def sneak(con):
    out = {}
    for m in LINE.finditer(con.call('coop.sneak')['out']):
        out[(m.group(1), int(m.group(2)))] = (int(m.group(3)), float(m.group(4)), float(m.group(5)), float(m.group(6)), float(m.group(7)))
    return out


def wait_for(fn, t):
    t0 = time.time()
    v = fn()
    while not v and time.time() - t0 < t:
        time.sleep(0.25)
        v = fn()
    return v


def unfaded(actor, observer, pid, who):
    """BUG-1412: the observer's game drew the actor's puppet see-through before any sneak (lab 20261121-003202: the
    guest's game drew the host's puppet at model alpha 0.13 with +0xB7C 1.0, sneak 0; the host's game drew the guest's
    puppet at model alpha 0.2 with +0xB7C 1.0 before, during and after the guest's sneak, and the game's sneak look never
    eased its +0xB7C). Inferred: the observer's camera fades a creature between it and the avatar (the partner stands
    5 m beside, BUG-0989); a faded puppet does not show the sneak look the test compares. The actor moves to another
    side of the observer until the puppet is drawn opaque (model alpha 0.9 or more), at most 4 places"""
    for k, off in enumerate(((0, 0, 0), (6, 0, 0), (0, 6, 0), (0, 0, 6), (-6, 0, 0))):
        if k:
            m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', observer.call('coop.avatar')['out'])
            if not m:
                break
            q = [float(v) + d for v, d in zip(m.groups(), off)]
            actor.call('coop.npc gopos %.2f %.2f %.2f' % tuple(q))
            time.sleep(2)
        p = sneak(observer).get(('puppet', pid))
        if p and p[4] >= 0.9:
            if k:
                print(f'  {who}: the other game drew the puppet see-through before the sneak; opaque after {k} move(s) '
                      f'({p}, BUG-1412)', flush=True)
            return True
        print(f'  {who}: the other game draws the puppet see-through before any sneak ({p}): moving it (BUG-1412)',
              flush=True)
    return False


def direction(ck, actor, observer, pid, who):
    ck(unfaded(actor, observer, pid, who), f'{who}: the other game draws the puppet opaque before the sneak')
    own = sneak(actor).get(('own', -1))
    print(f'  {who}: own before {own}', flush=True)
    ck(own is not None, f'{who}: its own avatar is there')
    if not own:
        return
    abil = actor.call('coop.abilities')['out']
    # the game's setter as the ability key calls it; a creature without the sneak ability (type 0xD / 0x52,
    # the guest's fixture creature) only with the setter's force flag
    has = re.search(r'type=0x(D|52) ', abil) is not None
    print(f'  {who}: sneak ability: {has}', flush=True)
    print('  ' + actor.call('coop.sneak ' + ('on' if has else 'force'))['out'].strip().replace('\n', ' | '), flush=True)
    time.sleep(0.3)
    own = sneak(actor).get(('own', -1))
    print(f'  {who}: own after sneak on {own}', flush=True)
    if not ck(own and own[0] == 1 and own[1] == 0, f'{who}: its own game sneaks (+0xBB1 1, noise 0)'):
        return
    pup = lambda: sneak(observer).get(('puppet', pid))
    # the puppet in the sneak state of the actor's game: +0xBB1 1, noise 0, and drawn as the actor's game draws
    # its own creature (the update eases the alpha: 0.2 for a forced sneak, 1.0 kept with the ability on the
    # fixture creatures), both settled
    def same():
        p, o = pup(), sneak(actor).get(('own', -1))
        return (p, o) if p and o and p[0] == 1 and abs(p[2] - o[2]) < 0.15 and abs(o[2] - o[4]) < 0.05 and \
            (time.time() - t_on > 3) else None
    t_on = time.time()
    got = wait_for(same, 8)
    p, o = got or (pup(), sneak(actor).get(('own', -1)))
    print(f'  {who}: the other game\'s puppet {p}, own {o}', flush=True)
    ck(p is not None and p[0] == 1, f'{who}: the other game\'s puppet sneaks (+0xBB1 {p and p[0]})')
    ck(p is not None and p[1] == 0, f'{who}: the other game\'s puppet makes no noise for its NPCs (+0xB80 {p and p[1]})')
    ck(got is not None, f'{who}: the other game draws the puppet as the actor\'s game draws its own creature '
                        f'(alpha {p and p[2]} / own {o and o[2]})')
    time.sleep(2)
    own = sneak(actor).get(('own', -1))
    p = pup()
    ck(own and own[0] == 1 and p and p[0] == 1 and abs(own[2] - p[2]) < 0.15,
       f'{who}: still sneaking 2 s later on both games, drawn alike (own {own}, puppet {p})')
    actor.call('coop.sneak off')
    back = wait_for(lambda: (lambda p: p if p and p[0] == 0 and p[1] > 0 and p[2] > 0.8 else None)(pup()), 4)
    print(f'  {who}: after sneak off: own {sneak(actor).get(("own", -1))}, puppet {pup()}', flush=True)
    ck(back, f'{who}: the other game\'s puppet stops sneaking (sneak 0, noise > 0, opaque): {pup()}')


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    direction(ck, guest, host, 1, 'guest')
    direction(ck, host, guest, 0, 'host')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
