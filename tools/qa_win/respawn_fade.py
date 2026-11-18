"""Cell: one player's respawn must not darken the others' screens (BUG-9122). Console only, no mouse.

    py tools/qa_win/respawn_fade.py <dier> <watcher>... [--tag name]
        dier / watcher: a QA instance number on this PC or 'msi'

The dier's cell takes a lethal bite (`coop.pool selfhit 20`) and gives up (`coop.giveup`); every watcher takes a
screenshot (`shot`) every ~0.4 s for 4 s and the mean brightness of each is printed. A respawn fade shows as a drop to
single digits (2026-11-11, switch effects.own_screen off: 81 -> 6 -> 36 -> 81; on: 72-74 throughout).
Exit code 1 when a watcher's brightness fell below half of its first value.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import qa  # noqa: E402

BRIGHT = ("Add-Type -AssemblyName System.Drawing; foreach($f in $args){$b=[System.Drawing.Bitmap]::FromFile($f); $s=0; $n=0; "
          "for($y=0;$y -lt $b.Height;$y+=40){for($x=0;$x -lt $b.Width;$x+=40){$c=$b.GetPixel($x,$y);$s+=($c.R+$c.G+$c.B)/3;$n++}}; "
          "$b.Dispose(); [int]($s/$n)}")


def brightness(paths, remote):
    script = '& {' + BRIGHT + '} ' + ' '.join("'%s'" % p for p in paths)
    cmd = ['ssh', '-o', 'BatchMode=yes', 'msi', script] if remote else ['powershell', '-NoProfile', '-Command', script]
    return [int(v) for v in subprocess.run(cmd, capture_output=True, text=True).stdout.split()]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tag = sys.argv[sys.argv.index('--tag') + 1] if '--tag' in sys.argv else time.strftime('%H%M%S')
    if len(args) < 2:
        sys.exit(__doc__)
    dier = qa.console(args[0])
    watchers = [(w, qa.console(w), w.lower() == 'msi') for w in args[1:] if w != tag]
    shots_dir = os.path.join(os.environ.get('TEMP', '.'), 'sporecoop-qa')
    os.makedirs(shots_dir, exist_ok=True)
    print('die:', qa.call(dier, 'coop.pool selfhit 20').strip())
    time.sleep(3)
    print('give up:', qa.call(dier, 'coop.giveup').strip())
    shots = {w: [] for w, _, _ in watchers}
    t0 = time.time()
    for i in range(10):
        for w, con, remote in watchers:
            path = (r'C:\temp\resp_%s_%s_%d.png' if remote else os.path.join(shots_dir, 'resp_%s_%s_%d.png')) % (tag, w, i)
            qa.call(con, 'shot ' + path)
            shots[w].append((round(time.time() - t0, 1), path))
        time.sleep(0.25)
    time.sleep(1.5)
    failed = False
    for w, _, remote in watchers:
        values = brightness([p for _, p in shots[w]], remote)
        print(w, ' '.join('%s:%d' % (t, v) for (t, _), v in zip(shots[w], values)))
        failed |= bool(values) and min(values) < values[0] / 2
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
