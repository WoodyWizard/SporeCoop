#!/usr/bin/env python3
"""sporectl - client for the SporeKit devkit console running inside Spore.

Usage:
  sporectl.py [-i N] <command> [args...]     run one console command
  sporectl.py [-i N]                         interactive REPL
Client-side extras:
  shot [path] [--scale 0.5]   screenshot -> out/shots/*.png (recompressed)
  wait-up [timeout]           wait until the console answers
  wait-mode <Mode> [timeout]  wait until game.mode reports Mode (e.g. Creature)
"""
import json
import os
import socket
import struct
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Console:
    def __init__(self, instance=1, timeout=90.0):
        self.port = 47800 + instance - 1
        self.timeout = timeout
        self.sock = None
        self.buf = b''

    def connect(self):
        self.sock = socket.create_connection(('127.0.0.1', self.port), timeout=3)
        self.sock.settimeout(self.timeout)

    def call(self, line):
        if self.sock is None:
            self.connect()
        self.sock.sendall(line.encode('utf-8') + b'\n')
        while b'\n' not in self.buf:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                # BUG-0407: the reply may still come; on this connection it would be taken as the next call's (a
                # test's fault check read a track row): a new connection for the next call
                self.sock.close()
                self.sock, self.buf = None, b''
                raise
            if not chunk:
                raise ConnectionError('console closed the connection')
            self.buf += chunk
        raw, self.buf = self.buf.split(b'\n', 1)
        return json.loads(raw.decode('utf-8', 'replace'))


def quote(a):
    if not a or any(c in a for c in ' \t"'):
        return '"' + a.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return a


def recompress_png(path, scale=1.0):
    """Re-encode the stored (uncompressed) PNG written by SporeKit with zlib."""
    data = open(path, 'rb').read()
    pos = 8
    w = h = 0
    idat = b''
    while pos < len(data):
        ln, = struct.unpack('>I', data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    rows = [raw[y * stride + 1:(y + 1) * stride] for y in range(h)]
    if scale != 1.0:
        nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
        out_rows = []
        for y in range(nh):
            src = rows[min(h - 1, int(y / scale))]
            r = bytearray(nw * 3)
            for x in range(nw):
                sx = min(w - 1, int(x / scale)) * 3
                r[x * 3:x * 3 + 3] = src[sx:sx + 3]
            out_rows.append(bytes(r))
        rows, w, h = out_rows, nw, nh
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)
    return w, h


def do_shot(con, args):
    scale = 1.0
    if '--scale' in args:
        i = args.index('--scale')
        scale = float(args[i + 1])
        args = args[:i] + args[i + 2:]
    if args:
        path = os.path.abspath(args[0])
    else:
        d = os.path.join(ROOT, 'out', 'shots')
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, time.strftime('shot-%Y%m%d-%H%M%S.png'))
    r = con.call('shot ' + quote(path))
    if not r['ok']:
        print(r['out'])
        return 1
    w, h = recompress_png(path, scale)
    print(f'{path} {w}x{h}')
    return 0


def wait_up(instance, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            c = Console(instance)
            if c.call('ping')['ok']:
                print(f'up after {time.time() - t0:.1f}s')
                return 0
        except OSError:
            pass
        time.sleep(1)
    print('timeout')
    return 1


def wait_mode(con, mode, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = con.call('game.mode')
        if r['ok'] and mode.lower() in r['out'].lower():
            print(r['out'])
            return 0
        time.sleep(1)
    print('timeout, last:', r['out'])
    return 1


def main(argv):
    instance = 1
    if len(argv) >= 2 and argv[0] == '-i':
        instance = int(argv[1])
        argv = argv[2:]
    if argv and argv[0] == 'wait-up':
        return wait_up(instance, float(argv[1]) if len(argv) > 1 else 120)
    con = Console(instance)
    try:
        con.connect()
    except OSError as e:
        print(f'cannot connect to 127.0.0.1:{con.port}: {e}', file=sys.stderr)
        return 2
    if not argv:
        while True:
            try:
                line = input('spore> ')
            except EOFError:
                return 0
            if line.strip():
                r = con.call(line)
                print(r['out'])
    cmd = argv[0]
    if cmd == 'shot':
        return do_shot(con, argv[1:])
    if cmd == 'wait-mode':
        return wait_mode(con, argv[1], float(argv[2]) if len(argv) > 2 else 120)
    r = con.call(' '.join(quote(a) for a in argv))
    print(r['out'])
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
