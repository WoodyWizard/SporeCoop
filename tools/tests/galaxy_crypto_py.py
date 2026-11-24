#!/usr/bin/env python3
"""Decision 8Q A: the galaxy traffic's crypto in plain Python, written from the RFCs (not from Monocypher), so the
server check talks to the server as an encrypted game and shows the server's Monocypher use is the standard one:
X25519 (RFC 7748), XChaCha20-Poly1305 (RFC 8439 with HChaCha20, as libsodium and Monocypher), BLAKE2b (hashlib).
Wire format: mods/coop/net/galaxy_crypto.h. Slow (pure Python): for tests only.

    py tools/tests/galaxy_crypto_py.py      (its own known-answer tests)
"""
import hashlib
import os
import struct

P = 2 ** 255 - 19
A24 = 121665


def _clamp(k):
    k = bytearray(k)
    k[0] &= 248
    k[31] &= 127
    k[31] |= 64
    return int.from_bytes(k, 'little')


def x25519(secret, pub):
    k = _clamp(secret)
    u = int.from_bytes(pub, 'little') & ((1 << 255) - 1)
    x1, x2, z2, x3, z3, swap = u, 1, 0, u, 1, 0
    for t in range(254, -1, -1):
        kt = (k >> t) & 1
        swap ^= kt
        if swap:
            x2, x3, z2, z3 = x3, x2, z3, z2
        swap = kt
        a, b = (x2 + z2) % P, (x2 - z2) % P
        aa, bb = a * a % P, b * b % P
        e = (aa - bb) % P
        c, d = (x3 + z3) % P, (x3 - z3) % P
        da, cb = d * a % P, c * b % P
        x3, z3 = (da + cb) ** 2 % P, x1 * (da - cb) ** 2 % P
        x2, z2 = aa * bb % P, e * (aa + A24 * e) % P
    if swap:
        x2, z2 = x3, z3
    return (x2 * pow(z2, P - 2, P) % P).to_bytes(32, 'little')


def x25519_public(secret):
    return x25519(secret, (9).to_bytes(32, 'little'))


def _rotl(v, n):
    return ((v << n) & 0xFFFFFFFF) | (v >> (32 - n))


def _qr(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] = _rotl(s[d] ^ s[a], 16)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] = _rotl(s[b] ^ s[c], 12)
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] = _rotl(s[d] ^ s[a], 8)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] = _rotl(s[b] ^ s[c], 7)


def _rounds(s):
    for _ in range(10):
        _qr(s, 0, 4, 8, 12); _qr(s, 1, 5, 9, 13); _qr(s, 2, 6, 10, 14); _qr(s, 3, 7, 11, 15)
        _qr(s, 0, 5, 10, 15); _qr(s, 1, 6, 11, 12); _qr(s, 2, 7, 8, 13); _qr(s, 3, 4, 9, 14)


SIGMA = struct.unpack('<4I', b'expand 32-byte k')


def chacha20_block(key, counter, nonce12):
    init = list(SIGMA) + list(struct.unpack('<8I', key)) + [counter & 0xFFFFFFFF] + list(struct.unpack('<3I', nonce12))
    s = init[:]
    _rounds(s)
    return struct.pack('<16I', *[(s[i] + init[i]) & 0xFFFFFFFF for i in range(16)])


def hchacha20(key, nonce16):
    s = list(SIGMA) + list(struct.unpack('<8I', key)) + list(struct.unpack('<4I', nonce16))
    _rounds(s)
    return struct.pack('<8I', *(s[0:4] + s[12:16]))


def chacha20_xor(key, counter, nonce12, data):
    out = bytearray()
    for i in range(0, len(data), 64):
        ks = chacha20_block(key, counter + i // 64, nonce12)
        out += bytes(a ^ b for a, b in zip(data[i:i + 64], ks))
    return bytes(out)


def poly1305(key, msg):
    r = int.from_bytes(key[:16], 'little') & 0x0ffffffc0ffffffc0ffffffc0fffffff
    s = int.from_bytes(key[16:], 'little')
    p, acc = (1 << 130) - 5, 0
    for i in range(0, len(msg), 16):
        n = int.from_bytes(msg[i:i + 16] + b'\x01', 'little')
        acc = (acc + n) * r % p
    return ((acc + s) & ((1 << 128) - 1)).to_bytes(16, 'little')


def _pad16(b):
    return b'\x00' * (-len(b) % 16)


def _mac(otk, ad, ct):
    return poly1305(otk, ad + _pad16(ad) + ct + _pad16(ct) + struct.pack('<QQ', len(ad), len(ct)))


def xchacha_lock(key, nonce24, ad, plain):
    sub = hchacha20(key, nonce24[:16])
    n12 = b'\x00' * 4 + nonce24[16:]
    otk = chacha20_block(sub, 0, n12)[:32]
    ct = chacha20_xor(sub, 1, n12, plain)
    return ct, _mac(otk, ad, ct)


def xchacha_unlock(key, nonce24, ad, ct, mac):
    sub = hchacha20(key, nonce24[:16])
    n12 = b'\x00' * 4 + nonce24[16:]
    otk = chacha20_block(sub, 0, n12)[:32]
    if _mac(otk, ad, ct) != mac:
        return None
    return chacha20_xor(sub, 1, n12, ct)


CONTEXT = b'SporeCoop galaxy key exchange v1'
HELLO_BYTES, ANSWER_BYTES, OVERHEAD = 96, 67, 25


class GameChannel:
    """A game's side of the exchange and the sealed datagrams (net/galaxy_crypto.h)."""

    def __init__(self):
        self.secret = os.urandom(32)
        self.pub = x25519_public(self.secret)
        self.tx = self.rx = None
        self.tx_counter = 0
        self.server_static = None

    def question(self):
        q = b'K\x01\x01' + self.pub
        return q + b'\x00' * (HELLO_BYTES - len(q))

    def answer(self, a):
        if len(a) < ANSWER_BYTES or a[0:3] != b'K\x02\x01':
            return False
        st, ep = a[3:35], a[35:67]
        s1, s2 = x25519(self.secret, st), x25519(self.secret, ep)
        k = hashlib.blake2b(CONTEXT + self.pub + st + ep + s1 + s2, digest_size=64).digest()
        self.tx, self.rx, self.server_static = k[:32], k[32:], st
        return True

    def seal(self, datagram):
        self.tx_counter += 1
        head = b'E' + struct.pack('<Q', self.tx_counter)
        ct, mac = xchacha_lock(self.tx, struct.pack('<Q', self.tx_counter) + b'\x00' * 16, head, datagram)
        return head + mac + ct

    def open(self, d):
        if len(d) < OVERHEAD + 1 or d[0:1] != b'E':
            return None
        return xchacha_unlock(self.rx, d[1:9] + b'\x00' * 16, d[:9], d[25:], d[9:25])


def _selftest():
    ok = True
    # RFC 7748 6.1
    a = bytes.fromhex('77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a')
    b = bytes.fromhex('5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb')
    ok &= x25519_public(a).hex() == '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a'
    ok &= x25519(a, x25519_public(b)).hex() == '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'
    # draft-irtf-cfrg-xchacha 2.2.1 (HChaCha20)
    k = bytes(range(32))
    n = bytes.fromhex('000000090000004a0000000031415927')
    ok &= hchacha20(k, n).hex() == '82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc'
    # RFC 8439 2.5.2 (Poly1305)
    pk = bytes.fromhex('85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b')
    ok &= poly1305(pk, b'Cryptographic Forum Research Group').hex() == 'a8061dc1305136c6c22b8baf0c0127a9'
    print('galaxy_crypto_py self test:', 'ok' if ok else 'FAILED')
    return ok


if __name__ == '__main__':
    raise SystemExit(0 if _selftest() else 1)
