"""Print the audio-track table of original Bink (.bik) movies (read-only): per track the sample rate, flags (stereo /
DCT) and the Bink track ID, from the header layout the Bink 1 demuxers use:
    'BIK'rev, file size-8, frames, largest frame, frames, width, height, fps num, fps den, video flags, track count,
    then per track: u32 max decoded size; per track: u16 sample rate, u16 flags; per track: u32 track ID.
Run with AssetTools/bin/py/python.exe:  python tools/systems/bink_tracks.py <file.bik> ...
"""
import struct, sys

for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        h = f.read(44)
        sig, size, frames, largest, frames2, w, hgt, fnum, fden, vflags, ntr = struct.unpack('<4sIIIIIIIIII', h)
        maxsz = struct.unpack('<%dI' % ntr, f.read(4 * ntr)) if ntr else ()
        rf = [struct.unpack('<HH', f.read(4)) for _ in range(ntr)]
        ids = struct.unpack('<%dI' % ntr, f.read(4 * ntr)) if ntr else ()
    print('%s: %s %dx%d %d frames @ %.3f fps, %d audio tracks' % (path.split('/')[-1], sig[:4], w, hgt, frames, fnum / max(1, fden), ntr))
    for i in range(ntr):
        rate, fl = rf[i]
        print('   track %d: id %d, %d Hz, flags 0x%04x (%s%s), max decoded %d' % (
            i, ids[i], rate, fl, 'stereo' if fl & 0x2000 else 'mono', ', DCT' if fl & 0x1000 else ', RDFT', maxsz[i]))
