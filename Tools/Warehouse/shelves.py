"""Pedal Factory warehouse: the hand-kept shelves (pedals, blocks, block types, part models).
The component values themselves come from the code (netlists parsed by build_warehouse.py) or from
curated_parts.csv, whose every row is checked against the pedal's source file."""

# id -> (name, source file, category, based on, width units)
PEDALS = {
    'bm': ('Ramhorn Fuzz', 'RamhornFuzz.h', 'Fuzz', "Electro-Harmonix Big Muff Pi (Ram's Head era)", 1),
    'hm': ('Buzzsaw',      'Buzzsaw.h',     'Distortion', 'Boss HM-2 Heavy Metal (1983)', 1),
    'rb': ('Rail Bender',  'RailBender.h',  'Overdrive', 'T-Rex Hobo Drive (V1.2.3)', 1),
    'ts': ('Valve Howler', 'ValveHowler.h', 'Overdrive', 'Ibanez TS-808 Tube Screamer', 1),
    'ad': ('Feral Hound',  'FeralHound.h',  'Distortion', 'VFE Alpha Dog v2 (RAT-style)', 1),
    'sb': ('Slow Burn',    'SlowBurn.h',    'Distortion', 'Soldano SLO pedal, with a 12AX7 in the gain stages', 1),
    'ng': ('Gatekeeper',   'Gatekeeper.h',  'Tools', 'Boss NF-1 Noise Gate (keyed loop)', 1),
    'th': ('Thunderhead',  'Thunderhead.h', 'Distortion', 'Diezel VH4 pedal', 1),
    'gh': ('Golden Haze',  'GoldenHaze.h',  'Distortion', 'EarthQuaker Devices Acapulco Gold', 1),
    'nc': ('Night Courier','NightCourier.h','Modulation', 'EarthQuaker Devices Dispatch Master', 1),
    'ct': ('Coral Tide',   'CoralTide.h',   'Boost / EQ', 'BJFE Sea Blue EQ + Baby Pink Booster', 2),
    'fb': ('Frostbite',    'Frostbite.h',   'Distortion', 'Artec Cool Drive CDV-1', 1),
    'tv': ('Twin Vise',    'TwinVise.h',    'Compressor', 'Trace Elliot SMX Dual Compressor (guitar voicing)', 2),
    'sl': ('Soft Landing', 'SoftLanding.h', 'Compressor', 'Toadworks Mr. Squishy', 1),
}

# Netlisted networks (SlowBurn::Mna and the pedal-local MNA variants): (file, network member) -> (block id, numbered nodes)
NETS = {
    ('SlowBurn.h', 'inNet'): ('SB-INTER', {1: 'BUF_OUT'}),
    ('SlowBurn.h', 'stack'): ('SB-STACK', {1: 'CF_OUT'}),
    ('SlowBurn.h', 'power'): ('SB-POWER', {1: 'IC3B+'}),
    ('SlowBurn.h', 'out'):   ('SB-OUT',   {1: 'PWR_OUT'}),
    ('Thunderhead.h', 'in'):    ('TH-INPUT', {1: 'IN'}),
    ('Thunderhead.h', 'inter'): ('TH-INTER', {1: 'IC1B_OUT'}),
    ('Thunderhead.h', 'stack'): ('TH-STACK', {1: 'IC2B_OUT'}),
    ('Thunderhead.h', 'amp3'):  ('TH-AMP3',  {1: 'IC3A+'}),
    ('Thunderhead.h', 'out'):   ('TH-OUT',   {1: 'IC3A_OUT'}),
    ('Frostbite.h', 'preNet'):  ('FB-PRE',   {1: 'IC1A_OUT'}),
    ('Frostbite.h', 'postNet'): ('FB-POST',  {1: 'IC1C_OUT'}),
    ('CoralTide.h', 'a'): ('CT-Q1EQ', {1: 'V+', 2: 'IN'}),
    ('CoralTide.h', 'b'): ('CT-Q2',   {1: 'V+', 2: 'EQ_OUT'}),
    ('CoralTide.h', 'p'): ('CT-BOOST',{1: 'V+', 2: 'IN'}),
    ('SoftLanding.h', 'cell'): ('SL-CELL', {}),
    ('SoftLanding.h', 'det'):  ('SL-DET', {}),
}
# OpStage::design(fs, Rf, Cf, Rg, Cg) calls (non-inverting op-amp stage, exact bilinear) -> block id
OPSTAGES = {
    ('Thunderhead.h', 'ic1b'): 'TH-IC1B', ('Thunderhead.h', 'ic2b'): 'TH-IC2B',
    ('Frostbite.h', 'ic1a'): 'FB-IC1A', ('Frostbite.h', 'ic1aMod'): 'FB-IC1A',
}
# netlist element handles -> the control they belong to
HANDLES = {
    'inRa': 'GAIN (A500k) lug3-wiper', 'inRb': 'GAIN (A500k) wiper-lug1',
    'stTt': 'TREBLE upper half', 'stTb': 'TREBLE lower half', 'stBass': 'BASS', 'stMt': 'MID upper', 'stMb': 'MID lower',
    'stMid': 'MIDDLE (rheostat)', 'pwDeep': 'DEEP toggle', 'pwPres': 'PRESENCE (C25k rheostat)',
    'intGain': 'GAIN (A250k rheostat)', 'a3Deep': 'DEEP (C25k)', 'a3Pres': 'PRESENCE (C25k)',
    'outVa': 'VOLUME (A10k) upper', 'outVb': 'VOLUME (A10k) lower',
    'preR1': 'PRE-SHAPE lug1-wiper', 'preR2': 'PRE-SHAPE wiper-lug2', 'postRl': 'GAIN pot (wiper-lug2) + R7',
    'postR1': 'POST-SHAPE lug1-wiper', 'postR2': 'POST-SHAPE wiper-lug2',
    'c.rTrebUp': 'HIGH (TREBLE 50kB) upper', 'c.rTrebDn': 'HIGH (TREBLE 50kB) lower',
    'c.rBassUp': 'LOW (BASS 50kA) upper', 'c.rBassDn': 'LOW (BASS 50kA) lower', 'c.rBoost': 'BOOST (50kC) + 150k bypass leg',
    's.rTop': 'GAIN (A500K) upper', 's.rBot': 'GAIN (A500K) lower', 's.rN': 'SQUISH network (20K || A10K + 22K), set from the knob',
}
