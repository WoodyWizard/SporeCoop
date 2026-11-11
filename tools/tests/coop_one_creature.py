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
  5. the guest's own evolution request (`coop.vote evolve` on the guest) opens no editor (refused: the host evolves).
SPORE_FIX_OFF=settings.all (creature.mode reads its default, each player's own): step 2 must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_one_creature.py
"""
import re
import time

import settings_lib as sl
import twoinst
from coop_evolve_in_place import add_part, cinematic, editor_ui, field, host_bundle, in_editor, mode, puppet_key
from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the creature editors' green check


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    editors = []  # every moment the guest's game was in an editor

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
        if not ck(own, f'the guest has creatures of its own ({own[:4]})'):
            return ck.result()
        guest.call(f'coop.creations choose creature {own[0]}')
        ck(sl.wait(lambda: avatar_key(guest) == own[0], 40), f'the guest plays its own creature {own[0]} ({avatar_key(guest)})')
        time.sleep(2)
        # 2. one creature for everyone
        ck(sl.force(host, [guest], 'creature.mode', 'host'), 'the guest got creature.mode host')
        ck(sl.wait(lambda: avatar_key(guest) == h0, 40), f"the guest's avatar plays the host's creature {h0} ({avatar_key(guest)})")
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
        # 5. the guest's own request
        print('  guest:', guest.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
        watch(15)
        ck(not editors, f"no editor opened on the guest's game ({editors[:3]})")
        ck('Creature' in mode(guest) and guest.call('coop.avatar')['ok'], f'the guest plays on ({mode(guest).strip()})')
        shot(guest, 'one_creature_guest.png')
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
