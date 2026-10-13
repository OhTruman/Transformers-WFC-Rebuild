"""Fetch the pinned AMD FidelityFX shadow denoiser sources (FidelityFX SDK v1.1.4, MIT) into third_party/ffx_denoiser/
(not committed; same pattern as fetch_ffx_sdk.py). PC ADAPTATION, M7 stage 1: the ray-traced sun shadow is denoised by
its five compute passes (prepare shadow mask, tile classification, three soft-shadow filter passes), compiled by the
pinned DXC at build time with their own embedded root signatures; nothing of it ships beside the exe. (SDK v2.x's
denoiser is FSR Ray Regeneration, which needs a Radeon RX 9000 series GPU.)

  fetch_ffx_denoiser.py      downloads (skips when present)
"""
import json
import os
import sys
import urllib.request

TAG = 'v1.1.4'
RAW = 'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/%s/' % TAG
TREE = 'https://api.github.com/repos/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/git/trees/%s?recursive=1' % TAG


def wanted(path):
    if path == 'LICENSE.txt': return 'LICENSE.txt'
    g = 'sdk/include/FidelityFX/gpu/'
    if path.startswith(g) and path.count('/') == 4 and path.endswith('.h'): return 'gpu/' + path[len(g):]
    if path.startswith(g + 'denoiser/') and path.count('/') == 5 and path.endswith('.h'): return 'gpu/' + path[len(g):]
    s = 'sdk/src/backends/dx12/shaders/denoiser/'
    if path.startswith(s) and ('shadow' in path) and path.endswith('.hlsl'): return 'passes/' + path[len(s):]
    return None


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'ffx_denoiser'))
    if os.path.exists(os.path.join(root, 'VERSION.txt')):
        print('present:', root); return 0
    tree = json.load(urllib.request.urlopen(TREE, timeout=60))
    files = [(p['path'], wanted(p['path'])) for p in tree.get('tree', []) if p.get('type') == 'blob']
    files = [(src, dst) for src, dst in files if dst]
    if len(files) < 10: print('unexpected tree (%d files)' % len(files)); return 1
    for src, dst in files:
        out = os.path.join(root, dst); os.makedirs(os.path.dirname(out), exist_ok=True)
        data = urllib.request.urlopen(RAW + src, timeout=60).read()
        open(out, 'wb').write(data)
        print('wrote', dst, len(data))
    open(os.path.join(root, 'VERSION.txt'), 'w').write(TAG + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
