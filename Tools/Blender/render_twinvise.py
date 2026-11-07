"""
Twin Vise - photoreal Blender render (based on the Trace Elliot SMX Dual Compressor, double-wide; Michael's artwork).
Michael's full-face artwork (Assets/twinvise_art.png, made by Tools/Art/make_twinvise_art.py from his original image:
rough gunmetal top, printed orange knob arcs, INPUT GAIN / LOW COMP / HIGH COMP / BALANCE / LEVEL, "TWIN VISE" on a
rusty bench vise, INPUT / HI COMP / EFFECT) UV-printed 2.6 mm inside the edge of a rough cast-gunmetal double-wide
enclosure (186.8 x 149.8 mm = plugin body 464 x 372 px). The real controls sit where the art's painted ones were:
five black knurled knobs with a machined (spun-ring) black-metal cap and a white pointer (like the art's), red HI COMP
and EFFECT LEDs and the bicolour INPUT LED in dark chrome bezels, and two satin hex-nut footswitches.
Uses pedal_render_common.py; LED / print-grade helpers from render_coraltide.py, the machined cap from render_frostbite.py.

Writes Assets/TwinVise/: twinvise_face.png, twinvise_knob_strip.png (8-column grid, one strip for all five knobs),
twinvise_switch_up/down.png (both footswitches), twinvise_led_on/off.png (red: EFFECT and HI COMP),
twinvise_meter_green/red/off.png (INPUT), and saves Tools/Blender/twinvise.blend.

Run from C:\\Dev\\pedalfactory-master:
    & "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b -P Tools\\Blender\\render_twinvise.py
Options after "--":   --quick   |   --hdri=NAME.exr

Positions MUST match Source/UI/FaceLayout.h (pedalId "tv"; fractions printed by Tools/Art/make_twinvise_art.py).
Pads MUST match RenderedFace. The layout part of this file imports without Blender (used for alignment checks).
"""
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Assets", "TwinVise")
TMP = os.path.join(HERE, "_frames_twinvise")
ART_IMAGE = os.path.join(ROOT, "Assets", "twinvise_art.png")

UI_BODY_PX = 464.0                         # double-wide: FaceLayout::bodyWidthFor(2)
UI_BODY_PX_H = 372.0
W, H = 91.0 * UI_BODY_PX / 226.0, 91.0 * UI_BODY_PX_H / 226.0     # 186.8 x 149.8 mm
KNOB_PAD, SWITCH_PAD, LED_PAD = 1.30, 1.35, 2.6
ART_INSET = 2.6                                         # mm (= FaceLayout::artInset2 * W)

# Albedos darker than the target look (AgX lifts and desaturates). First guesses: tune from the quick render.
GUNMETAL_BASE = (0.045, 0.046, 0.048, 1)               # the art's rim: rough dark grey, sRGB ~ (70-95)
GUNMETAL_LIGHT = (0.10, 0.10, 0.105, 1)                # lighter specks of the cast texture
STEEL_REFERENCE_TARGET = 0.74                          # same exposure basis as the other pedals
PRINT_SAT, PRINT_VALUE, PRINT_HUE = 1.0, 1.0, 0.5      # neutral start: the print grade corrects the rest

# ----------------------------------------------------------------------------- layout (= FaceLayout.h, pedalId "tv")
def fx(f): return -W / 2 + f * W
def fy(f): return H / 2 - f * H
KNOB_D = W * 0.1092                                     # all five knobs (one strip)
KNOB_C = [(0.1225, 0.1578), (0.3102, 0.1578), (0.4986, 0.1578), (0.6884, 0.1578), (0.8769, 0.1578)]
STRIP_KNOB = 2                                          # rendered on HIGH COMP (the face under it is a shadow catcher)
LED_D = 11.0 / UI_BODY_PX * W
LED_EFFECT, LED_HICOMP, LED_INPUT = (0.7048, 0.6932), (0.2945, 0.6941), (0.4993, 0.6328)
SW_D = W * 0.0969
SW_EFFECT, SW_HICOMP = (0.7062, 0.8132), (0.2931, 0.8132)   # images rendered once at EFFECT, used for both
BLOOM_GREEN = (0.15, 1.0, 0.25)
GRADE_GREEN = [(0.00, (0.00, 0.14, 0.02)), (0.30, (0.02, 0.62, 0.08)), (0.65, (0.08, 0.92, 0.18)),
               (0.90, (0.50, 1.00, 0.56)), (1.00, (0.92, 1.00, 0.92))]   # quick 1: pastel mint; deeper, like Coral Tide's blue

import render_coraltide as CT
import render_frostbite as FB

# ----------------------------------------------------------------------------- materials (Blender only)
def mat_cast_gunmetal(name, ref_obj):
    """Rough cast / hammered dark gunmetal like the art's top and rim: mottled dark grey with light specks, satin."""
    m, nt, p, g = C.new_material(name)
    co = C.body_coords(g, ref_obj)
    mott = g.ramp(g.noise(co, 1.6, detail=8, rough=0.6), [(0.35, 0.0), (0.70, 1.0)])
    speck = g.ramp(g.noise(co, 22.0, detail=4), [(0.62, 0.0), (0.78, 1.0)])
    amt = g.math("MAXIMUM", g.math("MULTIPLY", mott, 0.55), speck)
    g.l(g.mixc(amt, GUNMETAL_BASE, GUNMETAL_LIGHT), C.inp(p, "Base Color"))
    C.set_in(p, "Metallic", 0.55)
    g.l(g.math("ADD", 0.48, g.math("MULTIPLY", speck, -0.12)), C.inp(p, "Roughness"))
    bump = g.n("ShaderNodeBump"); C.set_in(bump, "Strength", 0.35); C.set_in(bump, "Distance", 0.05)
    g.l(g.noise(co, 7.0, detail=8, rough=0.7), C.inp(bump, "Height")); g.l(bump.outputs["Normal"], C.inp(p, "Normal"))
    return m

def mat_spun_cap(name):
    """Like the art's knob caps: dark spun / brushed metal with visible concentric rings and a soft grey sheen
    (quick 1 used Frostbite's black anodised cap: it read as plain glossy black)."""
    m, nt, p, g = C.new_material(name)
    C.set_in(p, "Base Color", (0.055, 0.055, 0.058, 1)); C.set_in(p, "Metallic", 1.0); C.set_in(p, "Roughness", 0.26)
    # quick 2: base 0.13 / rough 0.30 read silver-grey (cap mean sRGB 112 vs the art's 50)
    for nm, v in ((["Anisotropic", "Anisotropic Weight"], 0.4),):
        try: C.set_in(p, nm, v)
        except Exception: pass
    tc = g.n("ShaderNodeTexCoord")
    flat = g.n("ShaderNodeVectorMath", operation="MULTIPLY"); flat.inputs[1].default_value = (1.0, 1.0, 0.0)
    g.l(tc.outputs["Object"], flat.inputs[0])
    ln = g.n("ShaderNodeVectorMath", operation="LENGTH"); g.l(flat.outputs[0], ln.inputs[0])
    rings = g.math("SINE", g.math("MULTIPLY", ln.outputs["Value"], 2 * math.pi / 0.12))
    fine = g.math("SINE", g.math("MULTIPLY", ln.outputs["Value"], 2 * math.pi / 0.035))
    bump = g.n("ShaderNodeBump"); C.set_in(bump, "Strength", 0.45); C.set_in(bump, "Distance", 0.008)
    g.l(g.math("ADD", rings, g.math("MULTIPLY", fine, 0.5)), C.inp(bump, "Height")); g.l(bump.outputs["Normal"], C.inp(p, "Normal"))
    return m

# ----------------------------------------------------------------------------- build
def build(scene):
    import bpy
    colls = {k: bpy.data.collections.new(k) for k in
             ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff", "MeterGreen", "MeterRed", "MeterOff", "Cutters")}
    for c in colls.values(): scene.collection.children.link(c)
    colls["Cutters"].hide_render = True; colls["Cutters"].hide_viewport = True
    face = colls["Face"]

    body = C.enclosure("Enclosure", W, H, 30.0, 3.5, None)
    paint = mat_cast_gunmetal("CastGunmetal", body)
    body.data.materials.append(paint)
    face.objects.link(body)
    steel_ref = C.mat_brushed_steel(body, scuff_center=None)        # exposure reference only

    chrome = C.mat_metal("Chrome", (0.92, 0.92, 0.94, 1), 0.07, smudges=0.05)
    dark_chrome = C.mat_metal("DarkChrome", (0.30, 0.30, 0.32, 1), 0.12, smudges=0.05)
    satin = C.mat_metal("SatinMetal", (0.12, 0.105, 0.09, 1), 0.42, smudges=0.12)     # the art's switches: dark warm satin,
                                                    # sRGB ~ (83, 75, 68) (quick 1: 0.32 / 0.30 bright chrome; quick 2:
                                                    # 0.20 / 0.38 still (106, 107, 106), neutral)
    knob_skirt = C.mat_plastic("KnobSkirt", (0.008, 0.008, 0.009, 1), rough=0.30, spec=0.45, coat=0.2, smudges=0.06)
    knob_cap = mat_spun_cap("KnobCap")
    pointer = C.mat_simple("PointerWhite", (0.82, 0.82, 0.80, 1), rough=0.4)

    if os.path.exists(ART_IMAGE):
        o = FB.art_plane(FB.mat_art("ArtPrint", ART_IMAGE, body)); o.location = (0, 0, 0.005); face.objects.link(o)
    else:
        print("[pedal] WARNING: missing", ART_IMAGE)

    # LEDs: dark chrome bezels on the face; red lenses (EFFECT; HI COMP reuses the images), INPUT bicolour
    red_base, red_emit = (0.9, 0.03, 0.01, 1), (1.0, 0.07, 0.02, 1)
    green_base, green_emit = (0.03, 0.75, 0.08, 1), (0.06, 1.0, 0.12, 1)
    for tag, pos in (("Effect", LED_EFFECT), ("HiComp", LED_HICOMP), ("Input", LED_INPUT)):
        bez = C.cylinder(f"LedBezel{tag}", LED_D * 0.5 + 1.2, 1.2, mat=dark_chrome); C.bevel(bez, 0.4, 3)
        bez.location = (fx(pos[0]), fy(pos[1]), 0); face.objects.link(bez)
    lx, ly = fx(LED_EFFECT[0]), fy(LED_EFFECT[1])
    for key, on in (("LedOn", True), ("LedOff", False)):
        lens = C.dome(f"Lens{key}", LED_D * 0.5, 0.55, CT.mat_led_colour(f"Mat{key}", on, red_base, red_emit, 0.9), segs=48)
        lens.location = (lx, ly, 1.2); colls[key].objects.link(lens)
    mx, my = fx(LED_INPUT[0]), fy(LED_INPUT[1])
    for key, on, base, emit in (("MeterGreen", True, green_base, green_emit), ("MeterRed", True, red_base, red_emit),
                                ("MeterOff", False, (0.35, 0.30, 0.06, 1), (0, 0, 0, 1))):   # off: dim diffused amber-grey lens
        lens = C.dome(f"Lens{key}", LED_D * 0.5, 0.55, CT.mat_led_colour(f"Mat{key}", on, base, emit, 0.9), segs=48)
        lens.location = (mx, my, 1.2); colls[key].objects.link(lens)

    # one knob (the strip serves all five)
    kx, ky = fx(KNOB_C[STRIP_KNOB][0]), fy(KNOB_C[STRIP_KNOB][1])
    pivot = C.knurled_knob("Knob", KNOB_D * 0.5, 13.0, (knob_skirt, knob_cap, pointer), colls["Knob"], ridges=60, depth=0.95)
    pivot.location = (kx, ky, 0)

    # footswitch like the art's: satin hex nut on a washer, satin plunger with a domed cap
    sx, sy = fx(SW_EFFECT[0]), fy(SW_EFFECT[1])
    for key, travel in (("SwitchUp", 0.0), ("SwitchDown", -1.8)):
        c = colls[key]
        washer = C.cylinder(f"Washer{key}", SW_D * 0.52, 0.7, verts=96, mat=satin); C.bevel(washer, 0.2, 2)
        nut = C.cylinder(f"Nut{key}", SW_D * 0.5, 3.0, z0=0.7, verts=6, mat=satin); C.bevel(nut, 0.9, 3)
        nut.rotation_euler = (0, 0, math.radians(30))
        plunger = C.cylinder(f"Plunger{key}", SW_D * 0.34, 6.0 + travel, verts=96, mat=satin)
        C.bevel(plunger, 0.8, 4); C.smooth(plunger)
        cap = C.dome(f"Dome{key}", SW_D * 0.32, 0.40, satin); cap.location = (0, 0, 6.0 + travel)
        for o in (washer, nut, plunger, cap):
            o.location = (sx, sy, o.location.z); c.objects.link(o)

    return colls, dict(pivot=pivot, knob=(kx, ky), switch=(sx, sy), led=(lx, ly), meter=(mx, my),
                       body=body, paint=paint, steel_ref=steel_ref)

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
    groups = [colls[k] for k in ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff",
                                 "MeterGreen", "MeterRed", "MeterOff")]
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

    render_face(os.path.join(OUT, "twinvise_face.png"))

    art_objs = [o for o in face_objs if o.name.startswith("ArtPrint")]
    for o in art_objs: o.hide_render = True                           # catch shadows on the paint only
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
    C.stack_grid(paths, os.path.join(OUT, "twinvise_knob_strip.png"))
    scene.cycles.samples = ST.samples

    sx, sy = P["switch"]
    cam.frame(sx, sy, SW_D * SWITCH_PAD, SW_D * SWITCH_PAD)
    for key, fname in (("SwitchUp", "twinvise_switch_up.png"), ("SwitchDown", "twinvise_switch_down.png")):
        C.show_only([colls["Face"], colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, fname))
        C.post_fade(os.path.join(OUT, fname), inner=0.78, outer=0.97)

    for o in art_objs: o.hide_render = False
    C.set_catcher(face_objs, False)
    lx, ly = P["led"]
    cam.frame(lx, ly, LED_D * LED_PAD, LED_D * LED_PAD)
    for key, state in (("LedOn", "on"), ("LedOff", "off")):
        C.show_only([colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, f"twinvise_led_{state}.png"))
    on = os.path.join(OUT, "twinvise_led_on.png")
    C.bloom(on, colour=CT.BLOOM_RED)
    CT.colour_grade(on, CT.GRADE_RED)

    mx, my = P["meter"]
    cam.frame(mx, my, LED_D * LED_PAD, LED_D * LED_PAD)
    for key, state, bloom, grade in (("MeterGreen", "green", BLOOM_GREEN, GRADE_GREEN),
                                     ("MeterRed", "red", CT.BLOOM_RED, CT.GRADE_RED), ("MeterOff", "off", None, None)):
        C.show_only([colls[key]], groups)
        path = os.path.join(OUT, f"twinvise_meter_{state}.png")
        C.render_to(scene, path)
        if bloom:
            C.bloom(path, colour=bloom)
            CT.colour_grade(path, grade)

    luts = CT.fit_print_grade(os.path.join(OUT, "twinvise_face.png"), ART_IMAGE)
    CT.apply_print_grade(os.path.join(OUT, "twinvise_face.png"), luts)    # face only (Frostbite lesson)

    P["pivot"].rotation_euler = (0, 0, 0)
    hidden = (colls["SwitchDown"], colls["LedOff"], colls["MeterRed"], colls["MeterOff"])
    for c in groups: c.hide_render = c in hidden
    for c in hidden: c.hide_viewport = True
    cam.frame(0, 0, W, H)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "twinvise.blend"))
    print("[pedal] done")

if __name__ == "__main__":
    import importlib, pedal_render_common as C
    importlib.reload(C)
    CT.C = C; FB.C = C                          # the borrowed helpers use their modules' C ...
    CT.W, CT.H, CT.ART_INSET = W, H, ART_INSET  # ... and body size (fit_print_grade crops the art inset with them)
    FB.W, FB.H, FB.ART_INSET = W, H, ART_INSET  # art_plane sizes the print from these
    FB.PRINT_SAT, FB.PRINT_VALUE, FB.PRINT_HUE = PRINT_SAT, PRINT_VALUE, PRINT_HUE
    ST = C.Settings(sys.argv)
    main()
