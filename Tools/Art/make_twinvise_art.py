"""Builds Assets/twinvise_art.png from Michael's Twin Vise artwork (Tools/Art/twinvise_original.png, 1536 x 1024).

The template is a full double-wide pedal mock-up: rough gunmetal top, five knobs with printed orange scale arcs and
labels (INPUT GAIN / LOW COMP / HIGH COMP / BALANCE / LEVEL), "TWIN VISE" on a big rusty bench vise, the INPUT LED,
and HI COMP / EFFECT LEDs and footswitches. All labels are right: no lettering is added or changed.
  - The painted controls (5 knob bodies, 3 LEDs, 2 footswitches) are painted out with exemplar (patch-copy)
    inpainting (make_goldenhaze_art.py); the orange scale arcs stay (our knobs sit inside them).
  - The art is 3:2 but the 464 x 372 body's art area is ~1.256:1, so the flat top is made taller WITHOUT stretching:
    three plain bands of new texture are inserted - above the knobs, between the knob labels and the vise, and
    below the vise base. Each band = a smooth colour field (masked blur of the plain rows next to it, interpolated
    across the band) + fine texture quilted from plain gunmetal tiles between the knob arcs (high-pass, feathered
    seams, amplitude matched to the neighbouring rows).
The flat top inside the rim is cropped first. Only numpy, opencv-python (core) and pillow are needed. Run from
C:\\Dev\\pedalfactory-master:
    python Tools/Art/make_twinvise_art.py Tools/Art/twinvise_original.png Assets/twinvise_art.png
Without arguments it only prints the FaceLayout fractions. Source pixel coordinates refer to the 1536 x 1024 original.
Takes a few minutes. Results can differ very slightly between machines: the committed PNG is the reference."""
import os, sys, numpy as np, cv2
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_goldenhaze_art import exemplar_inpaint

CROP = (56, 44, 1480, 955)                       # flat top inside the rim (source px)
KNOBS = [(215, 154), (490, 154), (766, 154), (1044, 154), (1320, 154)]   # centres of the printed orange arcs
ARC_IN = 87                                      # arc inner radius: the painted knob (skirt + shadow) lies inside
KNOB_D = 160                                     # our knob diameter (painted skirt ~165 px)
LEDS = [(767, 612), (467, 684), (1068, 683)]     # INPUT, HI COMP, EFFECT
FEET = [(465, 824), (1070, 824)]                 # HI COMP, EFFECT: centre of the painted hex nut
FOOT_D = 142                                     # painted hex nut across corners
LABELS = [(215, 260, 76), (490, 260, 72), (766, 260, 74), (1044, 260, 62), (1320, 260, 40),   # knob labels
          (767, 662, 40), (467, 729, 54), (1068, 729, 46)]                                     # INPUT, HI COMP, EFFECT
# insertions (source row, extra rows): above the knobs, labels / vise, below the vise base
INSERTS = [(50, 55), (281, 100), (933, 68)]
OUT_W = 1424                                     # = crop width (no resampling)

ART_W, ART_H = 464 - 2 * 2.6 * 226 / 91, 372 - 2 * 2.6 * 226 / 91    # art area of the double-wide body (px at 1x)
INSET_X = 2.6 * 226 / 91 / 464                   # = FaceLayout::artInset2 (of W)
INSET_Y = 2.6 * 226 / 91 / 372                   # the same 2.6 mm as a fraction of H


def ymap(y):
    """source row -> row of the taller art (before the final resize, which is 1:1 here)"""
    out = y - CROP[1]
    for r, n in INSERTS:
        if y >= r: out += n
    return out


def out_size():
    h = CROP[3] - CROP[1] + sum(n for _, n in INSERTS)
    return CROP[2] - CROP[0], h


def fractions():
    w, h = out_size()
    fx = lambda x: INSET_X + (x - CROP[0]) / w * (1 - 2 * INSET_X)
    fy = lambda y: INSET_Y + ymap(y) / h * (1 - 2 * INSET_Y)
    fd = lambda d: d / w * (1 - 2 * INSET_X)
    return fx, fy, fd


def band_neighbours(shape):
    """rows right next to each inserted band that are plain background (colour field reference), minus the
    labels (above the middle band) and the vise parts that start just under it"""
    m = np.zeros(shape, bool)
    for r, n in INSERTS:
        y = ymap(r) - n                                # first row of the band
        m[max(y - 6, 0):y] = True
        m[y + n:y + n + 6] = True
    W = shape[1]
    lab = np.zeros(shape, bool)
    for cx, cy, hw in LABELS[:5]:                      # knob labels end ~6 px above the middle band
        x0 = max(cx - hw - 10 - CROP[0], 0); lab[:, x0:cx + hw + 10 - CROP[0]] = True
    y = ymap(281) - 100
    m[y - 6:y] &= ~lab[y - 6:y]
    # vise jaws / handle balls start ~6 px under the middle band: keep only the columns between them
    for x0, x1 in ((40, 130), (250, 470), (1040, 1260), (1360, 1424)):
        m[y + 100:y + 106, x0:x1] = False
    return m


def control_holes(shape):
    m = np.zeros(shape, np.uint8)
    for cx, cy in KNOBS:
        cv2.circle(m, (cx, cy), ARC_IN - 1, 255, -1)
    for cx, cy in LEDS:
        cv2.circle(m, (cx, cy), 32, 255, -1)                           # bezel + lens + glow
    for cx, cy in FEET:
        cv2.circle(m, (cx, cy), 78, 255, -1)
        cv2.ellipse(m, (cx + 4, cy + 10), (78, 76), 0, 0, 360, 255, -1)  # contact shadow
    return m > 0


def orange_mask(im):
    hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
    o = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 22) & (hsv[..., 1] > 110) & (hsv[..., 2] > 110)
    return cv2.dilate(o.astype(np.uint8), np.ones((9, 9), np.uint8)) > 0


def label_mask(shape):
    m = np.zeros(shape, np.uint8)
    for cx, cy, hw in LABELS:
        cv2.rectangle(m, (cx - hw - 10, cy - 22), (cx + hw + 10, cy + 22), 255, -1)
    return m > 0


def plain_mask(shape):
    """plain gunmetal (sources for the inserted bands): the knob row between the arcs and the strip below the vise"""
    m = np.zeros(shape, np.uint8)
    m[48:246, 60:1476] = 255
    for cx, cy in KNOBS:
        cv2.circle(m, (cx, cy), 104, 0, -1)
    m[918:950, 70:1466] = 255
    return m > 0


def run_windows(img, hole, allowed, wins, seed, patch=11, search=150):
    for (x0, y0, x1, y1) in wins:
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, img.shape[1]), min(y1, img.shape[0])
        sub = hole[y0:y1, x0:x1]
        if not sub.any(): continue
        img[y0:y1, x0:x1] = exemplar_inpaint(img[y0:y1, x0:x1], sub, patch=patch, search=search, seed=seed,
                                             allowed=allowed[y0:y1, x0:x1])
        hole[y0:y1, x0:x1] = False


def tile_sources(plain, th, tw):
    """top-left corners (source px) of fully plain th x tw tiles in the knob row"""
    ii = cv2.integral(plain.astype(np.uint8))
    out = []
    for y in range(48, 246 - th, 6):
        for x in range(60, 1476 - tw, 6):
            if ii[y + th, x + tw] - ii[y, x + tw] - ii[y + th, x] + ii[y, x] == th * tw: out.append((y, x))
    return out


def masked_rows(img, mask, y0, y1, sigma=40):
    """smooth horizontal colour profile of rows y0..y1 using only the masked (plain) pixels"""
    a = img[y0:y1].astype(np.float32); m = mask[y0:y1].astype(np.float32)[..., None]
    num = (a * m).sum(0); den = m.sum(0)[:, 0]
    num = cv2.GaussianBlur(num[None], (0, 0), sigma)[0]; den = cv2.GaussianBlur(den[None], (0, 0), sigma)[0]
    return num / np.maximum(den, 1e-3)[:, None]


def fill_band(a, pmask, y, n, src, hf, tiles, rng, tw=64, ov=16):
    """rows y..y+n of a (already opened up) get field + quilted high-pass texture"""
    W = a.shape[1]
    top = masked_rows(a, pmask, max(y - 7, 0), y)
    bot = masked_rows(a, pmask, y + n, min(y + n + 7, a.shape[0]))
    t = ((np.arange(n) + 0.5) / n)[:, None, None]
    field = top[None] * (1 - t) + bot[None] * t
    # amplitude of the texture next to the band
    def hf_std(y0, y1):
        m = pmask[y0:y1]
        return float(hf[y0:y1][m].std()) if m.any() else None
    ref = [v for v in (hf_std(max(y - 12, 0), y), hf_std(y + n, min(y + n + 12, a.shape[0]))) if v]
    th = n
    tex = np.zeros((n, W, 3), np.float32); wsum = np.zeros((n, W, 1), np.float32)
    ramp = np.ones(tw, np.float32); ramp[:ov] = np.linspace(0, 1, ov + 2)[1:-1]; ramp[-ov:] = ramp[:ov][::-1]
    for x in range(-ov, W, tw - ov):
        sy, sx = tiles[int(rng.integers(len(tiles)))]
        tile = src[sy:sy + th, sx:sx + tw]
        x0, x1 = max(x, 0), min(x + tw, W)
        tex[:, x0:x1] += tile[:, x0 - x:x1 - x] * ramp[x0 - x:x1 - x, None]
        wsum[:, x0:x1] += ramp[x0 - x:x1 - x, None]
    tex /= np.maximum(wsum, 1e-3)
    tex *= (np.mean(ref) / max(float(tex.std()), 1e-3)) if ref else 1.0
    a[y:y + n] = np.clip(field + tex, 0, 255).astype(np.uint8)


def build(src, out):
    im = cv2.cvtColor(np.array(Image.open(src).convert("RGB")), cv2.COLOR_RGB2BGR)
    H, W = im.shape[:2]
    # 1. paint out the controls
    hole = control_holes((H, W))
    near = cv2.dilate(hole.astype(np.uint8), np.ones((31, 31), np.uint8)) > 0
    allowed = ~near & ~label_mask((H, W)) & ~orange_mask(im)
    run_windows(im, hole, allowed, [(cx - 200, cy - 110, cx + 200, cy + 110) for cx, cy in KNOBS], seed=7)
    # LEDs and footswitches sit on the vise's cast iron: a wider no-copy ring (no painted shadows / bezel glints)
    near = cv2.dilate(hole.astype(np.uint8), np.ones((51, 51), np.uint8)) > 0
    allowed = ~near & ~label_mask((H, W)) & ~orange_mask(im)
    wins = [(cx - 160, cy - 130, cx + 160, cy + 130) for cx, cy in LEDS]
    wins += [(cx - 230, cy - 190, cx + 230, cy + 150) for cx, cy in FEET]
    for w in wins:
        run_windows(im, hole, allowed, [w], seed=3, patch=15, search=210)
    plain = plain_mask((H, W)) & ~label_mask((H, W)) & ~orange_mask(im)
    f = im.astype(np.float32)
    hf_src = f - cv2.GaussianBlur(f, (0, 0), 6)                      # fine texture only
    rng = np.random.default_rng(11)
    # 2. crop, open up the bands (bottom first so the source rows above stay put), fill them
    a = im[CROP[1]:CROP[3], CROP[0]:CROP[2]]
    p = plain[CROP[1]:CROP[3], CROP[0]:CROP[2]]
    hf = hf_src[CROP[1]:CROP[3], CROP[0]:CROP[2]]
    for r, n in sorted(INSERTS, reverse=True):
        k = r - CROP[1]
        a = np.concatenate([a[:k], np.zeros((n, a.shape[1], 3), np.uint8), a[k:]])
        # band rows next to the inserted ones count as plain background for the colour field
        p = np.concatenate([p[:k], np.zeros((n, p.shape[1]), bool), p[k:]])
        hf = np.concatenate([hf[:k], np.zeros((n, hf.shape[1], 3), np.float32), hf[k:]])
    bg = p | band_neighbours(a.shape[:2])
    for r, n in INSERTS:
        fill_band(a, bg, ymap(r) - n, n, hf_src, hf, tile_sources(plain, n, 64), rng)
    art = Image.fromarray(cv2.cvtColor(a, cv2.COLOR_BGR2RGB))
    if art.size[0] != OUT_W:
        art = art.resize((OUT_W, round(OUT_W * art.size[1] / art.size[0])), Image.LANCZOS)
    art.save(out, optimize=True)
    arr = np.array(art).astype(np.int64)
    print("art", art.size, "aspect %.4f (body art area %.4f)" % (art.size[1] / art.size[0], ART_H / ART_W),
          "pixel sum", int(arr.sum()))


if __name__ == "__main__":
    if len(sys.argv) >= 3:
        build(sys.argv[1], sys.argv[2])
    fx, fy, fd = fractions()
    print("knob d %.4fW at" % fd(KNOB_D), [(round(fx(x), 4), round(fy(y), 4)) for x, y in KNOBS])
    print("LEDs INPUT / HI COMP / EFFECT", [(round(fx(x), 4), round(fy(y), 4)) for x, y in LEDS])
    print("footswitch d %.4fW at HI COMP / EFFECT" % fd(FOOT_D), [(round(fx(x), 4), round(fy(y), 4)) for x, y in FEET])
