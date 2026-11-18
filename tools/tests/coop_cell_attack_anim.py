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


def main():
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
