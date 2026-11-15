#!/usr/bin/env python3
"""BUG-0629: a chat line sent with the Send button while the sender's game holds its frames reaches both
players, and the play bots' check sees it.

play_creature (out/lab/20261106-130130 #3) called a line lost that both games had: the host's game held
its frames (hitches of 0.4-1.1 s in a row), the bot's Send click waited in the window's queue, the game
took it 1.1 s later and both games showed the line 0.4 s after the bot's single look at 1.5 s. Here the
host types a line into the co-op window's chat, its main thread is held 2.5 s (`kit.test.hang`), the Send
click comes during the hold, and the play bots' check (play_creature.chat_reached) must find the line on
both games; the line must also be in the guest's chat (the game's chat, coop.status `chat:` lines).

  coop_chat_held_send.py      SPORE_CHAT_WAIT=0: the bots' old check (one look 1.5 s after the click) FAILs
"""
import re
import time

from play_creature import chat_reached
from twoinst import Checks, start


def f9(con):
    con.call('key 0x78 down')
    time.sleep(0.25)
    con.call('key 0x78 up')
    time.sleep(0.8)


def centre(m):
    return (int(m.group(1)) + int(m.group(3))) // 2, (int(m.group(2)) + int(m.group(4))) // 2


def main():
    host, guest = start('creature')
    ck = Checks()
    for i in range(2):
        st = host.call('coop.uistate')['out']
        if 'open=1' not in st:
            f9(host)
            st = host.call('coop.uistate')['out']
        m = re.search(r'chatedit (\d+) (\d+) (\d+) (\d+) 1', st)
        m2 = re.search(r'send (\d+) (\d+) (\d+) (\d+) 1', st)
        ck(m and m2, f'line {i + 1}: the co-op window is open with its chat line and Send button')
        if not (m and m2):
            break
        msg = f'host held send {i + 1} {int(time.time()) % 1000}'
        typed = None
        for attempt in range(3):
            host.call('click %d %d' % centre(m))
            time.sleep(0.6 + attempt)
            host.call(f'text "{msg}"')
            time.sleep(0.4)
            typed = re.search(r"chatedit .* '(.*)'", host.call('coop.uistate')['out'])
            if typed and msg in typed.group(1):
                break
        ck(typed and msg in typed.group(1), f'line {i + 1}: the chat line holds the typing ({typed and typed.group(1)!r})')
        print(' ', host.call('kit.test.hang 2500')['out'].strip(), flush=True)
        time.sleep(0.3)  # the main thread is held: the click waits in its queue
        host.call('click %d %d' % centre(m2))
        sent_at = time.time()
        ok, late, mine, theirs = chat_reached(host, guest, msg, sent_at)
        print(f'  line {i + 1}: on both {ok}, {late:.1f} s after the click', flush=True)
        ck(ok, f'line {i + 1}: the line sent during the held frames is seen on both games ({late:.1f} s after the click)')
        if ok:
            ck('<Alice> ' + msg in '\n'.join(guest.call('log 200')['out'].splitlines()),
               f'line {i + 1}: the guest\'s game shows it as the host\'s line')
        time.sleep(1)
    if 'open=1' in host.call('coop.uistate')['out']:
        f9(host)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
