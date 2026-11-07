"""Artec Cool Drive (CDV-1) - exact small-signal AC nodal analysis (diodes off, ideal op-amps).
Reference for the Frostbite DSP. Stage by stage and full chain, as drawn in "Artec - Cool Drive.pdf" (rev 1.1).
Plugin knobs: PRE = 1 - a, POST = 1 - a, GAIN = 1 - g (every knob gives "more" clockwise).
pre/post 'a' = fraction of the 50k SHAPE pot from lug 1 to the wiper; gain 'g' = fraction of the 100k GAIN pot from
lug 1 (R8 side) to the wiper."""
import numpy as np

def mna(elems, amps, src, out, f):
    nodes = sorted({n for e in elems for n in e[:2]} - {'0'})
    idx = {n: i for i, n in enumerate(nodes)}; N = len(nodes); M = N + 1 + len(amps)
    s = 2j * np.pi * f
    A = np.zeros((M, M), complex); b = np.zeros(M, complex)
    for n1, n2, kind, val in elems:
        y = 1 / val if kind == 'R' else s * val
        for p, q, sg in ((n1, n1, 1), (n2, n2, 1), (n1, n2, -1), (n2, n1, -1)):
            if p != '0' and q != '0': A[idx[p], idx[q]] += sg * y
    A[idx[src], N] = 1; A[N, idx[src]] = 1; b[N] = 1
    for k, (p, n, o) in enumerate(amps):
        r = N + 1 + k; A[idx[o], r] += 1
        if p != '0': A[r, idx[p]] += 1
        if n != '0': A[r, idx[n]] -= 1
    return np.linalg.solve(A, b)[idx[out]]

def ic1a(f, mod=False):
    C13 = 4.7e-6 if mod else 100e-9
    e = [('O','N','R',22e3), ('O','N','C',47e-12), ('N','M','R',1.8e3), ('M','0','C',C13), ('I','0','R',1e6)]
    return mna(e, [('I','N','O')], 'I', 'O', f)

def pre(a, f):          # source S = IC1A output, through C16 100n
    P = 50e3; r1 = max(a*P, 1.0); r2 = max((1-a)*P, 1.0)
    e = [('S','A','C',100e-9),
         ('A','F','R',100e3), ('A','B','R',4.7e3), ('A','H','R',100e3),
         ('B','E','R',47e3), ('B','C','C',22e-9), ('B','C','R',100e3),
         ('C','D','R',47e3), ('C','G','R',22e3), ('F','E','C',2.2e-9), ('F','G','R',100e3),
         ('E','G','R',1.8e3), ('H','D','C',4.7e-9), ('H','G','R',2.2e3),
         ('D','W','R',r1), ('W','E','R',r2)]
    return mna(e, [('0','W','G')], 'S', 'G', f)

def gain(g, f):
    P = 100e3; ra = max(g*P, 1.0)
    e = [('O','N','R',2e6), ('O','N','C',47e-12), ('N','M','C',1e-6), ('M','0','R',1.8e3 + ra), ('I','0','R',1e6)]
    return mna(e, [('I','N','O')], 'I', 'O', f)

def post(a, g, f, clipnet=True):   # source S = IC1C output; C2 / R2 / C3 / gain-pot load / C4 / IC1B
    P = 50e3; r1 = max(a*P, 1.0); r2 = max((1-a)*P, 1.0)
    rl = 10e3 + max((1-g)*100e3, 1.0)
    e = [('S','S2','C',1e-6), ('S2','K','R',10e3), ('K','0','C',10e-9), ('K','0','R',rl), ('K','Q','C',1e-6),
         ('Q','T','R',10e3), ('Q','X','R',4.7e3), ('Q','Z','R',100e3),
         ('T','O','R',100e3), ('T','U','C',2.2e-9), ('U','X','R',47e3), ('U','O','R',27e3),
         ('V','O','R',22e3), ('V','X','C',22e-9), ('V','X','R',100e3), ('V','Y','R',47e3),
         ('Y','Z','C',4.7e-9), ('Z','O','R',2.2e3), ('U','W','R',r1), ('W','Y','R',r2)]
    return mna(e, [('0','W','O')], 'S', 'O', f)

def chain(pa, g, qa, f, mod=False):
    return ic1a(f, mod) * pre(pa, f) * gain(g, f) * post(qa, g, f)

def db(x): return 20 * np.log10(abs(x))
FS = [30, 50, 100, 200, 400, 800, 1600, 3200, 6400, 10000]

if __name__ == '__main__':
    hdr = 'Hz      ' + ' '.join(f'{f:6d}' for f in FS)
    print('IC1A stock / mod'); print(hdr)
    for m in (False, True): print(f' mod={m!s:5}', ' '.join(f'{db(ic1a(f, m)):6.1f}' for f in FS))
    print('PRE (C16 incl.)'); print(hdr)
    for a in (0, .25, .5, .75, 1): print(f' a={a:<5}', ' '.join(f'{db(pre(a, f)):6.1f}' for f in FS))
    print('GAIN stage'); print(hdr)
    for g in (0, .1, .25, .5, 1): print(f' g={g:<5}', ' '.join(f'{db(gain(g, f)):6.1f}' for f in FS))
    print('POST incl. clip node (diodes off), g=0.5'); print(hdr)
    for a in (0, .25, .5, .75, 1): print(f' a={a:<5}', ' '.join(f'{db(post(a, .5, f)):6.1f}' for f in FS))
    print('POST a=0.5 vs gain pot (load on clip node)'); print(hdr)
    for g in (0, .5, 1): print(f' g={g:<5}', ' '.join(f'{db(post(.5, g, f)):6.1f}' for f in FS))
