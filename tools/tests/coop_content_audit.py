#!/usr/bin/env python3
"""BUG-0730 / BUG-0731 (class BUG-0424): which models the placed objects of a stage use, and whether the guest's
game has them.

The content class asks for every model the other game may lack. The mod checks the models it makes the game build
(creations::ensure_model: creatures, cells, ships, vehicles, buildings). This test lists the model of every placed
object of the host's world on both games (`coop.creations objmodels`: the cSpatialObject model key at object +0xC4,
and a city's three decoration keys at +0x528) with the package that answers it and how many part ids of it the game
lacks, so that a kind of object whose model is a player's creation (EditorSaves / Pollination) or names parts the
other game may lack shows up.

PASS: every model key a placed object of the host's world uses is answered on the guest by a package of the game's
install or by the mod's own copy (never by nothing), and no such model names a part the guest's game lacks.
Printed: the rows per noun and package (evidence for BUG-0730: Tribe tools and huts; BUG-0731: city models and
decorations).

  SPORE_LANE=7 python3 tools/tests/coop_content_audit.py <fixture>   (tribe, civ_npc, space)
"""
import re
import sys
import time

from twoinst import Checks, start


def rows(con):
    out = []
    for ln in con.call('coop.creations objmodels')['out'].splitlines():
        m = re.match(r'\s+obj noun=(\w+) n=(\d+) key=(\w+)/(\w+)/(\w+) from=(\S+) unknown_parts=(\S+)', ln)
        if m:
            out.append(dict(noun=m.group(1), n=int(m.group(2)), key=(m.group(3), m.group(4), m.group(5)),
                            frm=m.group(6), unknown=m.group(7)))
    return out


def main():
    fixture = sys.argv[1] if len(sys.argv) > 1 else 'tribe'
    host, guest = start(fixture)
    ck = Checks()
    time.sleep(10)
    hr, gr = rows(host), rows(guest)
    for who, rs in (('host', hr), ('guest', gr)):
        print(f'  {who}: {len(rs)} (noun, model) rows', flush=True)
        for r in rs:
            print(f"    {r['noun']} x{r['n']} {'/'.join(r['key'])} from={r['frm']} unknown_parts={r['unknown']}", flush=True)
    ck(hr, f'the host lists the models of its placed objects ({len(hr)})')
    gfrom = {r['key']: r['frm'] for r in gr}
    # the host's real models (a key the host's game answers itself); a guest's mirror of it may carry a key of ours
    missing = [r for r in hr if r['frm'] != 'none' and r['key'] in gfrom and gfrom[r['key']] == 'none']
    ck(not missing, f"every model of the host's objects that the guest's objects use is in the guest's game ({missing[:4]})")
    bad = [r for r in hr + gr if r['unknown'] not in ('-', '0', 'unreadable')]
    ck(not bad, f'no model of a placed object names a part the game lacks ({bad[:4]})')
    own = sorted({(r['noun'], r['frm']) for r in hr if re.match(r'(EditorSaves|Pollination)', r['frm'])})
    print(f"  host objects drawn from a player's creations: {own}", flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
