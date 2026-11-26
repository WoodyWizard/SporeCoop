#!/usr/bin/env python3
"""BUG-1578 (GitHub #223): Space / Space Rivals post-editor no-reload & in-session kept-colony restore.

Reproduces GitHub #223 ("Guest loses colonies and homeworld resets when host uses an editor and leaves
the planet"):
  1. Two-game Space Rivals session: guest emigrates to its own homeworld and colonizes a free planet P.
  2. Wait until host's kept colony store (`coop.space map kept`) tracks P (and guest's emigrated homeworld).
  3. Part A (`space.editor_no_reload`):
     - Host opens a Space design editor (`coop.designs editor 4E3F7777` - exact House editor from #223),
       and exits back to Space (while off-planet so any queued Space share would save immediately).
     - ON: host skips post-editor world share (`space_editor_skipped >= 1`), `world_offers_sent` and
       `world_offers_recv` stay unchanged, guest NEVER leaves Space (`world_epoch` unchanged), and P
       remains guest's colony untouched.
     - Twin OFF (`SPORE_FIX_OFF=space.editor_no_reload`): host sends a full MSG_WORLD reload after the
       editor -> MUST FAIL.
  4. Part B (`space.keep_colonies_reload`):
     - Host triggers a real in-session reload (`coop.share`) WITHOUT disconnecting the guest.
     - Guest passes through Galaxy Menu -> Loading -> Space with the same player id (id=1).
     - ON: host detects guest leaving Space (`reloads >= 1`), re-sends K_KEPT + K_KEPT_COLONY upon
       guest's return, and P is restored as a tech-5 colony of guest's empire (also in host roster).
     - Twin OFF (`SPORE_FIX_OFF=space.keep_colonies_reload`): host never unbinds guest, guest's fresh
       empty roster overwrites kept colonies with [], and P is lost after reload -> MUST FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_space_editor_colonies.py [--part a|b|both]
"""
import re
import sys
import time

from coop_space_colony_rejoin import epoch, planet_line
from coop_space_down_editor import trip_outfitter
from coop_space_map import no_movie, rosters, sp, wait
from harness import mode_strip as mode
import twoinst
from twoinst import Checks, start


def sr_status(con):
    out = sp(con, 'rivals status')
    return {k: v for k, v in re.findall(r'(\w+)=([0-9A-Fa-f.-]+)', out)}, out


def world_stats(con):
    st = con.call('coop.status').get('out') or ''
    m_off = re.search(r'world_offers_sent=(\d+) world_offers_recv=(\d+)', st)
    m_sp = re.search(r'space_editor_no_reload: fix=(\d+) space_editor_skipped=(\d+)', st)
    return {
        'offers_sent': int(m_off.group(1)) if m_off else -1,
        'offers_recv': int(m_off.group(2)) if m_off else -1,
        'space_fix': int(m_sp.group(1)) if m_sp else -1,
        'space_skipped': int(m_sp.group(2)) if m_sp else -1,
    }


def open_and_exit_space_editor(host, guest):
    # Open and exit the exact Space design editor 4E3F7777 (House Building) from GitHub #223
    out = (host.call('coop.designs editor 4E3F7777').get('out') or '').strip()
    print('  host editor request:', out, flush=True)
    # Wait briefly for the design vote to start, then pass it with `coop.vote go`
    v_up = wait(lambda: 'vote active' in (host.call('coop.vote').get('out') or ''), 6.0, 0.3)
    if v_up:
        host.call('coop.vote go')
    opened = wait(lambda: 'Editor' in mode(host) or 'Edit' in mode(host), 25.0, 0.5)
    print('  host entered editor:', bool(opened), mode(host), flush=True)
    if not opened:
        return False
    time.sleep(2.5)
    for _ in range(12):
        if 'Space' in mode(host):
            break
        host.call('click 1380 1062')
        time.sleep(1.5)
        if 'Space' not in mode(host):
            host.call('key esc')
            time.sleep(1.5)
    back = wait(lambda: 'Space' in mode(host), 35.0, 0.5)
    print('  host back in Space:', bool(back), mode(host), flush=True)
    return bool(back)


def main():
    part = 'both'
    if '--part' in sys.argv:
        part = sys.argv[sys.argv.index('--part') + 1]

    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    for c in (host, guest):
        no_movie(c)

    # Enable Space Rivals and wait for guest emigration
    host.call('coop.rivals mode space rivals')
    emig = wait(lambda: sr_status(guest)[0].get('emigrated') == '1' and
                sr_status(guest)[0].get('teleported') == '1', 40, 1.0)
    print('  guest space_rivals:', sr_status(guest)[1].strip(), flush=True)
    ck(emig is not None, 'guest emigrated to its own Space Rivals homeworld')
    if emig is None:
        return ck.result()
    time.sleep(2.0)

    # Guest colonizes a free planet in its current (home) star system
    out = sp(guest, 'allies colonize')
    print('  guest colonize:', out.splitlines()[0] if out else '-', flush=True)
    m = re.match(r'colonized ([0-9A-F]+)', out)
    ck(m is not None, 'guest founded a colony P in its system')
    if not m:
        return ck.result()
    planet = m.group(1)
    tech, civs = planet_line(guest, planet)
    gpol = civs[-1] if civs else None
    print(f'  guest colony {planet}: tech={tech} pol={gpol}', flush=True)

    def host_has_colony():
        return [c for c in rosters(host).get(1, {}).get('colonies', []) if c[0] == planet and c[2]]

    ck(wait(host_has_colony, 30, 1.0) is not None, f'host roster has guest colony {planet}')
    kept0 = sp(host, 'map kept')
    print('  host map kept before:', kept0.replace('\n', ' | '), flush=True)
    ck(planet in kept0, f'host kept store recorded guest colony {planet}')

    # ---- Part A: host uses Space editor -> must NOT trigger full MSG_WORLD reload ----
    if part in ('a', 'both'):
        hs0 = world_stats(host)
        gs0 = world_stats(guest)
        e0 = epoch(guest)
        ck(open_and_exit_space_editor(host, guest), 'host opened and exited Space design editor 4E3F7777')
        time.sleep(10.0)
        hs1 = world_stats(host)
        gs1 = world_stats(guest)
        e1 = epoch(guest)
        print('  after editor: host=', hs1, 'guest=', gs1, f'epoch {e0}->{e1}', flush=True)
        ck(hs1['space_skipped'] > hs0['space_skipped'],
           f'host skipped post-editor Space world share ({hs0["space_skipped"]} -> {hs1["space_skipped"]})')
        ck(hs1['offers_sent'] == hs0['offers_sent'],
           f'host sent 0 world offers after Space editor ({hs0["offers_sent"]} -> {hs1["offers_sent"]})')
        ck(gs1['offers_recv'] == gs0['offers_recv'] and e1 == e0,
           f'guest never reloaded after host Space editor (recv {gs0["offers_recv"]}->{gs1["offers_recv"]}, epoch {e0}->{e1})')
        t_a, c_a = planet_line(guest, planet)
        ck(t_a == 5 and gpol in c_a, f'guest colony {planet} intact after host Space editor (tech={t_a} civs={c_a})')

    # ---- Part B: genuine in-session reload (`coop.share`, no disconnect) restores guest colonies ----
    if part in ('b', 'both'):
        e_before = epoch(guest)
        share_out = (host.call('coop.share').get('out') or '').strip()
        print('  host coop.share:', share_out, flush=True)
        reloaded = wait(lambda: epoch(guest) > e_before and 'Space' in mode(guest), 180, 2.0)
        ck(reloaded is not None, f'guest completed in-session world reload (epoch {e_before} -> {epoch(guest)})')
        re_emig = wait(lambda: sr_status(guest)[0].get('emigrated') == '1' and
                       sr_status(guest)[0].get('teleported') == '1', 60, 1.0)
        ck(re_emig is not None, 'guest settled back on its emigrated Space Rivals homeworld after reload')
        no_movie(guest)

        def back():
            t, c = planet_line(guest, planet)
            return (t, c) if t == 5 and gpol in c else None

        restored = wait(back, 50, 2.0)
        done_ok = wait(lambda: 'done=1' in sp(host, 'map kept'), 30, 1.0)
        t_b, c_b = planet_line(guest, planet)
        kept_h = sp(host, 'map kept').replace('\n', ' | ')
        kept_g = sp(guest, 'map kept').splitlines()[0]
        print(f'  after in-session reload: planet {planet} tech={t_b} civs={c_b} | host: {kept_h} | guest: {kept_g}',
              flush=True)
        ck(restored is not None,
           f'guest colony {planet} restored after in-session reload without disconnect (tech={t_b} civs={c_b} pol={gpol})')
        ck(done_ok is not None, f'kept restore handshake finished cleanly (done=1, pending=0; {kept_h})')
        ck(wait(host_has_colony, 25, 1.0) is not None,
           f'host roster shows guest colony {planet} again after in-session reload')
        m_rel = re.search(r'reloads=(\d+)', kept_h)
        ck(m_rel is not None and int(m_rel.group(1)) >= 1,  # counter: real planet tech=5 and pol asserted on guest above
           f'host detected in-session reload and armed kept restore ({kept_h})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
