#!/usr/bin/env python3
"""BUG-0316: Cell: does a guest that swims onto a part token of the host's world get the part and the
mating-call button?

The game collects a part token in the collision code 0xE7B630 of the collecting player's own game:
CollectPart 0xE5D7B0 (the part's state S+0x34+4*part = 2, the mating-call button S+0x69 = 1), then
DestroyCell of the token (docs/re/cell.md#cell-mate-button, #cell-part-token). On a guest the token
is a mirror of the host's cell.

Two games in the Cell fixture. The guest's mating-call button is hidden first (`coop.pool mate
nobutton`, as before a first part). The host moves the nearest part token of its pool 4 m next to
the guest's cell (`coop.pool tokens to 1`; the host swims around until its pool has one); the guest
swims onto its mirror (`coop.pool swimto c<key> tp`, like a click). Checks, in the games' own state:
the guest's game shows the mating-call button (S+0x69), its part states gained the part (`coop.parts
cell`), the token is gone in the host's game, and the host's game has the part too (the party's
shared set, parts.shared_cell).

Not every token can be collected at a given moment by either game: in about 15 of 20 tries the host's own touch
of the same token failed too (lab 20261106-010456, -011422, -012210, -013445, -014745). CORRECTION (2026-11-07,
fix/w46-part-token): the cause was the game's "Goldie's demise" tutorial, started by the test's own food steps, which
leaves both avatars' collect locks set (see main); the test now marks it played first. The test then tries the next token (at most 4); it FAILS only when the host's game collects
a token the guest's could not, or when the guest's collect does not reach the host. When no collectable
token turns up it fails on "a part token one of the games can collect was found": a setup failure, not this
bug (the suite runs it in 'extended' only).

  coop_cell_part_token.py"""
import re
import time

from twoinst import Checks, start


def tokens(con, extra=''):
    out = con.call(f'coop.pool tokens {extra}'.strip())['out']
    return re.findall(r'^token (c\d+) pos=\((-?[\d.]+) (-?[\d.]+)\) dist=([\d.]+) size=[\d.]+ level=(-?\d+)', out, re.MULTILINE), out


def button(con):
    m = re.search(r'mate_button=(-?\d+)', con.call('coop.pool mate')['out'])
    return int(m.group(1)) if m else None


def parts(con):
    m = re.search(r'cell parts=(\d+)', con.call('coop.parts cell')['out'])
    return m.group(1) if m else None


def playing(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def find_tokens(host, guest, tried):
    """part tokens of the host's pool not tried yet: the host swims, kills, and levels up until the tiles give one"""
    for i in range(42):
        found = [t for t in tokens(host)[0] if t[0] not in tried]
        if found:
            return found
        if i % 6 == 5:  # a new level populates new tiles (parts not collected yet may lie there)
            print('  host:', host.call('coop.pool food 40')['out'].strip()[:60], flush=True)
            time.sleep(6)
            continue
        # some kills drop loot (SpawnLoot): the host kills the nearest creature on its screen
        seen = [l.split() for l in host.call('coop.pool see 120')['out'].splitlines() if l.startswith('see ')]
        prey = [f[1] for f in seen if f[2] == 'creature']
        if prey:
            host.call('coop.pool invulnfor 30')
            for _ in range(16):
                out = host.call(f'coop.pool kill {prey[0]}')['out'].strip()
                if 'dying' in out or out in ('no creature', 'gone'):
                    break
                time.sleep(0.6)
            time.sleep(1.5)
            continue
        dx, dy = [(40, 0), (0, 40), (-40, 0), (0, -40)][i % 4]
        host.call(f'coop.pool goto {dx} {dy}')
        time.sleep(4)
        guest.call('coop.pool tp 3 0')
    return []


def locks(con):
    m = re.search(r'own_locks=(\d+) anim=(-?\d+)', con.call('coop.pool tokens')['out'])
    return f'{m.group(1)} anim {m.group(2)}' if m else '?'


def touched(con, key):
    """<con> swims onto token <key>: True when its game collected it (the button shows, the token is gone)"""
    tl = next((l for l in con.call('coop.pool tokens')['out'].splitlines() if l.startswith(f'token {key} ')), '-')
    print(f'  locks (+0x112 +0x113 +0x178 +0x17F) {locks(con)}; {tl}', flush=True)
    print(f'  {con.name if hasattr(con, "name") else ""}swimto:', con.call(f'coop.pool swimto {key} tp')['out'].strip(), flush=True)
    return wait(lambda: button(con) == 1, 6)


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    # The food the search adds (`coop.pool food 40`) passes 120-150 once: the game's one-time "Goldie's demise"
    # tutorial (AddFoodPoints 0xE7CE10 -> 0xE79720) then left both avatars' locks set (+0x178 among them) and
    # 0xE7B630 collected no token in either game (lab 20261107-013013: own_locks 0010 on both before every touch;
    # the FAILs of 20261106-231404 / 20261107-004042). The test marks it played on both games (S+0x68).
    for con in (host, guest):
        con.call('coop.pool mate nogoldie')
    tried, result = set(), None
    for attempt in range(4):
        found = find_tokens(host, guest, tried)
        print(f'  attempt {attempt + 1}: host part tokens {found[:3]}', flush=True)
        if not found:
            break
        key = found[0][0]
        tried.add(key)
        guest.call('coop.pool tp 3 0')
        time.sleep(2)
        guest.call('coop.pool mate nobutton')
        host.call('coop.pool mate nobutton')
        b0, gp0, hp0 = button(guest), parts(guest), parts(host)
        print(f'  before: guest button {b0} parts {gp0} | host parts {hp0}', flush=True)
        out = host.call(f'coop.pool tokens to 1 {key}')['out']
        print('  host:', out.splitlines()[0], flush=True)
        if not out.startswith('moved'):
            continue
        if not wait(lambda: key in [t[0] for t in tokens(guest)[0]], 6):
            print(f'  the guest\'s game has no mirror of {key}: next token', flush=True)
            continue
        # a token moved from outside the active area gets its graphics object first
        wait(lambda: re.search(r'gfx=[1-9]', guest.call(f'coop.pool cell {key}')['out']), 10)
        print('  guest token:', ' | '.join(guest.call(f'coop.pool cell {key}')['out'].strip().splitlines())[:400], flush=True)
        got = touched(guest, key)
        time.sleep(4)  # the shared set (resent every 3 s)
        b1, gp1, hp1 = button(guest), parts(guest), parts(host)
        gone = not host.call(f'coop.pool cell {key}')['out'].startswith('cell ')
        print(f'  after the guest\'s touch: guest button {b1} parts {gp1} | host parts {hp1} | token gone on the host {gone}', flush=True)
        if got:
            result = (b0, b1, gp0, gp1, hp0, hp1, gone, got)
            break
        # control: the host's own touch of the same token. If the host's game does not collect it either, this
        # token cannot be collected now (seen: 3 of 7 tokens, lab 20261106-010456 #3 / #4, 20261106-011422 #1)
        hgot = touched(host, key)
        print(f'  control: the host\'s own touch collected it: {bool(hgot)} (button {button(host)}, parts {parts(host)})', flush=True)
        if hgot:
            result = (b0, b1, gp0, gp1, hp0, hp1, gone, got)  # the host collects what the guest could not: the bug
            break
    ck(result is not None, 'a part token one of the games can collect was found')
    if result is None:
        return ck.result()
    b0, b1, gp0, gp1, hp0, hp1, gone, got = result
    ck(got and b1 == 1, f'the guest\'s game shows the mating-call button after its touch ({b0} -> {b1})')
    changed = [i for i in range(min(len(gp0 or ''), len(gp1 or ''))) if gp0[i] != gp1[i]]
    ck(bool(changed), f'the guest\'s game has the part ({gp0} -> {gp1})')
    ck(gone, 'the token is gone in the host\'s game')
    ck(bool(changed) and hp1 is not None and all(hp1[i] != '0' for i in changed),
       f'the host\'s game has the part too ({hp0} -> {hp1}, part {changed})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
