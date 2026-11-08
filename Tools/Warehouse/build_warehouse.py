#!/usr/bin/env python3
"""Pedal Factory virtual warehouse (Michael 2026-11-08: an inventory of components like Amp Factory's, to build
pedals of our own design from it).

Every component of every pedal model, the part models they use, the circuit blocks they form, the generic block
types (the shelves for new designs) and each pedal as a recipe of blocks.

Sources (the code is the master copy):
  Source/Pedals/*.h              netlisted networks (Mna .R / .C / .opamp calls, OpStage::design) are parsed directly
  Tools/Warehouse/curated_parts.csv  the parts the code builds by hand (diodes, JFETs, triodes, pots, RC corners ...);
                                 every row carries a "check" text that must appear in that pedal's source file
  Tools/Warehouse/shelves.py     pedals, netlist -> block map, pot handles
  Tools/Warehouse/library.py     blocks, block types, part models
Outputs (warehouse/): inventory_<pedal>.csv, inventory_all.csv, parts.csv, blocks.csv, block_types.csv, recipes.csv,
  PedalFactory_Warehouse.xlsx, check_report.txt
Usage: python3 Tools/Warehouse/build_warehouse.py      (then recalculate the workbook: see warehouse/README.md)
"""
import csv, os, re, sys, collections

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, here)
from shelves import PEDALS, NETS, OPSTAGES, HANDLES
from library import BLOCKS, BLOCK_TYPES, PARTS

src_dir = os.path.join(root, 'Source', 'Pedals')
out = os.path.join(root, 'warehouse')
os.makedirs(out, exist_ok=True)

NET_PROV = {'sb': 'TRACED', 'th': 'DRAWN', 'fb': 'DRAWN', 'ct': 'DRAWN', 'sl': 'DRAWN'}
file_to_pedal = {v[1]: k for k, v in PEDALS.items()}
block_info = {b[0]: b for b in BLOCKS}
problems = []

def eng(x, kind):
    if kind == 'R':
        for div, u in ((1e6, 'M'), (1e3, 'k')):
            if abs(x) >= div * 0.9999: return ('%g' % (x / div)) + u
        return ('%g' % x) + 'R'
    for div, u in ((1e-6, 'u'), (1e-9, 'n'), (1e-12, 'p')):
        if abs(x) >= div * 0.9999: return ('%g' % (x / div)) + u
    return '%g' % x

def split_args(s):
    args, depth, cur = [], 0, ''
    for ch in s:
        if ch == ',' and depth == 0: args.append(cur.strip()); cur = ''; continue
        depth += ch == '('; depth -= ch == ')'; cur += ch
    if cur.strip(): args.append(cur.strip())
    return args

const_re = re.compile(r'\b(\w+)\s*=\s*([-+]?\d+(?:\.\d*)?(?:e[-+]?\d+)?)\b')
net_re = re.compile(r'auto&\s+(\w+)\s*=\s*(?:c|s)\.(\w+)\s*;')
elem_re = re.compile(r'(?:([\w.]+)\s*=\s*)?\b(\w+)\.(R|C|opamp)\s*\(')
design_re = re.compile(r'\bc\.(\w+)\.design\s*\(')

def parse_netlists(fname):
    pid = file_to_pedal[fname]
    lines = open(os.path.join(src_dir, fname), encoding='utf-8').read().split('\n')
    consts = {}
    for ln in lines:
        if 'constexpr' in ln:
            for k, v in const_re.findall(ln.split('//')[0]): consts.setdefault(k, float(v))
    def val(expr):
        try: return float(eval(expr, {'__builtins__': {}}, consts))
        except Exception: return None
    rows, var2net, counters = [], {}, collections.Counter()
    for no, ln in enumerate(lines, 1):
        code, _, comment = ln.partition('//')
        m = net_re.search(code)
        if m: var2net[m.group(1)] = m.group(2)
        found = []
        for m in elem_re.finditer(code):
            handle, var, kind = m.group(1), m.group(2), m.group(3)
            if var not in var2net or (fname, var2net[var]) not in NETS: continue
            i, depth = m.end(), 1
            while depth and i < len(code): depth += code[i] == '('; depth -= code[i] == ')'; i += 1
            found.append((handle, var2net[var], kind, split_args(code[m.end():i - 1])))
        refs = [t.strip() for t in comment.split(',')] if comment else []
        use_refs = len(refs) >= len(found) and found and all(re.fullmatch(r'[A-Z]+\d+', t) for t in refs[:len(found)])
        for k, (handle, net, kind, args) in enumerate(found):
            bid, numbered = NETS[(fname, net)]
            def node(a):
                a = a.strip()
                if a == '0': return '0'
                if a.isdigit(): return numbered.get(int(a), 'N' + a)
                return a
            if kind == 'opamp':
                idx = counters[(net, 'U')]; counters[(net, 'U')] += 1
                rows.append(dict(Pedal=PEDALS[pid][0], Block=block_info[bid][2], **{'Block ID': bid}, Ref='%s.U%d' % (net, idx),
                                 Kind='OPAMP', Value='ideal (nullor)', Nodes='+%s -%s o%s' % tuple(node(a) for a in args),
                                 Control='', **{'Part model': 'OPAMP-IDEAL'}, Provenance=NET_PROV[pid], Model='NETLIST',
                                 Function=comment.strip(), Source='%s:%d' % (fname, no), **{'Code check': 'netlist'}))
                continue
            idx = counters[(net, kind)]; counters[(net, kind)] += 1
            v = val(args[2])
            if v is None: problems.append('%s:%d value not evaluable: %s' % (fname, no, args[2])); continue
            ref = refs[k] if use_refs else '%s.%s%d' % (net, kind, idx)
            func = '' if use_refs else comment.strip()
            rows.append(dict(Pedal=PEDALS[pid][0], Block=block_info[bid][2], **{'Block ID': bid}, Ref=ref, Kind=kind,
                             Value=eng(v, kind), Nodes='%s %s' % (node(args[0]), node(args[1])),
                             Control=HANDLES.get(handle, handle or ''), **{'Part model': 'RES-IDEAL' if kind == 'R' else 'CAP-IDEAL'},
                             Provenance=NET_PROV[pid], Model='NETLIST', Function=func,
                             Source='%s:%d' % (fname, no), **{'Code check': 'netlist'}))
        for m in design_re.finditer(code):
            st = m.group(1)
            if (fname, st) not in OPSTAGES: continue
            i, depth = m.end(), 1
            while depth and i < len(code): depth += code[i] == '('; depth -= code[i] == ')'; i += 1
            args = split_args(code[m.end():i - 1])
            vals = [val(a) for a in args[1:5]]
            if len(args) != 5 or None in vals:
                problems.append('%s:%d OpStage %s not evaluable (curated instead)' % (fname, no, st)); continue
            bid = OPSTAGES[(fname, st)]
            for ref, kind, v, fn in zip(('Rf', 'Cf', 'Rg', 'Cg'), 'RCRC', vals, ('feedback', 'feedback cap', 'ground leg', 'ground-leg cap')):
                rows.append(dict(Pedal=PEDALS[pid][0], Block=block_info[bid][2], **{'Block ID': bid},
                                 Ref='%s.%s' % (st, ref), Kind=kind, Value=eng(v, kind), Nodes='',
                                 Control='MOD position' if st.endswith('Mod') else '',
                                 **{'Part model': 'RES-IDEAL' if kind == 'R' else 'CAP-IDEAL'}, Provenance=NET_PROV[pid],
                                 Model='OPSTAGE', Function='%s (%s)' % (fn, comment.strip() or 'OpStage::design'),
                                 Source='%s:%d' % (fname, no), **{'Code check': 'netlist'}))
    return rows

inventory = collections.defaultdict(list)
for fname in sorted({f for f, _ in NETS}):
    for r in parse_netlists(fname): inventory[file_to_pedal[fname]].append(r)

src_text = {pid: open(os.path.join(src_dir, p[1]), encoding='utf-8').read() for pid, p in PEDALS.items()}
for r in csv.DictReader(open(os.path.join(here, 'curated_parts.csv'), encoding='utf-8')):
    pid = r['pedal']
    if pid not in PEDALS: problems.append('curated: unknown pedal ' + pid); continue
    if r['block'] not in block_info: problems.append('curated: unknown block %s (%s)' % (r['block'], r['ref'])); continue
    if r['part'] not in PARTS: problems.append('curated: unknown part model %s (%s %s)' % (r['part'], pid, r['ref']))
    hit = [t for t in r['check'].split('|') if t and t in src_text[pid]]
    if not hit: problems.append('curated: check text not found in %s: %s %s -> "%s"' % (PEDALS[pid][1], pid, r['ref'], r['check']))
    line = next((i for i, ln in enumerate(src_text[pid].split('\n'), 1) if hit and hit[0] in ln), '')
    inventory[pid].append(dict(Pedal=PEDALS[pid][0], Block=block_info[r['block']][2], **{'Block ID': r['block']}, Ref=r['ref'],
                               Kind=r['kind'], Value=r['value'], Nodes=r['nodes'], Control=r['control'],
                               **{'Part model': r['part']}, Provenance=r['prov'], Model=r['model'], Function=r['function'],
                               Source='%s:%s' % (PEDALS[pid][1], line), **{'Code check': 'found' if hit else 'NOT FOUND'}))

# order every pedal's rows by recipe order of its blocks
order = {b[0]: i for i, b in enumerate(BLOCKS)}
COLS = ['Pedal', 'Block', 'Block ID', 'Ref', 'Kind', 'Value', 'Nodes', 'Control', 'Part model', 'Provenance', 'Model',
        'Function', 'Source', 'Code check']
all_rows = []
for pid in PEDALS:
    rows = sorted(inventory[pid], key=lambda r: order[r['Block ID']])
    all_rows += rows
    slug = PEDALS[pid][0].lower().replace(' ', '_')
    with open(os.path.join(out, 'inventory_%s.csv' % slug), 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, COLS); w.writeheader(); w.writerows(rows)
with open(os.path.join(out, 'inventory_all.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, COLS); w.writeheader(); w.writerows(all_rows)

for b in BLOCKS:
    if b[3] not in BLOCK_TYPES: problems.append('block %s: unknown type %s' % (b[0], b[3]))
    if not any(r['Block ID'] == b[0] for r in all_rows): problems.append('block %s has no components' % b[0])

# parts
use = collections.defaultdict(collections.Counter)
for r in all_rows: use[r['Part model']][r['Pedal']] += 1
part_rows = [[pid, *PARTS[pid], sum(use[pid].values()), ', '.join(use[pid])] for pid in PARTS]
for pm in use:
    if pm not in PARTS: problems.append('part model used but not on a shelf: ' + pm)
# blocks
comp = collections.Counter(r['Block ID'] for r in all_rows)
blk_rows = [[b[0], PEDALS[b[1]][0], b[2], b[3], BLOCK_TYPES[b[3]][0], b[4], comp[b[0]],
             ' '.join(sorted({r['Ref'] for r in all_rows if r['Block ID'] == b[0] and r['Kind'] in
                              ('OPAMP', 'DIODE', 'ZENER', 'JFET', 'MOSFET', 'BJT', 'TRIODE', 'IC')}))] for b in BLOCKS]
# block types
type_rows = []
for t, (name, what, how, notes) in BLOCK_TYPES.items():
    bs = [b for b in BLOCKS if b[3] == t]
    type_rows.append([t, name, what, how, notes, len(bs), ', '.join(sorted({PEDALS[b[1]][0] for b in bs})), ' '.join(b[0] for b in bs)])
# recipes
rec_rows, step = [], collections.Counter()
for b in BLOCKS:
    step[b[1]] += 1
    rec_rows.append([PEDALS[b[1]][0], PEDALS[b[1]][3], step[b[1]], b[0], b[2], b[3]])

def write(name, header, rows):
    with open(os.path.join(out, name), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f); w.writerow(header); w.writerows(rows)
PART_H = ['Part ID', 'Category', 'Name', 'Model / parameters', 'Provenance / credit', 'Options for the same slot', 'Instances', 'Used in']
BLK_H = ['Block ID', 'Pedal', 'Block', 'Type', 'Type name', 'What it does', 'Components', 'Active parts']
TYPE_H = ['Type', 'Name', 'What it is', 'How the code builds it', 'Design notes', 'Blocks', 'Pedals', 'Block IDs']
REC_H = ['Pedal', 'Based on', 'Step', 'Block ID', 'Block', 'Type']
write('parts.csv', PART_H, part_rows); write('blocks.csv', BLK_H, blk_rows)
write('block_types.csv', TYPE_H, type_rows); write('recipes.csv', REC_H, rec_rows)

# ---------------- workbook
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.utils import get_column_letter
wb = Workbook()
hdr_font = Font(name='Arial', bold=True, color='FFFFFF'); hdr_fill = PatternFill('solid', fgColor='2F4F4F')
body = Font(name='Arial', size=10)
def sheet(title, header, data, widths):
    ws = wb.create_sheet(title)
    ws.append(header)
    for row in data: ws.append(row)
    for c in ws[1]: c.font = hdr_font; c.fill = hdr_fill; c.alignment = Alignment(vertical='center', wrap_text=True)
    for row in ws.iter_rows(min_row=2):
        for c in row: c.font = body; c.alignment = Alignment(vertical='top', wrap_text=True)
    for i, w in enumerate(widths, 1): ws.column_dimensions[get_column_letter(i)].width = w
    ws.freeze_panes = 'A2'; ws.auto_filter.ref = ws.dimensions
    return ws

ws = wb.active; ws.title = 'Read me'
readme = [
    ['Pedal Factory virtual warehouse', ''],
    ['What', 'Every component of every Pedal Factory pedal model, the part models they use, the circuit blocks they form, '
             'the generic block types (the shelves for designing new pedals) and each pedal as a recipe of blocks.'],
    ['Rebuilt from', 'The code is the master copy. Netlisted networks (SlowBurn::Mna and the pedal-local MNA solvers, '
             'OpStage::design) are parsed straight from Source/Pedals/*.h. Everything the code builds by hand (diodes, JFETs, '
             'triodes, op-amp swing limits, pots, RC corners) is in Tools/Warehouse/curated_parts.csv, and each row is checked '
             'against its pedal source file (Code check = found). Run python3 Tools/Warehouse/build_warehouse.py.'],
    ['Tabs', ''],
    ['Pedals', 'One line per pedal with live component counts.'],
    ['Inventory', 'Every component: pedal, block, ref, kind, value, nodes, control, part model, provenance, model, function, source line.'],
    ['Parts', 'The shelves: part models with parameters, credit and options for the same slot.'],
    ['Blocks', 'Each pedal\'s circuit blocks with their type and component count.'],
    ['Block types', 'Generic building blocks (gain stages, clippers, tone stacks, compressor cells ...): what each is, how the code '
             'builds it today, design notes and every pedal that uses it. Start here to design a new pedal.'],
    ['Recipes', 'Every pedal as an ordered list of blocks (signal order).'],
    ['Model column', 'NETLIST = element of a solved nodal network (exact). OPSTAGE = R / C of an exact op-amp stage transfer '
             'function. SOLVED = part of a per-sample nonlinear solve (Newton). FILTER = sets a filter corner (one-pole / biquad). '
             'CURVE = behavioural waveshaper or swing limit. GAIN = a gain constant. CONTROL = a knob, toggle or footswitch. '
             'DSP = digital / behavioural algorithm. FIXED = in the real circuit but folded into another element.'],
    ['Provenance', 'DRAWN = from the schematic drawing; DRAWN-UNSURE = unclear on the drawing; TRACED = our trace of a layout; '
             'PARTS-LIST = clone parts list; DATASHEET; EST-STD = typical value; EST-FIT = fitted to an analysis; EST-EAR = tuned by '
             'ear; USER = Michael\'s choice; OURS = our change or addition to the original circuit.'],
    ['Not included', 'The generic Overdrive / Distortion / Fuzz placeholders (DriveBase) and board-level features (Transposer).'],
]
for r in readme: ws.append(r)
ws['A1'].font = Font(name='Arial', size=14, bold=True)
for row in ws.iter_rows(min_row=2):
    row[0].font = Font(name='Arial', size=10, bold=True)
    for c in row: c.alignment = Alignment(vertical='top', wrap_text=True)
    row[1].font = body
ws.column_dimensions['A'].width = 18; ws.column_dimensions['B'].width = 120

ped = sheet('Pedals', ['Pedal', 'Id', 'Category', 'Based on', 'Width', 'Blocks', 'Components', 'Netlisted', 'Solved', 'Controls'],
            [[p[0], pid, p[2], p[3], p[4]] for pid, p in PEDALS.items()], [16, 6, 13, 48, 7, 8, 12, 10, 9, 9])
for i in range(2, len(PEDALS) + 2):
    ped.cell(i, 6, '=COUNTIF(Blocks!$B:$B,A%d)' % i)
    ped.cell(i, 7, '=COUNTIF(Inventory!$A:$A,A%d)' % i)
    ped.cell(i, 8, '=COUNTIFS(Inventory!$A:$A,A%d,Inventory!$K:$K,"NETLIST")+COUNTIFS(Inventory!$A:$A,A%d,Inventory!$K:$K,"OPSTAGE")' % (i, i))
    ped.cell(i, 9, '=COUNTIFS(Inventory!$A:$A,A%d,Inventory!$K:$K,"SOLVED")' % i)
    ped.cell(i, 10, '=COUNTIFS(Inventory!$A:$A,A%d,Inventory!$K:$K,"CONTROL")' % i)
n = len(PEDALS) + 2
ped.cell(n, 1, 'Total').font = Font(name='Arial', bold=True)
for col in range(6, 11):
    L = get_column_letter(col); ped.cell(n, col, '=SUM(%s2:%s%d)' % (L, L, n - 1)).font = Font(name='Arial', bold=True)

sheet('Inventory', COLS, [[r[c] for c in COLS] for r in all_rows], [14, 26, 11, 16, 8, 22, 18, 22, 15, 12, 10, 48, 20, 10])
pw = sheet('Parts', PART_H, [r[:6] + [None, r[7]] for r in part_rows], [16, 11, 28, 60, 34, 28, 10, 50])
for i in range(2, len(part_rows) + 2): pw.cell(i, 7, '=COUNTIF(Inventory!$I:$I,A%d)' % i)
bw = sheet('Blocks', BLK_H, [r[:6] + [None, r[7]] for r in blk_rows], [12, 14, 30, 18, 28, 60, 11, 30])
for i in range(2, len(blk_rows) + 2): bw.cell(i, 7, '=COUNTIF(Inventory!$C:$C,A%d)' % i)
tw = sheet('Block types', TYPE_H, [r[:5] + [None] + r[6:] for r in type_rows], [18, 28, 44, 50, 44, 8, 40, 40])
for i in range(2, len(type_rows) + 2): tw.cell(i, 6, '=COUNTIF(Blocks!$D:$D,A%d)' % i)
sheet('Recipes', REC_H, rec_rows, [14, 46, 6, 12, 32, 18])
wb.save(os.path.join(out, 'PedalFactory_Warehouse.xlsx'))

with open(os.path.join(out, 'check_report.txt'), 'w', encoding='utf-8') as f:
    f.write('Pedal Factory warehouse check\n')
    f.write('components: %d (%d netlisted / op-stage from the code, %d curated)\n' % (
        len(all_rows), sum(r['Code check'] == 'netlist' for r in all_rows), sum(r['Code check'] != 'netlist' for r in all_rows)))
    for pid, p in PEDALS.items():
        rs = [r for r in all_rows if r['Pedal'] == p[0]]
        f.write('  %-14s %4d components, %2d blocks\n' % (p[0], len(rs), sum(b[1] == pid for b in BLOCKS)))
    f.write('part models: %d, blocks: %d, block types: %d\n' % (len(PARTS), len(BLOCKS), len(BLOCK_TYPES)))
    f.write('problems: %d\n' % len(problems))
    for pr in problems: f.write('  ' + pr + '\n')
print(open(os.path.join(out, 'check_report.txt'), encoding='utf-8').read())
