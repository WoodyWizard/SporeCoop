"""Read-only preflight for native modal dialogs that invalidate gameplay UI tests."""
from duo_villages import command
import random
import re
from duo_orders import wait_until


def registration_text(client):
    # Whole UI trees can exceed the console response limit. Caption searches
    # return only the matching window and its ancestors, including visibility.
    # Registration's main text is drawn by an embedded browser and has no native
    # caption. Its browser and Login controls are present only in this dialog.
    for caption in ('Quit Registration?', 'Quit registration?', 'Register your game', 'Create a Screen Name'):
        result = client.call('coop.ui find ' + caption)
        if not result['ok']:
            continue
        flags = re.findall(r'fl=0x([0-9a-f]+)', result['out'], re.I)
        if flags and all(int(value, 16) & 1 for value in flags):
            return result['out'].lower()
    native = command(client, 'coop.ui where 0451A4C0 0519E168')
    if all(re.search(rf'^{wid} [\d.]+ [\d.]+ [\d.]+ [\d.]+ enabled=', native, re.M)
           for wid in ('0451A4C0', '0519E168')):
        return 'register your game to get'
    return ''


def dismiss_registration(client):
    """Fixture setup only: decline account registration without entering credentials."""
    def tree():
        return registration_text(client)
    text = tree()
    if not any(value in text for value in ('quit registration?', 'register your game to get', 'create a screen name')):
        return
    if 'quit registration?' not in text:
        rect = re.search(r'^FFFFFFF2 ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',
                         command(client, 'coop.ui where FFFFFFF2'), re.M)
        if not rect:
            raise RuntimeError('Registration close button not found')
        x1, y1, x2, y2 = map(float, rect.groups())
        command(client, f'move {round((x1+x2)/2)} {round((y1+y2)/2)}')
        command(client, 'wait.frames 2 10000')
        command(client, f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
        # A stage load may dismiss the native browser itself while this click
        # is being processed. Absence is already the requested fixture state.
        wait_until(lambda: not tree() or 'quit registration?' in tree(),
                   'Registration confirmation did not appear', 5)
        if not tree():
            return
    rect = re.search(r'^FFFFFFF1 ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',
                     command(client, 'coop.ui where FFFFFFF1'), re.M)
    if not rect:
        raise RuntimeError('Registration confirmation OK button not found')
    x1, y1, x2, y2 = map(float, rect.groups())
    command(client, f'move {round((x1+x2)/2)} {round((y1+y2)/2)}')
    command(client, 'wait.frames 2 10000')
    command(client, f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
    wait_until(lambda: 'quit registration?' not in tree() and 'create a screen name' not in tree(),
               'Native registration did not close', 5)


def require_gameplay_ui(client):
    tree = registration_text(client)
    if ('quit registration?' in tree or 'register your game to get' in tree or
            'create a screen name' in tree):
        raise RuntimeError('Dismiss the native Spore registration dialog in this disposable profile before gameplay UI tests.')


def dismiss_service_error(client):
    """Close only the observed offline-service error in disposable test profiles."""
    def visible():
        result = client.call('coop.ui find There was an error connecting to the Spore servers')
        flags = re.findall(r'fl=0x([0-9a-f]+)', result['out'], re.I) if result['ok'] else []
        return bool(flags) and all(int(value, 16) & 1 for value in flags)
    if not visible():
        return False
    rect = re.search(r'^FFFFFFF2 ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',
                     command(client, 'coop.ui where FFFFFFF2'), re.M)
    if not rect:
        raise RuntimeError('Offline-service dialog close button unavailable')
    x1, y1, x2, y2 = map(float, rect.groups())
    command(client, f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
    wait_until(lambda: not visible(), 'Offline-service dialog did not close', 5)
    return True


def view_tribal_point(client, point):
    """Use native arrow-key panning, with the existing RTS camera geometry."""
    import play_civ
    play_civ.W, play_civ.H = map(int, re.search(r'screen (\d+) (\d+)', command(client, 'coop.uistate')).groups())
    play_civ.HUD_BOTTOM = play_civ.H - 95

    class TribalCamera(play_civ.Player):
        def keep_altitude(self, camera):
            # The asynchronous native login error can arrive after preflight.
            # Tribe uses different native zoom limits from Civilization.
            return dismiss_service_error(client)

        def do(self, line):
            for name, key in [('w', '0x26'), ('a', '0x25'), ('s', '0x28'), ('d', '0x27')]:
                line = line.replace(f'key {name} ', f'key {key} ')
            return super().do(line)

    driver = TribalCamera('Tribal camera', client, 1, random.Random(0), [])
    w, h = play_civ.W, play_civ.H
    result = driver.look_at(point, box=(w*.25, h*.30, w*.73, h*.68), tries=35)
    if not result:
        raise RuntimeError('Native camera did not bring the Tribal target into view')
    return tuple(map(round, result))
