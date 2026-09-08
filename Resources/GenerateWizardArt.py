# Copyright (c) 2026 GregOrigin. All Rights Reserved.
#
# Regenerates the setup wizard's derived chrome art in Resources/UI.
#
# Authoring-only. This is a plain CPython script (Pillow + numpy) -- it does not run inside
# the editor and is excluded from the packaged plugin by Config/FilterPlugin.ini.
#
#   python Resources/GenerateWizardArt.py
#
# Source art lives at https://gregorigin.com/LOGOS/ and is downloaded on demand into
# Resources/UI/_source/ (also excluded from packaging). The two hand-framed images --
# IOC_Hero.jpg and IOC_Emblem.png -- are crops, not derivations, so they are NOT touched
# here; everything this script writes is reproducible from the sources plus the constants
# below.
#
# Outputs:
#   IOC_Backdrop.jpg     dimmed, blurred cave art drawn behind the whole wizard
#   IOC_ScrimDown.png    transparent -> opaque vertical fade (tinted at the use site)
#   IOC_Vignette.png     transparent centre -> opaque edges, for corner falloff
#   IOC_GregOrigin.png   horizontal GregOrigin lockup, white linework on transparency
#
# And into Resources/Docs, for the shipped HTML manual:
#   ioc-hero.jpg         the IOC1 key art, downscaled
#   ioc-emblem.png       the arch mark at icon size
#   ioc-rock.jpg         a text-free rock crop, for use as texture

import os
import urllib.request

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
UI_DIR = os.path.join(HERE, "UI")
DOCS_DIR = os.path.join(HERE, "Docs")
SRC_DIR = os.path.join(UI_DIR, "_source")

LOGO_BASE = "https://gregorigin.com/LOGOS/"


def fetch(name):
    """Download a source image once and cache it under Resources/UI/_source."""
    os.makedirs(SRC_DIR, exist_ok=True)
    path = os.path.join(SRC_DIR, name)
    if not os.path.exists(path):
        print("  downloading %s" % name)
        urllib.request.urlretrieve(LOGO_BASE + name, path)
    return path


def write_backdrop():
    """
    The wizard's background layer.

    Blurred so it never competes with text for attention, and darkened hard because it is
    composited under translucent panels -- at full brightness the cyan highlights punch
    straight through the chrome and make body copy unreadable.
    """
    src = Image.open(fetch("IOC2.jpg")).convert("RGB")

    # It is blurred and drawn behind translucent panels, so source resolution buys
    # nothing past this -- and the plugin ships every byte of it.
    LONG_EDGE = 1600
    if src.width > LONG_EDGE:
        src = src.resize((LONG_EDGE, round(src.height * LONG_EDGE / src.width)), Image.LANCZOS)

    img = src.filter(ImageFilter.GaussianBlur(radius=3))
    a = np.asarray(img).astype(np.float32) / 255.0

    BRIGHTNESS = 0.42
    SATURATION = 0.75
    lum = (0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2])[..., None]
    a = (lum + (a - lum) * SATURATION) * BRIGHTNESS

    out = Image.fromarray(np.clip(a * 255.0, 0, 255).astype(np.uint8), "RGB")
    out.save(os.path.join(UI_DIR, "IOC_Backdrop.jpg"), quality=88, optimize=True)
    print("  IOC_Backdrop.jpg %dx%d" % out.size)


def write_scrim():
    """
    A one-dimensional alpha ramp, stretched wherever a hard edge needs dissolving.

    The exponent matters: a linear ramp reads as a visible band because the eye picks out
    the constant-slope region. Easing the start keeps the top of the fade imperceptible.
    """
    H, W = 256, 16
    t = np.linspace(0.0, 1.0, H) ** 1.55
    alpha = np.repeat((t * 255.0).astype(np.uint8)[:, None], W, axis=1)

    rgba = np.zeros((H, W, 4), np.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = alpha
    Image.fromarray(rgba, "RGBA").save(os.path.join(UI_DIR, "IOC_ScrimDown.png"))
    print("  IOC_ScrimDown.png %dx%d" % (W, H))


def write_vignette():
    """
    Corner falloff, drawn over the backdrop.

    Two jobs: it stops the backdrop's bright areas from reaching the window edges where the
    stepper and the nav buttons live, and it gives the flat panel a centre of gravity. The
    inner radius is generous so the middle of the page stays completely clear.
    """
    W, H = 512, 288
    yy, xx = np.mgrid[0:H, 0:W]
    nx = (xx / (W - 1.0)) * 2.0 - 1.0
    ny = (yy / (H - 1.0)) * 2.0 - 1.0
    d = np.sqrt(nx * nx + ny * ny) / np.sqrt(2.0)

    INNER, PEAK = 0.34, 0.92
    alpha = np.clip((d - INNER) / (1.0 - INNER), 0.0, 1.0) ** 1.4 * PEAK

    rgba = np.zeros((H, W, 4), np.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = (alpha * 255.0).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(os.path.join(UI_DIR, "IOC_Vignette.png"))
    print("  IOC_Vignette.png %dx%d" % (W, H))


def write_gregorigin():
    """
    The GregOrigin lockup, rebuilt horizontally for the wizard footer.

    The published lockup stacks the mark above the wordmark and bakes a smoky dark plate
    behind both, which is unusable as a watermark. Keying on luminance (rather than trusting
    the file's own alpha, which keeps that plate at partial opacity) leaves just the light
    strokes; recombining them side by side gives a strip that suits a footer, and painting
    them white lets the call site tint the whole mark with one colour.
    """
    src = Image.open(fetch("GregoLOGOalpha.png")).convert("RGBA")
    a = np.asarray(src).astype(np.float32)
    alpha = a[..., 3] / 255.0
    lum = (0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]) / 255.0

    LO, HI = 0.28, 0.88
    mask = alpha * np.clip((lum - LO) / (HI - LO), 0.0, 1.0)

    keyed = np.zeros(a.shape, np.uint8)
    keyed[..., 0:3] = 255
    keyed[..., 3] = (mask * 255.0).astype(np.uint8)
    keyed = Image.fromarray(keyed, "RGBA")

    ink = np.asarray(keyed)[..., 3]

    def trim(x0, y0, x1, y1):
        ys, xs = np.nonzero(ink[y0:y1, x0:x1] > 2)
        return (x0 + int(xs.min()), y0 + int(ys.min()),
                x0 + int(xs.max()) + 1, y0 + int(ys.max()) + 1)

    # The source separates the mark from the wordmark with a band of empty rows; these
    # splits bracket it.
    mark = keyed.crop(trim(0, 88, ink.shape[1], 396))
    word = keyed.crop(trim(0, 460, ink.shape[1], ink.shape[0]))

    # Authored at ~2x its on-screen size so it stays crisp when Slate scales it down.
    CAP_H, MARK_RATIO, GAP = 44, 1.62, 18
    word = word.resize((max(1, round(word.width * CAP_H / word.height)), CAP_H), Image.LANCZOS)
    ms = (CAP_H * MARK_RATIO) / mark.height
    mark = mark.resize((max(1, round(mark.width * ms)), round(mark.height * ms)), Image.LANCZOS)

    w, h = mark.width + GAP + word.width, max(mark.height, word.height)
    canvas = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    canvas.alpha_composite(mark, (0, (h - mark.height) // 2))
    canvas.alpha_composite(word, (mark.width + GAP, (h - word.height) // 2))
    canvas.save(os.path.join(UI_DIR, "IOC_GregOrigin.png"))
    print("  IOC_GregOrigin.png %dx%d" % canvas.size)


def write_docs_art():
    """
    Art for the shipped HTML manual.

    The manual is opened from disk by customers and is also mirrored to the website, so these
    live beside index.html rather than being referenced up the tree into Resources/UI -- a
    relative path out of the Docs folder breaks the moment the page is served from anywhere
    else.

    The hero is shipped whole rather than pre-cropped to a band: the page crops it with CSS
    object-fit, so the band height can change without regenerating art, and the framing stays
    correct at any viewport width.
    """
    os.makedirs(DOCS_DIR, exist_ok=True)

    src = Image.open(fetch("IOC1.jpg")).convert("RGB")

    HERO_W = 1400
    hero = src.resize((HERO_W, round(src.height * HERO_W / src.width)), Image.LANCZOS)
    hero.save(os.path.join(DOCS_DIR, "ioc-hero.jpg"), quality=82, optimize=True)
    print("  Docs/ioc-hero.jpg %dx%d" % hero.size)

    # The arch mark, from the hand-framed emblem rather than re-cropped here.
    emblem_path = os.path.join(UI_DIR, "IOC_Emblem.png")
    if os.path.exists(emblem_path):
        em = Image.open(emblem_path).convert("RGBA").resize((128, 128), Image.LANCZOS)
        em.save(os.path.join(DOCS_DIR, "ioc-emblem.png"))
        print("  Docs/ioc-emblem.png 128x128")
    else:
        problem = "IOC_Emblem.png missing; Docs/ioc-emblem.png not written"
        print("  WARNING: " + problem)

    # Top-left of the key art: cave ceiling and wall, well clear of the wordmark and the
    # tagline, so it can be used as plain rock texture without dragging type into the frame.
    rock = src.crop((0, 0, 760, 560)).resize((520, 383), Image.LANCZOS)
    rock.save(os.path.join(DOCS_DIR, "ioc-rock.jpg"), quality=80, optimize=True)
    print("  Docs/ioc-rock.jpg %dx%d" % rock.size)


if __name__ == "__main__":
    os.makedirs(UI_DIR, exist_ok=True)
    print("Writing wizard art into %s" % UI_DIR)
    write_backdrop()
    write_scrim()
    write_vignette()
    write_gregorigin()
    write_docs_art()
    print("Done. IOC_Hero.jpg and IOC_Emblem.png are hand-framed crops and are not "
          "regenerated by this script.")
