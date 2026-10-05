# Extract given SoundNodeWave objects from BOTH localized twins (_LOC_int / _LOC_FRA) of a map package into
# work/m8_loc/<lang>/ (read-only use of AssetTools' ue3pkg + sounds.locate_fsb; nothing written outside work/).
import os, sys, subprocess, hashlib
AT = 'F:/Transformers Rebuild/AssetTools/scripts/wfc'
sys.path.insert(0, AT)
import ue3pkg, sounds  # noqa
COOKED = 'F:/Transformers Rebuild/Game Dump/TransGame/CookedXenon/'
OUT = 'F:/Transformers Rebuild/Rebuild-Systems/work/m8_loc/'
VGM = 'F:/Transformers Rebuild/AssetTools/bin/vgmstream/vgmstream-cli.exe'
base = sys.argv[1]                      # e.g. MP_ESC_BrokenHope_Base_m
objs = sys.argv[2:]                     # e.g. WL_DX_OPRIME.DX_OPRIME_LK002657
for lang in ('int', 'FRA'):
    pkg = ue3pkg.Package(COOKED + '%s_LOC_%s.xxx' % (base, lang))
    want = {o.lower() for o in objs}
    for i, e in enumerate(pkg.exports):
        if pkg.class_name(e) not in ('SoundNodeWaveEx', 'SoundNodeWave'): continue
        op = pkg.object_path(i + 1)
        if op.lower() not in want: continue
        blob, how = sounds.locate_fsb(pkg, e)
        g, nm = op.split('.', 1)
        d = OUT + 'tree/' + lang + '/' + g + '/'
        os.makedirs(d, exist_ok=True)
        fsb = d + nm + '.fsb'
        open(fsb, 'wb').write(blob)
        wav = fsb[:-4] + '.wav'
        subprocess.run([VGM, '-o', wav, fsb], capture_output=True)
        print(lang, op, how, len(blob), hashlib.md5(open(wav, 'rb').read()).hexdigest()[:12] if os.path.exists(wav) else 'no wav',
              os.path.getsize(wav) if os.path.exists(wav) else 0)
