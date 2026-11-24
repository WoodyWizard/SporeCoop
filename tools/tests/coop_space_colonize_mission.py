#!/usr/bin/env python3
"""Space: when an allied partner colonizes the target planet of a colonize mission (such as the initial
tutorial mission 0x398ACA5E), the remaining player's colonize objective completes without destroying the
partner's colony (BUG-1539).

A player report (0.7.16.1, Space stage) had player 1 complete the initial colonize-planet tutorial mission;
player 2's mission stayed stuck because cMissionColonize::Update (0x00C4EAD0) only checked whether the
local player's empire owned the target planet.

1. Space fixture (non-Rivals). Host flies to a free star near home and finds an uncolonized planet P.
2. Guest creates and accepts the colonize tutorial mission targeting P (`coop.space colonize_mission create <P>`):
   verify the mission is active (`state=3 col_state=0 target=<P>`).
3. Host colonizes P (`coop.space allies colonize <P>`) and returns home; guest receives the partner colony
   mirror (`partner_col=1`).
4. Wait up to 15 s for `cMissionColonize::Update` on guest:
   - PASS: guest's mission advances out of `col_state=0` (`col_state=2` or `state=5`, `partner_done >= 1`),
     and the host's colony stays intact (`Alice's empire`).
   - SPORE_FIX_OFF=space.colonize_mission (space_colonize_mission_off): guest's mission stays stuck at
     `state=3 col_state=0` (`partner_done=0`): must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_colonize_mission.py
"""
import re
import time

from coop_space_allies import no_movie, place, sp, star_info, travel, wait
from twoinst import Checks, start


def first_free_planet(con, star):
    out = sp(con, f'allies star {star}')
    # lines like: "  planet 0046000C 'Esaris' tech=1 loaded(pol=FFFFFFFF)"
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


def mission_state(con):
    out = sp(con, 'colonize_mission')
    m_line = re.search(r'mission id=([0-9A-F]{8}) state=(-?\d+) col_state=(\d+) target=([0-9A-F]{8}) partner_col=(\d+)', out)
    m_sum = re.search(r'colonize_mission: count=(\d+) fix=(\d+) partner_done=(\d+) tut_epoch=(\d+)', out)
    res = {'raw': out.strip().replace('\n', ' | ')}
    if m_line:
        res['id'] = m_line.group(1)
        res['state'] = int(m_line.group(2))
        res['col_state'] = int(m_line.group(3))
        res['target'] = m_line.group(4)
        res['partner_col'] = int(m_line.group(5))
    if m_sum:
        res['count'] = int(m_sum.group(1))
        res['fix'] = int(m_sum.group(2))
        res['partner_done'] = int(m_sum.group(3))
        res['tut_epoch'] = int(m_sum.group(4))
    return res


def main():
    host, guest = start('space_free')
    ck = Checks()
    for c in (host, guest):
        no_movie(c, esc=True)
    home = place(host)[1]

    # 1. Find a free star near home and an uncolonized planet P in it
    free = [int(x) for x in re.findall(r'free (\d+)', sp(host, 'allies free'))]
    ck(bool(free), f'free stars near home: {free[:4]}')
    if not free:
        return ck.result()
    star = free[1] if len(free) > 1 else free[0]
    ck(travel(host, star, 'host') is not None, f'host flew to free star {star}')
    star_out = sp(host, f'allies star {star}')
    print('  host star:', star_out.strip().replace('\n', ' | '), flush=True)
    pid = first_free_planet(host, star)
    ck(pid is not None, f'found uncolonized planet {pid} in star {star}')
    if not pid:
        return ck.result()

    # 2. Guest creates and accepts the colonize tutorial mission targeting P
    create_out = sp(guest, f'colonize_mission create {pid}')
    print('  guest mission created:', create_out.strip().replace('\n', ' | '), flush=True)
    m0 = mission_state(guest)
    ck(m0.get('id') == '398ACA5E' and m0.get('target') == pid and m0.get('col_state') == 0 and m0.get('state') == 3,
       f'guest has active colonize tutorial mission targeting {pid} ({m0["raw"]})')
    if m0.get('target') != pid:
        return ck.result()

    # 3. Host colonizes P and flies home; guest receives the partner colony mirror
    col_out = sp(host, f'allies colonize {pid}')
    print('  host colonized:', col_out.splitlines()[0] if col_out else '', flush=True)
    ck(f'colonized {pid}' in col_out, f'host colonized target planet {pid}')
    ck(travel(host, home, 'host') is not None, 'host flew home')

    gi = wait(lambda: (lambda i: i if i and i[1] == "Alice's empire" else None)(star_info(guest, star)), 30)
    ck(gi is not None, f'guest sees partner colony on star {star} ({gi})')
    if not gi:
        return ck.result()

    # 4. Wait for cMissionColonize::Update to recognize partner's colony and advance objective
    done = wait(lambda: (lambda st: st if (st.get('col_state') == 2 or st.get('state') == 5 or st.get('count') == 0) else None)(mission_state(guest)), 15)
    m1 = mission_state(guest)
    print('  guest mission after partner colonized:', m1['raw'], flush=True)
    ck(done is not None,
       f'guest colonize mission advanced after partner colonized {pid} ({m1["raw"]})')
    ck(m1.get('partner_done', 0) >= 1,  # counter: confirms our hook completed the colonize objective; col_state/state checked above
       f'partner-colonized mission hook fired ({m1["raw"]})')

    # Verify host's colony is untouched
    gi_after = star_info(guest, star)
    ck(gi_after is not None and gi_after[1] == "Alice's empire",
       f'partner colony remains intact ({gi_after})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
