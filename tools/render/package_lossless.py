"""Lossless package slimming (PC ADAPTATION: package size; every byte the game reads stays identical).

  package_lossless.py droppable <render data root> [<ExtractedAssets root>]
      Prints the PNGs a package may omit: those with a verified DDS twin in <render data>/texture_formats.json
      (ExtractedAssets-relative, e.g. content/...png), plus the render-data lightmap copies
      (<render data>/<map>/lightmaps/<name>.png) whose verticalslice/maps/<map>/lightmaps/<name>.png key is indexed
      or that the map's lighting.json never references (never loaded).
      Every other PNG must ship (glTF-baked character textures, render-data tex/ cube faces, CLUT, ...).
  package_lossless.py compress <file> [<file> ...]
      Writes <file>.xpr next to each file (the Windows Compression API's XPRESS_HUFF buffer format: what
      platform::readFileMaybeCompressed reads when <file> is absent), verifies the round trip byte-for-byte, and
      prints the sizes. The package then omits <file> itself. Use it for .bin (glTF buffers) and, once Gameplay's
      reader goes through assets::readDataFile, .psa.
"""
import ctypes
import json
import os
import re
import sys

XPRESS_HUFF = 4


def _api():
    cab = ctypes.WinDLL('cabinet.dll')
    for n in ('CreateCompressor', 'Compress', 'CloseCompressor', 'CreateDecompressor', 'Decompress', 'CloseDecompressor'):
        getattr(cab, n).restype = ctypes.c_int
    return cab


def compress(cab, data):
    h = ctypes.c_void_p()
    if not cab.CreateCompressor(XPRESS_HUFF, None, ctypes.byref(h)):
        raise OSError('CreateCompressor failed')
    need = ctypes.c_size_t()
    cab.Compress(h, data, len(data), None, 0, ctypes.byref(need))
    buf = ctypes.create_string_buffer(max(need.value, len(data) + 1024))
    got = ctypes.c_size_t()
    ok = cab.Compress(h, data, len(data), buf, len(buf), ctypes.byref(got))
    cab.CloseCompressor(h)
    if not ok:
        raise OSError('Compress failed')
    return buf.raw[:got.value]


def decompress(cab, data):
    h = ctypes.c_void_p()
    if not cab.CreateDecompressor(XPRESS_HUFF, None, ctypes.byref(h)):
        raise OSError('CreateDecompressor failed')
    need = ctypes.c_size_t()
    cab.Decompress(h, data, len(data), None, 0, ctypes.byref(need))
    buf = ctypes.create_string_buffer(need.value)
    got = ctypes.c_size_t()
    ok = cab.Decompress(h, data, len(data), buf, len(buf), ctypes.byref(got))
    cab.CloseDecompressor(h)
    if not ok:
        raise OSError('Decompress failed')
    return buf.raw[:got.value]


def droppable(render_root, ea_root):
    idx = json.load(open(os.path.join(render_root, 'texture_formats.json'), encoding='utf-8'))['textures']
    lower = {k: v for k, v in idx.items() if not v.get('no_png')}
    for root, _, files in os.walk(os.path.join(ea_root, 'content')):
        for f in files:
            if f.lower().endswith('.png'):
                rel = os.path.relpath(os.path.join(root, f), ea_root).replace('\\', '/')
                if rel.lower() in lower:
                    print(os.path.join(ea_root, rel).replace('\\', '/'))
    for m in sorted(os.listdir(render_root)):
        lm = os.path.join(render_root, m, 'lightmaps')
        if not os.path.isdir(lm) or '.' in m:
            continue
        lj = os.path.join(render_root, m, 'lighting.json')    # the pages the map references
        used = {n.lower() for n in re.findall(r'LightMapTexture2D_\d+', open(lj, encoding='utf-8').read())} \
            if os.path.exists(lj) else None
        for f in sorted(os.listdir(lm)):
            if not f.lower().endswith('.png'):
                continue
            indexed = ('verticalslice/maps/%s/lightmaps/%s' % (m.lower(), f.lower())) in idx
            if indexed or (used is not None and f[:-4].lower() not in used):   # DDS twin, or never loaded
                print(os.path.join(lm, f).replace('\\', '/'))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'droppable':
        ea = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])), '..', '..', 'ExtractedAssets')
        droppable(sys.argv[2], os.path.normpath(ea))
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == 'compress':
        cab = _api()
        raw_total = packed_total = 0
        for path in sys.argv[2:]:
            data = open(path, 'rb').read()
            packed = compress(cab, data)
            if decompress(cab, packed) != data:
                print('ROUND TRIP FAILED', path)
                return 1
            open(path + '.xpr', 'wb').write(packed)
            raw_total += len(data)
            packed_total += len(packed)
        print('%d files: %.1f MB -> %.1f MB (%.0f %%), all round trips identical' %
              (len(sys.argv) - 2, raw_total / 2**20, packed_total / 2**20, 100.0 * packed_total / max(raw_total, 1)))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
