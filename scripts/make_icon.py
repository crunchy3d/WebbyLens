"""Generates the WebbyLens icons into assets/:

    webbylens.ico (16-256 px), webbylens-256.png, webbylens-512.png   application icon
    tray-live.ico / tray-sound.ico / tray-standby.ico / tray-paused.ico  tray icons

    python scripts/make_icon.py

Design: a camera lens "streaming" to the right on a rounded tile. The tray icons keep
exactly this shape and differ only in colour, so users always recognise the icon:
blue = live video, green = sound only, grey = standby/waiting, orange = paused.
Each size is drawn separately with 8x supersampling; sizes up to 24 px use a simplified
lens and a single wave so they stay crisp.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
TRAY_SIZES = [16, 20, 24, 32, 40, 48, 64]
SS = 8  # supersampling factor

WHITE = (255, 255, 255)
DARK = (12, 26, 54)       # lens glass

# (tile top, tile bottom, iris) per state
BLUE = ((64, 160, 235), (28, 70, 170), (60, 140, 230))
GREEN = ((60, 205, 130), (20, 135, 85), (60, 190, 120))
GRAY = ((150, 156, 166), (96, 102, 114), (120, 128, 140))
ORANGE = ((250, 176, 60), (225, 120, 20), (245, 160, 60))


def gradient_tile(n: int, top, bottom) -> Image.Image:
    """Rounded square with a vertical gradient, n x n pixels (already supersampled)."""
    grad = Image.new("RGBA", (n, n))
    px = grad.load()
    for y in range(n):
        t = y / (n - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        for x in range(n):
            px[x, y] = c
    mask = Image.new("L", (n, n), 0)
    margin = round(n * 0.04)
    ImageDraw.Draw(mask).rounded_rectangle(
        [margin, margin, n - 1 - margin, n - 1 - margin], radius=round(n * 0.22), fill=255)
    tile = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)
    return tile


def circle(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def draw_icon(size: int, colors=BLUE) -> Image.Image:
    """The WebbyLens icon: a lens streaming to the right."""
    top, bottom, iris = colors
    n = size * SS
    img = gradient_tile(n, top, bottom)
    d = ImageDraw.Draw(img)
    simple = size <= 24
    cx, cy, r = n * 0.40, n * 0.50, n * 0.24
    circle(d, cx, cy, r, WHITE)                       # lens ring
    circle(d, cx, cy, r * 0.74, DARK)                 # glass
    if not simple:
        circle(d, cx, cy, r * 0.52, iris)             # iris
        circle(d, cx, cy, r * 0.30, DARK)             # pupil
    circle(d, cx - r * 0.26, cy - r * 0.26, r * (0.17 if not simple else 0.22), WHITE)   # highlight
    for rr in ((0.33, 0.43) if not simple else (0.37,)):   # "streaming" waves
        rr *= n
        d.arc([cx - rr, cy - rr, cx + rr, cy + rr], start=-42, end=42, fill=WHITE, width=round(n * 0.06))
    return img.resize((size, size), Image.LANCZOS)


def save_ico(path: Path, colors, sizes):
    imgs = [draw_icon(s, colors) for s in sizes]
    imgs[-1].save(path, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    return imgs


def main():
    OUT.mkdir(exist_ok=True)
    (ROOT / "build").mkdir(exist_ok=True)

    images = save_ico(OUT / "webbylens.ico", BLUE, SIZES)
    images[-1].save(OUT / "webbylens-256.png")
    draw_icon(512).save(OUT / "webbylens-512.png")

    tray = {"tray-live.ico": BLUE, "tray-sound.ico": GREEN,
            "tray-standby.ico": GRAY, "tray-paused.ico": ORANGE}
    tray_imgs = {name: save_ico(OUT / name, colors, TRAY_SIZES) for name, colors in tray.items()}

    # previews (not shipped)
    sheet = Image.new("RGBA", (4 * 72 + 24, 48 + 24 + 32), (240, 240, 240, 255))
    for i, name in enumerate(tray):
        sheet.alpha_composite(tray_imgs[name][TRAY_SIZES.index(48)], (24 + i * 72, 12))
        sheet.alpha_composite(tray_imgs[name][TRAY_SIZES.index(24)], (36 + i * 72, 70))
    sheet.save(ROOT / "build" / "tray-preview.png")
    w = sum(SIZES) + 12 * (len(SIZES) + 1)
    sheet = Image.new("RGBA", (w, 256 + 24), (240, 240, 240, 255))
    x = 12
    for s, im in zip(SIZES, images):
        sheet.alpha_composite(im, (x, 12 + 256 - s))
        x += s + 12
    sheet.save(ROOT / "build" / "icon-preview.png")
    print("written:", OUT)


if __name__ == "__main__":
    main()
