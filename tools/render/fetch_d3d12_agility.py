"""Fetch the pinned Microsoft D3D12 Agility SDK runtime (NuGet Microsoft.Direct3D.D3D12) the optional D3D12 paths use
(PC ADAPTATION, M5 B: FSR 3.1's shaders need shader model 6.6; the in-box Windows 10 runtime stops at 6.5) into
third_party/d3d12_agility/ (not committed: Microsoft's redistributable licence; see its LICENSE.txt after fetching).

  fetch_d3d12_agility.py      downloads + extracts (skips when present)

The exe exports D3D12SDKVersion (= the minor version below) and D3D12SDKPath (".\\D3D12\\"); a build / package puts
D3D12Core.dll (and, for diagnostics, d3d12SDKLayers.dll) into D3D12\\ next to the exe. Only D3D12 code loads it: the
GL-only game never creates a D3D12 device.
"""
import io
import os
import sys
import urllib.request
import zipfile

VERSION = '1.614.1'   # D3D12SDKVersion 614
URL = 'https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/%s' % VERSION
FILES = {'build/native/bin/x64/D3D12Core.dll': 'bin/D3D12Core.dll',
         'build/native/bin/x64/d3d12SDKLayers.dll': 'bin/d3d12SDKLayers.dll',
         'LICENSE.txt': 'LICENSE.txt'}


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'd3d12_agility'))
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
