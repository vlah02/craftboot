#!/usr/bin/env python3
"""Assemble an animated WebP from raw frame dumps captured via
CRAFTBOOT_SHOT_SEQ (see src/platform/display_sdl.c): the dev binary, run
under SDL_VIDEODRIVER=dummy, writes each staging frame as
<dir>/frame_NNNN.raw (1920x1080 XRGB8888, no header) for a window of flips,
then exits. This script downscales and packs those frames into a WebP for
the README.

Usage:
    python3 tools/make_demo.py OUT.webp DIR [DIR ...] [--fps 15] [--width 960]
                               [--height 540] [--quality 80]

Multiple DIRs are concatenated in order (used for demo-worlds.webp, which
splices a few seconds from several separate captures -- each boot picks a
random panorama world).
"""
import argparse
import glob
import os
import sys

from PIL import Image

SRC_W, SRC_H = 1920, 1080
FRAME_BYTES = SRC_W * SRC_H * 4


def load_frame(path, size):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != FRAME_BYTES:
        raise ValueError(f"{path}: {len(data)} bytes, expected {FRAME_BYTES} "
                         f"({SRC_W}x{SRC_H} XRGB8888)")
    # SDL_PIXELFORMAT_XRGB8888 on little-endian: byte order in memory is
    # B, G, R, X per pixel -- Pillow's "BGRX" raw mode decodes exactly that
    # into an RGB image (the X/alpha byte is discarded).
    img = Image.frombuffer("RGB", (SRC_W, SRC_H), data, "raw", "BGRX", 0, 1)
    if size != (SRC_W, SRC_H):
        img = img.resize(size, Image.LANCZOS)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="output .webp path")
    ap.add_argument("dirs", nargs="+", help="one or more frame_*.raw directories, in order")
    ap.add_argument("--fps", type=float, default=15.0, help="playback fps (default 15)")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=540)
    ap.add_argument("--quality", type=int, default=80)
    ap.add_argument("--start", type=int, default=0,
                     help="skip this many frames at the start of EACH dir (default 0)")
    ap.add_argument("--end", type=int, default=None,
                     help="stop at this frame index (exclusive) within EACH dir (default: all)")
    ap.add_argument("--stride", type=int, default=1,
                     help="keep every Nth frame after --start/--end (native capture fps is "
                          "much higher than playback fps; use this to cover more real time "
                          "without an oversized frame count, e.g. native~100fps/--fps 15 -> ~7)")
    ap.add_argument("--limit", type=int, default=None,
                     help="cap the number of frames taken from EACH dir after striding")
    args = ap.parse_args()

    size = (args.width, args.height)
    paths = []
    for d in args.dirs:
        found = sorted(glob.glob(os.path.join(d, "frame_*.raw")))
        if not found:
            sys.exit(f"no frame_*.raw files in {d}")
        window = found[args.start:args.end]
        window = window[::args.stride]
        if args.limit is not None:
            window = window[:args.limit]
        if not window:
            sys.exit(f"{d}: --start/--end/--stride/--limit left no frames")
        paths.extend(window)

    frames = [load_frame(p, size) for p in paths]
    duration_ms = int(round(1000.0 / args.fps))
    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                    duration=duration_ms, loop=0, quality=args.quality, method=6)
    kib = os.path.getsize(args.out) / 1024.0
    print(f"{args.out}: {len(frames)} frames from {len(args.dirs)} dir(s), "
          f"{size[0]}x{size[1]} @ {args.fps:.0f}fps -> {kib:.0f} KiB")


if __name__ == "__main__":
    main()
