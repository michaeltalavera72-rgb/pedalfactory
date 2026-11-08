"""
Soft Landing - photoreal Blender render (based on the Toadworks Mr. Squishy; Michael's artwork).
Michael's full-face artwork (Assets/softlanding_art.png, made by Tools/Art/make_softlanding_art.py from his original
image: powder-blue sky, GAIN / SQUISH / LEVEL, "SOFT LANDING" over a sunset lake with mountains and pines) UV-printed
2.6 mm inside the edge of an enclosure painted to continue the art round its rounded rim (91 x 149.8 mm = plugin body
226 x 372 px). The real controls sit where the art's painted ones were: three ivory knobs with a softly scalloped side
and a black pointer line on a navy-black skirt (the art's; profile after Michael's Copilot scene script
soft_landing_blender_v2.py: 12 lobes, ivory (0.86, 0.78, 0.61) roughness 0.28), an amber LED in a dark bezel and a
warm satin footswitch on a hex nut. Uses pedal_render_common.py.

Copilot's script built a stand-alone animated scene (EEVEE, drifting clouds, rippling water, a 120-frame GIF). The
plugin doesn't play animations: like every other pedal, this renders the still images the plugin composites (the
"animation" is the 64-frame knob filmstrip). Blender has no animated-GIF writer of its own anyway (its movie output
goes through FFmpeg; Copilot's script assembled the GIF with Pillow).

Writes Assets/SoftLanding/: softlanding_face.png, softlanding_knob_strip.png (8-column grid, one strip for all three
knobs), softlanding_switch_up/down.png, softlanding_led_on/off.png, and saves Tools/Blender/softlanding.blend.

Run from C:\\Dev\\pedalfactory-master:
    & "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b -P Tools\\Blender\\render_softlanding.py
Options after "--":   --quick   |   --hdri=NAME.exr

Positions MUST match Source/UI/FaceLayout.h (pedalId "sl"; fractions printed by Tools/Art/make_softlanding_art.py).
Pads MUST match RenderedFace. The layout part of this file imports without Blender (used for alignment checks).
The print grade (fit from the face render vs the art, applied to the face only) is the Coral Tide one; the amber LED
material and grade are Golden Haze's.
"""
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Assets", "SoftLanding")
TMP = os.path.join(HERE, "_frames_softlanding")
ART_IMAGE = os.path.join(ROOT, "Assets", "softlanding_art.png")

W, H = 91.0, 91.0 * 372.0 / 226.0          # = plugin pedal body 226 x 372 px
UI_BODY_PX = 226.0
UI_BODY_PX_H = 372.0
KNOB_PAD, SWITCH_PAD, LED_PAD = 1.30, 1.35, 2.6
ART_INSET = 2.6                                         # mm (= FaceLayout::artInset * W)

# Albedos darker than the target look (AgX lifts and desaturates). First guesses: tune from the quick render.
RIM_DARKEN = 0.80                                       # the rim continues the art's edge colours, a little darker
IVORY = (0.98, 0.62, 0.30, 1)                           # the art's knob cap reads sRGB ~ (218-225, 196-201, 170-175)
                                                        # (quick 2: (0.95, 0.70, 0.42) read (195, 186, 170): AgX ate the
                                                        # warmth; ivory_grade() then pins the cap colour to the art)
IVORY_TARGET = (218.0, 196.0, 170.0)                    # sRGB mean of the art's cap tops
                                                        # (quick 1: (0.62, 0.50, 0.33) + coat read grey-beige (163, 158, 146))
SKIRT = (0.010, 0.016, 0.032, 1)                        # navy-black skirt (the art's dark ring)
STEEL_REFERENCE_TARGET = 0.74                           # same exposure basis as the other pedals
PRINT_SAT, PRINT_VALUE, PRINT_HUE = 1.0, 0.95, 0.5      # neutral start: the print grade corrects the rest

# ----------------------------------------------------------------------------- layout (= FaceLayout.h, pedalId "sl")
def fx(f): return -W / 2 + f * W
def fy(f): return H / 2 - f * H
KNOB_D = W * 0.2481                                     # GAIN / SQUISH / LEVEL skirt (one strip for all three)
KNOB_C = [(0.1708, 0.1605), (0.4994, 0.1605), (0.8281, 0.1605)]
STRIP_KNOB = 1                                          # the strip is rendered on SQUISH (art hidden: shadow on paint only)
LED_D = 13.0 / UI_BODY_PX * W                           # lens; the art's dark bezel is ~8.3 mm across
LED_C = (0.4994, 0.3350)
SW_D = W * 0.2316                                       # hex nut across corners (the art's)
SW_C = (0.4994, 0.8483)

import render_coraltide as CT                           # print grade (pure numpy at import)
import render_goldenhaze as GH                          # amber LED material + grade

# ----------------------------------------------------------------------------- materials (Blender only)
def mat_wrap_enclosure(name, ref_obj, path):
    """Painted metal that continues the artwork round the rim: the art projected from above (edge pixels extended
    past the print), a little darker, satin with a thin coat."""
    import bpy
    m, nt, p, g = C.new_material(name)
    co = C.body_coords(g, ref_obj)
    sep = g.n("ShaderNodeSeparateXYZ"); g.l(co, sep.inputs[0])
    u = g.remap(sep.outputs["X"], -W / 2 + ART_INSET, W / 2 - ART_INSET, 0.0, 1.0)
    v = g.remap(sep.outputs["Y"], -H / 2 + ART_INSET, H / 2 - ART_INSET, 0.0, 1.0)
    comb = g.n("ShaderNodeCombineXYZ"); g.l(u, comb.inputs[0]); g.l(v, comb.inputs[1])
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(path) if os.path.exists(path) else None
    if tex.image is not None: tex.image.colorspace_settings.name = "sRGB"
    tex.extension = "EXTEND"; tex.interpolation = "Cubic"
    g.l(comb.outputs[0], tex.inputs["Vector"])
    blur = g.n("ShaderNodeMix", data_type="RGBA", blend_type="MULTIPLY")   # darken (painted metal, not print)
    blur.inputs[0].default_value = 1.0
    g.l(tex.outputs["Color"], blur.inputs[6]); blur.inputs[7].default_value = (RIM_DARKEN,) * 3 + (1,)
    g.l(blur.outputs[2], C.inp(p, "Base Color"))
    g.l(g.ramp(g.noise(co, 1.0, detail=6), [(0.3, 0.42), (0.7, 0.55)]), C.inp(p, "Roughness"))
    C.set_in(p, ["Specular IOR Level", "Specular"], 0.35)
    C.set_in(p, ["Coat Weight", "Clearcoat"], 0.10)
    return m

def scalloped_knob(name, R, mats, coll, lobes=12, height=13.0):
    """Like the art's knobs: a navy-black skirt, then an ivory cap whose side is softly scalloped (Copilot's profile:
    r (1 + a cos(12 phi)), here sharpened into rounded lobes with narrow grooves, like the painted ones), a gently
    domed top and a black pointer line from the centre to 3/4 of the radius."""
    import bpy, bmesh
    skirt_mat, cap_mat, pointer_mat = mats
    skirt = C.cylinder(name + "Skirt", R, height * 0.30, verts=128, mat=skirt_mat)
    for vtx in skirt.data.vertices:                              # slight flare at the base
        if vtx.co.z < 0.01: vtx.co.x *= 1.02; vtx.co.y *= 1.02
    C.bevel(skirt, 0.6, 3); C.smooth(skirt)
    Rc = R * 0.935
    bm = bmesh.new()
    n = lobes * 16
    ring = []
    for i in range(n):
        a = 2 * math.pi * i / n + math.pi / 2
        s = math.cos(lobes * a)
        r = Rc * (1.0 - 0.055 * max(0.0, -s) ** 1.6)
        ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), height * 0.22)))
    f = bm.faces.new(ring)
    ext = bmesh.ops.extrude_face_region(bm, geom=[f])
    top = [v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=top, vec=(0, 0, height * 0.58))
    bmesh.ops.scale(bm, verts=top, vec=(0.95, 0.95, 1.0))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    side = C.mesh_obj(name + "Cap", bm, cap_mat); C.bevel(side, 1.4, 5, 40); C.smooth(side)
    Rd, rise = Rc * 0.93, 0.14                                  # dome: quick 1 (rise 0.06) read flat; the art's cap is
    hd = Rd * rise                                              # visibly rounded (~1.4 mm)
    z0 = height * 0.80 - 0.05
    dome = C.dome(name + "Top", Rd, rise, cap_mat, segs=96); dome.location = (0, 0, z0)
    C.smooth(dome)
    # pointer: the art's thick rounded black line from 0.14 R to 0.85 R, bent to follow the dome
    pbm = bmesh.new()
    bmesh.ops.create_cube(pbm, size=1.0)
    bmesh.ops.scale(pbm, vec=(R * 0.095, R * 0.71, 0.14), verts=pbm.verts)
    bmesh.ops.translate(pbm, vec=(0, R * 0.495, 0.0), verts=pbm.verts)
    long_edges = [e for e in pbm.edges if abs(e.verts[0].co.y - e.verts[1].co.y) > 1e-6]
    bmesh.ops.subdivide_edges(pbm, edges=long_edges, cuts=16, use_grid_fill=True)
    for vtx in pbm.verts:
        r = math.hypot(vtx.co.x, vtx.co.y)
        vtx.co.z += z0 + hd * math.sqrt(max(0.0, 1.0 - (r / Rd) ** 2)) - 0.03
    ptr = C.mesh_obj(name + "Pointer", pbm, pointer_mat)
    C.bevel(ptr, 0.04, 2, 60)
    pivot = bpy.data.objects.new(name + "Pivot", None); coll.objects.link(pivot)
    for o in (skirt, side, dome, ptr):
        o.parent = pivot; coll.objects.link(o)
    return pivot

def mat_textured_cap(name):
    """The footswitch's dark warm metal cap with a fine sandblasted texture (the art's)."""
    m, nt, p, g = C.new_material(name)
    C.set_in(p, "Base Color", (0.10, 0.075, 0.05, 1)); C.set_in(p, "Metallic", 1.0); C.set_in(p, "Roughness", 0.60)
    tc = g.n("ShaderNodeTexCoord")                              # quick 1 ((0.20, 0.17, 0.14) / 0.42) read smooth light grey
    bump = g.n("ShaderNodeBump"); C.set_in(bump, "Strength", 0.8); C.set_in(bump, "Distance", 0.05)   # quick 2: texture invisible
    g.l(g.noise(tc.outputs["Object"], 25.0, detail=6), C.inp(bump, "Height")); g.l(bump.outputs["Normal"], C.inp(p, "Normal"))
    return m

def ivory_grade(path, target=IVORY_TARGET):
    """Pins the knob caps' colour to the art: per-channel gain fitted so the mean of the bright cap pixels matches
    target (sRGB), applied only to bright pixels (luminance ramp 0.40 -> 0.60), so the black pointer, the navy skirt and
    the shadow stay as rendered. AgX desaturates a warm ivory towards grey whatever the albedo (quick 1 / 2)."""
    import numpy as np
    a = C.load_rgba(path)
    rgb = a[..., :3]
    L = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    w = np.clip((L - 0.40) / 0.20, 0.0, 1.0) * (a[..., 3] > 0.5)
    sel = w > 0.99
    if sel.sum() < 100: print("[pedal] ivory_grade: no cap pixels"); return
    meas = rgb[sel].mean(0) * 255.0
    gain = np.clip(np.array(target) / meas, 0.7, 1.5)
    print("[pedal] ivory_grade: cap mean", meas.round(1), "gain", gain.round(3))
    a[..., :3] = rgb * (1.0 - w[..., None]) + np.clip(rgb * gain, 0.0, 1.0) * w[..., None]
    C.save_rgba(a, path)

# ----------------------------------------------------------------------------- build
def build(scene):
    import bpy
    colls = {k: bpy.data.collections.new(k) for k in ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff", "Cutters")}
    for c in colls.values(): scene.collection.children.link(c)
    colls["Cutters"].hide_render = True; colls["Cutters"].hide_viewport = True
    face = colls["Face"]

    body = C.enclosure("Enclosure", W, H, 30.0, 3.5, None)
    paint = mat_wrap_enclosure("WrapPaint", body, ART_IMAGE)
    body.data.materials.append(paint)
    face.objects.link(body)
    steel_ref = C.mat_brushed_steel(body, scuff_center=None)        # exposure reference only

    chrome = C.mat_metal("WarmChrome", (0.50, 0.42, 0.33, 1), 0.26, smudges=0.05)   # quick 2 (0.62, 0.55, 0.47) / 0.18: silver   # the art's ring: warm, not mirror-bright
    groove = C.mat_simple("Groove", (0.015, 0.012, 0.010, 1), rough=0.6)
    satin = C.mat_metal("WarmSatin", (0.20, 0.15, 0.10, 1), 0.40, smudges=0.1)       # quick 2 (0.26, 0.21, 0.16): (99, 91, 80) vs (85, 71, 57)       # the art's nut (114, 96, 78); quick 1
                                                                                      # (0.42, 0.37, 0.31) / 0.30 read bright neutral chrome
    bezel_mat = C.mat_plastic("LedBezel", (0.02, 0.012, 0.008, 1), rough=0.30, spec=0.4, coat=0.3, smudges=0.05)
    knob_skirt = C.mat_plastic("KnobSkirt", SKIRT, rough=0.45, spec=0.30, coat=0.0, smudges=0.06)   # quick 1 (0.30 + coat) read blue-grey
    knob_cap = C.mat_plastic("KnobIvory", IVORY, rough=0.35, spec=0.30, coat=0.0, smudges=0.05)
    pointer = C.mat_simple("PointerBlack", (0.012, 0.010, 0.010, 1), rough=0.35)
    cap_mat = mat_textured_cap("SwitchCap")

    if os.path.exists(ART_IMAGE):
        o = art_plane(mat_art("ArtPrint", ART_IMAGE, body))
        o.location = (0, 0, 0.005); face.objects.link(o)
    else:
        print("[pedal] WARNING: missing", ART_IMAGE)

    # LED: dark bezel (~8.3 mm, like the art's), amber lens per state (Golden Haze's)
    lx, ly = fx(LED_C[0]), fy(LED_C[1])
    bez = C.cylinder("LedBezel", LED_D * 0.5 + 1.5, 1.3, mat=bezel_mat); C.bevel(bez, 0.45, 3); C.smooth(bez)
    bez.location = (lx, ly, 0); face.objects.link(bez)
    for key, on in (("LedOn", True), ("LedOff", False)):
        lens = C.dome(f"Lens{key}", LED_D * 0.5, 0.55, GH.mat_led_amber(f"Led{key}", on), segs=48)
        lens.location = (lx, ly, 1.3); colls[key].objects.link(lens)

    # one knob (the strip serves all three)
    kx, ky = fx(KNOB_C[STRIP_KNOB][0]), fy(KNOB_C[STRIP_KNOB][1])
    pivot = scalloped_knob("Knob", KNOB_D * 0.5, (knob_skirt, knob_cap, pointer), colls["Knob"])
    pivot.location = (kx, ky, 0)

    # footswitch: warm satin hex nut, a chrome ring, the dark textured cap (with the art's small notch)
    sx, sy = fx(SW_C[0]), fy(SW_C[1])
    for key, travel in (("SwitchUp", 0.0), ("SwitchDown", -1.8)):
        c = colls[key]
        # the art: a round satin flange with a hex nut whose corners are well rounded (round outline, flats only hint),
        # a dark groove, a thick warm-chrome ring with a small slot, and the big dark textured cap (quick 1: a sharp
        # bright-chrome hex read too small and too bright)
        flange = C.cylinder(f"Flange{key}", SW_D * 0.50, 0.8, verts=96, mat=satin); C.bevel(flange, 0.3, 2); C.smooth(flange)
        nut = C.cylinder(f"Nut{key}", SW_D * 0.505, 2.4, z0=0.8, verts=6, mat=satin); C.bevel(nut, 3.5, 6)                   # quick 2 (1.8): corners still sharp
        nut.rotation_euler = (0, 0, math.radians(30))
        grv = C.cylinder(f"Groove{key}", SW_D * 0.425, 0.3, z0=3.2, verts=96, mat=groove)
        ring = C.cylinder(f"Ring{key}", SW_D * 0.405, 1.6, z0=3.2, verts=96, mat=chrome); C.bevel(ring, 0.6, 3); C.smooth(ring)
        plunger = C.cylinder(f"Plunger{key}", SW_D * 0.33, 4.0 + travel, z0=3.2, verts=96, mat=chrome)
        C.bevel(plunger, 0.7, 4); C.smooth(plunger)
        cap = C.cylinder(f"Cap{key}", SW_D * 0.30, 0.6, z0=7.2 + travel, verts=96, mat=cap_mat); C.bevel(cap, 0.25, 3); C.smooth(cap)
        notch = C.box(f"Notch{key}", SW_D * 0.10, SW_D * 0.035, 0.3, center=(0, SW_D * 0.385, 4.85), mat=groove)
        for o in (flange, nut, grv, ring, plunger, cap, notch):
            o.location = (sx + o.location.x, sy + o.location.y, o.location.z); c.objects.link(o)

    return colls, dict(pivot=pivot, knob=(kx, ky), switch=(sx, sy), led=(lx, ly), body=body, paint=paint, steel_ref=steel_ref)

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
    groups = [colls[k] for k in ("Face", "Knob", "SwitchUp", "SwitchDown", "LedOn", "LedOff")]
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

    render_face(os.path.join(OUT, "softlanding_face.png"))

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
    C.stack_grid(paths, os.path.join(OUT, "softlanding_knob_strip.png"))
    ivory_grade(os.path.join(OUT, "softlanding_knob_strip.png"))
    scene.cycles.samples = ST.samples

    sx, sy = P["switch"]
    cam.frame(sx, sy, SW_D * SWITCH_PAD, SW_D * SWITCH_PAD)
    for key, fname in (("SwitchUp", "softlanding_switch_up.png"), ("SwitchDown", "softlanding_switch_down.png")):
        C.show_only([colls["Face"], colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, fname))
        C.post_fade(os.path.join(OUT, fname), inner=0.78, outer=0.97)

    for o in art_objs: o.hide_render = False
    C.set_catcher(face_objs, False)
    lx, ly = P["led"]
    cam.frame(lx, ly, LED_D * LED_PAD, LED_D * LED_PAD)
    for key, state in (("LedOn", "on"), ("LedOff", "off")):
        C.show_only([colls[key]], groups)
        C.render_to(scene, os.path.join(OUT, f"softlanding_led_{state}.png"))
    on = os.path.join(OUT, "softlanding_led_on.png")
    C.bloom(on, colour=GH.LED_BLOOM)
    GH.amber_grade(on)

    luts = CT.fit_print_grade(os.path.join(OUT, "softlanding_face.png"), ART_IMAGE)
    CT.apply_print_grade(os.path.join(OUT, "softlanding_face.png"), luts)   # face only (control images hold no art)

    P["pivot"].rotation_euler = (0, 0, 0)
    hidden = (colls["SwitchDown"], colls["LedOff"])
    for c in groups: c.hide_render = c in hidden
    for c in hidden: c.hide_viewport = True
    cam.frame(0, 0, W, H)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "softlanding.blend"))
    print("[pedal] done")

if __name__ == "__main__":
    import importlib, pedal_render_common as C
    importlib.reload(C)
    CT.C = C                                    # render_coraltide's helpers use its module-level C ...
    CT.W, CT.H, CT.ART_INSET = W, H, ART_INSET  # ... and its body size (fit_print_grade crops the art inset with them)
    GH.C = C                                    # Golden Haze's amber LED material / grade ...
    GH.LED_D = LED_D                            # ... sized for this lens
    ST = C.Settings(sys.argv)
    main()
