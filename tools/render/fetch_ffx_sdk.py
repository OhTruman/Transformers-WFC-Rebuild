"""Fetch the pinned AMD FidelityFX / FSR SDK pieces the optional D3D12 upscaling / frame-generation path uses
(PC ADAPTATION, A3a) into third_party/ffx_sdk/ (not committed: AMD's licence permits binary redistribution with its
notice; see third_party/ffx_sdk/license.md after fetching).

  fetch_ffx_sdk.py            downloads (skips files already present with the expected size)

The game loads amd_fidelityfx_loader_dx12.dll at run time; a package ships the three DLLs + license.md next to the
exe. Pinned to SDK tag v2.3.0 (FSR upscaling 3.1.5 / FSR 4, frame generation 3.1.6).
"""
import os
import sys
import urllib.request

TAG = 'v2.3.0'
BASE = 'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/%s/' % TAG
MEDIA = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/raw/%s/' % TAG   # (binary files)
FILES = [
    ('Kits/FidelityFX/docs/license.md', 'license.md'),
    ('Kits/FidelityFX/api/include/ffx_api.h', 'include/ffx_api.h'),
    ('Kits/FidelityFX/api/include/ffx_api.hpp', 'include/ffx_api.hpp'),
    ('Kits/FidelityFX/api/include/ffx_api_types.h', 'include/ffx_api_types.h'),
    ('Kits/FidelityFX/api/include/ffx_api_loader.h', 'include/ffx_api_loader.h'),
    ('Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h', 'include/dx12/ffx_api_dx12.h'),
    ('Kits/FidelityFX/api/include/dx12/ffx_api_dx12.hpp', 'include/dx12/ffx_api_dx12.hpp'),
    ('Kits/FidelityFX/upscalers/include/ffx_upscale.h', 'include/ffx_upscale.h'),
    ('Kits/FidelityFX/upscalers/include/ffx_upscale.hpp', 'include/ffx_upscale.hpp'),
    ('Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h', 'include/ffx_framegeneration.h'),
    ('Kits/FidelityFX/framegeneration/include/ffx_framegeneration.hpp', 'include/ffx_framegeneration.hpp'),
    ('Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h', 'include/dx12/ffx_api_framegeneration_dx12.h'),
    ('Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp', 'include/dx12/ffx_api_framegeneration_dx12.hpp'),
    ('Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll', 'bin/amd_fidelityfx_loader_dx12.dll'),
    ('Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll', 'bin/amd_fidelityfx_upscaler_dx12.dll'),
    ('Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll', 'bin/amd_fidelityfx_framegeneration_dx12.dll'),
]


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'ffx_sdk')
    for src, dst in FILES:
        out = os.path.normpath(os.path.join(root, dst))
        if os.path.exists(out) and os.path.getsize(out) > 0:
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        url = (MEDIA if src.endswith('.dll') else BASE) + src
        data = urllib.request.urlopen(url, timeout=120).read()
        if src.endswith('.dll') and data[:2] != b'MZ':
            print('NOT A DLL (LFS pointer?):', src); return 1
        open(out, 'wb').write(data)
        print('%8d  %s' % (len(data), dst))
    print('FFX SDK %s ready in %s' % (TAG, os.path.normpath(root)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
