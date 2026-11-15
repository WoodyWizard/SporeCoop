#!/usr/bin/env python3
"""Text the player sees in the co-op window and the report round, two games (0.7.14 P2: BUG-1027, BUG-1013, BUG-1014).

1. BUG-1027 (core.ui_locked_edit_utf16): the guest plays a caption (CRG_ClaimNest: the game's input lock), opens the
   co-op window, clicks the chat line and types 70 Cyrillic letters, DEL and an emoji (WM_CHAR, a surrogate pair):
   the game's own text field holds all 70 letters and the emoji, no DEL, no U+FFFD (before: the mod counted UTF-8
   bytes against the 120-character limit and stopped at 60, and the emoji became two U+FFFD); Send: the host's chat
   has the line.
2. BUG-1013 (core.sreport_host_cut): the host asks for the reports with a name of 14 CJK characters (42 bytes, the test
   seam `coop.sreport askname`): the guest's question names the whole host, valid UTF-8 (before: cut at 40 bytes, a
   broken character at the end).
3. BUG-1014 (core.sreport_writing): the guest sends its report, the host writes the session zip on a slow disk
   (`writedelay`): `coop.sreport status` says round=writing meanwhile (before: never, "none").

SPORE_FIX_OFF=core.ui_locked_edit_utf16 / core.sreport_host_cut / core.sreport_writing: that part FAILs.
"""
import re
import time

from coop_chat_caption import CLAIM, centre, cinematic, eax, f9, wait
from coop_ui import Ui
from twoinst import Checks, start

LETTERS = 'Ж' * 70
EMOJI = '\U0001F600'
HOST_NAME = '漢字仮名交じり文を書く練習帳'  # 14 CJK characters, 42 bytes


def field(out, name):
    m = re.search(rf'\b{name}=(\S+)', out)
    return m.group(1) if m else ''


def chat_part(guest, host, ck):
    ui = Ui(guest, 'guest')
    cm = eax(guest, 'Simulator::cCinematicManager::Get cdecl')
    print(' ', guest.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')
          ['out'].strip(), flush=True)
    ck(wait(lambda: cinematic(guest) == CLAIM, 10), 'guest: plays CRG_ClaimNest (a caption)')
    time.sleep(3)
    lock = int(ui.state().get('input_lock', '0'))
    ck(lock > 0, f'guest: the caption holds the game\'s input lock ({lock})')
    f9(guest)
    st = ui.wait(lambda s: s['open'] == '1' and s['chatedit']['vis'] and s['send']['vis'], 8, 'chat line')
    ck(st is not None, 'guest: the co-op window opens with its chat line')
    if st:
        x, y = centre(st['chatedit'])
        guest.call(f'click {x} {y}')
        time.sleep(0.6)
        guest.call(f'text "{LETTERS[:35]}"')
        guest.call('text "\x7f"')
        guest.call(f'text "{LETTERS[35:]}{EMOJI}"')
        time.sleep(1.0)
        typed = ui.state()['chatedit']['text']
        print(f'  guest: the chat line holds {len(typed)} characters: {typed[:6]}...{typed[-4:]!r}', flush=True)
        ck(typed == LETTERS + EMOJI,
           f'BUG-1027: the chat line typed under the caption holds all 70 letters and the emoji, no DEL, no U+FFFD '
           f'({typed.count("Ж")} letters, emoji {EMOJI in typed}, U+FFFD {typed.count(chr(0xFFFD))}, DEL {chr(0x7F) in typed})')
        x, y = centre(st['send'])
        guest.call(f'click {x} {y}')
        seen = wait(lambda: LETTERS[:40] in host.call('coop.status')['out'], 6)
        ck(bool(seen), 'the line reached the host\'s chat')
        f9(guest)
    for _ in range(3):  # the caption ends
        if not cinematic(guest):
            break
        guest.call('key enter')
        time.sleep(2)
    if cinematic(guest):
        guest.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall {cm:#x}')
        wait(lambda: not cinematic(guest), 5)


def report_part(host, guest, ck):
    print(' ', host.call(f'coop.sreport askname {HOST_NAME}')['out'].strip(), host.call('coop.sreport writedelay 3000')
          ['out'].strip(), flush=True)
    host.call('coop.sreport ask')
    st = wait(lambda: field(guest.call('coop.sreport status')['out'], 'guest') == 'asked'
              and guest.call('coop.sreport status')['out'], 15)
    line = re.search(r"^guestline \d+ '(.*)'$", st or '', re.M)
    line = line.group(1) if line else ''
    print(f'  guest: {line!r}', flush=True)
    ck(line.startswith(HOST_NAME + ' (host) asks') and '�' not in line,
       f'BUG-1013: the guest\'s question names the whole host ({line[:40]!r})')
    if not st:
        return
    guest.call('coop.sreport answer send')
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 90:
        r = field(host.call('coop.sreport status')['out'], 'round')
        seen.add(r)
        if r == 'writing' or (r == 'none' and time.time() - t0 > 5):
            break
        time.sleep(0.05)
    out = host.call('coop.sreport status')['out']
    print(f'  host: round values seen {sorted(seen)}; {out.splitlines()[1] if len(out.splitlines()) > 1 else ""}', flush=True)
    ck('writing' in seen, f'BUG-1014: coop.sreport status says round=writing while the zip is written ({sorted(seen)})')
    wait(lambda: field(host.call('coop.sreport status')['out'], 'round') == 'none', 60, 0.5)


def main():
    host, guest = start()
    ck = Checks()
    try:
        chat_part(guest, host, ck)
        report_part(host, guest, ck)
    finally:
        host.call('coop.sreport askname -')
        host.call('coop.sreport writedelay 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
