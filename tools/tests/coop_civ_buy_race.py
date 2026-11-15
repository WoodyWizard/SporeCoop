#!/usr/bin/env python3
"""BUG-0940: a player's vehicle purchase when another player took the last shared money a moment before the click.

The campaign's four bots bought at once: one bot read its city menu with the purchase button enabled, another
player's purchase took the last money, and the bot's click bought nothing. play_civ's `buy` took that for a lost
purchase ("bought a vehicle ... and the host made none"). Here play_civ's own Bot buys on the guest, once with money
(the control: a vehicle of the guest's is made on the host) and once with the shared money taken to 0 by the host
between the bot's look at the button and its click (the race, made on purpose).
Checked on the games' own state after the race: no vehicle of the guest's made on the host, the money 0 on both
(never below), the guest's game sent no purchase (its own refusal: `cmds_sent` unchanged); and the bot counts it as
the money running out (`buy_broke`), not as a PROBLEM. Before the fix of play_civ the bot said PROBLEM ('lost').

  SPORE_LANE=4 python3 tools/tests/coop_civ_buy_race.py
"""
import time

import lane
from play_civ import BTN_LAND, Bot, Game, civ_state, money, stat
from twoinst import Checks, start


class RaceBot(Bot):
    """the guest's bot; with `drain` set, the host spends the whole shared money right before the bot's click"""
    drain = False
    armed = False

    def where(self, *ids):
        out = super().where(*ids)
        if self.drain and BTN_LAND in ids:  # the bot looked at the purchase buttons: its next click buys
            self.armed = True
        return out

    def do(self, line):
        if self.armed and line.startswith('click '):
            self.drain = self.armed = False
            m = money(self.hc) or 0
            self.hc.call(f'coop.res add money {-m:.0f}')
            t0 = time.time()
            while time.time() - t0 < 5 and (money(self.c) or 0) > 0.5:
                time.sleep(0.2)
            time.sleep(0.5)
            print(f'  the host spent the shared money right before the click: host {money(self.hc)}, '
                  f'guest {money(self.c)}', flush=True)
        return super().do(line)


def set_money(host, guest, value):
    host.call(f'coop.res add money {value - (money(host) or 0):.0f}')
    t0 = time.time()
    while time.time() - t0 < 5 and abs((money(guest) or 0) - value) > 1:
        time.sleep(0.3)


def guest_vehicles(host):
    return {i for i, v in civ_state(host)[1].items() if v['owner'] == 1}


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(3)
    game = Game(host, guest, 940)
    bot = RaceBot('Bob', lane.GUEST, 1, game)
    # the control: with money the bot's purchase is made on the host
    set_money(host, guest, 20000)
    r = 'skip'
    for _ in range(3):
        r = bot.a_buy()
        if r.startswith('bought'):
            break
        time.sleep(2)
    print(f'  control purchase: {r}', flush=True)
    ck(r.startswith('bought'), f'the guest\'s bot buys a vehicle with money ({r})')
    # the race
    rounds, broke = 0, 0
    for k in range(4):
        set_money(host, guest, 20000)
        v0 = guest_vehicles(host)
        sent0 = stat(civ_state(guest)[2], 'cmds_sent')
        p0 = len(game.problems)
        bot.drain = True
        r = bot.a_buy()
        if bot.drain:  # the bot did not reach its click (no menu, no button): again
            bot.drain = bot.armed = False
            print(f'  race {k}: no click ({r})', flush=True)
            continue
        rounds += 1
        time.sleep(1)
        v1 = guest_vehicles(host)
        sent1 = stat(civ_state(guest)[2], 'cmds_sent')
        mh, mg = money(host), money(guest)
        print(f'  race {k}: the bot says "{r}"; new vehicles of the guest on the host {sorted(v1 - v0)}, money host {mh} '
              f'guest {mg}, purchases sent by the guest {sent1 - sent0}, bot counters {game.count}', flush=True)
        ck(not (v1 - v0), f'race {k}: no vehicle made on the host after the money ran out ({sorted(v1 - v0)})')
        ck(mh is not None and mg is not None and abs(mh) < 1 and abs(mg) < 1,
           f'race {k}: the shared money is 0 on both, not below (host {mh}, guest {mg})')
        ck(sent1 == sent0, f'race {k}: the guest\'s game refused the click itself, no purchase sent ({sent1 - sent0})')
        ck(len(game.problems) == p0 and r != 'lost',
           f'race {k}: the bot counts the money running out, not a lost purchase ("{r}", '
           f'{game.problems[p0:] if len(game.problems) > p0 else "no PROBLEM"})')
        broke += r != 'lost'
        if rounds >= 2:
            break
    ck(rounds >= 2, f'the bot clicked after the money ran out {rounds} times (wanted 2)')
    host.call('coop.res add money 20000')  # money for the next test of the lane
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
