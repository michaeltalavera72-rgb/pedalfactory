"""Trace Elliot SMX Dual Compressor - small-signal reference for the Twin Vise DSP (claude/smx-dualcomp-trace.md).
Drawing SMXDC/1 (18.3.93). Ideal op-amps, VCAs at a fixed transconductance (no side-chain = idle / uncompressed).

Guitar voicing (Michael, 2026-11-07: "for guitar only"): the band-split filters are moved up x~2.65 with E-series
capacitor swaps, as a guitar mod of the bass original would be:
    low-pass  C5 22n -> 8n2, C6 33n -> 12n    (868 Hz -> 2.36 kHz)
    high-pass C7 = C8 47n -> 18n              (228 Hz -> 595 Hz)
Everything else as drawn.

Knobs (plugin 0..1): GAIN g = INPUT GAIN wiper; LOW p = LOW COMPRESSION (dual: drive gang + side-chain gang, CW = more);
HIGH h = HIGH COMPRESSION (control-voltage attenuator); BAL b = E.Q. BALANCE (gangs opposite: low = 1 - b, high = b);
LEVEL v = OUTPUT LEVEL.
"""
import numpy as np

def par(*r): return 1.0 / sum(1.0 / x for x in r)

GUITAR = dict(C5=8.2e-9, C6=12e-9, C7=18e-9, C8=18e-9)
BASS   = dict(C5=22e-9, C6=33e-9, C7=47e-9, C8=47e-9)

def sk_lp(f, C5, C6, R=6.8e3):      # R19 = R20 = 6k8, C6 from the R19/R20 node to the output, C5 to 0 V
    s = 2j * np.pi * f
    return 1.0 / (s * s * R * R * C5 * C6 + s * 2 * R * C5 + 1.0)

def sk_hp(f, C7, C8, R24=10e3, R23=22e3):   # C7, C8 in series, R24 from the junction to the output, R23 to 0 V
    s = 2j * np.pi * f
    return (s * s * R24 * R23 * C7 * C8) / (s * s * R24 * R23 * C7 * C8 + s * R24 * (C7 + C8) + 1.0)

def input_gain(g, f):
    """IN -> IC1A (x5.68, 33p) -> INPUT GAIN pot (loaded by R12 22k into IC1B's virtual earth) -> IC1B (100k || 100p)."""
    s = 2j * np.pi * f
    a = 1.0 + 220e3 / (47e3 * (1.0 + s * 220e3 * 33e-12))
    P = 50e3; r1 = max((1 - g) * P, 1.0); r2 = max(g * P, 1.0)
    vth = r2 / (r1 + r2); rth = par(r1, r2)
    return -a * vth * (100e3 / (1.0 + s * 100e3 * 100e-12)) / (22e3 + rth)

def low_drive(p):
    """Voltage at node X / IC1D out. R21 10k -> A; A -(R10 220k || upper pot)- X; A -(lower pot + R22 10k)- 0 V;
    X loaded by R9 10k and R27 10k (into IC2A's virtual earth)."""
    P = 50e3; up = max((1 - p) * P, 0.1); lo = p * P
    zx = par(10e3, 10e3)
    if up < 1.0:
        za = par(zx, lo + 10e3)
        return za / (10e3 + za)
    rax = par(220e3, up)
    za = par(rax + zx, lo + 10e3)
    va = za / (10e3 + za)
    return va * zx / (rax + zx)

def low_stage(gmv=0.0, f=0.0):   # IC2A: -(1/R27) / (1/220k + s 33p + gm/151); gain from node X (current via R27 10k)
    return -(1.0 / 10e3) / (1.0 / 220e3 + 2j * np.pi * f * 33e-12 + gmv / 151.0)

def high_stage(gmv=0.0, f=0.0):  # IC2D: R34 47k in, 220k || 33p || VCA2 feedback
    return -(1.0 / 47e3) / (1.0 / 220e3 + 2j * np.pi * f * 33e-12 + gmv / 151.0)

def balance(x):             # 50k pot fraction x, wiper loaded by 33k into the mixer's virtual earth
    P = 50e3; r1 = max((1 - x) * P, 1.0); r2 = max(x * P, 1.0)
    rl = par(r2, 33e3)
    return rl / (r1 + rl)

def bands(f, g=0.5, p=0.5, b=0.5, v=0.5, caps=GUITAR, gm_low=0.0, gm_high=0.0):
    """Returns (low, high, total) complex transfer from IN to OUT (before the plugin's output scaling)."""
    ig = input_gain(g, f)
    low = ig * sk_lp(f, caps['C5'], caps['C6']) * low_drive(p) * low_stage(gm_low, f) * balance(1 - b)
    high = ig * sk_hp(f, caps['C7'], caps['C8']) * (-1.0) * high_stage(gm_high, f) * balance(b)
    mix = -47e3 / 33e3
    out = 50e3 * v / 97e3            # R58 47k into the OUTPUT LEVEL pot
    return low * mix * out, high * mix * out, (low + high) * mix * out

def db(x): return 20 * np.log10(np.abs(x))
FS = [50, 100, 200, 400, 800, 1600, 3200, 6400, 10000]

if __name__ == '__main__':
    print('low_drive(p):', ' '.join(f'{p:.2f}:{low_drive(p):.4f}' for p in np.linspace(0, 1, 5)))
    print('idle low gain x22 * drive (dB):', ' '.join(f'{db(22 * low_drive(p)):+.1f}' for p in np.linspace(0, 1, 5)))
    print('idle high gain x4.68 = %+.1f dB' % db(220 / 47))
    print('\nGUITAR idle, total IN -> OUT (dB) for knob combos (g, p, b, v) - compare with the plugin minus 6.02 dB (x0.5 V in):')
    for combo in ((.5, .5, .5, .5), (.5, 0, .5, .5), (.5, 1, .5, .5), (.5, .5, 0, .5), (.5, .5, 1, .5), (1, .5, .5, 1)):
        print('  ', combo, ' '.join(f'{db(bands(f, *combo)[2]):+6.2f}' for f in FS))
    for name, caps in (('BASS (as drawn)', BASS), ('GUITAR', GUITAR)):
        print(f'\n{name}: idle (uncompressed), all knobs noon')
        for f in FS:
            lo, hi, t = bands(f, caps=caps)
            print(f'  {f:6d} Hz  low {db(lo):+6.1f}  high {db(hi):+6.1f}  sum {db(t):+6.1f}')
        # equal-gain bands: shows the crossover shape alone
        print(f'{name}: bands normalised to equal mid-band gain (crossover shape)')
        for f in FS + [20000]:
            lo = sk_lp(f, caps['C5'], caps['C6']); hi = sk_hp(f, caps['C7'], caps['C8'])
            print(f'  {f:6d} Hz  LP-HP {db(lo - hi):+6.1f}  LP+HP {db(lo + hi):+6.1f}')
