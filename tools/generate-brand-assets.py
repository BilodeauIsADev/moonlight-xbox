#!/usr/bin/env python3
"""Regenerate the Lunalight tile, splash and store assets in Assets/,
plus the README banner in .github/assets/.

Requires Pillow and the Ubuntu Sans variable font
(/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf on Ubuntu).

    python3 tools/generate-brand-assets.py
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ASSETS = os.path.join(ROOT, "Assets")
FONT = "/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf"
SS = 4  # supersampling factor

BG = (12, 12, 14)
GLOW = (34, 34, 40)
INK_TOP = (255, 255, 255)
INK_BOTTOM = (140, 140, 150)
DARK_INK = (24, 24, 28)


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def gradient(size, top, bottom):
    """Diagonal gradient from top-left to bottom-right."""
    w, h = size
    small = Image.new("RGB", (64, 64))
    px = small.load()
    for y in range(64):
        for x in range(64):
            px[x, y] = lerp(top, bottom, (x + y) / 126)
    return small.resize((w, h), Image.BILINEAR)


def mark(d, ink=None):
    """Crescent moon with a faint earthshine disc and a small star, d x d px, RGBA."""
    D = d * SS
    r = D * 0.36
    cx, cy = D * 0.47, D * 0.53

    disc = Image.new("L", (D, D), 0)
    ImageDraw.Draw(disc).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    cut = Image.new("L", (D, D), 0)
    ox, oy, cr = cx + r * 0.42, cy - r * 0.30, r * 0.86
    ImageDraw.Draw(cut).ellipse((ox - cr, oy - cr, ox + cr, oy + cr), fill=255)
    crescent = ImageChops.subtract(disc, cut)

    # Earthshine: the hidden part of the disc, barely lit.
    shine = disc.point(lambda v: v * 0.10)

    star = Image.new("L", (D, D), 0)
    sx, sy, sr = cx + r * 0.95, cy - r * 0.95, r * 0.075
    ImageDraw.Draw(star).ellipse((sx - sr, sy - sr, sx + sr, sy + sr), fill=255)

    alpha = ImageChops.lighter(ImageChops.lighter(crescent, shine), star)
    fill = Image.new("RGB", (D, D), ink) if ink else gradient((D, D), INK_TOP, INK_BOTTOM)
    out = fill.convert("RGBA")
    out.putalpha(alpha)
    return out.resize((d, d), Image.LANCZOS)


def background(w, h, focus=(0.5, 0.5)):
    """Near-black plate with a soft glow behind the mark."""
    img = Image.new("RGB", (w, h), BG)
    glow = Image.new("L", (w, h), 0)
    gr = max(w, h) * 0.45
    fx, fy = focus[0] * w, focus[1] * h
    ImageDraw.Draw(glow).ellipse((fx - gr, fy - gr, fx + gr, fy + gr), fill=255)
    glow = glow.filter(ImageFilter.GaussianBlur(gr * 0.5))
    img = Image.composite(Image.new("RGB", (w, h), GLOW), img, glow)
    return img.convert("RGBA")


def font(px, weight=300):
    f = ImageFont.truetype(FONT, px)
    f.set_variation_by_axes([100, weight])
    return f


def wordmark(height, ink=INK_TOP, weight=300):
    """'LUNALIGHT' in wide-tracked light caps; returns RGBA cropped to the text."""
    return wordmark_text("LUNALIGHT", height, ink, weight)


def wordmark_text(text, height, ink=INK_TOP, weight=300):
    px = height * SS
    f = font(px, weight)
    tracking = px * 0.32
    widths = [f.getlength(c) for c in text]
    W = int(sum(widths) + tracking * (len(text) - 1) + px)
    canvas = Image.new("L", (W, px * 2), 0)
    dr = ImageDraw.Draw(canvas)
    x = px * 0.5
    for c, cw in zip(text, widths):
        dr.text((x, px * 0.4), c, font=f, fill=255)
        x += cw + tracking
    canvas = canvas.crop(canvas.getbbox())
    canvas = canvas.resize((max(1, canvas.width // SS), max(1, canvas.height // SS)), Image.LANCZOS)
    out = Image.new("RGBA", canvas.size, ink + (255,))
    out.putalpha(canvas)
    return out


def paste_center(base, layer, cx, cy):
    base.alpha_composite(layer, (round(cx - layer.width / 2), round(cy - layer.height / 2)))


def tile_square(s, mark_frac):
    img = background(s, s)
    paste_center(img, mark(round(s * mark_frac)), s / 2, s / 2)
    return img


def tile_with_word(w, h, mark_frac, word_frac):
    """Mark above a wordmark, stacked and centred."""
    img = background(w, h, (0.5, 0.42))
    m = mark(round(min(w, h) * mark_frac))
    t = wordmark(max(4, round(h * word_frac)))
    gap = h * 0.04
    total = m.height + gap + t.height
    top = (h - total) / 2
    paste_center(img, m, w / 2, top + m.height / 2)
    paste_center(img, t, w / 2, top + m.height + gap + t.height / 2)
    return img


def tile_wide(w, h):
    """Mark beside the wordmark."""
    img = background(w, h, (0.32, 0.5))
    m = mark(round(h * 0.56))
    t = wordmark(round(h * 0.11))
    gap = h * 0.02
    total = m.width + gap + t.width
    left = (w - total) / 2
    paste_center(img, m, left + m.width / 2, h / 2)
    paste_center(img, t, left + m.width + gap + t.width / 2, h / 2)
    return img


def readme_banner(w, h):
    """Wide lockup with a tagline, for the top of the README."""
    img = background(w, h, (0.5, 0.45))
    m = mark(round(h * 0.42))
    t = wordmark(round(h * 0.07))
    tag = wordmark_text("STREAM YOUR PC GAMES TO XBOX", round(h * 0.03), (150, 150, 158), 400)
    gap1, gap2 = h * 0.05, h * 0.045
    top = (h - (m.height + gap1 + t.height + gap2 + tag.height)) / 2
    paste_center(img, m, w / 2, top + m.height / 2)
    y = top + m.height + gap1
    paste_center(img, t, w / 2, y + t.height / 2)
    paste_center(img, tag, w / 2, y + t.height + gap2 + tag.height / 2)
    return img


def transparent_mark(s, frac, ink=None):
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    paste_center(img, mark(round(s * frac), ink), s / 2, s / 2)
    return img


SCALES = {100: 1, 125: 1.25, 150: 1.5, 200: 2, 400: 4}


def save(img, name):
    img.save(os.path.join(ASSETS, name), optimize=True)


def main():
    if not os.path.exists(FONT):
        sys.exit(f"missing font: {FONT}")

    for scale, f in SCALES.items():
        sz = lambda n: math.ceil(n * f)
        save(tile_square(sz(150), 0.58), f"Square150x150Logo.scale-{scale}.png")
        save(tile_square(sz(71), 0.66), f"SmallTile.scale-{scale}.png")
        save(tile_with_word(sz(310), sz(310), 0.46, 0.055), f"LargeTile.scale-{scale}.png")
        save(tile_wide(sz(310), sz(150)), f"Wide310x150Logo.scale-{scale}.png")
        save(tile_square(sz(44), 0.78), f"Square44x44Logo.scale-{scale}.png")
        save(tile_square(sz(50), 0.72), f"StoreLogo.scale-{scale}.png")
        # Splash: the image sits on the manifest's splash BackgroundColor, keep it transparent.
        sw, sh = sz(620), sz(300)
        splash = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
        m = mark(round(sh * 0.42))
        t = wordmark(round(sh * 0.05))
        top = (sh - (m.height + sh * 0.05 + t.height)) / 2
        paste_center(splash, m, sw / 2, top + m.height / 2)
        paste_center(splash, t, sw / 2, top + m.height + sh * 0.05 + t.height / 2)
        save(splash, f"SplashScreen.scale-{scale}.png")

    for ts in (16, 24, 32, 48, 256):
        save(tile_square(ts, 0.80), f"Square44x44Logo.targetsize-{ts}.png")
        save(transparent_mark(ts, 0.96, DARK_INK), f"Square44x44Logo.altform-lightunplated_targetsize-{ts}.png")
        # The project file names the 24px unplated variant differently.
        unplated = "targetsize-24_altform-unplated" if ts == 24 else f"altform-unplated_targetsize-{ts}"
        save(transparent_mark(ts, 0.96), f"Square44x44Logo.{unplated}.png")

    # Lock screen badge must be white on transparent.
    save(transparent_mark(48, 0.96, (255, 255, 255)), "LockScreenLogo.scale-200.png")

    # In-app logo and store poster.
    save(transparent_mark(512, 1.0), "LunalightMark.png")
    save(tile_with_word(720, 1080, 0.52, 0.03), "Store-Poster.png")

    github = os.path.join(ROOT, ".github", "assets")
    os.makedirs(github, exist_ok=True)
    readme_banner(1600, 560).save(os.path.join(github, "lunalight-banner.png"), optimize=True)


if __name__ == "__main__":
    main()
