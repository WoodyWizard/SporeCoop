#!/usr/bin/env python3
"""Decodes the JPEGs of tests/jpeg_tests.cpp (SporeKit's encoder for report screenshots) with Pillow
and compares each with its source .ppm: same size, mean error per channel under the quality's bound.

  jpeg_check.py <folder>
"""
import glob
import os
import sys

from PIL import Image, ImageChops, ImageStat

# mean absolute error (0-255) allowed per image: q80 on a busy scene is about 3-6
LIMIT = {'low_q10_200x100': 25}


def main():
    d = sys.argv[1]
    files = sorted(glob.glob(os.path.join(d, '*.jpg')))
    bad = 0
    if len(files) < 8:
        print(f'FAIL: only {len(files)} JPEGs in {d}')
        return 1
    for j in files:
        name = os.path.basename(j)[:-4]
        src = Image.open(j[:-4] + '.ppm').convert('RGB')
        try:
            img = Image.open(j)
            img.load()
        except Exception as e:  # noqa: BLE001 - any decoder error is a failure
            print(f'FAIL: {name}: Pillow cannot decode it: {e}')
            bad += 1
            continue
        img = img.convert('RGB')
        err = sum(ImageStat.Stat(ImageChops.difference(src, img)).mean) / 3
        lim = LIMIT.get(name, 9)
        ok = img.size == src.size and err <= lim
        print(f'  {"ok  " if ok else "FAIL"} {name}: {img.size[0]}x{img.size[1]}, mean error {err:.2f} (limit {lim}), '
              f'{os.path.getsize(j)} bytes, comment {img.info.get("comment", b"")!r}')
        bad += not ok
    print('jpeg_check: ' + ('OK' if not bad else f'FAILED ({bad})'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
