# Read-only use of AssetTools' socket reader over every MP weapon skeletal mesh (mp_weapons.json mesh.skeletal_mesh).
import json, os, sys
sys.path.insert(0, r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
os.chdir(r'F:/Transformers Rebuild/AssetTools')
import materials, vs_character
A = materials.assets()
W = json.load(open(r'F:/Transformers Rebuild/AssetTools/manifests/mp_content/mp_weapons.json', encoding='utf8'))['weapons']
out = {'source': 'AssetTools scripts/wfc/vs_character.sockets (SkeletalMeshSocket exports of the cooked weapon meshes), '
                 'run read-only by Gameplay pass 22; glTF-space conversion happens at load (ueSocketToGltf)', 'weapons': {}}
for key, w in W.items():
    m = (w.get('mesh') or {})
    sk = m.get('skeletal_mesh')
    if not sk: continue
    try:
        socks = vs_character.sockets(sk, A)
    except Exception as e:
        socks = None; print('ERR', key, sk, repr(e))
    out['weapons'][w['class']] = {'mesh': sk, 'sockets': socks}
    print(w['class'], len(socks) if socks is not None else 'error')
json.dump(out, open(r'F:/Transformers Rebuild/Rebuild-Gameplay/data/gameplay/weapon_sockets.json', 'w'), indent=1)
