#!/usr/bin/env python3
"""BUG-1243 (players' video): in the Cell stage, when one player attacks, the other player's cell plays the attack
animation too. Cell fixture, both cells in one place.

Each player in turn bites the nearest creature (`coop.pool kill -`: the game's own Bite from its avatar, as a ram
does) a few times. Judged on the game's own animation of every player cell (+0x1B0, `coop.pool anims`: the own
avatar and each partner's puppet) sampled on both games while it happens:
  * the attacker's own cell plays its attack animations (control);
  * the OTHER player's own cell plays none of the attacker's animations (on its own game);
  * the attacker's game shows the other player's puppet without the attacker's animations.

  coop_cell_attack_anim.py
"""
import re
import sys
import threading
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path for sporectl)
from sporectl import Console


def anims(con):
    out = con.call('coop.pool anims')['out']
    av = re.search(r'^anim avatar \S+ anim=([0-9A-F]+)', out, re.M)
    pp = re.findall(r'^anim puppet \S+ anim=([0-9A-F]+)', out, re.M)
    return (av.group(1) if av else None), pp


class Sampler(threading.Thread):
    def __init__(self, con):
        super().__init__(daemon=True)
        self.port = con.port - 47799
        self.avatar, self.puppet = [], []
        self.stop_ev = threading.Event()

    def run(self):
        c = Console(self.port, timeout=20)
        try:
            while not self.stop_ev.is_set():
                a, p = anims(c)
                t = time.time()
                if a:
                    self.avatar.append((t, a))
                for x in p:
                    self.puppet.append((t, x))
                self.stop_ev.wait(0.05)
        finally:
            c.sock and c.sock.close()

    def stop(self):
        self.stop_ev.set()
        self.join(10)


def window(rows, t0, t1):
    return {a for t, a in rows if t0 <= t <= t1}


def attack(con, n=5):
    """swim at the nearest creature on the screen (clicks on it, as a player rams it), then the game's Bite on it"""
    outs = []
    for _ in range(n):
        m = re.search(r'^see c\d+ creature \S+ \d+ ([\d.]+) (\d+) (\d+)', con.call('coop.pool see 8')['out'], re.M)
        if m:
            for _ in range(4):
                con.call(f'click {m.group(2)} {m.group(3)}')
                time.sleep(0.5)
        outs.append(con.call('coop.pool kill -')['out'].strip()[:60])
        time.sleep(1.0)
    return outs


def electric(host, guest, ck):
    """BUG-1243 with a weapon the game uses by itself: the host's cell gets the electric part (`coop.pool borrow electric
    graft`: the game's electric step then zaps creatures near it, animation 0x1E on the attacker). Both players' cells
    are the world's species (a guest without creations), so the host's game has the same model data for the guest's
    puppet. Both cells next to the same creature; only the host's game has the part. The guest's own game never
    zaps: its cell must not be shown attacking (0x1E) in the host's game either."""
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'
    got = host.call('coop.pool borrow electric graft')['out'].strip()
    print('  host: ' + got, flush=True)
    ck(got.startswith('borrowed'), f"the host's cell has the game's electric part ({got[:80]})")
    key, r = None, ''
    for _ in range(15):
        r = host.call('coop.pool kill - hit')['out']  # names the nearest creature
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'a creature next to the host ({r.strip()[:80]})')
    if not key:
        return
    print('  ' + host.call(f'coop.pool npcflag {key} hp 200')['out'].strip(), flush=True)
    sh, sg = Sampler(host), Sampler(guest)
    sh.start()
    sg.start()
    z0h, z0g = host.call('coop.pool anims')['out'], guest.call('coop.pool anims')['out']
    try:
        t0 = time.time()
        while time.time() - t0 < 25:
            host.call(f'coop.pool swimto {key} tp')
            guest.call(f'coop.pool swimto {key} tp')  # (the guest's mirror of the same creature: its key is the host's)
            time.sleep(1.5)
        t1 = time.time()
        keyframe('host_zaps', "Cell: the host's electric part zaps; the guest's cell, next to the same creature, does not zap on either game", settle=0)
    finally:
        sh.stop()
        sg.stop()
    z1h, z1g = host.call('coop.pool anims')['out'], guest.call('coop.pool anims')['out']
    h_av, h_pup = window(sh.avatar, t0, t1), window(sh.puppet, t0, t1)
    g_av, g_pup = window(sg.avatar, t0, t1), window(sg.puppet, t0, t1)
    print(f'  host avatar {sorted(h_av)} | host\'s puppet of the guest {sorted(h_pup)} | guest avatar {sorted(g_av)} | '
          f'guest\'s puppet of the host {sorted(g_pup)}', flush=True)
    for k in ('zaps', 'puppet_zaps'):
        print(f'  {k}: host {num_of(z0h, k)} -> {num_of(z1h, k)}, guest {num_of(z0g, k)} -> {num_of(z1g, k)}', flush=True)
    ck('1E' in h_av, f"the host's own cell plays its electric attack (0x1E) ({sorted(h_av)})")
    ck('1E' not in g_av, f"the guest's own cell does not zap in its own game ({sorted(g_av)})")
    ck('1E' not in h_pup or '1E' in g_av,
       f"the host's game shows the guest's cell attacking only when the guest's own game does ({sorted(h_pup)})")


def contact(host, guest, ck, part, attacker_name):
    """BUG-1243 the players' way: both cells carry the same weapon (`coop.pool borrow <part> graft` on both games: the
    world's species of both players, so each game's model data has it for the avatar and the partner's puppet); the
    attacker swims into the nearest creature again and again (the game's own contact decides the attack, docs/re/cell.md
    11), the other player does nothing and stays where it is, next to the attacker. The other player's own cell must
    play none of the attacker's animations on its own game, and its puppet on the attacker's game only what its own
    game plays."""
    att, oth = (host, guest) if attacker_name == 'host' else (guest, host)
    oname = 'guest' if attacker_name == 'host' else 'host'
    for con, n in ((host, 'host'), (guest, 'guest')):
        if part != 'none':
            print(f'  {n}: ' + con.call(f'coop.pool borrow {part} graft')['out'].strip(), flush=True)
    sa, so = Sampler(att), Sampler(oth)
    sa.start()
    so.start()
    try:
        time.sleep(3)
        t0 = time.time()
        idle_o = window(so.avatar, 0, t0)
        print(f'  idle: the {oname}\'s cell {sorted(idle_o)}', flush=True)
        r, hits = '', 0
        while time.time() - t0 < 30:
            m = re.search(r'^see (c\d+) creature ', att.call('coop.pool see 8')['out'], re.M)
            if m:
                r = att.call(f'coop.pool swimto {m.group(1)}')['out'].strip()
                hits += 1
            time.sleep(0.7)
        t1 = time.time()
        keyframe(f'{attacker_name}_contact_{part}', f"Cell: the {attacker_name} rams creatures with its {part}; the {oname}'s cell next to it does not attack on either game", settle=0)
        time.sleep(2)
    finally:
        sa.stop()
        so.stop()
        for con in (host, guest):
            if part != 'none':
                con.call('coop.pool borrow off')
    a_av = window(sa.avatar, t0, t1)
    a_pup, o_av, o_pup = window(sa.puppet, t0, t1 + 2), window(so.avatar, t0, t1 + 2), window(so.puppet, t0, t1 + 2)
    print(f'  {attacker_name} attacks ({hits} swims, last: {r[:60]}): its own cell {sorted(a_av)} | its puppet of the {oname} '
          f'{sorted(a_pup)} | the {oname}\'s own cell {sorted(o_av)} | the {oname}\'s puppet of the {attacker_name} {sorted(o_pup)}', flush=True)
    att_anims = a_av - {'0'}
    ck(att_anims, f"the {attacker_name}'s own cell played attack / eat animations ({sorted(att_anims)})")
    ck(not (att_anims & (o_av - idle_o)), f"the {oname}'s own cell plays none of the {attacker_name}'s animations ({sorted(att_anims & (o_av - idle_o))})")
    ck(not ((a_pup - {'0'}) - o_av), f"the {attacker_name}'s game shows the {oname}'s cell only with animations its own game plays ({sorted((a_pup - {'0'}) - o_av)})")


def num_of(text, key):
    m = re.search(r'\b' + key + r'=(\d+)', text)
    return int(m.group(1)) if m else None


def main():
    if '--contact' in sys.argv:
        part = sys.argv[sys.argv.index('--contact') + 1]
        host, guest = start('cell')
        ck = Checks()
        time.sleep(3)
        contact(host, guest, ck, part, 'host')
        time.sleep(3)
        contact(host, guest, ck, part, 'guest')
        ck.faults(host=host, guest=guest)
        return ck.result()
    if '--electric' in sys.argv:
        host, guest = start('cell')
        ck = Checks()
        time.sleep(3)
        try:
            electric(host, guest, ck)
        finally:
            host.call('coop.pool borrow off')
        ck.faults(host=host, guest=guest)
        return ck.result()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(4)
    for con in (host, guest):
        con.call('coop.effects log on')
    print('  host:', anims(host), '| guest:', anims(guest))
    sh, sg = Sampler(host), Sampler(guest)
    sh.start()
    sg.start()
    try:
        time.sleep(3)
        t0 = time.time()
        idle = (window(sh.avatar, 0, t0), window(sg.avatar, 0, t0), window(sh.puppet, 0, t0), window(sg.puppet, 0, t0))
        print(f'  idle: host avatar {sorted(idle[0])} guest avatar {sorted(idle[1])} host puppet {sorted(idle[2])} guest puppet {sorted(idle[3])}')
        # 1. the host attacks
        print('  host bites:', attack(host))
        keyframe('host_attacks', "Cell: the host bites; the guest's cell does not play the attack on either game", settle=0)
        t1 = time.time()
        time.sleep(2)
        h_att = window(sh.avatar, t0, t1) - idle[0]
        g_own = window(sg.avatar, t0, t1 + 2)
        h_pup = window(sh.puppet, t0, t1 + 2)
        g_pup = window(sg.puppet, t0, t1 + 2)
        print(f'  host attacks: host avatar {sorted(window(sh.avatar, t0, t1))} | guest avatar {sorted(g_own)} | '
              f'host\'s puppet of guest {sorted(h_pup)} | guest\'s puppet of host {sorted(g_pup)}', flush=True)
        ck(h_att, f"the host's own cell plays its attack animations ({sorted(h_att)})")
        ck(not (h_att & g_own), f"the guest's own cell does not play the host's attack ({sorted(h_att & g_own)})")
        ck(not (h_att & h_pup), f"the host's game shows the guest's cell without the host's attack ({sorted(h_att & h_pup)})")
        time.sleep(3)
        # 2. the guest attacks
        t2 = time.time()
        print('  guest bites:', attack(guest))
        keyframe('guest_attacks', "Cell: the guest bites; the host's cell does not play the attack on either game", settle=0)
        t3 = time.time()
        time.sleep(2)
        g_att = window(sg.avatar, t2, t3) - idle[1]
        h_own = window(sh.avatar, t2, t3 + 2)
        g_pup2 = window(sg.puppet, t2, t3 + 2)
        h_pup2 = window(sh.puppet, t2, t3 + 2)
        print(f'  guest attacks: guest avatar {sorted(window(sg.avatar, t2, t3))} | host avatar {sorted(h_own)} | '
              f'guest\'s puppet of host {sorted(g_pup2)} | host\'s puppet of guest {sorted(h_pup2)}', flush=True)
        ck(g_att, f"the guest's own cell plays its attack animations ({sorted(g_att)})")
        ck(not (g_att & h_own), f"the host's own cell does not play the guest's attack ({sorted(g_att & h_own)})")
        ck(not (g_att & g_pup2), f"the guest's game shows the host's cell without the guest's attack ({sorted(g_att & g_pup2)})")
    finally:
        sh.stop()
        sg.stop()
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name} effects: ' + ' | '.join(con.call('coop.effects')['out'].splitlines()[:6]))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
