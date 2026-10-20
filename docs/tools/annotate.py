"""Numbered markers on a screenshot, for the "control by control" tables.

    python annotate.py shot.png marks.json out.png

marks.json: [{"n": 1, "x": 12, "y": 40, "w": 120, "h": 22}, ...] in the
screenshot's pixels (the control's rectangle). Each control gets a soft frame
and a numbered badge at its left edge; badges that would overlap move down.
"""
import json
import sys

from PIL import Image, ImageDraw, ImageFont

ICE = (31, 111, 174)
ICE_LIGHT = (90, 169, 230)


def font(size):
    for name in ("segoeuib.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def annotate(src, marks, dst, scale=2):
    base = Image.open(src).convert("RGBA")
    # Draw on a larger canvas so the badges stay crisp when the PDF zooms.
    w, h = base.size
    img = base.resize((w * scale, h * scale), Image.LANCZOS)
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    r = 9 * scale
    f = font(11 * scale)
    placed = []
    for m in marks:
        x, y, cw, ch = (v * scale for v in (m["x"], m["y"], m["w"], m["h"]))
        d.rounded_rectangle([x - 1, y - 1, x + cw + 1, y + ch + 1], radius=3 * scale,
                            outline=ICE_LIGHT + (230,), width=max(2, scale))
        # Left of the control, so the badge never hides a checkbox or text.
        cx, cy = x - r - 3 * scale, y + min(ch, 2 * r) / 2
        cx = max(cx, r + 1)
        for px, py in placed:
            if abs(px - cx) < 2 * r and abs(py - cy) < 2 * r:
                cy = py + 2 * r + 2
        placed.append((cx, cy))
        d.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2], fill=(255, 255, 255, 255))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=ICE + (255,))
        d.text((cx, cy), str(m["n"]), fill="white", font=f, anchor="mm")
    Image.alpha_composite(img, over).convert("RGB").save(dst, optimize=True)


if __name__ == "__main__":
    src, marks, dst = sys.argv[1:4]
    annotate(src, json.load(open(marks, encoding="utf-8")), dst)
