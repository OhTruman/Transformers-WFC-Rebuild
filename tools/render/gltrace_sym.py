"""Symbolise WFC_GLTRACE / WFC_TEXTRACE lines: replaces every "exe+0x<rva>" in a log with the function that contains
it, using the linker map written next to the exe (wfc_rebuild.map, -Wl,-Map).

usage: gltrace_sym.py <wfc_rebuild.map> <log>   (prints the GLTRACE / TEXTRACE lines, symbolised)
"""
import bisect
import re
import sys

mapf, logf = sys.argv[1], sys.argv[2]
syms = []
for line in open(mapf, encoding='utf-8', errors='replace'):
    p = line.split(None, 4)
    if len(p) >= 4 and re.fullmatch(r'[0-9a-f]{8}', p[0]) and p[1] == '00000000' and p[2] == '0':
        syms.append((int(p[0], 16), (p[3] + (' ' + p[4] if len(p) > 4 else '')).strip()))
syms.sort()
addrs = [a for a, _ in syms]


def name(rva):
    i = bisect.bisect_right(addrs, rva) - 1
    if i < 0:
        return '?'
    s = re.sub(r'\(.*', '', syms[i][1])
    return '%s+0x%x' % (s[-90:], rva - syms[i][0])


for line in open(logf, encoding='utf-8', errors='replace'):
    if 'GLTRACE' in line or 'TEXTRACE' in line:
        print(re.sub(r'exe\+0x([0-9a-f]+)', lambda m: '[%s]' % name(int(m.group(1), 16)), line.rstrip()))
