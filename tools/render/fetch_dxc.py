"""Fetch the pinned Microsoft DirectX Shader Compiler (NuGet Microsoft.Direct3D.DXC) into third_party/dxc/ (not committed;
LLVM / MIT / Microsoft licences: see LICENSE-*.txt after fetching). PC ADAPTATION, M7: the ray-traced D3D12 passes
(RayQuery, SM 6.5+) are HLSL compiled to DXIL at build time and embedded in the exe, so nothing of DXC ships: the build
runs dxc.exe (+ dxcompiler.dll, dxil.dll beside it); the game never loads them.

  fetch_dxc.py      downloads + extracts (skips when present)
"""
import io
import os
import sys
import urllib.request
import zipfile

VERSION = '1.9.2609.5'
URL = 'https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/%s' % VERSION
FILES = {'build/native/bin/x64/dxc.exe': 'bin/dxc.exe',
         'build/native/bin/x64/dxcompiler.dll': 'bin/dxcompiler.dll',
         'build/native/bin/x64/dxil.dll': 'bin/dxil.dll',
         'LICENSE-MS.txt': 'LICENSE-MS.txt', 'LICENSE-LLVM.txt': 'LICENSE-LLVM.txt', 'LICENCE-MIT.txt': 'LICENCE-MIT.txt'}


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'dxc'))
    if all(os.path.exists(os.path.join(root, d)) for d in FILES.values()):
        print('present:', root); return 0
    print('downloading', URL)
    data = urllib.request.urlopen(URL, timeout=120).read()
    z = zipfile.ZipFile(io.BytesIO(data))
    names = {n.lower(): n for n in z.namelist()}
    for src, dst in FILES.items():
        n = names.get(src.lower())
        if not n: print('missing in package:', src); return 1
        out = os.path.join(root, dst); os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, 'wb').write(z.read(n))
        print('wrote', out, os.path.getsize(out))
    open(os.path.join(root, 'VERSION.txt'), 'w').write(VERSION + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
