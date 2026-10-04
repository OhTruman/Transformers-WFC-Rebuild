"""Edge test for the INT dialogue track (read-only): for each per-language centre track (5..9) of the subtitled WFC
movies, the language-specific signal (track minus the mean of 5..9) should rise right after each INT subtitle start
and fall right after each INT subtitle end if it is the INT (English) voice. Score per track = mean over cues of
log(energy just after start / just before start) + log(energy just before end+pad / just after end+pad).
Run with AssetTools/bin/py/python.exe:  python tools/systems/movie_lang_edges.py
"""
import glob, os, subprocess
import numpy as np

ROOT = 'F:/Transformers Rebuild/'
FF = ROOT + 'AssetTools/bin/ffmpeg/ffmpeg.exe'
SR = 8000
W = int(0.35 * SR)

def track(movie, t):
    raw = subprocess.run([FF, '-v', 'error', '-i', ROOT + 'ExtractedAssets/movies/%s.mkv' % movie, '-map', '0:a:%d' % t,
                          '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'], capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32)

total = np.zeros(5); wins = np.zeros(5, int); cues = 0
for txt in sorted(glob.glob(ROOT + 'Game Dump/TransGame/Movies/*.txt')):
    movie = os.path.basename(txt)[:-4]
    lines = open(txt).read().split()
    unit = float(lines[0])
    iv = [(int(a) / unit, int(b) / unit) for a, b, _ in (l.split(',') for l in lines[1:])]
    c = [track(movie, t) for t in range(5, 10)]
    n = min(len(x) for x in c)
    c = np.array([x[:n] for x in c])
    d = c - c.mean(axis=0)
    e = d * d
    def en(k, a, b):
        a, b = max(0, int(a)), min(n, int(b))
        return e[k, a:b].mean() + 1e-10 if b > a else 1e-10
    sc = np.zeros(5)
    for a, b in iv:
        s, t = int(a * SR), int(b * SR)
        for k in range(5):
            sc[k] += np.log10(en(k, s, s + W) / en(k, s - W, s)) + np.log10(en(k, t - W, t) / en(k, t, t + W))
    sc /= max(1, len(iv))
    total += sc * len(iv); cues += len(iv); wins[int(np.argmax(sc))] += 1
    print('%-14s %2d cues  ' % (movie, len(iv)) + '  '.join('t%d %+.2f' % (k + 5, sc[k]) for k in range(5)) + '   best t%d' % (np.argmax(sc) + 5))
print('ALL %d cues   ' % cues + '  '.join('t%d %+.2f' % (k + 5, total[k] / cues) for k in range(5)) + '   movies won: ' +
      ' '.join('t%d=%d' % (k + 5, wins[k]) for k in range(5)))
