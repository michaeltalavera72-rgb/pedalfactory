# Pedal Factory virtual warehouse

Every component of the Pedal Factory pedal models, the part models they use, the circuit blocks they form, the generic
block types (the shelves for designing pedals of our own), and each pedal as a recipe of blocks. Same idea as Amp
Factory's warehouse (plugin/warehouse), built for how the pedals are coded.

- `PedalFactory_Warehouse.xlsx` -- the browsable workbook: Read me, Pedals, Inventory, Parts, Blocks, Block types, Recipes
- `inventory_<pedal>.csv` -- every component of one pedal; `inventory_all.csv` -- all 14 pedals in one file
- `parts.csv` -- the shelves: part models, parameters, credit, options for the same slot, where they are used
- `blocks.csv` -- each pedal's circuit blocks (type, what it does, component count, active parts)
- `block_types.csv` -- generic building blocks: what each is, how the code builds it today, design notes, who uses it
- `recipes.csv` -- every pedal as an ordered list of blocks (signal order)
- `check_report.txt` -- counts and any problem found while building

The code is the master copy. Rebuild after changing a pedal:

    python3 Tools/Warehouse/build_warehouse.py
    soffice --headless --calc --convert-to xlsx --outdir /tmp/recalc warehouse/PedalFactory_Warehouse.xlsx
    (copy /tmp/recalc/PedalFactory_Warehouse.xlsx back over warehouse/ to store the formula results)

Where the values come from:
- Netlisted networks are parsed straight from Source/Pedals/*.h: every `.R()`, `.C()`, `.opamp()` of the SlowBurn::Mna
  networks and the pedal-local MNA solvers (Slow Burn, Thunderhead, Frostbite, Coral Tide, Soft Landing), plus the
  `OpStage::design(fs, Rf, Cf, Rg, Cg)` stages. Refs come from the trailing comment when the code names them
  (e.g. `// R26, R35, R27`), otherwise `<network>.<R|C|U><index>` = the element's index in that network's vector.
  Tools/Warehouse/shelves.py maps each network to its block and each pot handle (stTt, a3Pres ...) to its control.
- Everything a pedal builds by hand (diodes, JFETs, triodes, swing limits, pots, RC corners, DSP constants) is in
  Tools/Warehouse/curated_parts.csv. Each row has a `check` text that must appear in that pedal's source file; the
  build reports any row whose text is gone (Code check = NOT FOUND) so the warehouse cannot drift from the code silently.
- Tools/Warehouse/library.py holds the blocks, block types and part models.

Model column: NETLIST, OPSTAGE, SOLVED (per-sample Newton), FILTER, CURVE, GAIN, CONTROL, DSP, FIXED.
Provenance: DRAWN, DRAWN-UNSURE, TRACED, PARTS-LIST, DATASHEET, EST-STD, EST-FIT, EST-EAR, USER, OURS.

Next step (as in Amp Factory): turn the block types into a shared library (Source/Blocks/) that new pedals call,
starting with the pieces several pedals already duplicate (Mna, OpStage, one-pole helpers, tapers, diode clippers).
