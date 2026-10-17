# Read-only minidump scanner for wfc_rebuild hang / crash dumps (no cdb / WinDbg on this PC). Per dump:
#   - the exception (crash dumps), the thread list (id, name, RIP -> module + offset, symbol when the module is wfc_rebuild.exe and a
#     matching wfc_rebuild.map is found beside the exe the dump names - matched by the PE TimeDateStamp)
#   - "likely callers": return addresses found by scanning each thread's captured stack for values inside wfc_rebuild.exe (a
#     heuristic: no PDB unwind - stale stack slots can appear; the first hits nearest RSP are the most likely real frames)
#   - the watchdog / hang lines of the wfc.log beside the dump (the render-thread phase)
#   python minidump_scan.py <dump> [...]  [--frames 8] [--json out.json]
import os, re, struct, sys, json, bisect, glob
args = [a for a in sys.argv[1:]]; NF = 8; JOUT = None
if '--frames' in args: i = args.index('--frames'); NF = int(args[i + 1]); del args[i:i + 2]
if '--json' in args: i = args.index('--json'); JOUT = args[i + 1]; del args[i:i + 2]

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]
def mdstr(b, rva):
    n = u32(b, rva); return b[rva + 4: rva + 4 + n].decode('utf-16-le', 'replace')

MAPS = {}
def load_map(path):
    if path in MAPS: return MAPS[path]
    addrs, names = [], []
    rx = re.compile(r'^([0-9a-f]{8,16}) ([0-9a-f]{8}) +0 {8,}(\S.*)$')
    try:
        for l in open(path, encoding='utf-8', errors='replace'):
            m = rx.match(l)
            if m: addrs.append(int(m.group(1), 16)); names.append(m.group(3).strip())
    except OSError: pass
    o = sorted(zip(addrs, names)); MAPS[path] = ([a for a, _ in o], [n for _, n in o]); return MAPS[path]
def pe_stamp(exe):
    try:
        with open(exe, 'rb') as f: h = f.read(4096)
        pe = u32(h, 0x3c); return u32(h, pe + 8)
    except Exception: return None
# --exes <list file>: other wfc_rebuild.exe copies (with a .map beside them) to match a dump's exe by PE timestamp
STAMPS = {}
if '--exes' in args:
    i = args.index('--exes'); lst = args[i + 1]; del args[i:i + 2]
    for l in open(lst, encoding='utf-8-sig'):
        e = l.strip()
        if e:
            s = pe_stamp(e)
            if s is not None: STAMPS.setdefault(s, os.path.splitext(e)[0] + '.map')
def short(n):
    n = re.sub(r'std::__1::', 'std::', n); n = re.sub(r'\[abi:[^\]]*\]', '', n)
    return n if len(n) < 140 else n[:137] + '...'

def scan(path):
    b = open(path, 'rb').read()
    if b[:4] != b'MDMP': return {'dump': path, 'error': 'not a minidump'}
    ns, dr = u32(b, 8), u32(b, 12); st = {}
    for k in range(ns):
        t, sz, rva = struct.unpack_from('<III', b, dr + 12 * k); st[t] = (sz, rva)
    mods = []
    if 4 in st:
        _, r = st[4]; n = u32(b, r)
        for k in range(n):
            o = r + 4 + 108 * k; base, size, stamp, nrva = u64(b, o), u32(b, o + 8), u32(b, o + 16), u32(b, o + 20)
            mods.append((base, size, mdstr(b, nrva), stamp))
    def modof(a):
        for base, size, name, stamp in mods:
            if base <= a < base + size: return base, name, stamp
        return None
    mem = []   # (start, rva, size)
    if 5 in st:
        _, r = st[5]; n = u32(b, r)
        for k in range(n):
            o = r + 4 + 16 * k; mem.append((u64(b, o), u32(b, o + 12), u32(b, o + 8)))
    if 9 in st:
        _, r = st[9]; n = u64(b, r); base_rva = u64(b, r + 8); cur = base_rva
        for k in range(n):
            o = r + 16 + 16 * k; s, sz = u64(b, o), u64(b, o + 8); mem.append((s, cur, sz)); cur += sz
    names = {}
    if 24 in st:
        _, r = st[24]; n = u32(b, r)
        for k in range(n):
            o = r + 4 + 12 * k; tid, nr = u32(b, o), u64(b, o + 4)
            try: names[tid] = mdstr(b, nr)
            except Exception: pass
    exe = next((m for m in mods if m[2].lower().endswith('wfc_rebuild.exe')), None)
    sym = None; mapnote = 'no map'
    if exe:
        cand = [os.path.splitext(exe[2])[0] + '.map']
        if pe_stamp(exe[2]) == exe[3] and os.path.exists(cand[0]): sym = load_map(cand[0]); mapnote = 'map ' + cand[0]
        elif exe[3] in STAMPS: sym = load_map(STAMPS[exe[3]]); mapnote = 'map ' + STAMPS[exe[3]] + ' (same PE timestamp, another copy)'
        elif os.path.exists(cand[0]): mapnote = 'map present but the exe was rebuilt since (timestamp differs) - offsets only'
        else: mapnote = 'exe / map gone - offsets only'
    def name(a):
        m = modof(a)
        if not m: return f'0x{a:x}'
        base, mn, _ = m; off = a - base; mb = os.path.basename(mn)
        if sym and exe and base == exe[0]:
            A, N = sym; i = bisect.bisect_right(A, off) - 1
            if i >= 0: return f'{mb}!{short(N[i])}+0x{off - A[i]:x}'
        return f'{mb}+0x{off:x}'
    exc = None
    if 6 in st:
        _, r = st[6]; tid = u32(b, r); code, flags = u32(b, r + 8), u32(b, r + 12); addr = u64(b, r + 24)
        exc = {'thread': tid, 'code': f'0x{code:08x}', 'address': name(addr)}
    threads = []
    if 3 in st:
        _, r = st[3]; n = u32(b, r)
        for k in range(n):
            o = r + 4 + 48 * k; tid = u32(b, o); ss, sz, srva = u64(b, o + 24), u32(b, o + 32), u32(b, o + 36); csz, crva = u32(b, o + 40), u32(b, o + 44)
            rip = u64(b, crva + 0xF8) if csz >= 0x100 else 0; rsp = u64(b, crva + 0x98) if csz >= 0x100 else 0
            calls = []
            if exe and sz:
                stk = b[srva: srva + sz]; start = max(0, (rsp - ss)) if ss <= rsp < ss + sz else 0
                for q in range(start - start % 8, len(stk) - 7, 8):
                    v = u64(stk, q)
                    if exe[0] <= v < exe[0] + exe[1]:
                        nm = name(v)
                        if not calls or calls[-1] != nm: calls.append(nm)
                    if len(calls) >= NF: break
            threads.append({'tid': tid, 'name': names.get(tid, ''), 'rip': name(rip), 'callers': calls})
    # the log lines beside the dump
    d = os.path.dirname(path); logs = glob.glob(os.path.join(d, 'wfc.log')) + glob.glob(os.path.join(d, '*', 'wfc.log')) + glob.glob(os.path.join(os.path.dirname(d), 'wfc.log'))
    wl = []
    for lf in logs[:2]:
        try:
            for l in open(lf, encoding='utf-8', errors='replace'):
                if re.search(r'watchdog|hang|GPU driver reset|crash|exception|fatal', l, re.I): wl.append(l.strip()[:300])
        except OSError: pass
    wd = [l for l in wl if 'watchdog' in l or 'GPU driver reset' in l]; rest = [l for l in wl if l not in wd]
    return {'dump': path, 'exe': exe[2] if exe else None, 'symbols': mapnote, 'symbolized': sym is not None, 'exception': exc, 'threads': threads, 'log': (wd[:6] + rest[-3:])[:9], 'log_lines': sum(1 for _ in open(logs[0], encoding='utf-8', errors='replace')) if logs else 0}

out = []
for a in args:
    for p in sorted(glob.glob(a)):
        r = scan(p); out.append(r)
        print(f"== {p}\n   exe {r.get('exe')} ({r.get('symbols')})")
        if r.get('exception'): print(f"   EXCEPTION {r['exception']}")
        for l in r.get('log', []): print(f"   log: {l}")
        for t in r.get('threads', []):
            if t['rip'].startswith('wfc_rebuild') or t['callers'] or t['name']:
                print(f"   thread {t['tid']} {t['name']!r}: rip {t['rip']}")
                for c in t['callers'][:NF]: print(f"      <- {c}")
if JOUT: json.dump(out, open(JOUT, 'w'), indent=1)
