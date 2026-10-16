"""The 58 weapon / prop materials no compiled key covers (AssetTools work/gltf_wfcmat/missing_matinst_audit.json) - can
Rendering's matc compile them, and what do they need (Integration 2026-10-16: item 6, the RT "night and day" bar)?
Read only: imports Rendering's tools/render (matc, ue3obj) from its worktree, reads the cooked packages (Game Dump) and
ExtractedAssets; never exports textures (TexResolver misses are only listed). Per material:
  packages   the cooked packages holding the object (authored.db objects.package, largest serialized copy first), and which
             MP map packages cook it (_m / MP_*), i.e. whether it is ever seen in multiplayer
  chain      MIC -> ... -> root Material; the root's BlendMode / LightingModel / TwoSided / bUsedWithSkeletalMesh (UE3 draws
             the engine default material on a skeletal mesh whose material lacks that usage flag)
  matc       MatCompiler.build() on a Repo of those packages (+ TransGame / TR_AllShader fallbacks): OK, or the error;
             textures unresolved in ExtractedAssets; the compiler's notes
argv[1] = out json; argv[2] = Rendering's tools/render dir (default its worktree)."""
import json, os, sqlite3, sys, traceback, collections

out = sys.argv[1]
RT = sys.argv[2] if len(sys.argv) > 2 else 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render'
sys.path.insert(0, RT)
import ue3obj, matc   # noqa: E402
from build_materials import TexResolver   # noqa: E402

audit = json.load(open('F:/Transformers Rebuild/AssetTools/work/gltf_wfcmat/missing_matinst_audit.json', encoding='utf-8'))['missing']
db = sqlite3.connect('file:F:/Transformers Rebuild/AssetTools/manifests/authored.db?mode=ro', uri=True)
COOKED = ue3obj.COOKED
have = {f.lower(): f for f in os.listdir(COOKED)}

def pkgs_for(path):
    rows = db.execute("select package, serial_size, class from objects where lower(opath)=? order by serial_size desc", (path.lower(),)).fetchall()
    files, mp, cls = [], [], None
    for pkg, size, c in rows:
        cls = cls or c
        f = have.get((pkg + '.xxx').lower())
        if f and f not in files:
            files.append(f)
        if pkg.upper().startswith('MP_') and pkg not in mp:
            mp.append(pkg)
    return files, mp, cls, (rows[0][1] if rows else 0)

tr_cache = {}
res = []
for k, m in enumerate(audit):
    path = m['material']
    files, mp, cls, size = pkgs_for(path)
    row = {'material': path, 'meshes': m.get('meshes', []), 'class': cls, 'packages': files[:4], 'mp_map_packages': mp, 'serial_size': size}
    if not files:
        row['matc'] = 'NO PACKAGE (not in authored.db / Game Dump)'
        res.append(row); continue
    try:
        # the parent chain may live in other packages (a MIC body cooked without its parent, or the parent only in a shader
        # package): follow authored.db's Parent refs and add the packages holding each link (+ the shared shader packages)
        extra, q, seen = [], path, set()
        while q and q.lower() not in seen and len(seen) < 16:
            seen.add(q.lower())
            r0 = db.execute("select props from objects where lower(opath)=? and props like '%Parent%' order by serial_size desc limit 1", (q.lower(),)).fetchone()
            par = None
            if r0:
                try:
                    par = json.loads(r0[0]).get('Parent')
                except Exception:
                    par = None
            if isinstance(par, dict): par = par.get('ref')
            if not par: break
            pf, _, _, _ = pkgs_for(par)
            extra += [f for f in pf[:2] if f not in files and f not in extra]
            q = par
        row['parent_packages'] = extra
        shader = [f for f in ('TransGame.xxx', 'TR_AllShader_p.xxx', 'ENV_AllShader_p.xxx', 'WEP_AllShader_p.xxx', 'FX_AllShader_p.xxx') if f.lower() in have]
        repo = ue3obj.Repo(files[:3], fallback=[have[f.lower()] for f in shader] + extra)
        chain, p = [], path
        while p and repo.cls(p) == 'MaterialInstanceConstant' and len(chain) < 16:
            chain.append(p); p = ((repo.obj(p) or {}).get('Parent') or {}).get('ref')
        chain.append(p)
        row['chain'] = chain
        ro = repo.obj(p) or {}
        row['root_class'] = repo.cls(p)
        for key in ('BlendMode', 'LightingModel', 'TwoSided', 'bUsedWithSkeletalMesh', 'bUsedWithStaticLighting', 'bUsedAsSpecialEngineMaterial',
                    'bIsMasked', 'bUsedWithParticleSprites', 'bUsedWithBeamTrails', 'bUsedWithMeshParticles'):
            if key in ro:
                v = ro[key]
                row[key] = v if isinstance(v, (bool, int, float, str)) else str(v)
        out_dir = os.path.join(os.path.dirname(out), 'matc58_tex')
        tr = TexResolver(repo, out_dir) if 'tr' not in tr_cache else tr_cache['tr']
        tr.R = repo
        tr_cache['tr'] = tr
        tr.missing.clear(); tr.cache.clear()
        rt = path.split('.')[0].upper().startswith(('TR_', 'WEP_'))
        mc = matc.MatCompiler(repo, path, tr, runtime_params=rt)
        glsl, info = mc.build()
        row['matc'] = 'OK'
        row['glsl_lines'] = glsl.count('\n')
        row['textures'] = [{'object': str(t.get('object')), 'file': bool(t.get('file'))} for t in (info or {}).get('textures', [])]
        row['textures_missing'] = sorted({str(t.get('object')) for t in (info or {}).get('textures', []) if not t.get('file')})
        row['notes'] = (info or {}).get('notes', [])[:6] if isinstance(info, dict) else []
        for key in ('blend', 'blend_mode', 'lighting_model', 'two_sided', 'masked'):
            if isinstance(info, dict) and key in info:
                row['info_' + key] = info[key]
    except Exception as ex:
        row['matc'] = 'ERROR %s: %s' % (type(ex).__name__, str(ex)[:300])
        row['trace'] = traceback.format_exc().splitlines()[-3:]
    res.append(row)
    print('%2d %-80s %s' % (k, path[:80], row['matc'][:90]), flush=True)
summary = collections.Counter('OK' if r['matc'] == 'OK' else r['matc'].split(':')[0] for r in res)
json.dump({'generated_by': 'tools/systems/matc58_audit.py', 'summary': dict(summary), 'materials': res}, open(out, 'w', encoding='utf-8'), indent=1)
print(dict(summary))
