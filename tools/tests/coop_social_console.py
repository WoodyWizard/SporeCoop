#!/usr/bin/env python3
"""BUG-0149: digits typed in the cheat console are no social ability keys.

The guest engages a mirrored Kook (social stance, a click on it, as in coop_creature_social.py),
opens the cheat console (Ctrl+Shift+C) and types 1 and 2 there; after the console is closed it
presses 1 again. Judged on what the games play (BUG-0891: before, only the host's mod counter
`acts_in`, now printed for diagnostics): the guest's avatar and the host's puppet of it
(`coop.npc cur`: the game's current animation, `cur avatar` / `cur p<id>`) are sampled every
0.1 s before (idle), while the console is open and after it closed. The ability's animations are
the ones the press after the close brings that the idle sampling never showed; none of them may
play on either game while the console was open, and the press after the close must bring one.

  coop_social_console.py [--without-fix]   (--without-fix: coop.social textfix off, must FAIL)

The console opens only while the cheat manager's flag +0x64 is set (cCheatManager vt 0x4C,
SetEnabled; AppShortcuts_OnKeyDown -> vt 0x44 -> 0x67E730 returns at once without it). It is 0 on
the lane games, which load their save by command, not through the galaxy menu's UI (the GGE UI
code at 0xDEA3D0 sets it to 1, 0xDEB450 to 0): the test sets it first, as the game does
(docs/re/creature.md#creature-cheat-console-flag).
"""
import re
import sys
import time

from coop_creature_social import KOOK, engage, hold, num, social
from twoinst import Checks, shot, start


def enable_console(con):
    """the cheat manager's enabled flag (+0x64), which the galaxy menu's UI sets for a player"""
    m = re.search(r'eax=0x([0-9A-F]+)', con.call('call App::ICheatManager::Get cdecl')['out'])
    if m:
        con.call(f'call App::cCheatManager::func4Ch thiscall 0x{m.group(1)} 1')


def console_open(con):
    return '014A4606 none' not in con.call('coop.ui where 014A4606')['out']


def close_console(con):
    """Esc closes it (Ctrl+Shift+C does not); the pause menu Esc may open as well is closed"""
    from uifind import press, shown
    con.call('key esc')
    time.sleep(1.5)
    if shown(con, 'Resume Game'):
        press(con, 'Resume Game')
        time.sleep(1)


def console(con):
    for k in ('key ctrl down', 'key shift down', 'key c', 'key shift up', 'key ctrl up'):
        con.call(k)
        time.sleep(0.2)
    time.sleep(1.0)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    if without:
        print(' ', guest.call('coop.social textfix off')['out'].strip())
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(3)
    mirrored = set(re.findall(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' spawned', guest.call('coop.npc list')['out']))
    kooks = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                             host.call('coop.npc list')['out'])
             if m.group(1) in mirrored and float(m.group(2)) > 0]
    target = None
    for k in kooks[:3]:
        if engage(guest, 'n' + k):
            target = k
            break
    ck(target is not None, f'the guest engaged a mirrored Kook (n{target})')
    if not target:
        return ck.result()

    def acts():
        return num(r'acts_in=(\d+)', social(host), 0)

    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'

    def sample(secs, into_g, into_h):
        t0 = time.time()
        while time.time() - t0 < secs:
            a = re.search(r'^cur avatar anim=([0-9A-F]{8})', guest.call('coop.npc cur')['out'], re.M)
            b = re.search(r'^cur p' + gid + r' anim=([0-9A-F]{8})', host.call('coop.npc cur')['out'], re.M)
            if a:
                into_g.add(a.group(1))
            if b:
                into_h.add(b.group(1))
            time.sleep(0.1)

    idle_g, idle_h = set(), set()
    sample(4.0, idle_g, idle_h)
    print(f'  idle: guest avatar {sorted(idle_g)}, host puppet {sorted(idle_h)}', flush=True)
    ck(idle_g and idle_h, 'the guest\'s avatar and the host\'s puppet of it have an animation')
    enable_console(guest)
    console(guest)
    keys = guest.call('coop.social keys')['out'].strip()
    print('  guest:', keys)
    ck(console_open(guest), 'the cheat console is open on the guest (Ctrl+Shift+C)')
    a0 = acts()
    open_g, open_h = set(), set()
    for vk in (0x31, 0x32, 0x31):  # "121" typed into the console
        hold(guest, vk)
        sample(2.0, open_g, open_h)
    a1 = acts()
    print(f'  while the console was open: guest avatar {sorted(open_g)}, host puppet {sorted(open_h)}; '
          f'(host acts_in {a0} -> {a1})', flush=True)
    shot(guest, 'social_console_guest.png')
    close_console(guest)
    ck(not console_open(guest), 'the cheat console is closed again')
    time.sleep(1)
    guest.call(f'coop.social goto {target}')
    for _ in range(3):  # (the engagement may have run out meanwhile: engage again)
        if re.search(r'engaged=' + target + r'\b', social(guest)) or engage(guest, 'n' + target, 20):
            break
    sample(2.0, idle_g, idle_h)  # (idle again after the walk)
    a2 = acts()
    after_g, after_h = set(), set()
    hold(guest, 0x31)
    sample(3.0, after_g, after_h)
    a3 = acts()
    abil_g, abil_h = after_g - idle_g, after_h - idle_h
    print(f'  after the close, key 1: guest avatar {sorted(after_g)} (new: {sorted(abil_g)}), host puppet {sorted(after_h)} '
          f'(new: {sorted(abil_h)}); (host acts_in {a2} -> {a3})', flush=True)
    ck(abil_g, 'the key 1 plays the ability on the guest\'s avatar once the console is closed (a new animation)')
    ck(abil_h, 'the host\'s puppet of the guest plays it too')
    ck(not (abil_g & open_g), f'digits typed in the cheat console play no social ability on the guest\'s avatar '
       f'({sorted(abil_g & open_g)})')
    ck(not (abil_h & open_h), f'nor on the host\'s puppet of it ({sorted(abil_h & open_h)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
