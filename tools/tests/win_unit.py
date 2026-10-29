#!/usr/bin/env python3
"""Windows-side unit tests (tests/win_tests.cpp): the updater's file handling (BUG-0089, BUG-0114,
BUG-0150), how a received world is put into a save slot (BUG-0035, BUG-0087, BUG-0105), the setup
(BUG-0094 newest pack and no silent downgrade, BUG-0139 no close during a job: the real
SporeCoopSetup.exe with a hidden window) and install.bat (BUG-0080: the player's .ini files stay), against
the real Win32 file API under Wine in the lane's bottle. No game is started; the tests work in
%TEMP%\\sporecoop-win-tests of the bottle.

  SPORE_LANE=3 python3 tools/tests/win_unit.py [--old | --old-suffix | --old-keep]

--old switches the fixes off: the run must then FAIL (a test you never saw fail proves nothing).
--old-suffix / --old-keep switch off only update.old_suffix (BUG-0114) / update.keep_failed (BUG-0150).
For install.bat, --old runs the install.bat of 7874654 (before BUG-0080, from tools/package.sh).
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lane  # noqa: E402


OLD_BAT_COMMIT = '7874654'  # the last tools/package.sh that wrote install.bat itself (BUG-0080)


def crlf(text):
    return re.sub(r'\r*\n', '\r\n', text)


def stage_bat(old):
    """install.bat (and ini.exclude) to test, with Windows line endings as tools/package.sh ships them"""
    if old:
        sh_text = subprocess.run(['git', '-C', ROOT, 'show', f'{OLD_BAT_COMMIT}:tools/package.sh'],
                                 capture_output=True, text=True, check=True).stdout
        bat = sh_text.split("cat > \"$STAGE/install.bat\" <<'EOF'\n", 1)[1].split('\nEOF\n', 1)[0] + '\n'
    else:
        bat = open(os.path.join(ROOT, 'installer', 'install.bat')).read()
    out = os.path.join(ROOT, 'build', 'install_test.bat')
    with open(out, 'w', newline='') as f:
        f.write(crlf(bat))
    excl = os.path.join(ROOT, 'build', 'ini_test.exclude')
    with open(excl, 'w', newline='') as f:
        f.write(crlf(open(os.path.join(ROOT, 'installer', 'ini.exclude')).read()))
    return out, excl


def win(path):
    return 'Z:' + path.replace('/', '\\')


def main():
    assert lane.LANE > 1, 'never the real install (lane 1)'
    subprocess.run(['make', '-C', ROOT, '--no-print-directory', 'test-win'], check=True, stdout=subprocess.DEVNULL)
    exe = os.path.join(ROOT, 'build', 'win_tests.exe')
    out = os.path.join(lane.out_dir('win_unit'), 'win_tests.txt')
    args = [a for a in sys.argv[1:] if a in ('--old', '--old-suffix', '--old-keep')]
    bat, excl = stage_bat('--old' in args)
    args += ['--bat', win(bat), '--exclude', win(excl)]
    w = 'Z:' + exe.replace('/', '\\')
    wo = 'Z:' + out.replace('/', '\\')
    # bottles-cli does not hand the program's stdout back: cmd redirects it into a file
    cmd = f'cmd /c "{w} {" ".join(args)} > {wo} 2>&1"'
    if os.path.exists(out):
        os.remove(out)
    subprocess.run(['flatpak', 'run', '--command=bottles-cli', 'com.usebottles.bottles', 'run', '-b', lane.BOTTLE,
                    '-e', 'C:\\windows\\system32\\cmd.exe', '--', '/c', f'{w} {" ".join(args)} > {wo} 2>&1'],
                   capture_output=True, timeout=300, stdin=subprocess.DEVNULL)
    subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), 'kill'], capture_output=True,  # wine services
                   env=dict(os.environ, SPORE_LANE=str(lane.LANE)))
    text = open(out, errors='replace').read() if os.path.exists(out) else ''
    print(text.rstrip())
    ok = 'ALL OK' in text
    print('RESULT:', 'PASS' if ok else 'FAIL', f'({cmd})' if not text else '')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
