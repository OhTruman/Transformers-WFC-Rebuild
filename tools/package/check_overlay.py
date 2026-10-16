"""verify_slim's static glTF overlay row (Integration 2026-10-16): in a built package, the barrier glTF
(ExtractedAssets/content/WEP_Shield_p/Barrier/WEP_Barrier_SKEL.gltf, shipped as .gltf or its .xpr twin) must carry
materials[].extras.wfc_material = WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST; with the overlay recorded in
build_slim.json (FROZEN.json beside it), every overlay file's shipped bytes (decompressed) must equal the frozen file's sha256.
Prints PASS / FAIL lines; exit 1 on any FAIL. Read only.
  check_overlay.py <package dir (holds build_slim.json and 'Transformers Rebuild')>"""
import hashlib, json, os, sys

pkg = sys.argv[1]
top = os.path.join(pkg, 'Transformers Rebuild')
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild/tools/render')
import package_lossless as pl
cab = None

def shipped(rel):
    """the bytes the game reads for ROOT-relative rel: the plain file, or the decompressed .xpr twin; None if neither"""
    global cab
    p = os.path.join(top, rel)
    if os.path.isfile(p):
        return open(p, 'rb').read()
    if os.path.isfile(p + '.xpr'):
        cab = cab or pl._api()
        return pl.decompress(cab, open(p + '.xpr', 'rb').read())
    return None

fails = 0
BARRIER, WANT = 'ExtractedAssets/content/WEP_Shield_p/Barrier/WEP_Barrier_SKEL.gltf', 'WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST'
data = shipped(BARRIER)
if data is None:
    print('FAIL overlay.barrier: %s not shipped' % BARRIER); fails += 1
else:
    mats = json.loads(data.decode('utf-8')).get('materials', [])
    got = [(m.get('extras') or {}).get('wfc_material') for m in mats]
    ok = WANT in got
    fails += not ok
    print('%s overlay.barrier: extras.wfc_material %s (want %s)' % ('PASS' if ok else 'FAIL', got, WANT))
man = json.load(open(os.path.join(pkg, 'build_slim.json'), encoding='utf-8'))
ov = man.get('overlay')
if not ov:
    print('FAIL overlay.frozen: build_slim.json records no --overlay'); fails += 1
else:
    fz = json.load(open(os.path.join(ov['dir'], 'FROZEN.json'), encoding='utf-8'))
    bad = []
    for f in fz['files']:
        d = shipped('ExtractedAssets/' + f['path'])
        if d is None or hashlib.sha256(d).hexdigest() != f['sha256'].lower():
            bad.append(f['path'])
    fails += bool(bad)
    print('%s overlay.frozen: %d of %d files of %s (%s) ship byte-identical%s' % ('FAIL' if bad else 'PASS', len(fz['files']) - len(bad),
          len(fz['files']), fz.get('name', ''), fz.get('commit', ''), ('; differ: ' + ', '.join(bad[:5])) if bad else ''))
sys.exit(1 if fails else 0)
