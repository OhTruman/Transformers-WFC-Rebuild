"""Minimal Xenon PPC + VMX128 disassembler for short VMX blocks Ghidra can't decode.
Usage: vmx128dis.py <WfcQuery 'words' dump> <start hex> <end hex>
Encodings follow the public Xenia opcode tables (VX128 / VX128_1 / _2 / _3 / _4 / _P forms)."""
import re, sys

def vd128(i): return ((i >> 21) & 31) | (((i >> 2) & 3) << 5)
def va128(i): return ((i >> 16) & 31) | (((i >> 5) & 1) << 5) | (((i >> 10) & 1) << 6)
def vb128(i): return ((i >> 11) & 31) | ((i & 3) << 5)
def simm16(i): v = i & 0xFFFF; return v - 0x10000 if v & 0x8000 else v

VX128 = {  # op 5, mask 0xFC0003D0
    0x010: 'vaddfp128', 0x050: 'vsubfp128', 0x090: 'vmulfp128', 0x0D0: 'vmaddfp128', 0x110: 'vmaddcfp128',
    0x150: 'vnmsubfp128', 0x190: 'vdot3fp128', 0x1D0: 'vdot4fp128', 0x210: 'vand128', 0x250: 'vandc128',
    0x290: 'vnor128', 0x2D0: 'vor128', 0x310: 'vxor128', 0x350: 'vsel128', 0x390: 'vslo128', 0x3D0: 'vsro128'}
OP6_3 = {0x730: 'vspltw128', 0x770: 'vspltisw128', 0x7F0: 'vupkd3d128', 0x630: 'vcfsx128', 0x670: 'vcfux128',
         0x6B0: 'vrfim128', 0x6F0: 'vrfin128', 0x5B0: 'vctsxs128'}

def dis(a, i):
    op = i >> 26
    if op == 4 and (i & 0x3) == 3:  # VX128_1 loads / stores
        xo = i & 0x7F3
        names = {0x0C3: 'lvx128', 0x1C3: 'stvx128', 0x403: 'lvlx128', 0x443: 'lvrx128', 0x503: 'stvlx128',
                 0x543: 'stvrx128', 0x003: 'lvsl128', 0x043: 'lvsr128', 0x083: 'lvewx128', 0x183: 'stvewx128'}
        n = names.get(xo, 'vx128_1?%x' % xo)
        return f'{n} v{vd128(i)}, r{(i>>16)&31}, r{(i>>11)&31}'
    if op == 5:
        if (i & 0x210) == 0:
            return f'vperm128 v{vd128(i)}, v{va128(i)}, v{vb128(i)}, v{(i>>6)&7}'
        n = VX128.get(i & 0x3D0)
        if n: return f'{n} v{vd128(i)}, v{va128(i)}, v{vb128(i)}'
    if op == 6:
        x = i & 0x7F0
        if x in OP6_3:
            return f'{OP6_3[x]} v{vd128(i)}, v{vb128(i)}, {(i>>16)&31}'
        if (i & 0x3D0) == 0x300: return f'vmrghw128 v{vd128(i)}, v{va128(i)}, v{vb128(i)}'
        if (i & 0x3D0) == 0x340: return f'vmrglw128 v{vd128(i)}, v{va128(i)}, v{vb128(i)}'
        if (i & 0x630) == 0x210:
            return f'vpermwi128 v{vd128(i)}, v{vb128(i)}, 0x{(((i>>16)&31)|(((i>>6)&7)<<5)):02x}'
        if (i & 0x730) == 0x710:
            return f'vrlimi128 v{vd128(i)}, v{vb128(i)}, mask {(i>>16)&31:x}, rot {(i>>6)&3}'
        if (i & 0x3D0) == 0x300: return f'vmrghw128 v{vd128(i)}, v{va128(i)}, v{vb128(i)}'
        if (i & 0x3D0) == 0x340: return f'vmrglw128 v{vd128(i)}, v{va128(i)}, v{vb128(i)}'
        return f'op6? {i:08x}'
    if op == 4:  # classic VMX
        xo11 = i & 0x7FF; xo6 = i & 0x3F
        d, A, B, C = (i >> 21) & 31, (i >> 16) & 31, (i >> 11) & 31, (i >> 6) & 31
        if xo6 == 46: return f'vmaddfp v{d}, v{A}, v{C}, v{B}'
        if xo6 == 47: return f'vnmsubfp v{d}, v{A}, v{C}, v{B}'
        if xo6 == 43: return f'vperm v{d}, v{A}, v{B}, v{C}'
        m = {10: 'vaddfp', 74: 'vsubfp', 652: 'vspltw', 1156: 'vor', 1028: 'vand', 1220: 'vxor', 140: 'vmrghw', 396: 'vmrglw'}
        if xo11 in m: return f'{m[xo11]} v{d}, v{A}, v{B}'
        if ((i >> 1) & 0x3FF) == 103: return f'lvx v{d}, r{A}, r{B}'
        if ((i >> 1) & 0x3FF) == 231: return f'stvx v{d}, r{A}, r{B}'
        return f'vmx? {i:08x}'
    if op == 14: return f'addi r{(i>>21)&31}, r{(i>>16)&31}, {simm16(i)}'
    if op == 15: return f'lis r{(i>>21)&31}, {simm16(i)}'
    if op == 18: 
        li = i & 0x3FFFFFC; li = li - 0x4000000 if li & 0x2000000 else li
        return f'b{"l" if i&1 else ""} 0x{(a+li)&0xffffffff:08x}'
    if op == 31 and ((i >> 1) & 0x3FF) == 444: return f'or r{(i>>16)&31}, r{(i>>21)&31}, r{(i>>11)&31}'
    if op == 32: return f'lwz r{(i>>21)&31}, {simm16(i)}(r{(i>>16)&31})'
    if op == 36: return f'stw r{(i>>21)&31}, {simm16(i)}(r{(i>>16)&31})'
    if op == 48: return f'lfs f{(i>>21)&31}, {simm16(i)}(r{(i>>16)&31})'
    if op == 52: return f'stfs f{(i>>21)&31}, {simm16(i)}(r{(i>>16)&31})'
    return f'?? {i:08x}'

if __name__ == '__main__':
    words = {}
    for m in re.finditer(r'^\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s*$', open(sys.argv[1]).read(), re.M):
        words[int(m.group(1), 16)] = int(m.group(2), 16)
    s, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
    for a in range(s, e, 4):
        print(f'{a:08x}  {words[a]:08x}  {dis(a, words[a])}')
