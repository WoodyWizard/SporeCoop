"""An interrupted guest outfit must preserve campaign ownership after reconnect.

Start in both voted native outfitters, using duo_tribal_outfitter --leave-open.
The host deliberately stays in its editor until the guest has left.
"""
import argparse
import math
import re
import subprocess
import time
from coop_campaign import drag_slow
from duo_orders import inspect, wait_until
from duo_villages import Console, command


def stop_disposable_guest():
    """Hard-exit only instance 5 in the explicitly named disposable Windows lab."""
    script = r"""
$games = @(Get-CimInstance Win32_Process -Filter "Name='SporeApp.exe'" | Where-Object {
    $_.ExecutablePath -eq 'D:\SporeCoop-Duo-Test-01\SporebinEP1\SporeApp.exe' -and
    $_.CommandLine -match '-sporekit-instance:5(?: |$)' -and
    $_.CommandLine -match '-sporekit-profile:DuoQA[A-Za-z0-9]+' })
if ($games.Count -ne 1) { throw 'Expected exactly one disposable guest process' }
$profile = [regex]::Match($games[0].CommandLine, '-sporekit-profile:(DuoQA[A-Za-z0-9]+)').Groups[1].Value
Stop-Process -Id $games[0].ProcessId -Force -ErrorAction Stop
$profile
"""
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', script],
                            check=True, capture_output=True, text=True, timeout=15)
    profile = result.stdout.strip()
    if not re.fullmatch(r'DuoQA[A-Za-z0-9]+', profile): raise RuntimeError('Invalid lab profile')
    return profile


def restart_disposable_guest(profile):
    if not re.fullmatch(r'DuoQA[A-Za-z0-9]+', profile): raise RuntimeError('Invalid lab profile')
    script = ("$ErrorActionPreference='Stop'; $env:SteamAppId='24720'; $env:SteamGameId='24720'; "
              "Start-Process -FilePath 'D:\\SporeCoop-Duo-Test-01\\SporebinEP1\\SporeApp.exe' "
              "-WorkingDirectory 'D:\\SporeCoop-Duo-Test-01\\SporebinEP1' -WindowStyle Hidden "
              f"-ArgumentList @('-w','-sporekit-instance:5','-sporekit-profile:{profile}')")
    subprocess.run(['powershell.exe', '-NoProfile', '-Command', script], check=True, timeout=15)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hard-exit', action='store_true')
    parser.add_argument('--control', action='store_true')
    args = parser.parse_args()
    h, g = Console(4), Console(5)
    try:
        command(g, 'coop.fix creations.outfit_ack '+('off' if args.control else 'on'))
        original = re.search(r'mine creature: ([0-9A-F]+)', command(g, 'coop.creations'))[1]
        founders = command(h, 'coop.founders')
        root = re.search(r'guest ([0-9A-F]+)', founders)[1]
        drag_slow(g, 70, 111, 755, 335)
        command(g, 'click 1155 645')
        wait_until(lambda: 'Tribe' in command(g, 'game.mode'), 'Guest did not finish its outfitter', 20)
        wait_until(lambda: re.search(r'mine creature: ([0-9A-F]+)', command(g, 'coop.creations'))[1] != original,
                   'Guest did not adopt the newly saved outfit', 20)
        wait_until(lambda: re.search(r'player 1 creature: ([0-9A-F]+)', command(g, 'coop.creations'))[1] != root,
                   'Guest did not publish its saved outfit', 20)
        print('Before disconnect:', command(g, 'coop.creations'), flush=True)
        if 'Editor' not in command(h, 'game.mode'): raise RuntimeError('Host must still be editing')
        profile = stop_disposable_guest() if args.hard_exit else None
        if profile:
            g.sock.close()
            g = Console(5)
        else:
            command(g, 'coop.leave')
        command(h, 'click 1155 645')
        wait_until(lambda: 'Tribe' in command(h, 'game.mode'), 'Host did not finish its editor', 20)
        time.sleep(2)
        command(h, 'click 1155 645')
        wait_until(lambda: 'busy: -' in command(h, 'coop.status'), 'Host planner did not close', 20)
        wait_until(lambda: 'token=0 ' in command(h, 'coop.tribe outfits'),
                   'Host did not discard the departed editor transaction', 30)
        if profile:
            restart_disposable_guest(profile)
            def running():
                try: return g.call('coop.status')['ok']
                except (OSError, ConnectionError):
                    if g.sock: g.sock.close()
                    g.sock, g.buf = None, b''
                    return False
            wait_until(running, 'Restarted guest console unavailable', 45)
        command(g, 'coop.join 127.0.0.1:47810 DuoTribeGuest')
        time.sleep(2)  # Do not mistake the guest's pre-transfer local Tribe for a completed join.
        def owned():
            accepted = None
            for c in (h, g):
                if 'Tribe' not in command(c, 'game.mode') or 'busy: -' not in command(c, 'coop.status'):
                    return False
                if command(c,'coop.founders') != founders: raise RuntimeError('Reconnect changed founding identities')
                model = re.search(r'readable=1 home=[0-9A-F]+ guest=([0-9A-F]+)', command(c,'coop.tribe outfits'))
                if not model or (accepted and model[1] != accepted): return False
                accepted = model[1]
                groups, _ = inspect(c)
                if len(groups.get(accepted, {})) != 3 or any(row[0] != 1 for row in groups[accepted].values()):
                    return False
            return accepted
        try:
            current_model = wait_until(owned, 'Guest lost native village ownership after interrupted outfit/reconnect', 60)
        finally:
            print('Host after reconnect:', command(h, 'coop.creations'), command(h, 'coop.tribe'), flush=True)
        before = inspect(h)[1]
        command(g, 'coop.tribe select_village')
        rows = inspect(h)[0][current_model]
        start = {i: row[1] for i, row in rows.items()}
        center = [sum(pos[j] for pos in start.values())/len(start) for j in range(3)]
        tangent = [-center[2], 0, center[0]]
        target = [center[i]+35*tangent[i]/math.hypot(*tangent) for i in range(3)]
        target = [x*math.hypot(*center)/math.hypot(*target) for x in target]
        command(g, 'coop.tribe walk '+' '.join(f'{x:.2f}' for x in target))
        wait_until(lambda: inspect(h)[1] >= before+len(rows), 'Reconnected founder cannot issue orders')
        for c in (h, g):
            def moved():
                current = inspect(c)[0].get(current_model, {})
                return all(i in current and math.dist(pos,target)-math.dist(current[i][1],target)>3
                           for i,pos in start.items())
            wait_until(moved, 'Reconnected villagers did not walk on both peers', 30)
        print('PASS '+('hard-exit' if profile else 'disconnect')+
              ' outfit recovery retains native founder ownership and orders.', flush=True)
    finally:
        errors = []
        for c in (h,g):
            try:
                faults = command(c,'exc')
                print(faults,flush=True)
                if 'total faults: 0' not in faults: raise RuntimeError('Native fault')
            except Exception as error:
                errors.append(str(error))
            finally:
                try: c.call('quit')
                except Exception as error: errors.append(str(error))
        if errors: raise RuntimeError('; '.join(errors))


if __name__ == '__main__': main()
