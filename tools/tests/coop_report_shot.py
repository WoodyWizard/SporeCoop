#!/usr/bin/env python3
"""Report screenshots (BUG-0442, docs/REPORTING.md#reporting-shots), host + guest in Creature:

1. the guest marks a problem the way a player does (`/bug <text>` in the co-op window's chat): its
   log has the MARK and right after it a `report shot:` line naming shots/mark-<date>-<time>-s<N>.jpg,
   N the MARK's session second; the host, which gets the mark, takes its own
2. the report archive of the guest (`report`) has that JPEG under shots/: a valid image at most
   1280 px wide (the back buffer's width scaled down to it, the aspect kept), 5-500 KB, not black
   (mean brightness above 12 of 255, not one flat colour) and like the game's frame: its brightness
   within 30 of a full screenshot (`shot`) taken just before (once the guest's screen shows the
   world); the report text lists it next to its MARK line
3. the game kept its frame rate: the capture cost the frame under 100 ms and no `perf: hitch`
   (200 ms without a frame) came in the 5 s after the mark
4. "Report a problem" in the host's co-op window (a real click): the archive has a screenshot
   taken at the click (shots/report-*.jpg) and the window says "A screenshot of the game is included"

  coop_report_shot.py [--off]   --off (or SPORE_REPORT_SHOT=0): [report] screenshot=0 in coop.ini, the feature
                                off: must FAIL
"""
import io
import os
import re
import sys
import time
import zipfile

import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, start

LOGS = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')
INI = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'coop.ini')
FRAME_GATE_MS = 100.0


def log_path(inst):
    return os.path.join(LOGS, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def log_size(inst):
    try:
        return os.path.getsize(log_path(inst))
    except OSError:
        return 0


def log_from(inst, offset):
    try:
        with open(log_path(inst), errors='replace') as f:
            f.seek(offset)
            return f.read()
    except OSError:
        return ''


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def set_ini(value):
    """[report] screenshot=<value> in the lane's coop.ini (UTF-16 with a BOM or 8-bit, as it is); the old bytes"""
    old = open(INI, 'rb').read() if os.path.exists(INI) else b''
    utf16 = old[:2] == b'\xff\xfe'
    text = old.decode('utf-16') if utf16 else old.decode('utf-8', 'replace')
    nl = '\r\n' if '\r\n' in text else '\n'
    if re.search(r'(?m)^\[report\]\s*$', text):
        if re.search(r'(?ms)^\[report\][^\[]*?^screenshot=', text):
            text = re.sub(r'(?ms)(^\[report\][^\[]*?^screenshot=)[^\r\n]*', r'\g<1>' + str(value), text)
        else:
            text = re.sub(r'(?m)^\[report\]\s*$', f'[report]{nl}screenshot={value}', text, count=1)
    else:
        text = text.rstrip('\r\n') + f'{nl}{nl}[report]{nl}screenshot={value}{nl}'
    with open(INI, 'wb') as f:
        f.write(text.encode('utf-16') if utf16 else text.encode('utf-8'))
    return old


def image_stats(data):
    from PIL import Image, ImageStat
    img = Image.open(io.BytesIO(data))
    img.load()
    g = img.convert('L')
    st = ImageStat.Stat(g)
    lo, hi = g.getextrema()
    return img.format, img.size, st.mean[0], st.stddev[0], hi - lo


def archive_of(con):
    rep = con.call('report test')['out'].strip()
    path = os.path.join(LOGS, os.path.basename(rep.replace('\\', '/')))
    return path if path.endswith('.zip') and os.path.exists(path) else None


def main():
    off = '--off' in sys.argv or os.environ.get('SPORE_REPORT_SHOT') == '0'
    old_ini = set_ini(0 if off else 1)
    host, guest = start('creature')
    ck = Checks()
    try:
        print(f'  [report] screenshot={0 if off else 1} ({"the feature off: this run must FAIL" if off else "on"})')
        guest.call('wait.frames 60 20000')
        # the world shows some seconds after the guest's join (the screen is black before, but for the
        # overlay): a full screenshot (`shot`) of the game's frame first, the mark once it shows the world
        full = os.path.join(lane.out_dir('shots'), f'report_shot_full_{lane.GUEST}.png')
        fmean, t0 = 0.0, time.time()
        while time.time() - t0 < 45:
            if guest.call('shot ' + full)['ok'] and os.path.exists(full):
                fmean = image_stats(open(full, 'rb').read())[2]
                if fmean > 30:
                    break
            time.sleep(2)
        print(f'  the guest\'s frame before the mark: mean brightness {fmean:.1f} after {time.time() - t0:.0f} s')
        time.sleep(1)
        h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)

        # 1. a mark typed into the guest's co-op chat
        text = f'the partner floats over the ground {int(time.time()) % 10000}'
        said = guest.call(f'coop.say /bug {text}')['out']
        t_mark = time.time()
        ck('marked for the report' in said, f'the guest marked the problem ({said.strip()})')
        rx_mark = re.compile(r"MARK by '[^']*' \(player \d+\) at s ([\d.]+): " + re.escape(text))
        rx_shot = re.compile(r'report shot: (shots/(mark-\d{8}-\d{6}(?:-s(\d+))?(?:-\d+)?\.jpg)) (\d+)x(\d+) (\d+) KB q(\d+) '
                             r'of (\d+)x(\d+) \(on the frame ([\d.]+) ms[^)]*\) for: (.*)')
        gl = wait(lambda: (lambda t: t if rx_shot.search(t) or re.search(r'report shot: .*(not taken|off)', t) else None)(
            log_from(lane.GUEST, g0)), 15)
        glog = log_from(lane.GUEST, g0)
        mm = rx_mark.search(glog)
        ms = rx_shot.search(glog)
        ck(mm, 'the guest\'s log has the MARK')
        for ln in re.findall(r'.*report shot: .*', glog)[:4]:
            print('  guest:', ln.strip()[:300])
        ck(ms, 'the guest\'s log names the screenshot of the mark (report shot: shots/mark-...jpg)')
        name = ms.group(1) if ms else None
        if mm and ms:
            sess = int(float(mm.group(1)))
            ck(ms.group(3) is not None and abs(int(ms.group(3)) - sess) <= 1,
               f'its name carries the MARK\'s session second ({ms.group(2)}, MARK at s {mm.group(1)})')
            ck(text in ms.group(11) and 'MARK' not in ms.group(11),
               'the shot line names the mark it belongs to (not as a second "MARK by" line: tools count those)')
        hl = wait(lambda: re.search(r'report shot: shots/mark-\S+\.jpg .*for: the mark by .*' + re.escape(text),
                                    log_from(lane.HOST, h0)), 10)
        ck(hl, 'the host took its own screenshot of the guest\'s mark')

        # 3. the frame rate: the capture's cost on the frame, no hitch after the mark
        if ms:
            fm = float(ms.group(10))
            ck(fm < FRAME_GATE_MS, f'the capture cost the guest\'s frame {fm:.1f} ms (gate {FRAME_GATE_MS:.0f} ms)')
        time.sleep(max(0.0, 5 - (time.time() - t_mark)))
        hitches = re.findall(r'perf: hitch (\d+) ms.*', log_from(lane.GUEST, g0))
        ck(not hitches, f'no hitch on the guest in the 5 s after the mark ({hitches})')

        # 2. the archive
        bb = re.search(r'screen (\d+) (\d+)', guest.call('coop.uistate')['out'])
        bw, bh = (int(bb.group(1)), int(bb.group(2))) if bb else (0, 0)
        zp = archive_of(guest)
        ck(zp, f'the guest\'s report archive is written ({os.path.basename(zp) if zp else None})')
        z = zipfile.ZipFile(zp) if zp else None
        names = z.namelist() if z else []
        shots = [n for n in names if n.startswith('shots/')]
        print('  shots in the archive:', shots)
        ck(name and name in names, f'the archive has the mark\'s screenshot {name}')
        if z and name in names:
            data = z.read(name)
            try:
                fmt, (w, h), mean, sd, rng = image_stats(data)
            except Exception as e:  # noqa: BLE001 - not an image
                fmt, (w, h), mean, sd, rng = f'not an image: {e}', (0, 0), 0, 0, 0
            print(f'  {name}: {fmt} {w}x{h}, {len(data) >> 10} KB, mean brightness {mean:.1f}, stddev {sd:.1f}, '
                  f'range {rng}; back buffer {bw}x{bh}')
            ck(fmt == 'JPEG', 'it is a valid JPEG')
            want_w = min(1280, bw) if bw else w
            ck(w <= 1280 and w == want_w and (not bw or abs(h - bh * w / bw) <= 1),
               f'its size {w}x{h}: at most 1280 wide, the back buffer {bw}x{bh} scaled with its aspect')
            ck(5 << 10 <= len(data) <= 500 << 10, f'its file size {len(data) >> 10} KB is 5-500 KB')
            ck(mean > 12 and sd > 4 and rng > 40, 'it is not black or one flat colour (a game frame)')
            ck(abs(fmean - mean) < 30, f'its brightness {mean:.1f} is like the game\'s frame just before ({fmean:.1f}, `shot`)')
            rep = next((n for n in names if re.match(r'report-.*\.txt$', n)), None)
            rtxt = z.read(rep).decode('utf-8', 'replace') if rep else ''
            sec = rtxt.split('== Screenshots', 1)[1].split('\n== ', 1)[0] if '== Screenshots' in rtxt else ''
            print('  report:', ('== Screenshots' + sec).strip().replace('\n', ' | ')[:400])
            ck(re.search(re.escape(name) + r'\s+\d+x\d+ \d+ KB \| the mark by .*' + re.escape(text), sec),
               'the report lists the screenshot next to its MARK line')
            readme = z.read('README.txt').decode('utf-8', 'replace') if 'README.txt' in names else ''
            ck('shots/' in readme and 'screenshot=0' in readme, 'the archive\'s README explains shots/ and how to turn them off')

        # 4. "Report a problem" in the host's window: a screenshot of the click
        time.sleep(2.5)  # over the 2 s between two shots of one game (the host's of the mark)
        ui = Ui(host, 'host')
        host.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s['open'] == '1' and s['report']['vis'], 20, 'the co-op window')
        ck(st is not None, 'the host\'s co-op window is open with "Report a problem"')
        h1 = log_size(lane.HOST)
        z0 = {f for f in os.listdir(LOGS) if f.startswith('SporeCoop-report-')}
        time.sleep(1.1)  # a new archive name
        ui.click('report')
        st = ui.wait(lambda s: 'Report saved' in s.get('status', ''), 15, 'the report status')
        status = st.get('status', '') if st else ''
        print('  host status:', status[:300])
        ck('A screenshot of the game is included' in status, 'the window says a screenshot of the game is included')
        hlog = log_from(lane.HOST, h1)
        rs = re.search(r'report shot: (shots/report-\S+\.jpg) .*for: "Report a problem"', hlog)
        za = re.search(r'report archive: (\S+\.zip)', hlog)
        ck(rs, 'the host took a screenshot at the click' + (f' ({rs.group(1)})' if rs else ''))
        new = sorted({f for f in os.listdir(LOGS) if f.startswith('SporeCoop-report-')} - z0)
        zh = os.path.join(LOGS, os.path.basename(za.group(1).replace('\\', '/'))) if za else None
        hn = zipfile.ZipFile(zh).namelist() if zh and os.path.exists(zh) else []
        ck(rs and rs.group(1) in hn, f'the button\'s archive has it ({new})')
        host.call(f'key {VK_F9}')
    finally:
        with open(INI, 'wb') as f:
            f.write(old_ini)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
