#!/usr/bin/env python3
"""One-time: convert assets/panoramas/*.png to JPEG q90 4:4:4 and delete the PNGs."""
import glob, os
from PIL import Image
for p in sorted(glob.glob("assets/panoramas/*.png")):
    out = p[:-4] + ".jpg"
    Image.open(p).convert("RGB").save(out, "JPEG", quality=90, subsampling=0)
    os.remove(p)
    print(f"{out}  {os.path.getsize(out)/1048576:.2f} MB")
