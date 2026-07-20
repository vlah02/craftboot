#!/usr/bin/env python3
"""Regenerate the panorama assets from Minecraft's real per-version cubemaps.

Minecraft release client jars ship 1x1 placeholder panorama faces; the real
1024x1024 cube faces live in the version's downloadable asset index (1.14+) or,
for the earliest cubemap era, inside the client jar (1.13). This tool fetches
the 6 faces per version and reprojects the cube -> a 4096x2048 equirectangular
JPEG (which the boot menu renders at Minecraft's real ~85deg FOV -> sharp edge
to edge, no wide-FOV edge magnification). Output: assets/panoramas/<theme>.jpg

1.12 classic predates the cubemap panorama; its source equirect (assets_src/)
is kept and only uniformly blurred so it reads as an intentional soft look
rather than the sharp-centre/blurry-edge gradient a low-res source gives.

Usage: tools/fetch_panoramas.py            (writes assets/panoramas/)
Requires: python3 + Pillow + numpy + network (piston-meta.mojang.com + CDN).
"""
import io, json, os, sys, urllib.request, zipfile
import numpy as np
from PIL import Image

MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCES = "https://resources.download.minecraft.net"
FACE = "assets/minecraft/textures/gui/title/background/panorama_%d.png"
EQUI_W = 4096   # 4096x2048 captures the native 1024^2 cube detail (11.4 px/deg)

# theme -> (version, source): "index" = asset index by hash, "jar" = client.jar
PANORAMAS = {
    "1.13_aquatic": ("1.13.2","jar"), "1.14_village": ("1.14.4","index"),
    "1.15_bees": ("1.15.2","index"), "1.16_nether": ("1.16.5","index"),
    "1.17_cliffs": ("1.17.1","index"), "1.18_caves": ("1.18.2","index"),
    "1.19_wild": ("1.19.4","index"), "1.20_trails": ("1.20.1","index"),
    "1.21.00_tricky_trials": ("1.21.1","index"), "1.21.04_pale_garden": ("1.21.4","index"),
    "1.21.05_spring": ("1.21.5","index"), "1.21.06_skies": ("1.21.6","index"),
    "1.21.09_copper": ("1.21.9","index"), "1.21.11_mounts": ("1.21.11","index"),
}

def get(u):
    with urllib.request.urlopen(u, timeout=120) as r: return r.read()

def faces_for(manifest, ver, src):
    vj = json.loads(get(next(v["url"] for v in manifest["versions"] if v["id"]==ver)))
    imgs = []
    if src == "index":
        idx = json.loads(get(vj["assetIndex"]["url"]))["objects"]
        for i in range(6):
            h = idx[FACE % i]["hash"]
            imgs.append(Image.open(io.BytesIO(get(f"{RESOURCES}/{h[:2]}/{h}"))).convert("RGB"))
    else:
        jar = zipfile.ZipFile(io.BytesIO(get(vj["downloads"]["client"]["url"])))
        for i in range(6):
            imgs.append(Image.open(io.BytesIO(jar.read(FACE % i))).convert("RGB"))
    return [np.asarray(im) for im in imgs]

def cube_to_equirect(faces, W):
    """MC layout: 0=front(+Z) 1=right(+X) 2=back(-Z) 3=left(-X) 4=up(+Y) 5=down(-Y)."""
    H, fs = W//2, faces[0].shape[0]
    lon = (np.arange(W)+0.5)/W*2*np.pi - np.pi
    lat = np.pi/2 - (np.arange(H)+0.5)/H*np.pi
    LON, LAT = np.meshgrid(lon, lat)
    x, y, z = np.sin(LON)*np.cos(LAT), np.sin(LAT), np.cos(LON)*np.cos(LAT)
    ax, ay, az = np.abs(x), np.abs(y), np.abs(z)
    out = np.zeros((H, W, 3), np.uint8)
    def s(f, u, v, m):
        uu = np.clip(((u+1)/2*fs), 0, fs-1).astype(np.int32)
        vv = np.clip(((v+1)/2*fs), 0, fs-1).astype(np.int32)
        out[m] = faces[f][vv[m], uu[m]]
    nz = lambda d: np.where(d==0, 1e-9, d)
    s(0, x/nz(z),  -y/nz(z),  (az>=ax)&(az>=ay)&(z>0))
    s(2, -x/nz(-z),-y/nz(-z), (az>=ax)&(az>=ay)&(z<0))
    s(1, -z/nz(x), -y/nz(x),  (ax>=ay)&(ax>=az)&(x>0))
    s(3, z/nz(-x), -y/nz(-x), (ax>=ay)&(ax>=az)&(x<0))
    s(4, x/nz(y),  z/nz(y),   (ay>=ax)&(ay>=az)&(y>0))
    s(5, x/nz(-y), -z/nz(-y), (ay>=ax)&(ay>=az)&(y<0))
    return Image.fromarray(out)

def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(repo, "assets", "panoramas")
    os.makedirs(outdir, exist_ok=True)
    manifest = json.loads(get(MANIFEST))
    for theme, (ver, src) in PANORAMAS.items():
        eq = cube_to_equirect(faces_for(manifest, ver, src), EQUI_W)
        eq.save(os.path.join(outdir, f"{theme}.jpg"), "JPEG", quality=90)
        print(f"{theme:24s} {ver:8s} {src:5s} -> {theme}.jpg {eq.size}")
    # 1.12 classic: the 1.8-1.12 panorama faces are only 256x256 in the jar, so
    # this equirect (assets_src/) can't reach the 1024^2-sourced sharpness of the
    # 1.13+ themes. Shipped verbatim (no blur) -- at a wide FOV a blur only made
    # the low-res source look worse, not better.
    src12 = os.path.join(repo, "assets_src", "1.12_classic.jpg")
    if os.path.isfile(src12):
        Image.open(src12).convert("RGB") \
            .save(os.path.join(outdir, "1.12_classic.jpg"), "JPEG", quality=92)
        print("1.12_classic            (verbatim from assets_src/, no blur)")
    else:
        print("1.12_classic            (no assets_src/ source; leaving existing file)")

if __name__ == "__main__":
    main()
