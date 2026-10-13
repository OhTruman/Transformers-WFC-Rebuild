"""dmp_stacks.py: minimal x64 minidump reader (no debugger needed): per thread, RIP and the return-address candidates on its stack that fall inside
wfc_rebuild.exe, named from an lld map ("RVA size align name" symbol lines). Read-only.
  dmp_stacks.py <dump> [<wfc_rebuild.map>] [--frames N]"""
import bisect, re, struct, sys

dmp = open(sys.argv[1], 'rb').read()
mapf = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else None
NF = int(sys.argv[sys.argv.index('--frames') + 1]) if '--frames' in sys.argv else 14

sig, ver, nstreams, rva_dir = struct.unpack_from('<IIII', dmp, 0)
assert sig == 0x504d444d, 'not a minidump'
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', dmp, rva_dir + 12 * i)
    streams[t] = (size, rva)

def mstr(rva):
    n = struct.unpack_from('<I', dmp, rva)[0]
    return dmp[rva + 4: rva + 4 + n].decode('utf-16le', 'replace')

mods = []
if 4 in streams:
    _, rva = streams[4]
    n = struct.unpack_from('<I', dmp, rva)[0]
    for i in range(n):
        o = rva + 4 + 108 * i
        base, size = struct.unpack_from('<QI', dmp, o)
        name_rva = struct.unpack_from('<I', dmp, o + 20)[0]
        mods.append((base, size, mstr(name_rva).split('\\')[-1]))

mem = []                                     # (start, data offset, size)
if 5 in streams:
    _, rva = streams[5]
    n = struct.unpack_from('<I', dmp, rva)[0]
    for i in range(n):
        start, dsize, drva = struct.unpack_from('<QII', dmp, rva + 4 + 16 * i)
        mem.append((start, drva, dsize))

syms = []
if mapf:
    for line in open(mapf, encoding='utf-8', errors='replace'):
        m = re.match(r'^([0-9a-f]{8}) ([0-9a-f]{8})\s+0\s+(\S.*)$', line.rstrip())
        if m and int(m.group(2), 16) == 0:
            syms.append((int(m.group(1), 16), m.group(3).strip()))
    syms.sort()
skeys = [s[0] for s in syms]
exe = next((m for m in mods if m[2].lower() == 'wfc_rebuild.exe'), None)

def name(addr):
    for base, size, nm in mods:
        if base <= addr < base + size:
            if exe and base == exe[0] and syms:
                rv = addr - base
                i = bisect.bisect_right(skeys, rv) - 1
                if i >= 0:
                    return '%s+0x%x' % (syms[i][1][:110], rv - syms[i][0])
            return '%s+0x%x' % (nm, addr - base)
    return None

def read(addr, n):
    for start, drva, size in mem:
        if start <= addr and addr + n <= start + size:
            return dmp[drva + addr - start: drva + addr - start + n]
    return None

_, rva = streams[3]
n = struct.unpack_from('<I', dmp, rva)[0]
for i in range(n):
    o = rva + 4 + 48 * i
    tid, susp, pcls, prio, teb, st_start, st_size, st_rva, ctx_size, ctx_rva = struct.unpack_from('<IIIIQQIIII', dmp, o)
    rip = struct.unpack_from('<Q', dmp, ctx_rva + 0xF8)[0]
    rsp = struct.unpack_from('<Q', dmp, ctx_rva + 0x98)[0]
    print('thread %d: rip %s' % (tid, name(rip) or hex(rip)))
    stack = dmp[st_rva: st_rva + st_size]
    off = max(0, rsp - st_start)
    shown = 0
    for k in range(off, len(stack) - 7, 8):
        v = struct.unpack_from('<Q', stack, k)[0]
        if exe and exe[0] <= v < exe[0] + exe[1]:
            nm = name(v)
            print('    %s' % nm)
            shown += 1
            if shown >= NF:
                break
