"""Builds Assets/frostbite_art.png from Michael's Frostbite artwork (Tools/Art/frostbite_original.png, 1024 x 1536).

The template is a full pedal mock-up: frost-covered blue-grey top, GAIN / PRE / MOD / POST / LEVEL already printed,
"FROSTBITE" in icicle lettering over a frozen landscape. Its painted controls (four black knobs, the MOD bat toggle,
the blue LED and the footswitch) are painted out with exemplar (patch-copy) inpainting (make_goldenhaze_art.py), which
continues the frost texture; the real controls go exactly where they were. No lettering is added or changed.
The flat top inside the bevelled rim is cropped and resized to the 226 x 372 body's art area (2.6 mm inset).

Only numpy, opencv-python (core, no contrib) and pillow are needed. Run from C:\\Dev\\pedalfactory-master:
    python Tools/Art/make_frostbite_art.py Tools/Art/frostbite_original.png Assets/frostbite_art.png
Without arguments it only prints the FaceLayout fractions. Source pixel coordinates refer to the 1024 x 1536 original.
Results can differ very slightly between machines (float matching ties): the committed PNG is the reference."""
import os, sys, numpy as np, cv2
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_goldenhaze_art import exemplar_inpaint

CROP = (84, 44, 948, 1462)                       # flat top inside the rim (source px); the sides lose a little frost
                                                 # border so the art only needs a 2.7 % vertical stretch
KNOBS = [(246, 190), (777, 190), (248, 490), (777, 490)]   # GAIN, PRE, POST, LEVEL: centre of the painted skirt
KNOB_D = 206                                     # painted knob skirt diameter (widest row)
TOGGLE = (512, 342)                              # MOD: centre of the painted hex nut
TOGGLE_NUT = 100                                 # painted nut across flats
LED = (513, 561)
FOOT = (510, 1188)                               # centre of the painted footswitch washer
FOOT_D = 199
LABELS = [(246, 326, 62), (777, 326, 50), (512, 425, 52), (248, 611, 56), (777, 611, 70)]   # GAIN PRE MOD POST LEVEL: centre, half width

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
        # the painted knobs are seen slightly from the front: cap top ~110 px above the centre, skirt + contact shadow
        # ~112 px below; labels start ~125 px below the centre, so the mask stops short of them
        cv2.ellipse(m, (cx, cy - 2), (116, 116), 0, 0, 360, 255, -1)
        cv2.ellipse(m, (cx + 3, cy + 8), (114, 112), 0, 0, 360, 255, -1)
    tx, ty = TOGGLE
    cv2.ellipse(m, (tx, ty + 2), (60, 58), 0, 0, 360, 255, -1)          # hex nut + shadow
    cv2.rectangle(m, (tx - 26, ty - 62), (tx + 26, ty), 255, -1)       # bat lever (tip ~55 px above the nut centre)
    cv2.circle(m, LED, 54, 255, -1)                                    # chrome bezel + blue lens + glow
    fx, fy = FOOT
    cv2.circle(m, FOOT, 110, 255, -1)
    cv2.ellipse(m, (fx + 3, fy + 10), (110, 108), 0, 0, 360, 255, -1)  # contact shadow
    return m > 0


def source_ok(shape, hole):
    """Where patches may be copied from: not the printed labels, and not the dark painted shadows just around the
    holes (first run: copied label fragments and dark blobs into the knob holes)."""
    ok = np.ones(shape, np.uint8)
    for cx, cy, hw in LABELS:
        cv2.rectangle(ok, (cx - hw - 12, cy - 26), (cx + hw + 12, cy + 26), 0, -1)
    near = cv2.dilate(hole.astype(np.uint8), np.ones((61, 61), np.uint8)) > 0
    ok[near] = 0
    return ok > 0


def build(src, out):
    im = cv2.cvtColor(np.array(Image.open(src).convert("RGB")), cv2.COLOR_RGB2BGR)
    hole = hole_mask(im.shape[:2])
    allowed = source_ok(im.shape[:2], hole)
    filled = im.copy()
    # inpaint in windows around each hole (fast; every window has plenty of frost texture to copy from)
    wins = [(cx - 230, cy - 200, cx + 230, cy + 200) for cx, cy in KNOBS]
    wins += [(TOGGLE[0] - 190, TOGGLE[1] - 160, TOGGLE[0] + 190, LED[1] + 140),
             (FOOT[0] - 220, FOOT[1] - 200, FOOT[0] + 220, FOOT[1] + 200)]
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
    print("toggle", (round(fx(TOGGLE[0]), 4), round(fy(TOGGLE[1]), 4)), "nut %.4fW" % fd(TOGGLE_NUT))
    print("LED", (round(fx(LED[0]), 4), round(fy(LED[1]), 4)),
          "footswitch d %.4fW at" % fd(FOOT_D), (round(fx(FOOT[0]), 4), round(fy(FOOT[1]), 4)))
