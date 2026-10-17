"""Read-only check of the FaceFX animation-block layout hypothesis over every .fxa (argv[1] = content root).
Block (after <u32 len><cue><u32 len><anim name>, all BE):
  u32 0, u32 X, u32 n (curves), n x (u32 nameIndex, u32 0), i32 -K, f32 start, f32 end, u32 n, u32 0, f32 a, f32 b,
  n x u16 keyCount (sum = K), K x i16 time (t = v * end / 32767), K x u8 value,
  then the next block's prefix: f32 blendIn, f32 blendOut, u32 0 (12 bytes) before its cue length.
Reports how many blocks satisfy: counts sum to K, length matches exactly, times rise within each curve, the first time
maps to start and 32767 to end where present."""
import glob, os, re, struct, sys
root = sys.argv[1]
pat = re.compile(rb'\x00\x00\x00([\x08-\x60])([A-Za-z0-9_]+\.[A-Za-z0-9_.]+)\x00\x00\x00([\x08-\x60])(DX_[A-Za-z0-9_]+)')
tot = ok_len = ok_sum = ok_mono = last = 0; bad = []
for p in sorted(glob.glob(os.path.join(root, '*', '*.fxa'))):
    d = open(p, 'rb').read()
    bl = [m for m in pat.finditer(d) if m.group(1)[0] == len(m.group(2)) and m.group(3)[0] == len(m.group(4))]
    for i, m in enumerate(bl):
        tot += 1
        o = m.end()
        try:
            z, x, n = struct.unpack_from('>III', d, o); o += 12
            names = [struct.unpack_from('>II', d, o + 8 * k) for k in range(n)]; o += 8 * n
            negk, start, end = struct.unpack_from('>iff', d, o); o += 12
            n2, z2, a, b = struct.unpack_from('>IIff', d, o); o += 16
            counts = struct.unpack_from('>%dH' % n, d, o); o += 2 * n
            K = -negk
            if sum(counts) == K: ok_sum += 1
            times = struct.unpack_from('>%dh' % K, d, o); o += 2 * K
            vals = d[o:o + K]; o += K
            mono = True; j = 0
            for c in counts:
                seq = times[j:j + c]; j += c
                if any(seq[q + 1] <= seq[q] for q in range(len(seq) - 1)): mono = False
            if mono: ok_mono += 1
            if i + 1 < len(bl):
                if o + 12 == bl[i + 1].start(): ok_len += 1
                else: bad.append((os.path.basename(p), m.group(4).decode(), bl[i + 1].start() - (o + 12)))
            else:
                last += 1
        except struct.error as ex:
            if len(bad) < 6: bad.append((os.path.basename(p), m.group(4).decode(), str(ex)))
print('blocks %d | key counts sum to K: %d | times rise within every curve: %d | exact length to the next block: %d of %d (last blocks per file: %d)' % (
    tot, ok_sum, ok_mono, ok_len, tot - last, last))
import collections
print("mismatches per file:", collections.Counter(b[0] for b in bad).most_common())
print("extra-byte values:", collections.Counter(b[2] for b in bad if isinstance(b[2], int)).most_common(8))
