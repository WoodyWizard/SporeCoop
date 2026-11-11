#!/usr/bin/env python3
"""Tribe: the guest's roster shows the portraits of a species the host made in the session (BUG-0921).

Key-frame review of the 0.7.12 final check (campaign, campaign_ct): after Cell -> Creature -> Tribe every guest's
tribe roster (the member slots at the right edge) showed plain white squares, the host's had the portraits. The
species of that tribe is the host's own creation, made in the session (the Cell -> Creature change, the host's
editors). The guest registers the host's creation bundle under the host's own key above every package, and that
bundle had the model and the metadata only: the picture the roster asks for by the species key
({0x2F7D0004, 0x40626200, species}) was not in the guest's game. BUG-0643's test (tribe_portraits) puts another
creation of the host on the tribe by a command and checks the resource only; this one goes the players' way and
looks at the screen.

1. Two games on the cell fixture; the campaign's stage changes (coop_campaign.Campaign.advance: the players'
   buttons, votes and editors) Cell -> Creature -> Tribe.
2. In Tribe both games are shot; the roster's member slots are found in the picture by their frame (the gold top
   edge of a slot at the right edge of the screen), a slot whose inside is plain white is a white square.
PASS: the guest's game answers the picture of its player tribe's species, the guest's screen shows the roster
(2 slots or more) and no slot of it is a white square.

  SPORE_LANE=7 python3 tools/tests/coop_roster_portraits.py   (SPORE_FIX_OFF=creations.bundle_png must FAIL)
"""
import argparse
import os
import re
import time

import lane
import twoinst
from coop_campaign import Campaign, Player
from twoinst import Checks, shot, start


def roster_slots(path):
    """the roster's slots in a 1440x1080 shot: [(top y, share of near-white pixels inside)]. A slot's frame starts
    with a gold row (about 253 224 163) across the column x 1403..1429; its picture is the 21 rows from 2 below it"""
    from PIL import Image
    im = Image.open(path).convert('RGB')
    w, h = im.size
    sx, sy = w / 1440.0, h / 1080.0
    px = im.load()
    x0, x1 = int(1400 * sx), int(1432 * sx)
    tops = []
    for y in range(int(540 * sy), int(1030 * sy)):
        gold = 0
        for x in range(x0, x1):
            r, g, b = px[x, y]
            gold += r > 238 and 198 < g < 236 and 135 < b < 180
        if gold >= 0.45 * (x1 - x0) and (not tops or y - tops[-1] > 10 * sy):
            tops.append(y)
    out = []
    for y in tops:
        box = [px[x, yy] for yy in range(y + int(3 * sy), y + int(22 * sy)) for x in range(int(1407 * sx), int(1428 * sx))]
        white = sum(1 for r, g, b in box if r > 245 and g > 245 and b > 245) / max(1, len(box))
        out.append((y, white))
    return out


def png_known(con, inst):
    m = re.search(r'known=(\d)', con.call(f'coop.creations png {inst:08X}')['out'])
    return bool(m) and m.group(1) == '1'


def tribe_species(con):
    m = re.search(r'tribe \d+ \(player\) [^\n]*? (\d+) members species=(\w+)', con.call('coop.tribe')['out'])
    return (int(m.group(1)), int(m.group(2), 16)) if m else (0, 0)


def main():
    host, guest = start('cell')
    ck = Checks()
    a = argparse.Namespace(start='cell', end='tribe', minutes=0, advance_minutes=None, players=2, noise=0.0,
                           rejoins=False, finale=False, finale_from_galaxy=False, jump=False, seed=1)
    players = [Player('Alice', lane.HOST), Player('Bob', lane.GUEST, lane.JOIN)]
    camp = Campaign(a, players, lane.out_dir('campaign', time.strftime('%Y%m%d-%H%M%S')), 1)
    camp.ids()
    camp.health()
    ck(camp.advance('cell'), 'Cell -> Creature: both play the Creature stage (the species is made in the session)')
    ck(not camp.dead and camp.advance('creature'), 'Creature -> Tribe: both play the Tribe stage')
    camp.stop = True
    if camp.dead:
        ck(False, 'a game died on the way')
        return ck.result()
    # the roster is on the screen once the intro is over and the player tribe exists
    slots = {}
    paths = {}
    t0 = time.time()
    while time.time() - t0 < 120:
        for who, con in (('guest', guest), ('host', host)):
            con.call('key enter')  # a caption of the new stage
            paths[who] = shot(con, f'roster_portraits_{who}.png')
            slots[who] = roster_slots(paths[who]) if os.path.exists(paths[who]) else []
        if len(slots['guest']) >= 2 and tribe_species(guest)[0] >= 2 and time.time() - t0 > 15:
            break
        time.sleep(4)
    twoinst.keyframe('roster', 'the tribe roster (the member slots at the right edge) shows the portraits of the '
                               'species made in the session on the guest as on the host: no white squares')
    for who, con in (('host', host), ('guest', guest)):
        n, sp = tribe_species(con)
        white = [y for y, wsh in slots[who] if wsh > 0.8]
        print(f'  {who}: player tribe of {n} members, species {sp:08X}, picture of it known={int(png_known(con, sp))}; '
              f'roster slots on the screen {len(slots[who])}, white squares {len(white)} '
              f'(white share per slot: {" ".join(f"{wsh:.2f}" for _, wsh in slots[who])})', flush=True)
    gn, gsp = tribe_species(guest)
    hn, hsp = tribe_species(host)
    gwhite = [y for y, wsh in slots['guest'] if wsh > 0.8]
    hwhite = [y for y, wsh in slots['host'] if wsh > 0.8]
    ck(gsp and gsp == hsp, f'the guest\'s player tribe is of the host\'s species ({gsp:08X} / {hsp:08X})')
    ck(png_known(host, hsp), f'the host\'s game has the picture of its species {hsp:08X}')
    ck(png_known(guest, gsp), f'the guest\'s game answers the picture of the species {gsp:08X} (what the roster draws)')
    ck(len(slots['guest']) >= 2, f'the guest\'s screen shows the roster ({len(slots["guest"])} slots found)')
    ck(not gwhite, f'no slot of the guest\'s roster is a white square ({len(gwhite)} of {len(slots["guest"])} white)')
    ck(not hwhite, f'no slot of the host\'s roster is a white square ({len(hwhite)} of {len(slots["host"])} white)')
    for it in camp.F.items:
        print(f'  campaign finding [{it["kind"]}] {it["stage"]}: {it["text"][:160]}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
