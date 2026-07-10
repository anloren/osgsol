#!/usr/bin/env python3
"""R/B-swap corruption classifier for EarthExplorer 1920x1080 captures.

Samples known-ocean pixels (default EARTH_SUN_TO_CAMERA autocap view).
Blue-dominant = clean; red-dominant = corrupted tile.
Exit: prints per-point verdicts + final CLEAN/CORRUPT/UNKNOWN.
"""
import sys
from PIL import Image

# 已知海洋采样点 (x, y)：北大西洋(赤道以北瓦片) + 南大西洋(赤道以南瓦片)
NORTH_OCEAN = [(760, 420), (800, 350), (735, 470), (860, 300), (700, 500), (820, 430)]
SOUTH_OCEAN = [(900, 800), (950, 850), (1000, 780), (870, 700)]

def dom(px):
    r, g, b = px[0], px[1], px[2]
    if b > r + 25: return 'B'
    if r > b + 25: return 'R'
    return '?'

def classify(path, verbose=True):
    im = Image.open(path).convert('RGB')
    if im.size != (1920, 1080):
        sx, sy = im.size[0] / 1920.0, im.size[1] / 1080.0
    else:
        sx = sy = 1.0
    reds = blues = unk = 0
    details = []
    for pts, tag in ((NORTH_OCEAN, 'N'), (SOUTH_OCEAN, 'S')):
        for (x, y) in pts:
            px = im.getpixel((int(x * sx), int(y * sy)))
            d = dom(px)
            details.append((tag, x, y, px[:3], d))
            if d == 'R': reds += 1
            elif d == 'B': blues += 1
            else: unk += 1
    if verbose:
        for t, x, y, p, d in details:
            print(f"  {t} ({x},{y}) rgb={p} -> {d}")
    # 判定：任何采样点红主导 => 有瓦片被 R/B 交换
    if reds >= 1:
        verdict = 'CORRUPT'
    elif blues >= len(details) - 2:
        verdict = 'CLEAN'
    else:
        verdict = 'UNKNOWN'
    print(f"{path}: reds={reds} blues={blues} unk={unk} => {verdict}")
    return verdict

if __name__ == '__main__':
    rc = 0
    for p in sys.argv[1:]:
        v = classify(p)
        if v == 'CORRUPT': rc = 1
        elif v == 'UNKNOWN': rc = 2
    sys.exit(rc)
