#!/usr/bin/env python3
"""BUG-1243 (players' video): in the Cell stage, when one player attacks, the other player's cell plays the attack
animation too. Cell fixture, both cells in one place.

Each player in turn bites the nearest creature (`coop.pool kill c<key>`: the game's own Bite from its avatar, as a
ram does) a few times. The fixture's cell has no weapon: its bite plays no attack animation of its own (+0x1B0 stays
0 on the attacker too, lab 20261118-095915), so the control is that the bites land (the creature's hp in the host's
game, the game's own field, falls). Judged on the game's own animation of every player cell (+0x1B0, `coop.pool
anims`: the own avatar and each partner's puppet) sampled on both games while it happens:
  * the bites land (control);
  * the OTHER player's own cell, idle, plays no animation it did not play while idle (on its own game);
  * the attacker's game shows the other player's puppet without an animation (cell puppets get none relayed).
The weapon variants (--contact jaw|spike, --electric) do play attacker animations and check them.

--electric also checks what the guest's game draws of the host's zap (BUG-1266, key-frame review: a trail of giant
white rings centred on the host's cell): the world effects the guest's game created from the host's relay
(`coop.effects recent`, its 'p' rows: id, place, the effect's scale) hold none of the effects the host's Cell graphics
update made for its own cell (its 'm' rows marked held=1: the electric part's charge field 0x17AAB61F / 0x5CA636DC, the
zap's lightning, the propulsion), and every one it did play has the scale the host's game gave it.

  coop_cell_attack_anim.py
  coop_cell_attack_anim.py --electric                                (PASS)
  SPORE_FIX_OFF=effects.cell_held coop_cell_attack_anim.py --electric (FAIL: the guest plays the host's charge fields)
"""
import math
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


RECENT_RE = re.compile(r'^recent (\w) from=(\d+) id=([0-9A-F]{8}) at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) local_d=([-\d.]+) '
                       r'age=([\d.]+)(?: scale=([-\d.]+) held=(\d))?', re.M)
CHARGE = {'17AAB61F', '5CA636DC'}  # the electric part's charge field (SporeCoop::CellGfx_ElectricField)


class Recent:
    """the world effects a game made ('m'), sent ('s') and played from a partner ('p'), polled from `coop.effects recent`
    (a ring of 160) and kept by (kind, player, id, place) with the wall time they happened"""

    def __init__(self):
        self.rows = {}

    def poll(self, con):
        now = time.time()
        for m in RECENT_RE.finditer(con.call('coop.effects recent')['out']):
            d, frm, fid = m.group(1), int(m.group(2)), m.group(3)
            pos = tuple(round(float(m.group(i)), 1) for i in (4, 5, 6))
            at = now - float(m.group(8))
            key = (d, frm, fid, pos, round(at))
            if not any(k[:4] == key[:4] and abs(k[4] - key[4]) <= 1 for k in self.rows):
                self.rows[key] = dict(dir=d, frm=frm, id=fid, pos=pos, at=at,
                                      scale=float(m.group(9)) if m.group(9) else None, held=m.group(10) == '1')

    def of(self, d, frm, t0, t1):
        return [r for r in self.rows.values() if r['dir'] == d and r['frm'] == frm and t0 - 1 <= r['at'] <= t1 + 1]


def window(rows, t0, t1):
    return {a for t, a in rows if t0 <= t <= t1}


def host_hp(host, key):
    """the creature's hp in the host's game (the game's own field), -1 when the host has it no more (killed, eaten)"""
    m = re.search(r'\bhp=(-?\d+)', host.call(f'coop.pool cell c{key}')['out'])
    return int(m.group(1)) if m else -1


def attack(con, host, n=5):
    """swim at the nearest creature on the screen (clicks on it, as a player rams it), then the game's Bite on it (with no
    creature on the screen: the nearest one, by the game's Bite from where the cell is). Returns the outputs and the bites
    whose creature lost hp in the host's game (or died there): the attack really happened."""
    outs, landed, key = [], 0, None
    for _ in range(n):
        m = re.search(r'^see c(\d+) creature \S+ (\d+) ([\d.]+) (\d+) (\d+)', con.call('coop.pool see 60')['out'], re.M)
        if m:
            key = m.group(1)
            for _ in range(4):
                con.call(f'click {m.group(4)} {m.group(5)}')
                time.sleep(0.5)
        elif key is None or host_hp(host, key) <= 0:
            r = con.call('coop.pool kill -')['out'].strip()  # (names the nearest creature; this bite is not counted)
            k = re.match(r'bit c(\d+) ', r)
            key = k.group(1) if k and 'dying' not in r else None
            outs.append('first ' + r[:50])
            time.sleep(1.2)
            if key is None:
                continue
        before = host_hp(host, key)
        outs.append(con.call(f'coop.pool kill c{key}')['out'].strip()[:60])
        time.sleep(1.2)
        after = host_hp(host, key)
        landed += before > 0 and after < before
    return outs, landed


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
    rh, rg = Recent(), Recent()
    try:
        t0 = time.time()
        while time.time() - t0 < 25:
            host.call(f'coop.pool swimto {key} tp')
            guest.call(f'coop.pool swimto {key} tp')  # (the guest's mirror of the same creature: its key is the host's)
            rh.poll(host)
            rg.poll(guest)
            time.sleep(1.5)
        t1 = time.time()
        keyframe('host_zaps', "Cell: the host's electric part zaps (its own game: the yellow lightning at its cell); the guest's "
                 "screen shows the host's cell without a trail of giant white rings around it; the guest's cell, next to "
                 "the same creature, does not zap on either game", settle=0)
        rh.poll(host)
        rg.poll(guest)
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
    # BUG-1266: what the guest's game created of the host's zap
    made = rh.of('m', 0, t0, t1)
    held = {r['id'] for r in made if r['held']}
    sent = rh.of('s', 0, t0, t1)
    played = rg.of('p', 0, t0, t1)
    print(f'  host made {len(made)} world effects, its cell graphics held {sorted(held)}; sent {len(sent)} '
          f'({sorted({r["id"] for r in sent})})', flush=True)
    print(f'  guest played {len(played)} of the host\'s: ' + ', '.join(
        f'{r["id"]} scale {r["scale"]}' for r in sorted(played, key=lambda r: r['at'])[:24]), flush=True)
    for con, n in ((host, 'host'), (guest, 'guest')):
        line = [l for l in con.call('coop.effects')['out'].splitlines() if l.startswith('effects cell held')]
        print(f'  {n}: {line[0] if line else "no cell held line"}', flush=True)
    ck(held & CHARGE or {r['id'] for r in made} & CHARGE,
       f"the host's zap makes the electric charge field in its own game ({sorted({r['id'] for r in made} & CHARGE)})")
    shown = sorted({r['id'] for r in played} & (held | CHARGE))
    ck(not shown, f"the guest's game draws none of the effects the host's game keeps at its own cell (charge field, "
                  f"lightning, propulsion): played {shown}")
    off = []
    for r in played:
        twin = [x for x in sent if x['id'] == r['id'] and x['scale'] is not None and abs(x['at'] - r['at']) < 3]
        if twin and r['scale'] is not None and min(abs(x['scale'] - r['scale']) for x in twin) > 0.02 * max(1.0, r['scale']):
            off.append((r['id'], r['scale'], [x['scale'] for x in twin]))
    ck(not off, f"every host effect the guest's game plays has the host's scale ({off[:4]})")


def swim_to_creature(con, r=300):
    """BUG-1355: no creature on the screen: swim towards the nearest creature of this game's pool within r m (a guest:
    its mirrors of the host's); its distance, None when there is none"""
    me = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'])
    near = [(float(m.group(1)), float(m.group(2))) for m in
            re.finditer(r'^npc \S+ mirror=\d+ ailock=\d+ pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call(f'coop.pool npcs {r}')['out'], re.M)]
    if not me or not near:
        return None
    x, y = float(me.group(1)), float(me.group(2))
    nx, ny = min(near, key=lambda p: math.hypot(p[0] - x, p[1] - y))
    con.call(f'coop.pool goto {nx - x:.1f} {ny - y:.1f}')
    return math.hypot(nx - x, ny - y)


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
        r, hits, looks = '', 0, []
        while time.time() - t0 < 30:
            # BUG-1267: the fixture's spot can be empty, or eaten empty by the first round: 0 swims is a test without
            # its case, not a pass or a fail. BUG-1355: `see N` lists the N nearest cells ON THE SCREEN (N is a row
            # count, not a radius): with none on the screen the attacker swims towards the nearest creature of its
            # game's pool (`npcs <r>`: within r m, on the screen or not), as a player who goes looking for prey
            m = re.search(r'^see (c\d+) creature ', att.call('coop.pool see 60')['out'], re.M)
            if m:
                r = att.call(f'coop.pool swimto {m.group(1)}')['out'].strip()
                hits += 1
            else:
                d = swim_to_creature(att)
                if d is not None and (not looks or looks[-1] != round(d)):
                    looks.append(round(d))
            time.sleep(0.7)
        if looks:
            print(f'  the {attacker_name} swam towards creatures off its screen ({looks[:8]} m away)', flush=True)
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
    if not hits:
        print(f'  the {attacker_name} found no creature to ram on its screen nor within 300 m of its pool: no attack, its '
              f'checks left out', flush=True)
        return False
    ck(att_anims, f"the {attacker_name}'s own cell played attack / eat animations ({sorted(att_anims)})")
    ck(not (att_anims & (o_av - idle_o)), f"the {oname}'s own cell plays none of the {attacker_name}'s animations ({sorted(att_anims & (o_av - idle_o))})")
    ck(not ((a_pup - {'0'}) - o_av), f"the {attacker_name}'s game shows the {oname}'s cell only with animations its own game plays ({sorted((a_pup - {'0'}) - o_av)})")
    return True


def num_of(text, key):
    m = re.search(r'\b' + key + r'=(\d+)', text)
    return int(m.group(1)) if m else None


def main():
    if '--contact' in sys.argv:
        part = sys.argv[sys.argv.index('--contact') + 1]
        host, guest = start('cell')
        ck = Checks()
        time.sleep(3)
        ran = [contact(host, guest, ck, part, 'host')]
        time.sleep(3)
        ran.append(contact(host, guest, ck, part, 'guest'))
        ck.faults(host=host, guest=guest)
        if not all(ran):
            return ck.inconclusive('a player found no creature to ram (no attack happened)')
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
        # 1. the host attacks (the fixture's cell has no weapon: its bite plays no attack animation of its own, +0x1B0
        # stays 0 on the attacker; what is checked is that the bites land and the OTHER cell stays as idle as it was)
        outs, landed = attack(host, host)
        print('  host bites:', outs, f'landed {landed}', flush=True)
        keyframe('host_attacks', "Cell: the host bites; the guest's cell does not play the attack on either game", settle=0)
        t1 = time.time()
        time.sleep(2)
        g_own = window(sg.avatar, t0, t1 + 2)
        h_pup = window(sh.puppet, t0, t1 + 2)
        g_pup = window(sg.puppet, t0, t1 + 2)
        print(f'  host attacks: host avatar {sorted(window(sh.avatar, t0, t1))} | guest avatar {sorted(g_own)} | '
              f'host\'s puppet of guest {sorted(h_pup)} | guest\'s puppet of host {sorted(g_pup)}', flush=True)
        ck(landed >= 1, f"the host's bites land: the bitten creature lost hp in the host's game ({landed} of {len(outs)})")
        ck(not (g_own - idle[1]), f"the guest's own cell, idle, plays no new animation while the host attacks ({sorted(g_own - idle[1])})")
        ck(not (h_pup - {'0'}), f"the host's game shows the guest's cell without an attack animation ({sorted(h_pup)})")
        time.sleep(3)
        # 2. the guest attacks
        t2 = time.time()
        outs, landed = attack(guest, host)
        print('  guest bites:', outs, f'landed {landed}', flush=True)
        keyframe('guest_attacks', "Cell: the guest bites; the host's cell does not play the attack on either game", settle=0)
        t3 = time.time()
        time.sleep(2)
        h_own = window(sh.avatar, t2, t3 + 2)
        g_pup2 = window(sg.puppet, t2, t3 + 2)
        h_pup2 = window(sh.puppet, t2, t3 + 2)
        print(f'  guest attacks: guest avatar {sorted(window(sg.avatar, t2, t3))} | host avatar {sorted(h_own)} | '
              f'guest\'s puppet of host {sorted(g_pup2)} | host\'s puppet of guest {sorted(h_pup2)}', flush=True)
        ck(landed >= 1, f"the guest's bites land: the bitten creature lost hp in the host's game ({landed} of {len(outs)})")
        ck(not (h_own - idle[0]), f"the host's own cell, idle, plays no new animation while the guest attacks ({sorted(h_own - idle[0])})")
        ck(not (g_pup2 - {'0'}), f"the guest's game shows the host's cell without an attack animation ({sorted(g_pup2)})")
    finally:
        sh.stop()
        sg.stop()
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name} effects: ' + ' | '.join(con.call('coop.effects')['out'].splitlines()[:6]))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
