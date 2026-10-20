"""Compare flat patches in decoded limited-range yuv420p10le frames.

For fisheye, select output samples by their inverse-warped source coordinates.
This measures patch interiors, not edge reconstruction or arbitrary images.
"""
import argparse
from array import array
import json
import math
from pathlib import Path
import statistics
import sys


def read_frame(path, width, height):
    data = Path(path).read_bytes()
    if len(data) != width * height * 3:
        raise ValueError("Expected exactly one planar yuv420p10le frame at the supplied resolution")
    values = array('H')
    values.frombytes(data)
    if sys.byteorder != 'little':
        values.byteswap()
    if max(values) > 1023:
        raise ValueError("Values exceed 10 bits: P010 or 16-bit input is not yuv420p10le")
    return values


def sample(frame, x, y, width, height):
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError("Sample outside recorded frame")
    chroma = (y // 2) * (width // 2) + x // 2
    return (frame[y * width + x], frame[width * height + chroma],
            frame[width * height * 5 // 4 + chroma])


def pq_nits(code):
    # Neutral patches only: limited-range Y equals the common R'G'B' PQ code.
    n = min(1., max(0., (code - 64) / 876)) ** (1 / (2523 / 32))
    return 10000 * (max(n - 3424 / 4096, 0) / (2413 / 128 - 2392 / 128 * n)) ** (1 / (2610 / 16384))


def source_uv(x, y, width, height, expansion):
    u, v = (x + .5 - expansion) / width, (y + .5 - expansion) / height
    dx, dy = u - .5, v - .5
    r, b = math.hypot(dx, dy), math.sqrt(.5)
    if r == 0:
        return u, v
    return (.5 + dx / r * math.tan(r * .76) * b / math.tan(b * .76),
            .5 + dy / r * math.tan(r * .80) * b / math.tan(b * .80))


def median(samples):
    if not samples:
        raise ValueError("No patch interior samples; enlarge patches or check the scene transforms")
    return [statistics.median(c) for c in zip(*samples)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clean', required=True)
    parser.add_argument('--test', required=True)
    parser.add_argument('--patches', required=True, help='JSON list of name, rect [x,y,w,h], neutral fields in source pixels')
    parser.add_argument('--mode', choices=['identity', 'fisheye'], default='identity')
    parser.add_argument('--width', type=int, default=3840)
    parser.add_argument('--height', type=int, default=2160)
    parser.add_argument('--source-width', type=int, default=1920)
    parser.add_argument('--source-height', type=int, default=1080)
    parser.add_argument('--clean-origin', type=int, nargs=2, default=[960, 540])
    parser.add_argument('--test-origin', type=int, nargs=2, help='Default identity 960 540; fisheye 860 440')
    parser.add_argument('--margin', type=int, default=8)
    parser.add_argument('--stride', type=int, default=4)
    args = parser.parse_args()
    if args.width % 2 or args.height % 2 or args.stride < 1 or args.margin < 0:
        parser.error('Frame dimensions must be even; stride positive; margin nonnegative')
    expansion = 100 if args.mode == 'fisheye' else 0
    origin = args.test_origin or ([860, 440] if expansion else [960, 540])
    clean = read_frame(args.clean, args.width, args.height)
    test = read_frame(args.test, args.width, args.height)
    patches = json.loads(Path(args.patches).read_text())
    rows = []
    failed = False
    for patch in patches:
        x, y, w, h = patch['rect']
        lo_x, lo_y, hi_x, hi_y = x+args.margin, y+args.margin, x+w-args.margin, y+h-args.margin
        reference = [sample(clean, args.clean_origin[0]+i, args.clean_origin[1]+j, args.width, args.height)
                     for j in range(lo_y, hi_y, args.stride) for i in range(lo_x, hi_x, args.stride)]
        output = []
        if expansion:
            for j in range(0, args.source_height+2*expansion, args.stride):
                for i in range(0, args.source_width+2*expansion, args.stride):
                    u, v = source_uv(i, j, args.source_width, args.source_height, expansion)
                    sx, sy = u*args.source_width-.5, v*args.source_height-.5
                    if lo_x <= sx < hi_x and lo_y <= sy < hi_y:
                        output.append(sample(test, origin[0]+i, origin[1]+j, args.width, args.height))
        else:
            output = [sample(test, origin[0]+i, origin[1]+j, args.width, args.height)
                      for j in range(lo_y, hi_y, args.stride) for i in range(lo_x, hi_x, args.stride)]
        a, b = median(reference), median(output)
        delta = [v-u for u,v in zip(a,b)]
        row = dict(name=patch['name'], clean_ycbcr=a, test_ycbcr=b, delta_ycbcr=delta, samples=len(output))
        passed = max(abs(d) for d in delta) <= 2
        if patch.get('neutral', False):
            nits_a, nits_b = pq_nits(a[0]), pq_nits(b[0])
            row.update(clean_nits=nits_a, test_nits=nits_b, delta_nits=nits_b-nits_a)
            passed = passed and abs(nits_b-nits_a) <= max(1, nits_a*.01)
        row['within_screening_tolerance'] = passed
        failed |= not passed
        rows.append(row)
    print(json.dumps(dict(mode=args.mode, patches=rows, passed=not failed), indent=2))
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
