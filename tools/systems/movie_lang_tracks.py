"""Identify which per-language centre track of a WFC movie carries the INT (English) dialogue (read-only).

The shipped Bink movies carry 10 mono tracks: 0..4 shared by every language, 5..9 identical in the dialogue-free logos
but different in the dialogue movies (one centre track per language). For each of 5..9 the language-specific part
(track minus the mean of 5..9) is compared with the INT subtitle timing (Game Dump Movies/<movie>.txt, first line =
units per second): the fraction of its energy that falls inside subtitle intervals.
Run with AssetTools/bin/py/python.exe:  python tools/systems/movie_lang_tracks.py <movie> ...
"""
import subprocess, sys
import numpy as np

ROOT = 'F:/Transformers Rebuild/'
FF = ROOT + 'AssetTools/bin/ffmpeg/ffmpeg.exe'
SR = 8000

def track(movie, t):
    raw = subprocess.run([FF, '-v', 'error', '-i', ROOT + 'ExtractedAssets/movies/%s.mkv' % movie, '-map', '0:a:%d' % t,
                          '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'], capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32)

for movie in sys.argv[1:]:
    lines = open(ROOT + 'Game Dump/TransGame/Movies/%s.txt' % movie).read().split()
    unit = float(lines[0])
    iv = [(int(a) / unit, int(b) / unit) for a, b, _ in (l.split(',') for l in lines[1:])]
    c = [track(movie, t) for t in range(5, 10)]
    n = min(len(x) for x in c)
    c = np.array([x[:n] for x in c])
    mean = c.mean(axis=0)
    mask = np.zeros(n, bool)
    for a, b in iv:
        mask[int(a * SR):min(n, int(b * SR))] = True
    print('%s: %d subtitle intervals (%.1f s of %.1f s)' % (movie, len(iv), mask.sum() / SR, n / SR))
    for k in range(5):
        d = c[k] - mean
        e = d * d
        inside = e[mask].sum() / max(1e-12, e.sum())
        print('   track %d: language-specific RMS %.1f dB, %.0f%% of its energy inside the INT subtitle intervals' % (
            k + 5, 10 * np.log10(max(1e-12, e.mean())), 100 * inside))
