#!/usr/bin/env python3
"""BUG-1283 (found by the Lineages fork (PR #196), its BUG-9115): a guest does not see the host's nest alphas as alphas.

The nest's alpha is cCreatureAnimal +0xB58 bit 0x1 (its card reads "Alpha <name>", SocialPropose 0xDA43D0 adds a social
round; docs/re/creature.md#creature-nest-alpha). The host's NPC state carried dead / impressed / pack only, so a guest's
proxy kept the bit its own game's herd gave it. Now the state carries NF_ALPHA and the guest's proxy takes the host's.

The test reads the game's own flag on both machines (`coop.npc list`: alpha=, the proxy's +0xB58 & 1 on the guest):
  1. every proxy the guest has made shows the host's bit (natural state of the fixture's nests);
  2. planted on the guest: a proxy of a host non-alpha gets the bit in the guest's game (as its own herd would give it):
     the host's state takes it away again within 3 s;
  3. planted on the host: a host NPC made an alpha (its game's flag): the guest's proxy shows it within 3 s, and loses
     it when the host's is cleared again.

  SPORE_FIX_OFF=npc.alpha coop_npc_alpha.py   (2 and 3: must FAIL)
"""
import re
import time

from twoinst import Checks, keyframe, start

HOST_ROW = re.compile(r'^\s*#(\d+) ([0-9A-Fa-fx]+) species=\S+ hp=\S+ dist=([-\d.]+)( DEAD)? .* alpha=(-?\d)$', re.M)
GUEST_ROW = re.compile(r'^\s*#(\d+) species=\S+ (spawned|pending) .* ptr=([0-9A-Fa-fx]+) .* game_dead=(-?\d+) .* alpha=(-?\d)$',
                       re.M)


def host_rows(con):
    return {int(m.group(1)): {'ptr': int(m.group(2), 16), 'dist': float(m.group(3)), 'dead': bool(m.group(4)),
                              'alpha': int(m.group(5))} for m in HOST_ROW.finditer(con.call('coop.npc list')['out'])}


def guest_rows(con):
    return {int(m.group(1)): {'ptr': int(m.group(3), 16), 'dead': m.group(4) == '1', 'alpha': int(m.group(5))}
            for m in GUEST_ROW.finditer(con.call('coop.npc list')['out']) if m.group(2) == 'spawned'}


def flags(con, ptr):
    out = con.call(f'mem.read 0x{ptr + 0xB58:08X} 4')['out']  # "AAAAAAAA  b0 b1 b2 b3"
    words = out.split()
    if len(words) < 5 or not all(re.fullmatch(r'[0-9A-Fa-f]{2}', w) for w in words[1:5]):
        return None
    return int.from_bytes(bytes(int(w, 16) for w in words[1:5]), 'little')


def set_bit(con, ptr, on):
    f = flags(con, ptr)
    if f is None:
        return False
    f = (f | 1) if on else (f & ~1)
    con.call(f'mem.write 0x{ptr + 0xB58:08X} {f.to_bytes(4, "little").hex()}')
    return True


def guest_alpha_after(guest, nid, want, secs=3.0):
    t0 = time.time()
    a = None
    while time.time() - t0 < secs:
        a = guest_rows(guest).get(nid, {}).get('alpha')
        if a == want:
            return a
        time.sleep(0.25)
    return a


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(12)
    hr, gr = host_rows(host), guest_rows(guest)
    both = sorted(i for i in gr if i in hr and not hr[i]['dead'] and not gr[i]['dead'])
    diff = [i for i in both if hr[i]['alpha'] != gr[i]['alpha']]
    h_alphas = [i for i in both if hr[i]['alpha'] == 1]
    print(f'  matched live NPCs {len(both)}, host alphas among them {h_alphas}, differing {diff}', flush=True)
    ck(len(both) >= 3, f'the guest has proxies of the host\'s NPCs ({len(both)})')
    ck(not diff, f'1. every proxy shows the host\'s alpha bit ({len(diff)} of {len(both)} differ: {diff[:8]})')
    keyframe('alpha_nest', 'Creature: the guest near the host\'s nest; the same creatures stand around it in both games '
             '(an alpha\'s card reads "Alpha <name>" on both when hovered; not hovered here)')

    plain = [i for i in both if hr[i]['alpha'] == 0 and gr[i]['alpha'] == 0]
    ck(len(plain) >= 1, f'a host non-alpha with a proxy exists ({len(plain)})')
    if plain:
        nid = min(plain, key=lambda i: hr[i]['dist'])
        ok = set_bit(guest, gr[nid]['ptr'], True)
        a = guest_alpha_after(guest, nid, 0)
        print(f'  2. guest planted the bit on proxy #{nid}: alpha now {a}', flush=True)
        ck(ok and a == 0, f'2. the guest\'s own alpha mark on proxy #{nid} goes back to the host\'s (no alpha): {a}')
        set_bit(guest, gr[nid]['ptr'], False)

        ok = set_bit(host, hr[nid]['ptr'], True)
        a_on = guest_alpha_after(guest, nid, 1)
        print(f'  3. host made #{nid} an alpha: the guest\'s proxy alpha {a_on}', flush=True)
        ck(ok and a_on == 1, f'3. the host\'s alpha #{nid} is an alpha on the guest too: {a_on}')
        set_bit(host, hr[nid]['ptr'], False)
        a_off = guest_alpha_after(guest, nid, 0)
        ck(a_off == 0, f'3. and no alpha again once the host\'s is not: {a_off}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
