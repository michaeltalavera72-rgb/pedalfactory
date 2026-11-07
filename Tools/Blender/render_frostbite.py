"""
Frostbite - photoreal Blender render (based on the Artec Cool Drive CDV-1; Michael's artwork).
Michael's full-face artwork (Assets/frostbite_art.png, made by Tools/Art/make_frostbite_art.py from his original image:
frost-covered blue-grey top, GAIN / PRE / MOD / POST / LEVEL, "FROSTBITE" in icicle lettering over a frozen landscape)
UV-printed 2.6 mm inside the edge of a frosted blue-grey enclosure (91 x 149.8 mm = plugin body 226 x 372 px).
The real controls sit where the art's painted ones were: four black fluted knobs with a machined black-metal cap and a
white pointer line (like the art's), a chrome bat-lever MOD toggle on a big hex nut, a blue LED in a chrome bezel and
a satin-silver / chrome footswitch. Uses pedal_render_common.py.

Writes Assets/Frostbite/: frostbite_face.png, frostbite_knob_strip.png (8-column grid, one strip for all four knobs),
frostbite_switch_up/down.png, frostbite_led_on/off.png, frostbite_toggle_up/down.png, and saves
Tools/Blender/frostbite.blend.

Run from C:\\Dev\\pedalfactory-master:
    & "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b -P Tools\\Blender\\render_frostbite.py
Options after "--":   --quick   |   --hdri=NAME.exr

Positions MUST match Source/UI/FaceLayout.h (pedalId "fb"; fractions printed by Tools/Art/make_frostbite_art.py) and
FrostbiteFace (Source/Faces/PedalFaces.cpp: togglePad). Pads MUST match RenderedFace.
The layout part of this file imports without Blender (used for alignment checks).
The print grade (fit from the face render vs the art, applied to face + control images) is the Coral Tide one.
"""
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Assets", "Frostbite")
TMP = os.path.join(HERE, "_frames_frostbite")
ART_IMAGE = os.path.join(ROOT, "Assets", "frostbite_art.png")

W, H = 91.0, 91.0 * 372.0 / 226.0          # = plugin pedal body 226 x 372 px
UI_BODY_PX = 226.0
UI_BODY_PX_H = 372.0
KNOB_PAD, SWITCH_PAD, LED_PAD = 1.30, 1.35, 2.6
TOGGLE_PAD = 1.30                                       # = FrostbiteFace::togglePad
ART_INSET = 2.6                                         # mm (= FaceLayout::artInset * W)

# Albedos darker than the target look (AgX lifts and desaturates). First guesses: tune from the quick render.
FROST_BASE = (0.16, 0.19, 0.25, 1)                      # enclosure rim/sides: blue-grey under ... (quick 1: 0.075/0.10/0.14
                                                        # read sRGB (117, 128, 138), quick 2: 0.11/0.14/0.19 -> (123, 134, 144);
                                                        # the art's rim is ~ (160, 168, 180))
FROST_WHITE = (0.50, 0.53, 0.58, 1)                     # ... white frost (the art's rim reads sRGB ~ (160, 168, 180))
STEEL_REFERENCE_TARGET = 0.74                           # same exposure basis as the other pedals
PRINT_SAT, PRINT_VALUE, PRINT_HUE = 1.0, 0.95, 0.5      # neutral start: the print grade corrects the rest

# ----------------------------------------------------------------------------- layout (= FaceLayout.h, pedalId "fb")
def fx(f): return -W / 2 + f * W
def fy(f): return H / 2 - f * H
KNOB_D = W * 0.2248                                     # GAIN / PRE / POST / LEVEL (one strip for all four)
KNOB_C = [(0.2054, 0.1167), (0.7848, 0.1167), (0.2075, 0.3210), (0.7848, 0.3210)]
STRIP_KNOB = 0                                          # the strip is rendered on GAIN (art hidden: shadow on paint only)
LED_D = 13.0 / UI_BODY_PX * W
LED_C = (0.4967, 0.3693)
SW_D = W * 0.194
SW_C = (0.4935, 0.7961)
TOG_C, TOG_WH = (0.4956, 0.2202), (W * 0.14, W * 0.20)
TOGGLE_POS = (("ToggleUp", -32.0, "up"), ("ToggleDown", 32.0, "down"))
BLOOM_BLUE = (0.20, 0.45, 1.0)
GRADE_BLUE = [(0.00, (0.00, 0.02, 0.22)), (0.30, (0.00, 0.12, 0.85)), (0.65, (0.08, 0.38, 1.00)),
              (0.90, (0.55, 0.75, 1.00)), (1.00, (0.92, 0.96, 1.00))]   # Coral Tide's deep blue (the art's LED)

# reuse Coral Tide's LED / print-grade helpers (pure numpy, no Blender needed at import)
import render_coraltide as CT

# ----------------------------------------------------------------------------- materials (Blender only)
def mat_frost_enclosure(name, ref_obj):
    """Blue-grey powder coat under patchy white frost, like the art's rim: matte, slightly sparkly."""
    m, nt, p, g = C.new_material(name)
    co = C.body_coords(g, ref_obj)
    frost = g.ramp(g.noise(co, 0.9, detail=10, rough=0.65, distortion=0.5), [(0.42, 0.0), (0.62, 1.0)])
    fine = g.ramp(g.noise(co, 14.0, detail=6), [(0.45, 0.0), (0.70, 1.0)])
    amt = g.math("MAXIMUM", frost, g.math("MULTIPLY", fine, 0.45))
    g.l(g.mixc(amt, FROST_BASE, FROST_WHITE), C.inp(p, "Base Color"))
    g.l(g.math("ADD", 0.50, g.math("MULTIPLY", amt, 0.25)), C.inp(p, "Roughness"))
    C.set_in(p, ["Specular IOR Level", "Specular"], 0.35)
    C.set_in(p, ["Coat Weight", "Clearcoat"], 0.05)
    bump = g.n("ShaderNodeBump"); C.set_in(bump, "Strength", 0.15); C.set_in(bump, "Distance", 0.03)
    g.l(g.noise(co, 9.0, detail=6, rough=0.6), C.inp(bump, "Height")); g.l(bump.outputs["Normal"], C.inp(p, "Normal"))
    return m

def fluted_knob(name, R, mats, coll, flutes=10, height=12.0):
    """Like the art's knobs: black skirt with rounded flutes, a machined black-metal cap with concentric rings, and a
    white pointer line running from near the centre to the cap's edge."""
    import bpy, bmesh
    skirt_mat, cap_mat, pointer_mat = mats
    bm = bmesh.new()
    n = flutes * 16
    ring = []
    for i in range(n):
        a = 2 * math.pi * i / n + math.pi / 2
        s = math.cos(flutes * a)
        r = R * (1.0 - 0.075 * max(0.0, -s) ** 3.0)              # narrow grooves between broad rounded lobes
                                                                  # (quick 1: 12 x 4.5 % ^0.6 read like a bottle cap,
                                                                  #  quick 2: 10 x 10 % ^0.8 like a gear)
        ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), 0)))
    f = bm.faces.new(ring)
    ext = bmesh.ops.extrude_face_region(bm, geom=[f])
    top = [v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=top, vec=(0, 0, height * 0.70))
    bmesh.ops.scale(bm, verts=top, vec=(0.92, 0.92, 1.0))        # taper towards the cap
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    skirt = C.mesh_obj(name + "Skirt", bm, skirt_mat); C.bevel(skirt, 0.8, 4, 40); C.smooth(skirt)
    cap = C.cylinder(name + "Cap", R * 0.78, height * 0.30, z0=height * 0.68, verts=128, mat=cap_mat)   # quick 1: 0.80, 2: 0.74
    C.bevel(cap, R * 0.08, 5); C.smooth(cap)
    ptr = C.box(name + "Pointer", R * 0.075, R * 0.48, 0.10, center=(0, R * 0.50, height * 0.98 + 0.05), mat=pointer_mat)
    C.bevel(ptr, 0.03, 2)
    pivot = bpy.data.objects.new(name + "Pivot", None); coll.objects.link(pivot)
    for o in (skirt, cap, ptr):
        o.parent = pivot; coll.objects.link(o)
    return pivot

def mat_machined_black(name):
    """Black anodised cap with fine concentric machining rings (radial-gradient bump), like the art's."""
    m, nt, p, g = C.new_material(name)
    C.set_in(p, "Base Color", (0.040, 0.040, 0.044, 1)); C.set_in(p, "Metallic", 1.0); C.set_in(p, "Roughness", 0.22)
    tc = g.n("ShaderNodeTexCoord")
    flat = g.n("ShaderNodeVectorMath", operation="MULTIPLY"); flat.inputs[1].default_value = (1.0, 1.0, 0.0)
    g.l(tc.outputs["Object"], flat.inputs[0])
    ln = g.n("ShaderNodeVectorMath", operation="LENGTH"); g.l(flat.outputs[0], ln.inputs[0])
    rings = g.math("SINE", g.math("MULTIPLY", ln.outputs["Value"], 2 * math.pi / 0.18))
    bump = g.n("ShaderNodeBump"); C.set_in(bump, "Strength", 0.25); C.set_in(bump, "Distance", 0.005)
    g.l(rings, C.inp(bump, "Height")); g.l(bump.outputs["Normal"], C.inp(p, "Normal"))
    return m

# ----------------------------------------------------------------------------- build
def build(scene):
    import bpy
    colls = {k: bpy.data.collections.new(k) for k in
             ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff", "ToggleUp", "ToggleDown", "Cutters")}
    for c in colls.values(): scene.collection.children.link(c)
    colls["Cutters"].hide_render = True; colls["Cutters"].hide_viewport = True
    face = colls["Face"]

    body = C.enclosure("Enclosure", W, H, 30.0, 3.5, None)
    paint = mat_frost_enclosure("FrostPaint", body)
    body.data.materials.append(paint)
    face.objects.link(body)
    steel_ref = C.mat_brushed_steel(body, scuff_center=None)        # exposure reference only

    chrome = C.mat_metal("Chrome", (0.92, 0.92, 0.94, 1), 0.07, smudges=0.05)
    dark_chrome = C.mat_metal("DarkChrome", (0.30, 0.30, 0.32, 1), 0.12, smudges=0.05)
    satin = C.mat_metal("SatinMetal", (0.30, 0.30, 0.31, 1), 0.30, smudges=0.1)
    gunmetal = C.mat_metal("Gunmetal", (0.12, 0.12, 0.13, 1), 0.25, smudges=0.05)        # the art's dark ring under the toggle
    knob_skirt = C.mat_plastic("KnobSkirt", (0.006, 0.006, 0.007, 1), rough=0.26, spec=0.45, coat=0.3, smudges=0.06)
    knob_cap = mat_machined_black("KnobCap")
    pointer = C.mat_simple("PointerWhite", (0.80, 0.80, 0.80, 1), rough=0.4)

    if os.path.exists(ART_IMAGE):
        o = art_plane(mat_art("ArtPrint", ART_IMAGE, body))
        o.location = (0, 0, 0.005); face.objects.link(o)
    else:
        print("[pedal] WARNING: missing", ART_IMAGE)

    # LED: chrome bezel (like the art), blue lens per state
    lx, ly = fx(LED_C[0]), fy(LED_C[1])
    bez = C.cylinder("LedBezel", LED_D * 0.5 + 1.0, 1.2, mat=dark_chrome); C.bevel(bez, 0.4, 3)   # quick 1: chrome +1.3 read as a white ring
    bez.location = (lx, ly, 0); face.objects.link(bez)
    for key, on in (("LedOn", True), ("LedOff", False)):
        lens = C.dome(f"Lens{key}", LED_D * 0.5, 0.55,
                      CT.mat_led_colour(f"Mat{key}", on, (0.02, 0.10, 0.90, 1), (0.04, 0.18, 1.0, 1), 0.8), segs=48)
        lens.location = (lx, ly, 1.2); colls[key].objects.link(lens)

    # one knob (the strip serves all four)
    kx, ky = fx(KNOB_C[STRIP_KNOB][0]), fy(KNOB_C[STRIP_KNOB][1])
    pivot = fluted_knob("Knob", KNOB_D * 0.5, (knob_skirt, knob_cap, pointer), colls["Knob"])
    pivot.location = (kx, ky, 0)

    sx, sy = fx(SW_C[0]), fy(SW_C[1])
    for key, travel in (("SwitchUp", 0.0), ("SwitchDown", -1.8)):
        c = colls[key]
        washer = C.cylinder(f"Washer{key}", SW_D * 0.56, 0.7, verts=96, mat=satin); C.bevel(washer, 0.2, 2)
        nut = C.cylinder(f"Nut{key}", SW_D * 0.5, 3.0, z0=0.7, verts=6, mat=satin); C.bevel(nut, 0.9, 3)
        nut.rotation_euler = (0, 0, math.radians(30))
        plunger = C.cylinder(f"Plunger{key}", SW_D * 0.32, 6.0 + travel, verts=96, mat=chrome)
        C.bevel(plunger, 0.8, 4); C.smooth(plunger)
        cap = C.dome(f"Dome{key}", SW_D * 0.30, 0.45, satin); cap.location = (0, 0, 6.0 + travel)
        for o in (washer, nut, plunger, cap):
            o.location = (sx, sy, o.location.z); c.objects.link(o)

    # MOD toggle: big chrome hex nut (the art's is ~10 mm across flats), bushing, chrome bat lever up / down
    tx, ty = fx(TOG_C[0]), fy(TOG_C[1])
    for key, tilt, _ in TOGGLE_POS:
        c = colls[key]
        nut = C.cylinder(f"TNut{key}", 5.4, 1.8, z0=0.5, verts=6, mat=dark_chrome); C.bevel(nut, 0.4, 2)   # quick 2: chrome read pale grey
        nut.rotation_euler = (0, 0, math.radians(30))
        ring = C.cylinder(f"TRing{key}", 4.0, 0.8, z0=2.3, verts=64, mat=gunmetal); C.bevel(ring, 0.25, 2)
        bush = C.cylinder(f"TBush{key}", 3.0, 3.0, z0=3.1, verts=48, mat=dark_chrome); C.bevel(bush, 0.3, 2)
        lever = C.cylinder(f"TLever{key}", 1.1, 9.5, verts=32, mat=chrome); C.smooth(lever)
        for v in lever.data.vertices:                                       # bat taper: thicker at the tip
            k = v.co.z / 9.5
            v.co.x *= 1.0 + 0.55 * k; v.co.y *= 1.0 + 0.55 * k
        ball = C.dome(f"TBall{key}", 1.8, 1.0, chrome, segs=32); ball.location = (0, 0, 9.5)
        piv = bpy.data.objects.new(f"TPivot{key}", None); c.objects.link(piv)
        piv.location = (tx, ty, 5.8); piv.rotation_euler = (math.radians(tilt), 0, 0)
        for o in (lever, ball):
            o.parent = piv; c.objects.link(o)
        for o in (nut, ring, bush):
            o.location = (tx, ty, o.location.z); c.objects.link(o)

    return colls, dict(pivot=pivot, knob=(kx, ky), switch=(sx, sy), led=(lx, ly), toggle=(tx, ty),
                       body=body, paint=paint, steel_ref=steel_ref)

def mat_art(name, path, ref_obj):
    """The artwork as a UV print: full colour, satin, thin coat (single-wide art, no alpha needed)."""
    import bpy
    m, nt, p, g = C.new_material(name)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(path)
    tex.image.colorspace_settings.name = "sRGB"
    tex.interpolation = "Cubic"
    tex.extension = "EXTEND"
    hs = nt.nodes.new("ShaderNodeHueSaturation")
    C.set_in(hs, "Saturation", PRINT_SAT); C.set_in(hs, "Value", PRINT_VALUE); C.set_in(hs, "Hue", PRINT_HUE)
    g.l(tex.outputs["Color"], C.inp(hs, "Color")); g.l(hs.outputs["Color"], C.inp(p, "Base Color"))
    co = C.body_coords(g, ref_obj)
    g.l(g.ramp(g.noise(co, 1.2, detail=6), [(0.3, 0.50), (0.7, 0.64)]), C.inp(p, "Roughness"))
    C.set_in(p, ["Specular IOR Level", "Specular"], 0.10)
    C.set_in(p, ["Coat Weight", "Clearcoat"], 0.08)
    return m

def art_plane(mat):
    import bmesh
    bm = bmesh.new()
    hw, hh = W / 2 - ART_INSET, H / 2 - ART_INSET
    vs = [bm.verts.new(v) for v in ((-hw, -hh, 0), (hw, -hh, 0), (hw, hh, 0), (-hw, hh, 0))]
    f = bm.faces.new(vs)
    uv = bm.loops.layers.uv.new("UVMap")
    for loop, t in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))): loop[uv].uv = t
    return C.mesh_obj("ArtPrint", bm, mat)

# ----------------------------------------------------------------------------- render
def main():
    import bpy
    os.makedirs(OUT, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    scene = C.setup_scene(ST)
    C.world_hdri(scene, ST.hdri, rotation_deg=40.0)
    C.add_key_light(scene)
    C.add_softbox(scene)
    colls, P = build(scene)
    cam = C.Camera(scene, UI_BODY_PX / W * ST.oversample)
    groups = [colls[k] for k in ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff", "ToggleUp", "ToggleDown")]
    face_objs = list(colls["Face"].objects)

    def render_face(path):
        C.set_catcher(face_objs, False)
        C.show_only([colls["Face"]], groups)
        cam.frame(0, 0, W, H)
        C.render_to(scene, path)

    def render_reference(path):
        body = P["body"]
        body.data.materials[0] = P["steel_ref"]
        hidden = [o for o in face_objs if o is not body]
        for o in hidden: o.hide_render = True
        try: render_face(path)
        finally:
            for o in hidden: o.hide_render = False
            body.data.materials[0] = P["paint"]

    C.auto_exposure(scene, render_reference, STEEL_REFERENCE_TARGET, os.path.join(TMP, "exposure_probe.png"))

    render_face(os.path.join(OUT, "frostbite_face.png"))

    art_objs = [o for o in face_objs if o.name.startswith("ArtPrint")]
    for o in art_objs: o.hide_render = True                           # catch shadows on the paint only (Coral Tide lesson)
    C.set_catcher(face_objs, True)
    C.show_only([colls["Face"], colls["Knob"]], groups)
    kx, ky = P["knob"]
    cam.frame(kx, ky, KNOB_D * KNOB_PAD, KNOB_D * KNOB_PAD)
    paths = []
    scene.cycles.samples = ST.knob_samples
    for i in range(ST.knob_frames):
        pos = i / (ST.knob_frames - 1)
        angle = math.radians(-135.0 + pos * 270.0)
        P["pivot"].rotation_euler = (0, 0, -angle)
        p = os.path.join(TMP, f"knob_{i:03d}.png"); C.render_to(scene, p); paths.append(p)
    C.stack_grid(paths, os.path.join(OUT, "frostbite_knob_strip.png"))
    scene.cycles.samples = ST.samples

    sx, sy = P["switch"]
    cam.frame(sx, sy, SW_D * SWITCH_PAD, SW_D * SWITCH_PAD)
    for key, fname in (("SwitchUp", "frostbite_switch_up.png"), ("SwitchDown", "frostbite_switch_down.png")):
        C.show_only([colls["Face"], colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, fname))
        C.post_fade(os.path.join(OUT, fname), inner=0.78, outer=0.97)

    tx, ty = P["toggle"]
    cam.frame(tx, ty, TOG_WH[0] * TOGGLE_PAD, TOG_WH[1] * TOGGLE_PAD)
    for key, fname in ((k, f"frostbite_toggle_{n}.png") for k, _, n in TOGGLE_POS):
        C.show_only([colls["Face"], colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, fname))
        C.post_fade(os.path.join(OUT, fname), inner=0.80, outer=0.98)

    for o in art_objs: o.hide_render = False
    C.set_catcher(face_objs, False)
    lx, ly = P["led"]
    cam.frame(lx, ly, LED_D * LED_PAD, LED_D * LED_PAD)
    for key, state in (("LedOn", "on"), ("LedOff", "off")):
        C.show_only([colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, f"frostbite_led_{state}.png"))
    on = os.path.join(OUT, "frostbite_led_on.png")
    C.bloom(on, colour=BLOOM_BLUE)
    CT.colour_grade(on, GRADE_BLUE)

    luts = CT.fit_print_grade(os.path.join(OUT, "frostbite_face.png"), ART_IMAGE)
    # face only: the control images are rendered over a shadow catcher (art hidden), so they hold only black knobs and
    # metal - quick 1 graded them too and the blue art's curves turned the black knobs brownish grey
    CT.apply_print_grade(os.path.join(OUT, "frostbite_face.png"), luts)

    P["pivot"].rotation_euler = (0, 0, 0)
    hidden = (colls["SwitchDown"], colls["LedOff"], colls["ToggleUp"])
    for c in groups: c.hide_render = c in hidden
    for c in hidden: c.hide_viewport = True
    cam.frame(0, 0, W, H)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "frostbite.blend"))
    print("[pedal] done")

if __name__ == "__main__":
    import importlib, pedal_render_common as C
    importlib.reload(C)
    CT.C = C                                    # render_coraltide's helpers use its module-level C ...
    CT.W, CT.H, CT.ART_INSET = W, H, ART_INSET  # ... and its body size (fit_print_grade crops the art inset with them)
    ST = C.Settings(sys.argv)
    main()
