"""Minimal Xenos (Xbox 360 GPU) shader microcode disassembler for WFC shader caches.

Used to read the ORIGINAL compiled UE3 shaders (lightmap decode, lighting, fog, post) as the
rendering specification. Bit layouts follow xenia's public src/xenia/gpu/ucode.h.

Shader binary (big-endian dwords) as found inside UE3 ShaderCache exports:
  +0x00 magic 0x102A1100 (pixel) / 0x102A1101 (vertex)
  +0x04 microcode offset      +0x08 microcode size
  +0x10 CTAB offset           ...
Usage:  python xenos_dis.py <shadercache.bin> [--find NAME ...] [--index N]
"""
import struct, sys

VOP = ['add', 'mul', 'max', 'min', 'seq', 'sgt', 'sge', 'sne', 'frc', 'trunc', 'floor', 'mad',
       'cndeq', 'cndge', 'cndgt', 'dp4', 'dp3', 'dp2add', 'cube', 'max4', 'setp_eq_push',
       'setp_ne_push', 'setp_gt_push', 'setp_ge_push', 'kill_eq', 'kill_gt', 'kill_ge', 'kill_ne',
       'dst', 'maxa']
SOP = ['adds', 'adds_prev', 'muls', 'muls_prev', 'muls_prev2', 'maxs', 'mins', 'seqs', 'sgts', 'sges',
       'snes', 'frcs', 'truncs', 'floors', 'exp', 'logc', 'log', 'rcpc', 'rcpf', 'rcp', 'rsqc', 'rsqf',
       'rsq', 'maxas', 'maxasf', 'subs', 'subs_prev', 'setp_eq', 'setp_ne', 'setp_gt', 'setp_ge',
       'setp_inv', 'setp_pop', 'setp_clr', 'setp_rstr', 'kills_eq', 'kills_gt', 'kills_ge', 'kills_ne',
       'kills_one', 'sqrt', 'op41', 'mulsc', 'mulsc', 'addsc', 'addsc', 'subsc', 'subsc', 'sin', 'cos',
       'retain_prev']
VNSRC = {'add': 2, 'mul': 2, 'max': 2, 'min': 2, 'seq': 2, 'sgt': 2, 'sge': 2, 'sne': 2, 'frc': 1,
         'trunc': 1, 'floor': 1, 'mad': 3, 'cndeq': 3, 'cndge': 3, 'cndgt': 3, 'dp4': 2, 'dp3': 2,
         'dp2add': 3, 'cube': 2, 'max4': 1, 'dst': 2, 'maxa': 2}
CMP = 'xyzw'


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


def read_ctab(b, off):
    """D3DXSHADER_CONSTANTTABLE (big-endian); offsets relative to the table start."""
    size, creator, ver, n, info, flags, target = struct.unpack_from('>7I', b, off)
    out = []
    for k in range(n):
        name_o, rset, ridx, rcnt, _res, tinfo, dflt = struct.unpack_from('>IHHHHII', b, off + info + 20 * k)
        e = b.index(b'\0', off + name_o)
        out.append({'name': b[off + name_o:e].decode('latin-1'), 'set': ['bool', 'int', 'float', 'sampler'][rset]
                    if rset < 4 else rset, 'reg': ridx, 'count': rcnt})
    return out


class Shader:
    def __init__(self, b, off):
        self.b, self.off = b, off
        h = struct.unpack_from('>8I', b, off)
        self.magic = h[0]
        self.kind = 'ps' if h[0] == 0x102A1100 else 'vs'
        self.uc_off, self.uc_size = h[1], h[2]
        self.ctab_off = h[4]
        try:
            self.consts = read_ctab(b, off + self.ctab_off + 4)
        except Exception:
            self.consts = []
        # program info (header word 6): literal-pool byte size, then code byte size. The literal
        # pool (vec4 float constants) precedes the instructions inside the microcode region.
        self.lit_size, self.code_size = struct.unpack_from('>2I', b, off + h[6])
        if self.lit_size + self.code_size != self.uc_size:
            self.lit_size, self.code_size = 0, self.uc_size
        self.literals = struct.unpack_from('>%df' % (self.lit_size // 4), b, off + self.uc_off)
        self.words = struct.unpack_from('>%dI' % (self.code_size // 4), b,
                                        off + self.uc_off + self.lit_size)

    def name_of(self, cls, reg):
        for c in self.consts:
            if c['set'] == cls and c['reg'] <= reg < c['reg'] + max(1, c['count']):
                return c['name'] + ('[%d]' % (reg - c['reg']) if c['count'] > 1 else '')
        return None

    # ---------------------------------------------------------------- operands
    def src(self, w, i):
        d0, d1, d2 = w
        reg = bits(d2, [16, 8, 0][i - 1], 8)
        swz = bits(d1, [16, 8, 0][i - 1], 8)
        neg = bits(d1, [26, 25, 24][i - 1], 1)
        is_temp = bits(d2, [31, 30, 29][i - 1], 1)
        if is_temp:
            s = 'r%d' % (reg & 0x3F)
            if reg & 0x80: s = '|%s|' % s
        else:
            s = 'c%d' % reg
            nm = self.name_of('float', reg)
            if nm: s += '{%s}' % nm
            if bits(d0, 7, 1): s = '|%s|' % s
        sw = ''.join(CMP[((swz >> (2 * k)) + k) & 3] for k in range(4))
        if sw != 'xyzw': s += '.' + sw
        return ('-' if neg else '') + s

    def alu(self, w):
        d0, d1, d2 = w
        vop = bits(d2, 24, 5); sop = bits(d0, 26, 6)
        vdst = bits(d0, 0, 6); sdst = bits(d0, 8, 6); exp = bits(d0, 15, 1)
        vmask = bits(d0, 16, 4); smask = bits(d0, 20, 4)
        vclamp = bits(d0, 24, 1); sclamp = bits(d0, 25, 1)
        pred = ''
        if bits(d1, 28, 1): pred = '(%sp0) ' % ('' if bits(d1, 27, 1) else '!')
        out = []
        dn = lambda r: ('oC%d' % r if self.kind == 'ps' else ('oPos' if r == 62 else 'o%d' % r)) if exp else 'r%d' % r
        m = lambda mk: ''.join(CMP[k] if mk >> k & 1 else '_' for k in range(4))
        vname = VOP[vop] if vop < len(VOP) else 'v%d' % vop
        if vmask or vname.startswith(('kill', 'setp', 'maxa')):
            n = VNSRC.get(vname, 2)
            out.append('%s%s%s %s.%s, %s' % (pred, vname, '_sat' if vclamp else '', dn(vdst), m(vmask),
                                             ', '.join(self.src(w, i) for i in range(1, n + 1))))
        sname = SOP[sop] if sop < len(SOP) else 's%d' % sop
        if smask or sname.startswith(('kill', 'setp')):
            if sname in ('mulsc', 'addsc', 'subsc'):
                treg = (sop & 1) | (bits(d2, 29, 1) << 1) | (bits(d1, 0, 8) & 0x3C)
                creg = bits(d2, 0, 8)
                a = 'c%d' % creg
                nm = self.name_of('float', creg)
                if nm: a += '{%s}' % nm
                a += '.' + CMP[(3 + bits(d1, 6, 2)) & 3]
                if bits(d1, 24, 1): a = '-' + a
                ops = '%s, %sr%d.%s' % (a, '-' if bits(d1, 24, 1) else '', treg, CMP[bits(d1, 0, 2) & 3])
            elif sname in ('retain_prev', 'setp_clr'):
                ops = ''
            else:
                s3 = self.src(w, 3)
                base, _, sw = s3.partition('.')
                sw = sw or 'xyzw'
                if sname in ('adds', 'muls', 'maxs', 'mins', 'seqs', 'sgts', 'sges', 'snes', 'subs', 'maxas'):
                    ops = '%s.%s, %s.%s' % (base, sw[3], base, sw[0])
                else:
                    ops = '%s.%s' % (base, sw[3])
            out.append('%s%s%s %s.%s, %s' % (pred, sname, '_sat' if sclamp else '', dn(sdst), m(smask), ops))
        return '  ||  '.join(out) if out else 'nop'

    def fetch(self, w):
        d0, d1, d2 = w
        op = bits(d0, 0, 5)
        src = bits(d0, 5, 6); dst = bits(d0, 12, 6); ci = bits(d0, 20, 5)
        sswz = bits(d0, 26, 6); dswz = bits(d1, 0, 12)
        ds = ''.join('xyzw01?_'[(dswz >> (3 * k)) & 7] for k in range(4))
        if op == 0:
            return 'vfetch r%d.%s, r%d, vf%d' % (dst, ds, src, ci)
        nm = self.name_of('sampler', ci) or 'tf%d' % ci
        ss = ''.join(CMP[(sswz >> (2 * k)) & 3] for k in range(3))
        names = {1: 'tfetch', 16: 'getgrad?', 17: 'getcomptexlod', 18: 'getgradients', 19: 'getweights',
                 24: 'setlod', 25: 'setgradh', 26: 'setgradv'}
        lod = bits(d2, 2, 7)
        if lod & 0x40: lod -= 0x80
        return '%s r%d.%s, r%d.%s, %s%s' % (names.get(op, 'fop%d' % op), dst, ds, src, ss, nm,
                                            ' lodbias=%g' % (lod / 16.0) if lod else '')

    def disasm(self):
        w = self.words
        lines = []
        # control flow: pairs of 48-bit instructions in 3 dwords, until first exec address
        cf = []
        k = 0
        first_exec = len(w) // 3
        while k * 3 < first_exec * 3 and k * 3 + 2 < len(w):
            d0, d1, d2 = w[k * 3:k * 3 + 3]
            a = (d0, d1 & 0xFFFF); bq = ((d1 >> 16) | (d2 << 16)) & 0xFFFFFFFF, d2 >> 16
            for (x0, x1) in (a, bq):
                opc = bits(x1, 12, 4)
                cf.append((opc, x0, x1))
                if opc in (1, 2, 3, 4, 5, 6, 13, 14):
                    first_exec = min(first_exec, bits(x0, 0, 12))
            k += 1
        for opc, x0, x1 in cf:
            if opc in (1, 2, 3, 4, 5, 6, 13, 14):
                addr, cnt, seq = bits(x0, 0, 12), bits(x0, 12, 3), bits(x0, 16, 12)
                cond = ''
                if opc in (3, 4, 13, 14): cond = ' b%d=%d' % (bits(x1, 2, 8) if False else bits(x0 >> 0, 0, 0), 0)
                lines.append('exec%s @%d n=%d' % ('_end' if opc in (2, 4, 6, 14) else '', addr, cnt))
                for j in range(cnt):
                    ins = w[(addr + j) * 3:(addr + j) * 3 + 3]
                    if len(ins) < 3: break
                    is_fetch = (seq >> (2 * j)) & 1
                    lines.append('   %3d  %s' % (addr + j, self.fetch(ins) if is_fetch else self.alu(ins)))
                if opc in (2, 4, 6, 14): break
            elif opc == 12:
                lines.append('alloc')
            elif opc:
                lines.append('cf op %d' % opc)
        return lines


def literals(b, sh):
    """Literal float pool: the words right before the microcode that decode as sane floats."""
    return None


def shaders(b):
    import re
    for m in re.finditer(rb'\x10\x2a\x11[\x00\x01]', b):
        try:
            yield Shader(b, m.start())
        except Exception:
            pass


if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    want = [a for a in sys.argv[2:] if not a.startswith('--')]
    for i, s in enumerate(shaders(b)):
        names = {c['name'] for c in s.consts}
        if all(n in names for n in want):
            print('=== #%d @%d %s ucode=%d consts: %s' % (i, s.off, s.kind, s.uc_size,
                  ', '.join('%s:%s%d' % (c['name'], c['set'][0], c['reg']) for c in s.consts)))
