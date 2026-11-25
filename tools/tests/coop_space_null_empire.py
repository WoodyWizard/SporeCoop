#!/usr/bin/env python3
"""Space: entering a star system with a tech-5 planet whose star has no empire (mEmpireID == -1 or
missing cEmpire) does not crash in cEmpire::IsContactedByPlayer (0xC30A70, null this) (BUG-1544, GitHub #222).

1. Space fixture: check `coop.empirecontact` reports `hooked=1`.
2. Pick a free (unowned) star near home, find a planet P in it, and fly back to home.
3. Mark P as an orphaned tech-5 planet (`coop.empirecontact orphan <P>`, star_empire=FFFFFFFF).
4. Call `coop.empirecontact probe <P>` (exercises Simulator::GetMainSpeciesImageKey) and then fly
   into that star system (exercises UI::SpaceGameUI::Load -> FillStarTooltipPlanetInfo ->
   GetMainSpeciesImageKey -> cEmpire::IsContactedByPlayer(nullptr)).
   - PASS: returns cleanly, host enters the star system, `guarded >= 1`, 0 faults.
   - SPORE_FIX_OFF=space.null_empire_contact (`space_null_empire_off`): crashes at 0xC30A70 reading
     0x00000084: must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_null_empire.py
"""
import re
import sys
import time

from coop_space_allies import no_movie, place, sp, travel
from twoinst import Checks, start


def first_free_planet(con, star):
    out = sp(con, f'allies star {star}')
    for ln in out.splitlines():
        m = re.search(r'\bplanet ([0-9A-F]{8})\b', ln)
        if m and ('tech=0' in ln or 'tech=1' in ln or 'pol=FFFFFFFF' in ln):
            return m.group(1)
    star_hex = f'{star:06X}'
    for ln in out.splitlines():
        m = re.search(rf'\bplanet ([0-9A-F]{{2}}{star_hex})\b', ln)
        if m:
            return m.group(1)
    return None


def guarded_count(con):
    out = con.call('coop.empirecontact')['out'].strip()
    m = re.search(r'guarded=(\d+)', out)
    return int(m.group(1)) if m else -1, out


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        no_movie(c, esc=True)
    home = place(host)[1]

    g0, st0 = guarded_count(host)
    print('  initial:', st0, flush=True)
    ck('hooked=1' in st0, f'null empire contact guard hooked ({st0})')

    free = [int(x) for x in re.findall(r'free (\d+)', sp(host, 'allies free'))]
    ck(bool(free), f'free stars near home: {free[:4]}')
    if not free:
        return ck.result()
    star = free[1] if len(free) > 1 else free[0]

    # Discover a planet in the free star, then return home so entering it later triggers SpaceGameUI::Load
    ck(travel(host, star, 'host') is not None, f'host flew to free star {star}')
    pid = first_free_planet(host, star)
    ck(pid is not None, f'found planet {pid} in free star {star}')
    if not pid:
        return ck.result()
    ck(travel(host, home, 'host') is not None, 'host returned home')

    orphan_out = host.call(f'coop.empirecontact orphan {pid}')['out'].strip()
    print('  orphan:', orphan_out, flush=True)
    ck('-> 5' in orphan_out and 'star_empire=FFFFFFFF' in orphan_out,
       f'marked planet {pid} tech 5 on unowned star ({orphan_out})')

    # 1. Direct GetMainSpeciesImageKey probe
    try:
        probe_out = host.call(f'coop.empirecontact probe {pid}')['out'].strip()
        print('  probe:', probe_out, flush=True)
        ck(f'empirecontact probe {pid}:' in probe_out, f'GetMainSpeciesImageKey returned cleanly ({probe_out})')
    except Exception as e:
        ck(False, f'GetMainSpeciesImageKey probe crashed or disconnected: {e}')
        return ck.result()

    g1, st1 = guarded_count(host)
    ck(g1 >= g0 + 1, f'guard caught null empire on probe ({st1})')

    # 2. Actually enter that star system (real UI::SpaceGameUI::Load -> FillStarTooltipPlanetInfo + SpaceGameUI::Update)
    try:
        arr = travel(host, star, 'host')
        ck(arr is not None, f'host entered star system {star} with orphaned tech-5 planet')
        time.sleep(1.5)
    except Exception as e:
        ck(False, f'entering star system {star} crashed or disconnected: {e}')
        return ck.result()

    g2, st2 = guarded_count(host)
    print('  after enter:', st2, flush=True)
    m_ui = re.search(r'ui_guarded=(\d+)', st2)
    ui_g = int(m_ui.group(1)) if m_ui else 0
    ck(g2 >= g1 + 1, f'cEmpire::IsContactedByPlayer guard fired on system enter ({st2})')
    ck(ui_g >= 1, f'SpaceGameUI empire badge guard fired on system update ({st2})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
