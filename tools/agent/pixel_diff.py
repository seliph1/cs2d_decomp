#!/usr/bin/env python3
"""Pixel comparison of two screenshots (the original client vs ours, or two of our runs).

    tools/agent/pixel_diff.py <reference> <ours> [options]

Both images are loaded with Pillow (BMP and PNG both work). The reference may be a shot of the
original's *window*, which `drive_orig.ps1 shot:` writes with the border and title bar in it: pass
`--window-shot` to crop it to the client area (3 px border, 26 px title - what `drive_orig.ps1`'s
`mouse:` step assumes).

Report: differing pixels (%), mean and max channel delta, PSNR, a per-band breakdown (horizontal
strips, to tell the HUD from the playfield) and the worst 32x32 cells. `--mask x,y,w,h` leaves a
region out of the statistics (e.g. the HUD), `--diff out.png` writes a heat map (bright = differing).
"""
import argparse
import math
import sys

from PIL import Image, ImageChops

BANDS = 8


def parse_box(text):
    parts = [int(v) for v in text.replace(" ", "").split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("expected x,y,w,h")
    return tuple(parts)


def masked(x, y, boxes):
    return any(bx <= x < bx + bw and by <= y < by + bh for bx, by, bw, bh in boxes)


def stats(ref, ours, tol, mask_boxes):
    diff = ImageChops.difference(ref, ours).convert("L")
    px = diff.load()
    w, h = diff.size
    total = differing = 0
    sum_delta = max_delta = 0
    sum_sq = 0.0
    bands = [[0, 0, 0] for _ in range(BANDS)]  # (total, differing, sum delta) per horizontal strip
    for y in range(h):
        band = min(BANDS - 1, y * BANDS // h)
        for x in range(w):
            if masked(x, y, mask_boxes):
                continue
            v = px[x, y]
            total += 1
            sum_delta += v
            sum_sq += v * v
            bands[band][0] += 1
            bands[band][2] += v
            if v > max_delta:
                max_delta = v
            if v > tol:
                differing += 1
                bands[band][1] += 1
    return {"total": total, "differing": differing, "max": max_delta, "size": (w, h),
            "bands": [tuple(b) for b in bands],
            "mean": sum_delta / total if total else 0.0, "mse": sum_sq / total if total else 0.0}


def worst_cells(ref, ours, tol, mask_boxes, cell=32, top=5):
    diff = ImageChops.difference(ref, ours).convert("L")
    px = diff.load()
    w, h = diff.size
    cells = []
    for cy in range(0, h, cell):
        for cx in range(0, w, cell):
            n = bad = 0
            for y in range(cy, min(cy + cell, h)):
                for x in range(cx, min(cx + cell, w)):
                    if masked(x, y, mask_boxes):
                        continue
                    n += 1
                    if px[x, y] > tol:
                        bad += 1
            if n:
                cells.append((bad / n, cx, cy))
    cells.sort(reverse=True)
    return [c for c in cells if c[0] > 0][:top]


def main():
    ap = argparse.ArgumentParser(description="Pixel comparison of two CS2D screenshots")
    ap.add_argument("reference")
    ap.add_argument("ours")
    ap.add_argument("--window-shot", action="store_true",
                    help="the reference is a whole CS2D window: crop the client area first")
    ap.add_argument("--crop", type=parse_box, help="crop both images (x,y,w,h)")
    ap.add_argument("--crop-ref", type=parse_box, help="crop only the reference")
    ap.add_argument("--mask", type=parse_box, action="append", default=[],
                    help="leave a region out of the statistics (repeatable)")
    ap.add_argument("--tol", type=int, default=0, help="per-channel delta counted as equal (default 0)")
    ap.add_argument("--diff", help="write a difference heat map here")
    args = ap.parse_args()

    ref = Image.open(args.reference).convert("RGB")
    ours = Image.open(args.ours).convert("RGB")
    print(f"reference {args.reference}: {ref.size[0]}x{ref.size[1]}")
    print(f"ours      {args.ours}: {ours.size[0]}x{ours.size[1]}")

    if args.window_shot:
        # drive_orig.ps1's mouse: step assumes a 3 px border and a 26 px title above the client area
        box = (3, 26, 3 + ours.size[0], 26 + ours.size[1])
        if box[2] > ref.size[0] or box[3] > ref.size[1]:
            print(f"the window shot is smaller than a {ours.size[0]}x{ours.size[1]} client area", file=sys.stderr)
            return 2
        ref = ref.crop(box)
    if args.crop:
        c = args.crop
        ref = ref.crop((c[0], c[1], c[0] + c[2], c[1] + c[3]))
        ours = ours.crop((c[0], c[1], c[0] + c[2], c[1] + c[3]))
    elif args.crop_ref:
        c = args.crop_ref
        ref = ref.crop((c[0], c[1], c[0] + c[2], c[1] + c[3]))
    if ref.size != ours.size:
        print(f"size mismatch after cropping: {ref.size} vs {ours.size}", file=sys.stderr)
        return 2

    s = stats(ref, ours, args.tol, args.mask)
    pct = 100.0 * s["differing"] / s["total"] if s["total"] else 0.0
    psnr = 10.0 * math.log10(255 * 255 / s["mse"]) if s["mse"] else 99.0
    print(f"compared  {s['size'][0]}x{s['size'][1]} ({s['total']} px"
          + (f", {len(args.mask)} masked region(s)" if args.mask else "") + ")")
    print(f"differing {s['differing']} px = {pct:.2f}% (> {args.tol}), mean |delta| {s['mean']:.2f}, "
          f"max {s['max']}, PSNR {psnr:.2f} dB")
    print("bands (top to bottom): " + " ".join(
        f"{100.0 * bd / bt:.1f}%" if bt else "-" for bt, bd, _ in s["bands"]))
    cells = worst_cells(ref, ours, args.tol, args.mask)
    if cells:
        print("worst 32x32 cells (x,y): " + ", ".join(f"({cx},{cy}) {100 * r:.0f}%" for r, cx, cy in cells))

    if args.diff:
        ImageChops.difference(ref, ours).convert("L").point(lambda v: min(255, v * 3)).save(args.diff)
        print(f"wrote {args.diff}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

