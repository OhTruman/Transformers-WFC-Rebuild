"""Read-only: locate the animation blocks of a .fxa (cue link + DX name, BE length-prefixed) and dump the bytes between
the name and the next block as BE u32 words / floats, for decoding the curve section. argv[1] = file, argv[2] = block index."""
import re, struct, sys
d = open(sys.argv[1], 'rb').read()
want = int(sys.argv[2]) if len(sys.argv) > 2 else 0
pat = re.compile(rb'\x00\x00\x00([\x08-\x60])([A-Za-z0-9_]+\.[A-Za-z0-9_.]+)\x00\x00\x00([\x08-\x60])(DX_[A-Za-z0-9_]+)')
blocks = [m for m in pat.finditer(d) if m.group(1)[0] == len(m.group(2)) and m.group(3)[0] == len(m.group(4))]
print('blocks', len(blocks))
starts = [m.start() for m in blocks]
for i, m in enumerate(blocks[:6]):
    nxt = starts[i + 1] if i + 1 < len(starts) else len(d)
    print('%2d cue@%06x name %-22s body %06x..%06x (%d bytes)' % (i, m.start(), m.group(4).decode(), m.end(), nxt, nxt - m.end()))
m = blocks[want]; a = m.end(); b = starts[want + 1] if want + 1 < len(starts) else len(d)
print('pre-cue 24 bytes:', d[m.start() - 24:m.start()].hex(' '))
body = d[a:b]
print('body', len(body), 'bytes')
for o in range(0, min(len(body), 640), 16):
    chunk = body[o:o + 16]
    words = [struct.unpack('>I', chunk[k:k + 4])[0] for k in range(0, len(chunk) - 3, 4)]
    fl = ['%.4g' % struct.unpack('>f', chunk[k:k + 4])[0] for k in range(0, len(chunk) - 3, 4)]
    print('%04x  %-48s  %s  %s' % (o, chunk.hex(' '), words, fl))
