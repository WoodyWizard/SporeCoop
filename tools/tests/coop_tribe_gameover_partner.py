#!/usr/bin/env python3
"""Tribe: the host's game over while the guest's members live, and the play's rule that answers it (BUG-0922).

The 0.7.12 final check's play_tribe ended with the host in the game-over caption ("Alas! Your population has fallen
below the minimum ...") for its last 40 s while the guest played on: the play pressed OK only when no member of the
player tribe had health on the host, and the guest's members are in the host's player tribe too.

1. Two games on the tribe fixture, 2 more members (the player tribe's members are split between the players). The
   host's game knocks out every member of the HOST (owner 0, the chief too) with the game's own blows (`coop.tribe
   pincer`, an NPC member the attacker); the guest's members keep their health.
2. Whether the game shows the caption is the game's; when it does, play_tribe's rule (play_tribe.GameOver, the same
   object the play uses) must press OK within 25 s of it, and the host bounce back (the caption gone, Tribe play).
   With the members rule (PLAY_TRIBE_OLD_GAMEOVER=1, the rule before the fix) it never fires while a guest member
   lives: the host is still in the caption 40 s later (FAIL).
Key frame `caption`: both games while the caption is up (the host's caption, the guest's play).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover_partner.py
"""
import re
import time

from play_tribe import GameOver, caption_up, tribe_state
from twoinst import Checks, game_runs, keyframe, start


import os
KO_WAIT = float(os.environ.get('GO_PARTNER_KO_WAIT', 60))  # s after the chief's fall without a game over (the guest's live)
LAST_AT = float(os.environ.get('GO_PARTNER_LAST_AT', 84))  # s after the chief's fall: the last member's killing blow. A
# knocked-out chief stands up 91-97 s after its fall (docs/re/tribe.md#tribe-gameover-list): it is still down at the last
# fall (the game over) and up 6-12 s later, around the caption, as in the reported play (BUG-0922's twin needs it up
# within the members rule's 15 s)


def chief_row(con):
    """the player tribe's leader on this game: hp, dead flag, node"""
    for line in con.call('coop.tribe')['out'].splitlines():
        if re.search(r' leader=1 ', line) and not line.lstrip().startswith('#0 '):
            m = re.search(r'#(\d+) .*hp=([\d.]+) .*dead=(\d) bt=(\w+)', line)
            if m:
                return [f'chief #{m.group(1)} hp={m.group(2)} dead={m.group(3)} bt={m.group(4)}']
    return ['no chief']


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def player_rows(con):
    hs = tribe_state(con)
    return ' '.join(f'#{i}:{v["hp"]:.0f}' for i, v in sorted(hs.items()) if v['tribe'] == 'p') + ' ' + counted(con)


def counted(con):
    """the player tribe's second member list (cTribe +0x354..+0x358), whose size the citizen's TakeDamage compares
    with 2 at a member's fall (0xC8F500): under 2 posts the game over (BUG-1083)"""
    out = con.call('coop.tribe').get('out') or ''
    m = re.search(r'tribe \d+ \(player\) .* obj=([0-9A-Fa-f]{8})', out)
    if not m:
        return 'list=?'
    ids = {int(p, 16): i for i, p in re.findall(r'#(\d+) ([0-9A-Fa-f]{8}) owner=', out)}
    t = int(m.group(1), 16)
    r = (con.call(f'mem.read 0x{t + 0x334:08X} 0x28').get('out') or '').split()
    b = bytes(int(x, 16) for x in r if len(x) == 2)
    if len(b) < 0x28:
        return 'list=?'
    beg, end = (int.from_bytes(b[o:o + 4], 'little') for o in (0x20, 0x24))
    n = (end - beg) // 4 if end >= beg else -1
    v = (con.call(f'mem.read 0x{beg:08X} {min(max(n, 0), 32) * 4}').get('out') or '').split() if 0 < n else []
    vb = bytes(int(x, 16) for x in v if len(x) == 2)
    who = [int.from_bytes(vb[o:o + 4], 'little') for o in range(0, len(vb), 4)]
    return (f'list={n} robo={b[0]}/{int.from_bytes(b[4:8], "little")}/{b[8]} [' +
            ' '.join(f'#{ids[w - 0xC0]}' if w - 0xC0 in ids else f'{w:08X}' for w in who) + ']')


T0 = time.time()


def ts():
    return f'[{time.time() - T0:6.1f}]'


def mode(con):
    return (con.call('game.mode').get('out') or '').split()[-1:]


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip()[:80], flush=True)
    time.sleep(6)
    hs = tribe_state(host)
    mine = [i for i, v in hs.items() if v['tribe'] == 'p' and v['owner'] == 0]
    theirs = [i for i, v in hs.items() if v['tribe'] == 'p' and v['owner'] != 0]
    enemy = [i for i, v in hs.items() if v['tribe'] != 'p' and v['hp'] > 0]
    chief = next((int(m) for m in re.findall(r'#(\d+) \S+ owner=0 .* leader=1 ', host.call('coop.tribe')['out'])), None)
    print(f'  host members {mine} (chief #{chief}), guest members {theirs}, an enemy {enemy[:1]}', flush=True)
    if not ck(mine and theirs and enemy, 'the player tribe has the host\'s and the guest\'s members, another tribe one'):
        return ck.result()
    t0, fell = time.time(), None
    while time.time() - t0 < 60:
        hs = tribe_state(host)
        alive = [i for i in mine if i in hs and hs[i]['hp'] > 0]
        if fell is None and chief in hs and hs[chief]['hp'] <= 0:
            fell = time.time()
        if not alive:
            break
        print(f'    {ts()} blows on {alive}: ' + ' '.join(chief_row(host)), flush=True)
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 5000')
        time.sleep(1)
    fell = fell or time.time()
    hs = tribe_state(host)
    living = [i for i in theirs if i in hs and hs[i]['hp'] > 0]
    print(f'  host members with health {[i for i in mine if i in hs and hs[i]["hp"] > 0]}, guest members {living}; '
          f'the chief #{chief} fell {time.time() - fell:.1f} s ago', flush=True)
    ck(living, f'the guest\'s members keep their health ({living})')

    # no game over while the guest's members live (the host's game counts them: lab 20261112-091820), and the knocked-out
    # chief: in the reported play the host's chief lay knocked out and was "back on its feet on the host" (its health
    # over 0, the guest's log) 3 s into the caption, which the members rule took for a living member. The times are counted
    # from the chief's fall (BUG-1083)
    up = False
    while time.time() - fell < KO_WAIT and not up:
        time.sleep(min(5.0, max(0.1, KO_WAIT - (time.time() - fell))))
        up = caption_up(host)
        print(f'    {ts()} {time.time() - fell:3.0f} s after the chief\'s fall: ' + ' '.join(chief_row(host)) + ' | ' +
              player_rows(host), flush=True)
    ck(not up, f'no game over on the host while the guest\'s members live ({KO_WAIT:.0f} s after the chief\'s fall)')
    # 2. the guest's members too, with blows the size of the epic's (it knocked the tribe out in the reported play). The
    # game posts its game over at a member's fall only when fewer than 2 members are still up in its list (cTribe +0x354,
    # 0xC8F500, docs/re/tribe.md#tribe-gameover-list); a fallen member leaves that list at its next behaviour tick, so two
    # members falling in one frame both still count each other and no game over comes at all (BUG-1083, the game's own
    # rule; the old test's two back-to-back blows did that now and then). So one member at a time: the others first, then
    # the last one's killing blow at LAST_AT s after the chief's fall, while the chief still lies knocked out and 6-12 s
    # before it stands up (91-97 s after its fall), as in the reported play.
    # GO_PARTNER_SAME_FRAME=1 (the twin): the killing blows on the last two in one call (one frame).
    same = os.environ.get('GO_PARTNER_SAME_FRAME') == '1'

    def living():
        hs = tribe_state(host)
        return [i for i in theirs if i in hs and hs[i]['hp'] > 0], hs

    def blows(ids, dmg, until, why):
        while time.time() < until and not caption_up(host):
            alive, hs = living()
            ids = [i for i in ids if i in alive]
            if not ids or all(hs[i]['hp'] <= 100 for i in ids) and dmg == 'wound':
                return
            print(f'    {ts()} {why} {ids}: ' + player_rows(host) + ' busy ' + busy(host), flush=True)
            if same and len(ids) > 1 and dmg == 'kill':
                print('     ', host.call(f'coop.tribe pincer {",".join(map(str, ids))} {enemy[0]} 100')['out'][:160], flush=True)
            else:
                for i in ids if dmg == 'wound' else ids[:1]:
                    host.call(f'coop.tribe pincer {i} {enemy[0]} 100')
            time.sleep(0.7 if dmg == 'kill' else 1.5)

    alive, _ = living()
    last = alive[-2:] if same else alive[-1:]
    blows([i for i in alive if i not in last], 'kill', time.time() + 30, 'killing blows on')
    while time.time() - fell < LAST_AT - 2 and not caption_up(host):  # (a wounded member heals ~4 hp a second)
        time.sleep(0.2)
    blows(last, 'wound', time.time() + 10, 'blows on')
    while time.time() - fell < LAST_AT and not caption_up(host):
        time.sleep(0.2)
    print(f'    {ts()} the last killing blow{"s" if same else ""} {time.time() - fell:.1f} s after the chief\'s fall', flush=True)
    blows(last, 'kill', time.time() + 15, 'killing blows on')
    print(f'    {ts()} the guest\'s members are down {time.time() - fell:.1f} s after the chief\'s fall: ' +
          player_rows(host), flush=True)
    rows = [ln.strip()[:120] for ln in host.call('coop.tribe')['out'].splitlines() if re.search(r'dead=1|hp=0\.', ln)]
    print('  knocked out on the host:', *rows[:8], sep='\n    ', flush=True)
    rule = GameOver()
    print(f'  the play\'s game-over rule: {"members (old)" if rule.old else "the caption"}', flush=True)
    t1, seen, pressed, shot = time.time(), None, None, False
    while time.time() - t1 < 70:
        up = caption_up(host)
        if up and seen is None:
            seen = time.time() - t1
        if up and not shot and seen is not None and time.time() - t1 > seen + 1:
            keyframe('caption', 'the host in the game-over caption ("Alas! Your population has fallen below the '
                     'minimum..."), the guest in play: the caption must be closed by the play within seconds', settle=0)
            shot = True
        if int(time.time() - t1) % 3 == 0:
            print(f'    {ts()} caption {up} busy {busy(host)}: ' + ' '.join(chief_row(host)) + ' | ' + player_rows(host),
                  flush=True)
        if rule.due(host):
            host.call('key enter')
            pressed = time.time() - t1
            break
        if seen is not None and time.time() - t1 > seen + 40:
            break
        time.sleep(0.5)
    print(f'  caption on the host after {seen if seen is None else round(seen, 1)} s; OK pressed at '
          f'{pressed if pressed is None else round(pressed, 1)} s', flush=True)
    hs = tribe_state(host)
    print(f'  the play\'s view at the end: player members with health '
          f'{[(i, v["hp"]) for i, v in hs.items() if v["tribe"] == "p" and v["hp"] > 0]}', flush=True)
    if not ck(seen is not None, 'the game over came on the host (every member knocked out)'):
        pass
    else:
        ck(pressed is not None and pressed - seen < 25,
           f'the play\'s rule closes the caption within 25 s (seen {seen:.0f} s, OK at {pressed})')
        t2, back = time.time(), False
        while pressed is not None and time.time() - t2 < 60 and not back:
            back = not caption_up(host) and mode(host) == ['Tribe']
            time.sleep(1)
        ck(back, 'the host bounced back: no caption, Tribe play')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
