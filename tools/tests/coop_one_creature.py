#!/usr/bin/env python3
"""F5 of 0.7.12 (BUG-0769): one creature for everyone, the session setting creature.mode = host.

Creature fixture, two players (cold: the setting is a lane knob); the guest has creatures of its own:
  1. the guest plays a creature of its own (`coop.creations choose`, as the window's arrows): its avatar's species key
     is not the host's;
  2. the host sets creature.mode host (`coop.settings force`; in a real session the lobby before the start): within
     40 s the guest's own avatar plays the host's creature (the avatar's species key in the guest's game = the host's
     avatar's), its co-op window shows no creation arrows, and its "next creature" changes nothing;
  3. the host evolves (`coop.vote evolve`, the game's evolution request; +40 DNA): its editor opens at once without a
     vote; the guest gets no "Evolve now" offer and its game never opens an editor; the host adds a mouth (a new
     species key) and leaves with done;
  4. the guest's game: its avatar becomes the host's new creature (the avatar's species key = the host's new one)
     without a reload, its puppet of the host shows it too, and in all that time no editor opened on the guest;
  5. the guest's own evolution request (`coop.vote evolve` on the guest) opens no editor (refused: the host evolves);
  6. the same with the game's own request (`coop.vote mate`: the creature stage's handler of the mating call), and
     the guest's creature still walks.
What the guest really sees (BUG-0920, screen_look.py): after every step a screenshot of the guest's game shows the
world (not a black picture) and the game's HUD (the timeline bar, the minimap); `game.mode` and `coop.avatar` said
all was well while the screen was black. After the swaps the guest's health is at most its body's maximum (BUG-0928).
SPORE_FIX_OFF=settings.all (creature.mode reads its default, each player's own): step 2 must FAIL.
SPORE_FIX_OFF=core.evo_flag_clear: steps 5 and 6 must FAIL (a black screen); ONE_CREATURE_PROBE=1 then shows whether
the picture comes back (60 s, input, F9, the game's flag cleared by hand).
SPORE_FIX_OFF=creations.swap_health: the health check of step 1 must FAIL (10 of 16 after the swap).

  SPORE_LANE=6 python3 tools/tests/coop_one_creature.py
"""
import os
import re
import time

import screen_look
import settings_lib as sl
import twoinst
from coop_evolve_in_place import add_part, cinematic, editor_ui, field, host_bundle, in_editor, mode, puppet_key
from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the creature editors' green check


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def hp(con):
    m = re.search(r'hp=(-?[\d.]+)/(-?[\d.]+)', con.call('coop.avatar')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def pos(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(v) for v in m.groups()) if m else None


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    editors = []  # every moment the guest's game was in an editor

    def sees(step, name, tries=3):
        # BUG-0920: what the guest really sees after a step: the world drawn (not black) and the game's own HUD
        s = None
        for i in range(tries):
            s = screen_look.look(guest, name)
            if s is None or (screen_look.drawn(s) and screen_look.hud(s)):
                break
            time.sleep(2)  # a fade or a caption may still run
        print(f'  guest picture {step}: {screen_look.text(s)}; evolution pending flag '
              + guest.call('mem.read 0x1686AF0 1')['out'].strip()[-40:], flush=True)
        if s is None:
            print(f'  (no picture check {step}: no Pillow or no shot)', flush=True)
            return True
        ck(screen_look.drawn(s), f"{step}: the guest's screen shows the world, not a black picture ({screen_look.text(s)})")
        ck(screen_look.hud(s), f"{step}: the guest's screen shows the game's HUD ({screen_look.text(s)})")
        return screen_look.drawn(s) and screen_look.hud(s)

    def watch(secs, until=None):
        t0 = time.time()
        while time.time() - t0 < secs:
            if in_editor(guest):
                editors.append(mode(guest).strip())
            for c in (host, guest):  # the evolution's captions, as a player confirms them
                if cinematic(c):
                    c.call('key enter')
            if until and until():
                return True
            time.sleep(0.5)
        return False

    try:
        time.sleep(3)
        h0 = avatar_key(host)
        # 1. the guest's own creature
        own = re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
        own = [k for k in own if k != h0]
        hfull = hp(guest)
        if not ck(own, f'the guest has creatures of its own ({own[:4]})'):
            return ck.result()
        guest.call(f'coop.creations choose creature {own[0]}')
        ck(sl.wait(lambda: avatar_key(guest) == own[0], 40), f'the guest plays its own creature {own[0]} ({avatar_key(guest)})')
        time.sleep(2)
        h = hp(guest)  # BUG-0928: a full creature is full in its new body (the old number stayed: 10 of 16)
        if hfull and hfull[0] >= hfull[1] - 0.01:
            ck(h and abs(h[0] - h[1]) < 0.5, f"the guest's full health is full in its own creature's body ({hfull} -> {h})")
        sees('step 1 (its own creature)', 'one_creature_s1.png')
        # 2. one creature for everyone
        ck(sl.force(host, [guest], 'creature.mode', 'host'), 'the guest got creature.mode host')
        ck(sl.wait(lambda: avatar_key(guest) == h0, 40), f"the guest's avatar plays the host's creature {h0} ({avatar_key(guest)})")
        time.sleep(2)
        h = hp(guest)  # BUG-0928: the swap keeps the share of health, never more than the new body's maximum
        ck(h and 0 < h[0] <= h[1] + 0.01, f"the guest's health fits its new body after the swap ({h})")
        sees("step 2 (the host's creature)", 'one_creature_s2.png')
        twoinst.keyframe('host_creature', 'creature.mode host: the guest is drawn with the host\'s creature (model and '
                                          'paint) on both screens, both on the ground')
        guest.call('key 120 down')  # F9: the co-op window
        time.sleep(0.3)
        guest.call('key 120 up')
        st = gu.wait(lambda s: s.get('open') == '1', 8, 'the co-op window')
        ck(st and not st['cprev']['vis'] and not st['cnext']['vis'],
           f"the guest's window shows no creation arrows ({st and st['cprev']}, {st and st['cnext']})")
        shot(guest, 'one_creature_window.png')
        print('  guest:', guest.call('coop.creations next creature')['out'].strip()[:100], flush=True)
        time.sleep(3)
        ck(avatar_key(guest) == h0, f"the guest's next creature changes nothing ({avatar_key(guest)})")
        guest.call('key 120 down')
        time.sleep(0.3)
        guest.call('key 120 up')
        # 3. the host evolves
        if float(field(host.call('coop.dna')['out'], r'dna=(-?[\d.]+)') or 0) < 40:
            host.call('coop.dna add 40')
            watch(10)
        print(' ', host.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
        ck(watch(20, lambda: in_editor(host)), f"the host's editor opens at once ({mode(host).strip()})")
        ck('vote active' not in host.call('coop.vote')['out'], 'no vote runs')
        st = gu.state()
        ck(not (st.get('open') == '1' and st['ready']['vis'] and st['ready']['text'] in ('Evolve now', 'Эволюция')),
           f"the guest gets no Evolve now offer ({st.get('open')}, {st.get('ready')})")
        ck(watch(90, lambda: editor_ui(host)), 'the host\'s editor shows its interface')
        sees("step 3 (the host is in its editor)", 'one_creature_s3.png')
        host.call('wait.frames 200 60000')
        time.sleep(3)
        dna = lambda: float(field(host.call('coop.dna')['out'], r'dna=(-?[\d.]+)') or 0)  # noqa: E731
        d_in = dna()
        add_part(host, 780)  # the unlocked mouth (coop_evolve_in_place.py)
        if dna() == d_in:
            add_part(host, 170)
        time.sleep(2)
        host.call('click %d %d' % DONE)
        ck(watch(120, lambda: 'Creature' in mode(host) and host.call('coop.avatar')['ok']), 'the host is back in its stage')
        h1 = avatar_key(host)
        ck(h1 and h1 != h0, f"the host's editor gave it a new creature ({h0} -> {h1})")
        # 4. every player's creature changed
        # (the puppet of the host has the host's newest bundle's key in the guest's game, or the species key itself)
        drawn = lambda: puppet_key(guest, 'creature') in (host_bundle(guest, 'creature')[0], h1)  # noqa: E731
        watch(60, lambda: avatar_key(guest) == h1 and drawn())
        g1, p1, b1 = avatar_key(guest), puppet_key(guest, 'creature'), host_bundle(guest, 'creature')
        print(f'  guest: avatar {g1}, puppet of the host {p1}, the host\'s bundle {b1}; mode {mode(guest).strip()}', flush=True)
        ck(g1 == h1, f"the guest's avatar is the host's new creature ({g1}, host {h1})")
        ck(p1 and p1 in (b1[0], h1) and b1[0] != h0, f"the guest's game draws the host with the new creature ({p1}, bundle {b1})")
        watch(4)
        h = hp(guest)
        ck(h and 0 < h[0] <= h[1] + 0.01, f"the guest's health fits the host's new creature ({h})")
        sees("step 4 (the host's new creature)", 'one_creature_s4.png', tries=8)
        # 5. the guest's own request
        p0 = pos(guest)
        print('  guest:', guest.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
        watch(15)
        ck(not editors, f"no editor opened on the guest's game ({editors[:3]})")
        ck('Creature' in mode(guest) and guest.call('coop.avatar')['ok'], f'the guest plays on ({mode(guest).strip()})')
        seen = sees('step 5 (its own evolution request refused)', 'one_creature_guest.png')
        # 6. the game's own request of the guest (its mating call's handler, not a request built by the mod)
        print('  guest:', guest.call('coop.vote mate')['out'].strip()[:100], flush=True)
        watch(10)
        ck(not editors, f"no editor opened on the guest's game after its game's own request ({editors[:3]})")
        seen = sees("step 6 (its game's own evolution request refused)", 'one_creature_guest6.png') and seen
        guest.call('key w down')  # the guest plays on: its creature walks
        time.sleep(2)
        guest.call('key w up')
        p1 = pos(guest)
        ck(p1 and p0 and dist(p0, p1) > 0.5, f"the guest's creature walks after the refusals ({p0} -> {p1})")
        if not seen and os.environ.get('ONE_CREATURE_PROBE'):  # exploration (BUG-0920): does the picture come back?
            for what, act in (('45 s later', lambda: watch(45)),
                              ('after a movement key', lambda: (guest.call('key w down'), time.sleep(1.5), guest.call('key w up'))),
                              ('after F9 twice', lambda: (guest.call('key 120 tap'), time.sleep(1), guest.call('key 120 tap'))),
                              ('after the flag is cleared by hand', lambda: guest.call('mem.write 0x1686AF0 00'))):
                act()
                time.sleep(2)
                print(f'  probe {what}:', screen_look.text(screen_look.look(guest, 'one_creature_probe.png')),
                      '| flag', guest.call('mem.read 0x1686AF0 1')['out'].strip()[-40:], flush=True)
        shot(host, 'one_creature_host.png')
        twoinst.keyframe('evolved', 'after the host\'s editor: both creatures drawn as the host\'s NEW creature on both '
                                    'screens (not the old one), no editor or caption left open on the guest')
    finally:
        sl.reset([host], ('creature.mode',))
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
