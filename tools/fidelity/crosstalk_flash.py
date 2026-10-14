# Muzzle flashes on the idle local player's gun (weapon-crosstalk.ps1). argv: idleframesdir out.json [largePx]
# The player gives no input, so the camera is static between respawns; a frame has a flash EVENT when, with the view unchanged
# across +-4 captures (mean |max-channel difference| < 4 at 320x180), >= largePx pixels (at 320x180) are 80+ brighter than the
# darker of those two neighbours and warm or white. Large = near the camera: on a17309b7 every such event was a muzzle blast
# drawn at the player's own gun (a bot's flash re-anchored to the local MuzzleFlash socket); distant bot fire stays far below.
# Consecutive flagged captures (gap <= 3) are one event. events.png: up to 9 events for review.
import sys, os, json
import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
large = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
fs = sorted(f for f in os.listdir(src) if f.lower().endswith('.bmp'))
if len(fs) < 20:
    json.dump({"status": "UNKNOWN", "note": f"{len(fs)} frames"}, open(out, 'w')); print("UNKNOWN: too few frames"); sys.exit(0)
S = np.stack([np.asarray(Image.open(os.path.join(src, f)).convert('RGB').resize((320, 180)), dtype=np.int16) for f in fs])
mx = S.max(axis=3); n = len(fs)
hits = []; static = 0
for i in range(4, n - 4):
    if np.abs(mx[i - 4].astype(np.int32) - mx[i + 4]).mean() >= 4: continue
    static += 1
    ref = np.minimum(mx[i - 4], mx[i + 4]); r, g, b = S[i, ..., 0], S[i, ..., 1], S[i, ..., 2]
    sp = ((mx[i] - ref) > 80) & ((r >= b + 30) | ((r > 200) & (g > 200) & (b > 200)))
    c = int(sp.sum())
    if c >= large: hits.append((i, c))
ev = []
for i, c in hits:
    if ev and i - ev[-1][1] <= 3: ev[-1][1] = i; ev[-1][2] = max(ev[-1][2], c)
    else: ev.append([i, i, c])
res = {"frames": n, "static_frames": static, "large_px": large, "flash_frames": len(hits), "events": len(ev),
       "first": fs[ev[0][0]] if ev else None, "list": [[fs[a], fs[b], c] for a, b, c in ev[:100]]}
json.dump(res, open(out, 'w'), indent=1)
if ev:
    pick = [max(range(a, b + 1), key=lambda k: dict(hits).get(k, 0)) for a, b, c in ev[:9]]
    sheet = Image.new('RGB', (3 * 640, ((len(pick) + 2) // 3) * 360))
    for k, i in enumerate(pick): sheet.paste(Image.open(os.path.join(src, fs[i])).convert('RGB').resize((640, 360)), ((k % 3) * 640, (k // 3) * 360))
    sheet.save(os.path.join(os.path.dirname(out), "events.png"))
print(f"idle frames {n} ({static} with a static view): large flash events {len(ev)} ({len(hits)} frames), first {res['first']}")
