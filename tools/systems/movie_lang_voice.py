"""Match the per-language centre tracks (5..9) of the 10-track WFC movies to languages by voice spectrum (read-only).

Reference voices: the centre track (5) of the single-language text movies FMV_*text_<LANG> (INT / FRA / ESN narration).
For every 10-track dialogue movie, each centre track's long-term log-mel spectrum over its loudest (speech) frames is
compared (cosine distance after mean removal) with each language's reference; the per-track nearest language and a
per-movie optimal one-to-one assignment are printed.
Run with AssetTools/bin/py/python.exe:  python tools/systems/movie_lang_voice.py
"""
import glob, itertools, os, subprocess
import numpy as np

R = 'F:/Transformers Rebuild/'
FF = R + 'AssetTools/bin/ffmpeg/ffmpeg.exe'
SR = 16000

def track(path, t):
    raw = subprocess.run([FF, '-v', 'error', '-i', path, '-map', '0:a:%d' % t, '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                         capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32)

def mel_fb(n_fft=512, n_mels=40):
    f = np.linspace(0, SR / 2, n_fft // 2 + 1)
    mel = lambda x: 2595 * np.log10(1 + x / 700.0)
    pts = 700 * (10 ** (np.linspace(mel(80), mel(7600), n_mels + 2) / 2595) - 1)
    fb = np.zeros((n_mels, len(f)))
    for i in range(n_mels):
        a, b, c = pts[i], pts[i + 1], pts[i + 2]
        fb[i] = np.clip(np.minimum((f - a) / (b - a), (c - f) / (c - b)), 0, None)
    return fb

FB = mel_fb()

def profile(x):
    n = 512
    fr = np.lib.stride_tricks.sliding_window_view(x, n)[::256] * np.hanning(n)
    sp = np.abs(np.fft.rfft(fr, axis=1)) ** 2
    lm = np.log(sp @ FB.T + 1e-10)
    e = lm.mean(axis=1)
    keep = e >= np.percentile(e, 70)              # loudest 30 % of frames (speech-dominated)
    p = lm[keep].mean(axis=0)
    return p - p.mean()

def cos(a, b):
    return float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b)))

refs = {}
for lang in ('INT', 'FRA', 'ESN'):
    ps = [profile(track(f, 5)) for f in sorted(glob.glob(R + 'ExtractedAssets/movies/FMV_*text_%s.mkv' % lang))]
    if ps: refs[lang] = np.mean(ps, axis=0); print('reference %s: %d text movies' % (lang, len(ps)))
langs = list(refs)
votes = {k: {l: 0 for l in langs} for k in range(5, 10)}
for txt in sorted(glob.glob(R + 'Game Dump/TransGame/Movies/*.txt')):
    movie = os.path.basename(txt)[:-4]
    path = R + 'ExtractedAssets/movies/%s.mkv' % movie
    ps = {k: profile(track(path, k)) for k in range(5, 10)}
    sim = {(k, l): cos(ps[k], refs[l]) for k in ps for l in langs}
    # best one-to-one assignment of the reference languages to distinct tracks
    best = max(itertools.permutations(range(5, 10), len(langs)), key=lambda p: sum(sim[(p[i], langs[i])] for i in range(len(langs))))
    for i, l in enumerate(langs): votes[best[i]][l] += 1
    print('%-12s ' % movie + '  '.join('t%d:' % k + ','.join('%s%.2f' % (l[0], sim[(k, l)]) for l in langs) for k in range(5, 10)) +
          '   assign ' + ' '.join('%s=t%d' % (langs[i], best[i]) for i in range(len(langs))))
print('votes (one-to-one assignments across movies):')
for k in range(5, 10): print('   track %d: %s' % (k, votes[k]))
