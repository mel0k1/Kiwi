#!/usr/bin/env python3
"""Generate the hicolor icon set for Kiwi from the mascot PNG.

Sizes follow the freedesktop hicolor icon theme convention. The master
artwork is assets/logo.png (256x256 RGBA, flat kiwi bird); smaller sizes
are downscaled with LANCZOS for crisp small renditions. 512 is upscaled
from the 256 master and therefore skipped on purpose.
"""
from __future__ import annotations

import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "assets", "logo.png")
OUT_BASE = os.path.join(ROOT, "assets", "icons", "hicolor")
SIZES = [16, 22, 24, 32, 48, 64, 128, 256]
NAME = "io.github.mel0k1.Kiwi.png"


def main() -> int:
    master = Image.open(MASTER).convert("RGBA")
    if master.size != (256, 256):
        master = master.resize((256, 256), Image.LANCZOS)
    for size in SIZES:
        out_dir = os.path.join(OUT_BASE, f"{size}x{size}", "apps")
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, NAME)
        icon = master if size >= 256 else master.resize((size, size), Image.LANCZOS)
        icon.save(out_path, "PNG", optimize=True)
        print(f"{out_path} ({size}x{size})")
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
