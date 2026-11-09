# Pedal Factory — Handoff

Last updated: 2026-11-08 (after befe369, virtual warehouse). Read this first when starting a new session.
Deeper history lives in the claude.ai project "Pedal Facory" (claude/project-summary*.md, per-pedal plan/trace docs).

## Where things are
- Working copy (source of truth): `C:\Dev\pedalfactory-master` on Michael's PC. Before overwriting a file, compare it
  with git HEAD / the last version written — Michael edits too.
- GitHub (public): https://github.com/michaeltalavera72-rgb/pedalfactory, branch `main`.
  Michael pushes from Windows (`git push origin main`); cloud sessions and the device shell cannot push.
- Build: JUCE 8.0.15 + Signalsmith Stretch 1.4.0 (FetchContent), VS 2026, CMake 4.2+. See README.md.
- Graphics: Blender 5.2 (`Tools/Blender/render_<pedal>.py`, shared `pedal_render_common.py`), CPU renders.
- Line endings: README.md, PedalBase.h and PluginEditor.cpp are CRLF; keep each file's style.

## State of main (2026-11-08)
| Commit  | What |
|---------|------|
| befe369 | Virtual warehouse: component inventory, parts, blocks, block types, recipes for all 14 pedals |
| 70da2c8 | Soft Landing (Toadworks Mr. Squishy) JFET compressor, artwork face + full render |
| 95f0a11 | Twin Vise (Trace Elliot SMX Dual Compressor), double-wide, Compressor category, bicolour meter LED |
| 39b6d89 | Frostbite (Artec Cool Drive) distortion |
| 8feda1e | Coral Tide (BJFE Sea Blue EQ + Baby Pink Booster), double-wide support, Boost / EQ category |
| fd30740 | Night Courier (EQD Dispatch Master) delay + reverb, Modulation category, 3-way toggle |
| 31b9da6 | Golden Haze (EQD Acapulco Gold) |
| 5786bae | Board presets (save/load pedals, order, settings, transposer) |

## Pedals (14 modeled + 3 generic placeholders)
| Pedal | id | Based on | Category | Sound judged? |
|-------|----|----------|----------|---------------|
| Gatekeeper | ng | Boss NF-1 (keyed gate) | Tools | Approved |
| Coral Tide | ct | BJFE Sea Blue EQ + Baby Pink Booster (double-wide) | Boost / EQ | Not yet |
| Twin Vise | tv | Trace Elliot SMX Dual Compressor (double-wide) | Compressor | Not yet |
| Soft Landing | sl | Toadworks Mr. Squishy | Compressor | Not yet |
| Valve Howler | ts | Ibanez TS-808 | Overdrive | Not yet |
| Rail Bender | rb | T-Rex Hobo Drive | Overdrive | — |
| Slow Burn | sb | Soldano SLO pedal + modeled 12AX7 | Overdrive | Not yet |
| Thunderhead | th | Diezel VH4 pedal | Distortion | Not yet (face approved) |
| Feral Hound | ad | VFE Alpha Dog (RAT-style) | Distortion | Not yet |
| Golden Haze | gh | EQD Acapulco Gold | Distortion | — |
| Frostbite | fb | Artec Cool Drive | Distortion | Not yet |
| Ramhorn Fuzz | bm | EHX Big Muff (Ram's Head) | Fuzz | Not tuned by ear |
| Buzzsaw | hm | Boss HM-2 | Distortion | Approved |
| Night Courier | nc | EQD Dispatch Master | Modulation | — |

Global section: Semitone Transposer (LIVE splice shifter / STUDIO Signalsmith), approved by ear.

## Virtual warehouse (newest work)
- `warehouse/`: PedalFactory_Warehouse.xlsx + CSVs — 486 components, 14 pedals, 81 blocks, 30 generic block types,
  38 part models. Rebuild with `Tools/Warehouse/build_warehouse.py` (parses Source/Pedals/*.h; curated parts in
  `Tools/Warehouse/curated_parts.csv`, each with a "check" string that must appear in the pedal source).
- Next step (as in Amp Factory): turn the block types into a shared library (`Source/Blocks/`) for pedals of our own design.

## Open items
- Soft Landing: the 470 pF GAIN treble bleed is a guess (value missing on the drawing) — may be too bright; ask Michael.
- Sounds still to be judged by Michael: Soft Landing, Twin Vise, Coral Tide, Frostbite, Thunderhead, Slow Burn,
  Feral Hound, Valve Howler; tune Ramhorn Fuzz by ear. Transposer STUDIO mode not yet judged.
- Generic Overdrive / Distortion / Fuzz placeholders: replace as real models arrive.
- Optional: Gatekeeper "gate open" LED; Transposer panel Blender render; odd knob-frame count (exact noon);
  allow the same pedal type more than once?; git rm unused Cinzel Decorative font.
- Later: more global features, overall board look, installer.

## Ground rules
- Original pedal names and face designs; real pedal named only in the "based on" tooltip / docs (not-affiliated note).
- Every pedal implements getBasedOn() and describe() for every control; voiced for guitar only.
- Pedal body 226 x 372 px (double-wide 464 x 372); photoreal Blender render for every pedal.
- No animated pedal faces — CPU and memory are reserved for audio.
- Per-pedal routine: schematic -> name + Michael's Copilot artwork -> DSP module + register in makeLayout ->
  Python reference analysis (Tools/Analysis) within ~0.5 dB -> art script (Tools/Art) -> render script -> quick renders
  -> full render -> Windows build -> Michael judges the sound.
