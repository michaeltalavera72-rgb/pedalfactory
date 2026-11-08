"""Builds Assets/softlanding_art.png from Michael's Soft Landing artwork (Tools/Art/softlanding_original.png, 1024 x 1536).

The template is a full pedal mock-up: powder-blue top, GAIN / SQUISH / LEVEL already printed in navy, "SOFT LANDING" over
a sunset lake with mountains, clouds and pines. Its painted controls (three cream scalloped knobs on navy skirts, the
amber LED in a dark bezel, the chrome footswitch on its hex nut) are painted out: knobs and LED sit on plain sky, so
they get a smooth colour field (multi-scale normalised convolution of the surrounding sky) plus the sky's fine
paper texture quilted from plain tiles nearby (exemplar inpainting left pale discs there - Twin Vise lesson); the
footswitch sits on the striped lake, so it gets exemplar (patch-copy) inpainting (make_goldenhaze_art.py) with the
dark shore silhouettes excluded as sources. The real controls go exactly where the painted ones were. No
lettering is added or changed. The flat top inside the rounded rim is cropped (the side jacks and the rim are left out)
and resized to the 226 x 372 body's art area (2.6 mm inset): a 1.7 % vertical stretch.

Only numpy, opencv-python (core, no contrib) and pillow are needed. Run from C:\\Dev\\pedalfactory-master:
    python Tools/Art/make_softlanding_art.py Tools/Art/softlanding_original.png Assets/softlanding_art.png
Without arguments it only prints the FaceLayout fractions. Source pixel coordinates refer to the 1024 x 1536 original.
Results can differ very slightly between machines (float matching ties): the committed PNG is the reference."""
import os, sys, numpy as np, cv2
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_goldenhaze_art import exemplar_inpaint

CROP = (84, 48, 939, 1464)                       # flat top inside the rim (body 52..971 x 30..1482 in the source)
KNOBS = [(213, 258), (511, 258), (809, 258)]     # GAIN, SQUISH, LEVEL: centre of the painted navy skirt (widest row)
KNOB_D = 225                                     # painted skirt diameter
LED = (511, 514)                                 # dark bezel 470..553
FOOT = (511, 1267)                               # centre of the painted hex nut
FOOT_D = 210                                     # nut across corners (widest)
LABELS = [(213, 415, 62), (511, 415, 95), (810, 415, 76)]   # GAIN SQUISH LEVEL: centre, half width (rows 395..434)

W_MM, H_MM, M_MM = 91.0, 91.0 * 372 / 226, 2.6   # body and art inset (mm)


def fractions():
    """Body fractions (x of W, y of H) for FaceLayout: the art is printed 2.6 mm inside the 91 x 149.8 mm body."""
    fx = lambda x: M_MM / W_MM + (x - CROP[0]) / (CROP[2] - CROP[0]) * (W_MM - 2 * M_MM) / W_MM
    fy = lambda y: M_MM / H_MM + (y - CROP[1]) / (CROP[3] - CROP[1]) * (H_MM - 2 * M_MM) / H_MM
    fd = lambda d: d / (CROP[2] - CROP[0]) * (W_MM - 2 * M_MM) / W_MM
    return fx, fy, fd


def hole_mask(shape):
    m = np.zeros(shape, np.uint8)
    for cx, cy in KNOBS:
        # painted slightly from the front: the cream cap reaches ~125 px above the skirt centre, the skirt ~110 px below
        # it; the labels start 137 px below the centre, so the mask stops short of them
        # + the painted ambient-occlusion halo (~20 px darker sky all round, deeper below; labels are kept)
        cv2.circle(m, (cx, cy), 140, 255, -1)
        cv2.ellipse(m, (cx, cy - 26), (134, 134), 0, 0, 360, 255, -1)
        cv2.ellipse(m, (cx + 2, cy + 14), (140, 142), 0, 0, 360, 255, -1)     # painted drop shadow below the skirt
    for (ax, _), (bx, _) in zip(KNOBS, KNOBS[1:]):                    # the AO-darkened gaps between neighbouring knobs
        cv2.rectangle(m, (ax + 100, KNOBS[0][1] - 110), (bx - 100, KNOBS[0][1] + 134), 255, -1)
    cv2.circle(m, LED, 68, 255, -1)                                    # bezel + amber glow + halo
    cv2.ellipse(m, (LED[0], LED[1] + 6), (66, 68), 0, 0, 360, 255, -1)  # its shadow
    fx, fy = FOOT
    cv2.circle(m, FOOT, 112, 255, -1)
    cv2.ellipse(m, (fx + 2, fy + 8), (112, 110), 0, 0, 360, 255, -1)  # contact shadow
    return m > 0


def source_ok(shape, hole):
    """Where patches may be copied from: not the printed labels / title, not the painted shadows around the holes."""
    ok = np.ones(shape, np.uint8)
    for cx, cy, hw in LABELS:
        cv2.rectangle(ok, (cx - hw - 12, cy - 30), (cx + hw + 12, cy + 30), 0, -1)
    cv2.rectangle(ok, (200, 590), (830, 890), 0, -1)                    # SOFT LANDING lettering
    near = cv2.dilate(hole.astype(np.uint8), np.ones((41, 41), np.uint8)) > 0
    ok[near] = 0
    return ok > 0


def field_fill(img, hole, known, deg=3):
    """Smooth colour field inside `hole`: the sky is a soft 2-D gradient, so fit it (cubic polynomial in x, y per
    channel, least squares on the `known` sky pixels), then add the local residual (normalised convolution, sigma 14)
    faded out over ~30 px from the hole's edge so the fill meets its surroundings without a seam. (Plain multi-scale
    normalised convolution pulled the pink clouds into the big merged knob holes: pale discs.)"""
    f = img.astype(np.float32)
    H, W = hole.shape
    ys, xs = np.where(known)
    sel = np.random.default_rng(3).choice(len(ys), min(len(ys), 60000), replace=False)
    ys, xs = ys[sel], xs[sel]
    def basis(y, x):
        u, v = x / W - 0.5, y / H - 0.5
        t = [np.ones_like(u), u, v]
        if deg >= 2: t += [u * u, u * v, v * v]
        if deg >= 3: t += [u ** 3, u * u * v, u * v * v, v ** 3]
        return np.stack(t, -1)
    A = basis(ys.astype(np.float64), xs.astype(np.float64))
    coef = np.linalg.lstsq(A, f[ys, xs].astype(np.float64), rcond=None)[0]
    gy, gx = np.mgrid[0:H, 0:W]
    poly = (basis(gy.astype(np.float64), gx.astype(np.float64)) @ coef).astype(np.float32)
    w = known.astype(np.float32)
    res = cv2.GaussianBlur((f - poly) * w[..., None], (0, 0), 14) / np.maximum(cv2.GaussianBlur(w, (0, 0), 14), 1e-3)[..., None]
    dist = cv2.distanceTransform(hole.astype(np.uint8), cv2.DIST_L2, 5)
    fade = np.exp(-dist / 30.0)
    out = f.copy()
    out[hole] = poly[hole] + res[hole] * fade[hole][:, None]
    return out


def texture_fill(img, field, hole, plain, rng, tile=48, ov=16):
    """Adds the art's fine texture (high-pass, sigma 5) to the field: random plain tiles, feathered overlap-add."""
    f = img.astype(np.float32)
    hp = f - cv2.GaussianBlur(f, (0, 0), 5)
    H, W = hole.shape
    ys, xs = np.where(hole)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    # candidate tile corners fully inside plain sky
    integ = cv2.integral(plain.astype(np.uint8))
    cands = [(y, x) for y in range(0, H - tile, 8) for x in range(0, W - tile, 8)
             if integ[y + tile, x + tile] - integ[y, x + tile] - integ[y + tile, x] + integ[y, x] == tile * tile]
    acc = np.zeros((H, W, 3), np.float32); wsum = np.zeros((H, W), np.float32)
    win = np.ones(tile, np.float32); win[:ov] = np.linspace(0.05, 1, ov); win[-ov:] = np.linspace(1, 0.05, ov)
    w2 = np.outer(win, win)
    step = tile - ov
    for ty in range(max(0, y0 - ov), min(H - tile, y1) + 1, step):
        for tx in range(max(0, x0 - ov), min(W - tile, x1) + 1, step):
            sy, sx = cands[rng.integers(len(cands))]
            acc[ty:ty + tile, tx:tx + tile] += hp[sy:sy + tile, sx:sx + tile] * w2[..., None]
            wsum[ty:ty + tile, tx:tx + tile] += w2
    tex = acc / np.maximum(wsum, 1e-6)[..., None]
    out = f.copy()
    m = cv2.GaussianBlur(hole.astype(np.float32), (0, 0), 2)              # soft edge
    res = field + tex
    out = out * (1 - m[..., None]) + res * m[..., None]
    return np.clip(out, 0, 255).astype(np.uint8)


def build(src, out):
    im = cv2.cvtColor(np.array(Image.open(src).convert("RGB")), cv2.COLOR_RGB2BGR)
    hole = hole_mask(im.shape[:2])
    allowed = source_ok(im.shape[:2], hole)
    filled = im.copy()
    rng = np.random.default_rng(11)
    # knobs + LED: plain sky -> colour field + quilted texture. Known = sky only (not labels, not the holes' halo)
    lab = np.zeros(im.shape[:2], bool)                                 # label glyphs (+ antialias): kept as painted
    dark = im.astype(np.int32).sum(-1) < 260
    for cx, cy, hw in LABELS:
        lab[cy - 20:cy + 21, cx - hw - 8:cx + hw + 8] = dark[cy - 20:cy + 21, cx - hw - 8:cx + hw + 8]
    lab[575:900, 190:840] = im[575:900, 190:840].astype(np.int32).sum(-1) < 400    # SOFT LANDING glyphs (the F's
    lab = cv2.dilate(lab.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0         # top touches the LED's halo)
    sky_hole = hole.copy(); sky_hole[FOOT[1] - 140:, :] = False
    halo = cv2.dilate(sky_hole.astype(np.uint8), np.ones((7, 7), np.uint8)) > 0
    halo &= ~lab                                                       # never repaint the labels
    known = ~halo & ~lab
    known[:CROP[1] + 6, :] = False; known[:, :CROP[0] + 4] = False; known[:, CROP[2] - 4:] = False   # not the rim
    r, b = im[..., 2].astype(np.int32), im[..., 0].astype(np.int32)
    sky = known & (b > r + 25)                                         # blue sky only (no pink clouds)
    sky[650:, :] = False
    led_hole = halo.copy(); led_hole[:LED[1] - 90, :] = False
    knob_hole = halo & ~led_hole
    field = field_fill(filled, knob_hole, sky)
    title = np.zeros_like(known); title[575:900, 190:840] = True       # SOFT LANDING lettering: not a source
    boxes = np.zeros_like(known)
    for cx, cy, hw in LABELS:
        boxes[cy - 30:cy + 31, cx - hw - 14:cx + hw + 14] = True       # whole label boxes (glyph edges are dark too)
    gy, gx = np.mgrid[0:known.shape[0], 0:known.shape[1]]
    near_led = np.hypot(gx - LED[0], gy - LED[1]) < 130
    field[led_hole] = field_fill(filled, led_hole, known & ~title & ~boxes & near_led, deg=2)[led_hole]   # keeps the
    # sky's blue -> peach shading (a flat local average left a visible blob)
    plain = known.copy(); plain[470:, :] = False                       # sky above the clouds only
    plain &= ~(cv2.dilate(sky_hole.astype(np.uint8), np.ones((31, 31), np.uint8)) > 0)   # no painted shadows
    plain &= cv2.erode(plain.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    filled = texture_fill(filled, field, halo, plain, rng)
    hole[halo] = False
    # footswitch: striped lake -> exemplar, dark shore silhouettes not allowed as sources
    shore = cv2.dilate((im.astype(np.int32).sum(-1) < 150).astype(np.uint8), np.ones((9, 9), np.uint8)) > 0
    allowed &= ~shore
    wins = [(FOOT[0] - 230, FOOT[1] - 190, FOOT[0] + 230, FOOT[1] + 190)]
    for (x0, y0, x1, y1) in wins:
        x0, y0 = max(x0, CROP[0]), max(y0, CROP[1]); x1, y1 = min(x1, CROP[2]), min(y1, CROP[3])
        sub_hole = hole[y0:y1, x0:x1]
        if not sub_hole.any(): continue
        filled[y0:y1, x0:x1] = exemplar_inpaint(filled[y0:y1, x0:x1], sub_hole, patch=11, search=200, seed=7,
                                                    allowed=allowed[y0:y1, x0:x1])
        hole[y0:y1, x0:x1] = False
    art = Image.fromarray(cv2.cvtColor(filled, cv2.COLOR_BGR2RGB)).crop(CROP)
    ow = 1200; oh = round(ow * (H_MM - 2 * M_MM) / (W_MM - 2 * M_MM))
    art = art.resize((ow, oh), Image.LANCZOS)
    art.save(out, optimize=True)
    a = np.array(art).astype(np.int64)
    print("art", art.size, "aspect %.4f" % (art.size[1] / art.size[0]), "pixel sum", int(a.sum()))


if __name__ == "__main__":
    if len(sys.argv) >= 3:
        build(sys.argv[1], sys.argv[2])
    fx, fy, fd = fractions()
    print("knob d %.4fW at" % fd(KNOB_D), [(round(fx(x), 4), round(fy(y), 4)) for x, y in KNOBS])
    print("LED", (round(fx(LED[0]), 4), round(fy(LED[1]), 4)),
          "footswitch d %.4fW at" % fd(FOOT_D), (round(fx(FOOT[0]), 4), round(fy(FOOT[1]), 4)))
