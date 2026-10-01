"""Decode Xbox 360 cooked UE3 textures that the extraction tools do not export:
TextureCube (6 faces) and TextureFlipBook (a Texture2D subclass), PF_DXT1/DXT5.

Cooked layout (WFC licensee 144, verified on Streets cubemaps):
  TextureCube native tail: 6 x ( [int32 NumMips] NumMips x ( [u32 BulkFlags][int32 Count]
  [int32 SizeOnDisk][int32 OffsetInFile][int32 ?] data[SizeOnDisk] [int32 SizeX][int32 SizeY] ) )
Mip data is Xbox-tiled (XGAddress2DTiledOffset over 4x4 blocks) with 16-bit big-endian words.
"""
import struct
import numpy as np


def tiled_offset(x, y, width_blocks, log_bpp):
    aligned = (width_blocks + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return (((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp


def untile(data, w_blocks, h_blocks, block_bytes):
    log_bpp = {8: 3, 16: 4}[block_bytes]
    out = bytearray(w_blocks * h_blocks * block_bytes)
    aw = (w_blocks + 31) & ~31
    for y in range(h_blocks):
        for x in range(w_blocks):
            src = tiled_offset(x, y, aw, log_bpp) * block_bytes
            dst = (y * w_blocks + x) * block_bytes
            if src + block_bytes <= len(data):
                out[dst:dst + block_bytes] = data[src:src + block_bytes]
    # 16-bit endian swap
    a = np.frombuffer(bytes(out), '>u2').astype('<u2')
    return a.tobytes()


def _c565(c):
    r = ((c >> 11) & 31) * 255 // 31; g = ((c >> 5) & 63) * 255 // 63; b = (c & 31) * 255 // 31
    return np.array([r, g, b], 'i4')


def decode_dxt1(data, w, h):
    img = np.zeros((h, w, 4), 'u1')
    bw, bh = max(1, w // 4), max(1, h // 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            c0, c1, bits = struct.unpack_from('<HHI', data, o)
            p0, p1 = _c565(c0), _c565(c1)
            if c0 > c1:
                pal = [p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3]; alpha = [255] * 4
            else:
                pal = [p0, p1, (p0 + p1) // 2, np.zeros(3, 'i4')]; alpha = [255, 255, 255, 0]
            for k in range(16):
                i = (bits >> (2 * k)) & 3
                yy, xx = by * 4 + k // 4, bx * 4 + k % 4
                if yy < h and xx < w:
                    img[yy, xx, :3] = pal[i]; img[yy, xx, 3] = alpha[i]
    return img


def decode_dxt5(data, w, h):
    img = np.zeros((h, w, 4), 'u1')
    bw, bh = max(1, w // 4), max(1, h // 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 16
            a0, a1 = data[o], data[o + 1]
            abits = int.from_bytes(data[o + 2:o + 8], 'little')
            if a0 > a1:
                ap = [a0, a1] + [((6 - k) * a0 + (k + 1) * a1) // 7 for k in range(6)]
            else:
                ap = [a0, a1] + [((4 - k) * a0 + (k + 1) * a1) // 5 for k in range(4)] + [0, 255]
            c0, c1, bits = struct.unpack_from('<HHI', data, o + 8)
            p0, p1 = _c565(c0), _c565(c1)
            pal = [p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3]
            for k in range(16):
                yy, xx = by * 4 + k // 4, bx * 4 + k % 4
                if yy < h and xx < w:
                    img[yy, xx, :3] = pal[(bits >> (2 * k)) & 3]
                    img[yy, xx, 3] = ap[(abits >> (3 * k)) & 7]
    return img


def read_mip_chain(t, o):
    n = struct.unpack_from('>i', t, o)[0]; o += 4
    mips = []
    for _ in range(n):
        flags, count, size, off = struct.unpack_from('>Iiii', t, o); o += 20   # + 1 extra int32 (WFC)
        size = max(size, 0)
        data = t[o:o + size]; o += size
        sx, sy = struct.unpack_from('>ii', t, o); o += 8
        mips.append((sx, sy, data))
    return mips, o


def decode_mip(sx, sy, data, fmt):
    bb = 8 if fmt == 'PF_DXT1' else 16
    wb, hb = max(1, sx // 4), max(1, sy // 4)
    lin = untile(data, wb, hb, bb)
    return decode_dxt1(lin, sx, sy) if bb == 8 else decode_dxt5(lin, sx, sy)


def decode_cube(tail, fmt, edge):
    """Returns 6 RGBA arrays (UE3 face order +X -X +Y -Y +Z -Z), top mip of each face.
    Packed mip-tail entries (flags 0x21, size -1) are irregular, so each face's chain is located
    by its mip-0 header: [NumMips][flags=0][count=S][size=S][offset][extra] with S = top-mip bytes."""
    bb = 8 if fmt == 'PF_DXT1' else 16
    S = max(1, edge // 4) ** 2 * bb
    faces = []
    o = 0
    while len(faces) < 6:
        found = -1
        for q in range(o, len(tail) - 24, 4):
            n, fl, cnt, sz = struct.unpack_from('>iIii', tail, q)
            if 1 <= n <= 16 and fl == 0 and cnt == S and sz == S:
                found = q; break
        if found < 0: raise ValueError('cube face %d not found' % len(faces))
        data = tail[found + 24:found + 24 + S]
        faces.append(decode_mip(edge, edge, data, fmt))
        o = found + 24 + S
    return faces
