#!/usr/bin/env python3
"""Generate the placeholder tile/splash PNGs an AppxManifest needs.

Usage: make_assets.py <output_dir> [icon.png]

If an icon is given it is centered on every asset, otherwise a plain colored square is used.
"""

import os
import sys

from PIL import Image

SIZES = {
    "StoreLogo.png": (50, 50),
    "Square44x44Logo.png": (44, 44),
    "Square150x150Logo.png": (150, 150),
    "Wide310x150Logo.png": (310, 150),
    "SplashScreen.png": (620, 300),
}
BACKGROUND = (16, 17, 20, 255)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    out_dir = sys.argv[1]
    icon = Image.open(sys.argv[2]).convert("RGBA") if len(sys.argv) > 2 and os.path.exists(sys.argv[2]) else None
    os.makedirs(out_dir, exist_ok=True)

    for name, (w, h) in SIZES.items():
        image = Image.new("RGBA", (w, h), BACKGROUND)
        if icon is not None:
            side = int(min(w, h) * 0.8)
            scaled = icon.resize((side, side), Image.LANCZOS)
            image.alpha_composite(scaled, ((w - side) // 2, (h - side) // 2))
        image.save(os.path.join(out_dir, name))
    print(f"wrote {len(SIZES)} assets to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
