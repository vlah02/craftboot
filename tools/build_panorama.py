#!/usr/bin/env python3
"""
Convert the 6 Minecraft panorama cubemap faces into one seamless equirectangular
PNG that the app can scroll horizontally with perfect wraparound.

Usage: build_panorama.py <faces_dir> <output.png> [width] [height]
Faces expected: panorama_0..5.png  (0 front, 1 right, 2 back, 3 left, 4 up, 5 down)
"""
import os
import sys

import numpy as np
from PIL import Image


def main():
    facedir = sys.argv[1] if len(sys.argv) > 1 else "."
    out = sys.argv[2] if len(sys.argv) > 2 else "panorama.png"
    W = int(sys.argv[3]) if len(sys.argv) > 3 else 2400
    H = int(sys.argv[4]) if len(sys.argv) > 4 else 1200

    faces = [
        np.asarray(Image.open(os.path.join(facedir, f"panorama_{i}.png")).convert("RGB"))
        for i in range(6)
    ]
    S = faces[0].shape[0]
    facearr = np.stack(faces)  # (6, S, S, 3)

    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    lon = (u * 2 * np.pi - np.pi)[None, :]        # (1, W): -pi..pi, increases -> turn right
    lat = (np.pi / 2 - v * np.pi)[:, None]        # (H, 1): +pi/2 (top) .. -pi/2 (bottom)

    x = np.cos(lat) * np.sin(lon) * np.ones((H, W))
    y = np.sin(lat) * np.ones((H, W))
    z = np.cos(lat) * np.cos(lon) * np.ones((H, W))
    ax, ay, az = np.abs(x), np.abs(y), np.abs(z)

    face_id = np.zeros((H, W), np.int64)
    sc = np.zeros((H, W)); tc = np.zeros((H, W)); ma = np.ones((H, W))

    mX = (ax >= ay) & (ax >= az)
    mY = (ay >= ax) & (ay >= az) & ~mX
    mZ = ~mX & ~mY
    px, nx = mX & (x > 0), mX & (x <= 0)
    py, ny = mY & (y > 0), mY & (y <= 0)
    pz, nz = mZ & (z > 0), mZ & (z <= 0)

    # +X = right(1), -X = left(3)
    face_id[px] = 1; sc[px] = -z[px]; tc[px] = -y[px]; ma[px] = ax[px]
    face_id[nx] = 3; sc[nx] =  z[nx]; tc[nx] = -y[nx]; ma[nx] = ax[nx]
    # +Y = up(4), -Y = down(5)
    face_id[py] = 4; sc[py] =  x[py]; tc[py] =  z[py]; ma[py] = ay[py]
    face_id[ny] = 5; sc[ny] =  x[ny]; tc[ny] = -z[ny]; ma[ny] = ay[ny]
    # +Z = front(0), -Z = back(2)
    face_id[pz] = 0; sc[pz] =  x[pz]; tc[pz] = -y[pz]; ma[pz] = az[pz]
    face_id[nz] = 2; sc[nz] = -x[nz]; tc[nz] = -y[nz]; ma[nz] = az[nz]

    # bilinear sample within each face (smooths face content vs nearest)
    fu = np.clip(((sc / ma) + 1) / 2 * (S - 1), 0, S - 1)
    fv = np.clip(((tc / ma) + 1) / 2 * (S - 1), 0, S - 1)
    u0 = np.floor(fu).astype(np.int64); u1 = np.minimum(u0 + 1, S - 1)
    v0 = np.floor(fv).astype(np.int64); v1 = np.minimum(v0 + 1, S - 1)
    wu = (fu - u0)[..., None]; wv = (fv - v0)[..., None]
    fa = facearr.astype(np.float32)
    p00 = fa[face_id, v0, u0]; p01 = fa[face_id, v0, u1]
    p10 = fa[face_id, v1, u0]; p11 = fa[face_id, v1, u1]
    top = p00 + (p01 - p00) * wu
    bot = p10 + (p11 - p10) * wu
    result = np.clip(top + (bot - top) * wv, 0, 255)  # (H, W, 3)

    outimg = Image.fromarray(result.astype(np.uint8))
    if out.lower().endswith((".jpg", ".jpeg")):
        outimg.save(out, "JPEG", quality=90, subsampling=0)
    else:
        outimg.save(out)
    print(f"wrote {out} ({W}x{H}) from {S}x{S} faces")


if __name__ == "__main__":
    main()
