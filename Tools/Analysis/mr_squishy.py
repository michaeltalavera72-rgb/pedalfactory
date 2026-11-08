"""Toadworks Mr. Squishy - small-signal reference for the Soft Landing DSP (claude/mr-squishy-trace.md,
claude/soft-landing-plan.md). Drawing: Timm H., Rev 1.0, 3/26/2012.

Idle (no side-chain: Vc = 0) AC response from the input jack to the output, with Q2 linearised at its rest bias
(Vds = 0: a conductance 2 k a(Vgs) between B and N, gate network loading B). Ideal op-amp, ideal source.
Same behavioural constants as Source/Pedals/SoftLanding.h ("good pair" JFETs, SQUISH travel, 470p bleed, levelScale).

Knobs (plugin 0..1): g = GAIN (A500K, lug 3 = input, lug 1 = 0 V), q = SQUISH (CW = wiper toward lug 1, the knob
covers the first `travel` of the rotation from lug 3), v = LEVEL (A10K).
"""
import numpy as np

def par(*r): return 1.0 / sum(1.0 / x for x in r)
def a_taper(x): return (81.0 ** x - 1.0) / 80.0

Q1_IDSS, Q1_VP = 3.3e-3, -3.0
Q2_IDSS, Q2_VP = 1.0e-3, -0.5
PINCH_SOFT, VN_END = 0.03, 0.15
BLEED = 470e-12
LEVEL_SCALE = 5.9

def q1_current():
    lo, hi = 0.0, abs(Q1_VP) / 6.2e3
    for _ in range(80):
        i = 0.5 * (lo + hi)
        if i - Q1_IDSS * (1 - i * 6.2e3 / abs(Q1_VP)) ** 2 > 0: hi = i
        else: lo = i
    return 0.5 * (lo + hi)

def squish_rn(rot):
    r1 = max(1.0, 10e3 * a_taper(1.0 - rot))
    return par(20e3, r1 * (32e3 - r1) / 32e3)       # R1 || (10k - R1 + 22k), || 20K

I1 = q1_current()
def travel():
    lo, hi = 0.0, 1.0
    for _ in range(60):
        m = 0.5 * (lo + hi)
        if I1 * squish_rn(m) > abs(Q2_VP) + VN_END: lo = m
        else: hi = m
    return lo
TRAVEL = travel()

def v_n(q): return I1 * squish_rn(TRAVEL * q)

def q2_conductance(q):
    u = (-v_n(q) - Q2_VP) / PINCH_SOFT
    a = PINCH_SOFT * np.log1p(np.exp(u))
    sl = 1.0 / (1.0 + np.exp(-u))                     # d a / d vgs (softplus slope)
    k = Q2_IDSS / Q2_VP ** 2
    return 2 * k * a * sl                             # d Ich / d vDS at vDS = 0

def response(f, g, q, v=0.5):
    """Complex gain input -> output (digital full scale = 0.5 V in, LEVEL wiper x LEVEL_SCALE out)."""
    s = 2j * np.pi * f
    rb = max(1.0, 500e3 * a_taper(g)); rt = max(1.0, 500e3 - rb)
    gq = q2_conductance(q)
    # nodes: A, B, M, G, N, P   (input = 1 V, Vc = AC ground)
    Y = np.zeros((6, 6), complex); I = np.zeros(6, complex)
    def y(a, b, val):
        if a is not None: Y[a, a] += val
        if b is not None: Y[b, b] += val
        if a is not None and b is not None: Y[a, b] -= val; Y[b, a] -= val
    A, B, M, G, N, P = range(6)
    y(A, None, 1 / rt + s * BLEED); I[A] += 1 / rt + s * BLEED      # from the input (1 V)
    y(A, None, 1 / rb + 1 / 4.7e6)
    y(A, B, s * 0.1e-6)
    y(B, M, s * 560e-12); y(M, G, 1 / 470e3); y(G, None, 1 / 470e3)
    y(B, N, gq + 1e-12)
    y(N, None, 1 / squish_rn(TRAVEL * q) + s * 4.7e-6)
    y(B, P, s * 0.1e-6); y(P, None, 1 / 235e3)
    vp = np.linalg.solve(Y, I)[P]
    amp = (1 + s * 230e3 * 4.7e-6) / (1 + s * 10e3 * 4.7e-6)
    cc = 10e3 / (10e3 + 1 / (s * 4.7e-6))
    return 0.5 * vp * amp * cc * a_taper(v) * LEVEL_SCALE

if __name__ == "__main__":
    import sys
    g = float(sys.argv[1]) if len(sys.argv) > 1 else 0.8
    q = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
    v = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    print(f"I_Q1 {I1*1e3:.3f} mA, travel {TRAVEL:.3f}, V_N {v_n(q):.3f} V")
    for f in [50, 100, 200, 400, 800, 1600, 3200, 6400, 10000]:
        print(f"{f:7d} Hz  {20*np.log10(abs(response(f, g, q, v))):8.3f} dB")
