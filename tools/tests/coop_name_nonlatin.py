#!/usr/bin/env python3
"""BUG-0897: a guest whose name is not in Latin letters must be called by that name on the host, not "Player".

The owner saw "+4 DNA from Player's hunt" in a player's recording although the guest's nickname was Cyrillic. The
host's notices take the name from the session roster (net::Session::name_of; the default of [coop] name is
"Player"). The guest gets a Cyrillic name three ways, one round each, and joins the host again with it:
 1. typed into the co-op window's name field (the name step, `coop.uistate first`; the field gets WM_CHAR, as the
    game's own text input; Continue saves it);
 2. written into mods\\coop.ini as UTF-8 by an editor ([coop] name=...) before the games start (SporeKit converts the
    file at the mod's start; `coop.name reload` then takes the name as the start does: the lab's join names the
    guest Bob);
 3. the same in the ANSI code page cp1251 (an old Notepad on a Russian Windows). SporeKit reads a file that is not
    valid UTF-8 in the system's ANSI code page: on a Russian Windows that is cp1251 (the name comes back), in the
    lab (Wine, ANSI 1252) the letters come back as other letters. The round checks that the name is not lost
    ("Player") and that both games show the same text.
On the HOST after each join: the session's player list (`coop.status` "version <id> '<name>'"), the lobby row
(`coop.uistate` rows) and the notice "<name> joined the pack" (the game's notice panel) name the guest so, never
"Player" or "guestN"; the guest's own name (`coop.name`) is the same.
The lane's coop.ini is put back byte for byte at the end.

  SPORE_LANE=5 python3 tools/tests/coop_name_nonlatin.py
"""
import os
import re
import time

import lane
import twoinst
from sporectl import Console
from twoinst import Checks, start
from coop_ui import Ui

INI = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'coop.ini')
TYPED, UTF8, ANSI = 'Иван Петров', 'Пётр', 'Ольга'


def toasts(con):
    return re.findall(r"^\s*toast \S* \([^)]*\) '(.*?)'(?: \||$)", con.call('coop.status')['out'], re.M)


def roster(con):
    return re.findall(r"^version (\d+) '(.*)': ", con.call('coop.status')['out'], re.M)


def write_ini_name(old, name, encoding):
    """the lane's coop.ini with [coop] name=<name>, written in `encoding` (SporeKit may have made it UTF-16)"""
    text = old.decode('utf-16') if old[:2] == b'\xff\xfe' else old.decode('utf-8', 'replace')
    lines, sect, done = [], '', False
    for ln in text.splitlines():
        s = ln.strip()
        if s.startswith('['):
            if sect == '[coop]' and not done:
                lines.append('name=' + name)
                done = True
            sect = s
        if sect == '[coop]' and s.startswith('name='):
            if not done:
                lines.append('name=' + name)
                done = True
            continue
        lines.append(ln)
    if not done:
        lines += ['[coop]', 'name=' + name] if '[coop]' not in text else ['name=' + name]
    with open(INI, 'wb') as f:
        f.write(('\r\n'.join(lines) + '\r\n').encode(encoding))


def rejoin(ck, host, guest, label, want):
    """the guest (out of the session, its name set) joins again; the host must name it `want`"""
    t0 = time.time()
    out = guest.call(f'coop.join 127.0.0.1:{lane.PORT}')['out'].strip()
    print(f'  {label}: guest {out}', flush=True)
    ck(twoinst.wait_guest_in(host, guest), f'{label}: the guest is in the host\'s world again')
    names = [n for i, n in roster(host) if i != '0']
    print(f'  {label}: host roster {names}', flush=True)
    ck(want in names, f'{label}: the host\'s player list names the guest {want!r} ({names})')
    ck(not any(n == 'Player' or re.fullmatch(r'guest\d+', n) for n in names), f'{label}: no "Player" / "guestN"')
    rows = lobby_rows(host, want)
    ck(want in rows, f'{label}: the host\'s lobby row names the guest ({rows})')
    seen = None
    while time.time() - t0 < 90 and seen is None:
        seen = next((t for t in toasts(host) if t.startswith(want) and 'joined' in t), None)
        time.sleep(0.5)
    ck(seen is not None, f'{label}: the host\'s notice "{want} joined the pack" ({seen!r})')
    gname = guest.call('coop.name')['out'].strip()
    ck(gname == f'name: {want}', f'{label}: the guest\'s own name ({gname!r})')


def up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            c = Console(inst)
            if c.call('ping')['ok']:
                return c
        except OSError:
            pass
        time.sleep(1)
    return None


def lobby_rows(host, want):
    """the names in the host's lobby (F9 window, coop.uistate rows; the rows of a closed window are the ones of its last
    refresh: waited for up to 10 s to show `want`)"""
    host.call('coop.uistate 1')
    st = Ui(host, 'host').wait(lambda s: any(r and r[0] == want for r in s['rows']), 10, 'the lobby rows')
    st = st or Ui(host, 'host').state()
    host.call('coop.uistate 0')
    return [r[0] for r in (st or {'rows': []})['rows'] if r]


def leave(guest, host):
    guest.call('coop.leave')
    for _ in range(40):
        if len(roster(host)) <= 1:
            break
        time.sleep(0.5)
    time.sleep(2)


def main():
    host, guest = start('creature')
    ck = Checks()
    old = open(INI, 'rb').read()
    try:
        # 1. typed into the name field
        leave(guest, host)
        guest.call('coop.uistate first')
        guest.call('coop.uistate 1')
        ui = Ui(guest, 'guest')
        st = ui.wait(lambda s: s.get('step') == 'name' and s['name']['vis'], 20, 'the name step')
        ck(st is not None, 'typed: the co-op window is on the name step')
        if st:
            ui.type_into('name', TYPED)
            field = ui.state()['name']['text']
            print(f'  typed: the field holds {field!r}', flush=True)
            ck(field == TYPED, f'typed: the name field holds the typed Cyrillic letters ({field!r})')
            ui.click('nameok')
            time.sleep(1)
        guest.call('coop.uistate 0')
        rejoin(ck, host, guest, 'typed', TYPED)

        # 2. coop.ini as UTF-8, 3. as cp1251
        for label, name, enc in (('ini UTF-8', UTF8, 'utf-8'), ('ini cp1251', ANSI, 'cp1251')):
            # the file as a player leaves it before starting the game: both games start cold with it (SporeKit reads and
            # converts it at the mod's start), the guest joins as Bob (the lab's join), leaves, and takes the name of
            # coop.ini as the start does (`coop.name reload`)
            lane.sh('kill')
            time.sleep(2)
            write_ini_name(old, name, enc)
            host, guest = start('creature')
            leave(guest, host)
            print(f'  {label}: ' + guest.call('coop.name reload')['out'].strip(), flush=True)
            got = guest.call('coop.name')['out'].strip()[len('name: '):]
            print(f'  {label}: the guest\'s name after the reload {got!r}', flush=True)
            if enc == 'cp1251' and got != name:
                # Wine's ANSI code page here is not 1251: the same bytes in that page (what a Windows of that
                # language reads correctly); the name must not be lost
                ck(got and got != 'Player', f'{label}: the name is read, in the system\'s ANSI page ({got!r})')
                want = got
            else:
                ck(got == name, f'{label}: the name read from coop.ini ({got!r})')
                want = name
            rejoin(ck, host, guest, label, want)
        ck.faults(host=host, guest=guest)
    finally:
        with open(INI, 'wb') as f:
            f.write(old)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
