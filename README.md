# Virtual Pedalboard (JUCE 8, VST3 + Standalone)

## Build on Windows (Visual Studio 2026)
Needs CMake 4.2 or newer (the VS 2026 generator was added in 4.2). VS 2026 ships one;
use the "Developer PowerShell for VS" so it is on the PATH. Check with: cmake --version

    cmake -B build -G "Visual Studio 18 2026" -A x64
    cmake --build build --config Release

The first configure downloads JUCE 8.0.15 from GitHub (needs internet, takes a minute).

Or open the solution in the build folder (VirtualPedalboard.sln or .slnx) in Visual Studio, set "VirtualPedalboard_Standalone"
as the startup project, and press F5.

Outputs: build\VirtualPedalboard_artefacts\Release\
  - Standalone\Virtual Pedalboard.exe
  - VST3\Virtual Pedalboard.vst3   -> copy this folder to C:\Program Files\Common Files\VST3
                                      so your DAW finds it (or set COPY_PLUGIN_AFTER_BUILD TRUE
                                      in CMakeLists.txt and run VS as administrator)

Visual Studio 2022 also works: use -G "Visual Studio 17 2022".

## Audio driver (Standalone)
ASIO is enabled (JUCE_ASIO=1). In the Standalone: Options > Audio/MIDI Settings >
Audio device type: ASIO, then pick your interface's driver. Untick "Mute audio input".
ASIO only lists drivers that are installed (your interface's own driver, or ASIO4ALL /
FlexASIO if you have no interface driver). The VST3 uses whatever driver your DAW uses.
Licensing: the bundled ASIO SDK is GPLv3 (or Steinberg's own license). Fine for personal
use; to sell or give away a closed-source build, sign Steinberg's free ASIO license.

## Using it
Drag a pedal by its title bar to reorder; X removes; "+ Add pedal" adds (pedals are grouped into Tools / Overdrive / Fuzz / Distortion submenus).

## Global section: Semitone Transposer (below the board)
Shifts the guitar up/down in whole semitones (-12..+12) to play in other tunings without retuning.
It sits first in the chain (right after INPUT level), so the drive pedals hear the re-tuned guitar.
Footswitch + LED = on/off (15 ms crossfade), TRANSPOSE (double-click = 0), MIX (100 % = shifted only),
LEVEL (-24..+12 dB), and a LIVE / STUDIO toggle:
- LIVE (default): low-latency splice shifter (Source/Global/Transposer.h, SpliceShifter). Pick attacks
  come through ~3 ms after they're played; sustained notes run ~7 ms behind, chords ~13 ms. 0 st = no delay.
- STUDIO: Signalsmith Stretch phase-vocoder shifter. Smoother on octave-up chords, ~85 ms delay
  (reported to the DAW for compensation). Not for playing live.
Parameters: gx_transpose, gx_mix, gx_level, gx_on, gx_studio (saved with the session).
Test programs: Tools/Analysis/Transposer/ (pitch accuracy, chords, attacks, clicks, latency).

## Adding a pedal
Subclass DriveBase (or PedalBase), return its category from getCategory() (the "+ Add pedal" submenu: Tools, Boost / EQ, Overdrive, Fuzz, Distortion, Modulation), and register it in PedalboardProcessor::makeLayout. New categories go in the PedalCategory enum in PedalBase.h.
A second footswitch (e.g. boost): override secondarySwitchId()/secondarySwitchLabel(), bind
secondaryParam, and handle primaryOn()/secondaryOn() in process(); the face gets two switches + LEDs.
A double-wide pedal: override widthUnits() to return 2. It takes two board slots plus the gap between them
(body 464 x 372 px, FaceLayout::aspectFor(2)); drag-and-drop handles mixed widths. Give it a FaceLayout branch
(keyed on the pedal id) and pass the width to RenderedFace(prefix, 2) so renders of that shape are accepted.
On two-switch pedals primarySwitchLabel() names the left footswitch in its tooltip (default "Drive").
A mini toggle switch (e.g. a voicing switch, never a bypass): set toggleId (a bool parameter, true = up)
and toggleLabel; with 6 knobs the face uses the Slow Burn layout (FaceLayout::make(..., withToggle)).
Optional artwork: write a PedalFace (see Source/Faces/PedalFaces.cpp) and return it from
createFaceFor() for the pedal's id. Without a face the pedal gets the plain generic strip.

## Pedals
- Ramhorn Fuzz (id "bm"): our model of the Ram's Head-era Big Muff Pi circuit,
  with an original face design. Volume / Tone / Sustain, footswitch = bypass.
- Buzzsaw (id "hm"): our model of the 1983 Boss HM-2 "Heavy Metal" circuit, from the service-notes
  schematic. Level / Low / High / Dist, footswitch = bypass. Original blood-red face design.
  COLOR MIX EQ fitted to a nodal analysis of the real network (Tools/Analysis/hm2_colormix.py).
- Rail Bender (id "rb"): our model of the T-Rex Hobo Drive (schematic V1.2.3). Drive / Tone / Level /
  Boost knobs and TWO footswitches: left = Drive on/off, right = Boost on/off (0..+15 dB clean boost
  before the drive; works with the drive off). Germanium-diode overdrive. Original rusty-orange face.
- Valve Howler (id "ts"): our model of the Ibanez TS-808 Tube Screamer. Drive / Tone / Level,
  footswitch = bypass. 1N914 diodes in the op-amp feedback (solved per sample), mid-hump bass cut,
  exact tone-stage transfer function. Original metallic jade-green face with a vacuum-tube emblem.
- Feral Hound (id "ad"): our model of the VFE Alpha Dog v2 (a RAT-style LM308 distortion), traced from
  the stripboard layout in schematics/VFE/Alpha Dog. Gain / Filter / Level (big knobs) and Fat / Soft / Hard
  (small knobs), footswitch = bypass. Soft and Hard have a centre detent (noon = no added clipping;
  double-click = noon): Soft CCW = germanium, CW = MOSFET in the op-amp feedback; Hard CCW = stacked
  silicon, CW = classic single pair to ground. Slew-limited LM308 model. Original matte-black face.
- Slow Burn (id "sb"): built on the Soldano Super Lead Overdrive pedal (the SLO-100 amp in a box), traced from
  the stripboard layout in schematics/Soldano, with a modeled 12AX7 (the SLO-100's preamp valve) in place of
  the pedal's two big op-amp gain stages. Gain / Presence / Volume (big knobs), Bass / Middle / Treble (small),
  DEEP mini toggle (up = full low end), footswitch = bypass. Koren 12AX7 model with grid current and Miller
  capacitance, B+ 250 V; exact SLO tone stack, presence/deep power stage and output network. Small-signal
  voicing matches the real (op-amp) circuit within 0.5 dB from 50 Hz to 6 kHz
  (Tools/Analysis/slo_reference_response.py). Gloss-black face with Michael's candle-and-sun artwork (Assets/slowburn_art.png) and the valve standing
  upright in a window.
- Thunderhead (id "th"): our model of the Diezel VH4 pedal (the VH4 amp's high-gain channel in a box), from
  the schematic in schematics/Diezel (pot values from the PedalPCB Valhalla parts list). Gain / Deep / Volume
  (big knobs), Bass / Middle / Treble / Presence (small), AMP/DIRECT mini toggle (up = the pedal's darker
  "amp input" output, down = the unfiltered "power amp" tap, level-matched), footswitch = bypass. Zener-limited
  x101 stage and a diode gate solved per sample, rail-clipping TL072 stage, Marshall-style stack, presence and
  gyrator "deep" resonance in the last stage. Small-signal response matches the circuit within 0.5 dB from
  30 Hz to 4 kHz (Tools/Analysis/vh4_reference_response.py). Face = Michael's storm artwork (Tools/Art/
  thunderhead_original.png -> make_thunderhead_art.py -> Assets/thunderhead_art.png: painted controls removed,
  labels for the seven knobs and the toggle printed in Bitter), photoreal render via Tools/Blender/render_thunderhead.py.
- Frostbite (id "fb"): our model of the Artec Cool Drive CDV-1 distortion, from the schematic in schematics/Artec
  (rev 1.1). GAIN / PRE / POST / LEVEL knobs and a MOD mini toggle (the two factory mods printed on the schematic:
  C13 100n -> 4u7 for more bass into the gain stage, and a second 1N4148 in series with each clipping diode;
  crossfaded, click-free). TL074 on 9 V: x13 treble-tilted preamp, PRE-SHAPE multi-feedback network (mid-focused
  edge <-> flat), x21..x1112 gain stage clipping at the op-amp swing, then a 1N4148 pair at a node loaded by the
  POST-SHAPE network (warm mids <-> wide / bright), all solved as circuits (SlowBurn::Mna; diode node by Newton,
  4x oversampling). Small-signal response within 0.3 dB of Tools/Analysis/cooldrive.py (30 Hz - 6 kHz).
  Face = Michael's frost artwork (Tools/Art/frostbite_original.png -> make_frostbite_art.py -> Assets/frostbite_art.png:
  painted controls removed by exemplar inpainting, printed labels kept), photoreal render via Tools/Blender/render_frostbite.py.
- Coral Tide (id "ct", double-wide): our model of two Bearfoot / BJFE circuits in one enclosure, from traced
  schematics - the Sea Blue EQ (two JFET stages around a passive treble / bass network) and the Baby Pink
  Booster (one 2N5952 JFET stage). BOOST / LEVEL / LOW / HIGH knobs and the ORDER mini toggle (down = EQ > B,
  up = B > EQ: the boost drives the EQ's JFETs into light grit); footswitches BOOST (left, red LED) and EQ
  (right, blue LED; main switch). With the boost off its JFET stage stays in the path as a near-unity buffer,
  like the original. LEVEL is a master output level (x0..x2.8, unity by default); the EQ's own volume is fixed at
  unity. Every JFET stage is solved as a circuit (square-law JFET + network, Newton per sample, 2x oversampling),
  biased to the traced voltages; small-signal response within ~0.3 dB of Tools/Analysis/bjfe_dual.py. Category
  Boost / EQ. Face = Michael's coral-reef artwork (Tools/Art/coraltide_original.png -> make_coraltide_art.py ->
  Assets/coraltide_art.png: made taller to fit the 464 x 372 body, painted controls and the mock-up's maker's mark
  removed), photoreal render via Tools/Blender/render_coraltide.py.
- Twin Vise (id "tv", double-wide, category Compressor): our model of the Trace Elliot SMX Dual Compressor, a
  two-band bass compressor, from the drawing in schematics/Trace Elliot (SMXDC/1), re-voiced for guitar (band
  split moved up: low-pass 2.36 kHz, high-pass 595 Hz, by capacitor swaps). INPUT GAIN / LOW COMP / HIGH COMP /
  BALANCE / LEVEL knobs, footswitches HI COMP (left: compression on the high band on/off) and EFFECT (right, main),
  plus the original's bicolour INPUT LED (green = signal, red = hot). Each band is an op-amp with an OTA (SSM2024)
  in its feedback path, solved per sample, driven by its own feedback-type side-chain (precision rectifier x67.7,
  RC envelopes as drawn: low 22 ms / 120 ms, high 1 ms / 70 ms). The OTA's gain constant is set by ear (~2:1 at
  noon). 2x oversampling; small-signal response within 0.3 dB of Tools/Analysis/smx_dualcomp.py. Vector face
  until the artwork arrives.
- Soft Landing (id "sl", category Compressor): our model of the Toadworks Mr. Squishy, a discrete JFET
  compressor, from the drawing in schematics/Toadworks (Timm H., Rev 1.0). GAIN / SQUISH / LEVEL. A 2N5457 (Q2) is
  a voltage-controlled shunt between two 0.1u caps ahead of an RC4558 x23 stage; a feedback side-chain (2K1 + 1N914
  into 4u7 || 100k: ~10 ms attack, 0.47 s release) turns it on, and a second 2N5457 (Q1, constant-current source)
  with SQUISH sets its rest bias = the threshold (fully left: close to a clean boost; up: lower ceiling, deeper
  squish). High GAIN thins the lows while compressing, as on the original. 470p GAIN treble bleed (value missing on
  the drawing). The JFET pair is a chosen "good pair" (the drawing has no trimmer). The input / gain cell is solved
  as a circuit per sample (Q2 channel + gate junctions, Newton), 2x oversampling; small-signal response within
  0.03 dB (0.3 dB at 10 kHz) of Tools/Analysis/mr_squishy.py. Face = Michael's sunset-lake artwork
  (Tools/Art/softlanding_original.png -> make_softlanding_art.py -> Assets/softlanding_art.png: painted knobs, LED and
  footswitch removed - sky by a fitted colour field + quilted texture, lake by exemplar inpainting), photoreal render
  via Tools/Blender/render_softlanding.py (ivory scalloped knobs after Michael's Copilot scene script).
- Overdrive, Distortion, Fuzz: generic placeholders.

## Third-party code
- Signalsmith Stretch 1.4.0 and Signalsmith Linear 0.6.4 (MIT license), fetched by CMake. Used by the
  Transposer's STUDIO mode. https://github.com/Signalsmith-Audio/signalsmith-stretch

## Third-party assets
- Assets/AlfaSlabOne-Regular.ttf: Alfa Slab One by JM Solé, SIL Open Font License 1.1
  (Assets/AlfaSlabOne-OFL.txt). Free to embed in the plugin, including commercially.
- Assets/MetalMania-Regular.ttf: Metal Mania, SIL Open Font License 1.1 (Assets/MetalMania-OFL.txt).
- Assets/Rye-Regular.ttf: Rye, SIL Open Font License 1.1 (Assets/Rye-OFL.txt).
- Assets/Poppins-BoldItalic.ttf: Poppins, SIL Open Font License 1.1 (Assets/Poppins-OFL.txt).
- Assets/BlackOpsOne-Regular.ttf: Black Ops One, SIL Open Font License 1.1 (Assets/BlackOpsOne-OFL.txt).
- Tools/Art/fonts/Bitter[wght].ttf: Bitter, SIL Open Font License 1.1 (Tools/Art/fonts/Bitter-OFL.txt). Only used
  to bake Thunderhead's control labels into its artwork (not embedded in the plugin).

## Photoreal pedal graphics (Blender)
Each pedal with artwork has Tools/Blender/render_<pedal>.py (shared code in pedal_render_common.py).
Run from this folder, then rebuild (cmake -B build ... and cmake --build ...):
    & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b -P Tools\Blender\render_buzzsaw.py -- --quick
Drop "-- --quick" for the final render. Without renders a pedal shows a vector fallback face.

Hovering a pedal's name shows which real pedal it is based on (PedalBase::getBasedOn()).

## Status
Built and played on Windows (VS 2026, JUCE 8.0.15). Each new pedal is also build-tested on Linux
with headless audio tests before delivery.
