"""Compile every material used by a map's world.glb (and optional extra material paths) from the
ORIGINAL cooked graphs into GLSL material functions + texture binding tables.

Output: <out>/materials_glsl.json
    { "<material object path>": { "glsl": "...", "info": {...}, "error": null } }
Textures missing from ExtractedAssets/content are exported with umodel from the map packages
into <out>/tex/ (never into the shared extraction tree).

    python build_materials.py MP_IAC_Streets <out_dir> [extra_material_path ...]
"""
import json, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages, CONTENT, COOKED  # noqa: E402
import matc  # noqa: E402
import impact_decals  # noqa: E402
import xbox_texture  # noqa: E402

UMODEL = r'F:/Transformers Rebuild/AssetTools/bin/umodel/umodel_64.exe'
VS = r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice'


def glb_json(path):
    f = open(path, 'rb').read()
    n = struct.unpack_from('<I', f, 12)[0]
    return json.loads(f[20:20 + n])


class TexResolver:
    def __init__(self, repo, out):
        self.R = repo
        self.out = out
        self.by_name = {}
        for root, _, files in os.walk(CONTENT):
            for fn in files:
                if fn.lower().endswith('.png'):
                    self.by_name.setdefault(fn[:-4].lower(), []).append(os.path.join(root, fn))
        self.missing = {}
        self.cache = {}

    def props(self, path):
        o = self.R.obj(path) or {}
        um = o.get('UnpackMin'); ux = o.get('UnpackMax')
        unpack = [um.get(k, 0.0) for k in range(4)] if isinstance(um, dict) else None
        # [CONF] DXT5 xGxA normal maps (X in alpha, Y in green; UnpackMin authored -1 on R,G,B only):
        # the ORIGINAL compiled character pixel shader unpacks BOTH fetched channels, (A, G) * 2 - 1
        # (MP_IAC_Streets_BASE_m ShaderCache, reconstructed-normal UberLight PS: tfetch .yw then
        # mad r.xy, r.zy, c251.x(=2.0), c254.w(=-1.0)). Apply the R unpack to alpha as well.
        if o.get('Format') == 'PF_DXT5' and unpack and unpack[:3] == [-1.0, -1.0, -1.0] and unpack[3] == 0.0:
            unpack[3] = -1.0
        return {'srgb': bool(o.get('SRGB', True)), 'format': o.get('Format'),
                'address_x': o.get('AddressX', 'TA_Wrap'), 'address_y': o.get('AddressY', 'TA_Wrap'),
                'lod_group': o.get('LODGroup'), 'size': [o.get('SizeX'), o.get('SizeY')],
                'unpack_min': unpack,
                'compression': o.get('CompressionSettings')}

    def native_decode(self, path, cls, name):
        """Decode Xbox-tiled cooked TextureCube / TextureFlipBook data (tools/render/xbox_texture.py)
        that umodel does not export. Writes PNGs into <out>/tex (never the shared tree)."""
        from PIL import Image
        os.makedirs(os.path.join(self.out, 'tex'), exist_ok=True)
        o = self.R.obj(path) or {}
        fmt = o.get('Format', 'PF_DXT1')
        try:
            _, tail = self.R.native_tail(path)
            if cls == 'TextureCube':
                files = [os.path.join(self.out, 'tex', '%s_f%d.png' % (name, k)) for k in range(6)]
                if not all(os.path.exists(x) for x in files):
                    for img, fn in zip(xbox_texture.decode_cube(tail, fmt, int(o.get('EdgeSize', 256))), files):
                        Image.fromarray(img).save(fn)
                return files[0], files
            fn = os.path.join(self.out, 'tex', name + '.png')
            if not os.path.exists(fn):
                mips, _ = xbox_texture.read_mip_chain(tail, 0)
                sx, sy, data = next(m for m in mips if m[2])
                Image.fromarray(xbox_texture.decode_mip(sx, sy, data, fmt)).save(fn)
            return fn, None
        except Exception as ex:
            print('native decode failed %s: %s' % (path, ex))
            return None, None

    def __call__(self, path):
        if not path: return None
        if path in self.cache: return self.cache[path]
        parts = path.split('.')
        name = parts[-1]
        cands = [os.path.join(CONTENT, *parts[:-1], name + '.png'),
                 os.path.join(CONTENT, *parts[:-2], name + '.png')]
        f = next((c for c in cands if os.path.exists(c)), None)
        if not f:
            lst = self.by_name.get(name.lower(), [])
            pref = [x for x in lst if parts[0].lower() in x.lower()]
            f = (pref or lst or [None])[0]
        cls = self.R.cls(path)
        faces = None
        if not f and cls in ('TextureCube', 'TextureFlipBook') and self.R.find(path):
            f, faces = self.native_decode(path, cls, name)
        if not f:
            loc = os.path.join(self.out, 'tex', name + '.png')
            f = loc if os.path.exists(loc) else None
            if not f:
                self.missing[path] = loc
        d = {'object': path, 'file': f.replace('\\', '/') if f else None, 'class': cls}
        if faces: d['faces'] = [x.replace('\\', '/') for x in faces]
        d.update(self.props(path))
        self.cache[path] = d
        return d


def umodel_export(repo, objs, out):
    """Export missing texture objects from whichever loaded package holds them."""
    tmp = os.path.join(out, 'umodel_tex')
    by_pkg = {}
    for path in objs:
        h = repo.find(path)
        if not h: continue
        by_pkg.setdefault(repo.pkgs[h[0]].name, []).append(path.split('.')[-1])
    for pkg, names in by_pkg.items():
        for k in range(0, len(names), 40):
            args = [UMODEL, '-export', '-path=' + COOKED, '-game=trans', '-out=' + tmp, '-png', pkg + '.xxx'] + \
                   ['-obj=' + n for n in names[k:k + 40]]
            subprocess.run(args, capture_output=True, text=True)
    os.makedirs(os.path.join(out, 'tex'), exist_ok=True)
    got = 0
    for root, _, files in os.walk(tmp):
        for fn in files:
            if fn.endswith('.png'):
                dst = os.path.join(out, 'tex', fn)
                if not os.path.exists(dst):
                    os.replace(os.path.join(root, fn), dst)
                got += 1
    return got


def resolve_default_slots(repo, j):
    """world.glb sections that AssetTools left on WFC_Default (its asset DB could not resolve the
    static mesh's section material). umodel's per-mesh glTF still names the section material
    (FStaticMeshElement.Material); resolve that name against the map packages' Material/MIC
    exports (same source package preferred). 'dummy_material_N' = null reference in the original
    (UE3 renders those with the engine default material) and stays unresolved."""
    from collections import defaultdict
    byname = defaultdict(list)
    for k, (pi, ix) in repo.index.items():
        pk = repo.pkgs[pi]
        if pk.class_name(pk.exports[ix - 1]) in ('Material', 'MaterialInstanceConstant'):
            byname[k.rsplit('.', 1)[-1]].append(pk.object_path(ix))
    nomat = {i for i, m in enumerate(j['materials']) if not m.get('extras', {}).get('wfc_material')}
    meshname = {n['mesh']: n.get('extras', {}).get('mesh') for n in j['nodes'] if 'mesh' in n}
    out = {}
    for mi, m in enumerate(j['meshes']):
        for k, pr in enumerate(m['primitives']):
            if pr.get('material') not in nomat: continue
            mesh = meshname.get(mi)
            if not mesh: continue
            gl = os.path.join(CONTENT, *mesh.split('.')) + '.gltf'
            if not os.path.exists(gl): continue
            g = json.load(open(gl, encoding='utf-8'))
            # vs_map skipped empty LOD sections; keep the same order of non-empty primitives
            prims = [q for q in g['meshes'][0]['primitives'] if g['accessors'][q['indices']]['count'] > 0]
            if k >= len(prims): continue
            nm = (g['materials'][prims[k].get('material', 0)].get('name') or '').lower()
            cands = byname.get(nm, [])
            if not cands: continue
            pref = [c for c in cands if c.split('.')[0].lower() == mesh.split('.')[0].lower()]
            out[(mesh, k)] = (pref or cands)[0]
    return out


def main():
    mapname = sys.argv[1]
    out = sys.argv[2]
    extra = [a.strip() for a in sys.argv[3:] if a.strip()]   # tolerate CRLF list files
    os.makedirs(out, exist_ok=True)
    # TransGame.xxx (startup package) cooks the pickup FX and their materials (AssetTools 7a69756
    # streets_pickup_fx.json: package TransGame); map copies win when both exist (largest export).
    # UI_GFxHud_p: the HUD post-process chain materials (Milestone E: LowHealth / StaticDischarge screen effects)
    repo = Repo(list(reversed(map_packages(mapname)[0])), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx', 'UI_GFxHud_p.xxx'])
    j = glb_json(os.path.join(VS, 'Maps', mapname, 'world.glb'))
    names = {m.get('extras', {}).get('wfc_material') for m in j['materials']}
    for extra_glb in ('bsp.glb', 'decals.glb'):  # rebuilt by build_lighting.py (run it first)
        f = os.path.join(out, extra_glb)
        if os.path.exists(f):
            names |= {m.get('extras', {}).get('wfc_material') for m in glb_json(f).get('materials', [])}
    slot_map = resolve_default_slots(repo, j)
    names |= set(slot_map.values())
    json.dump({'%s|%d' % k: v for k, v in slot_map.items()},
              open(os.path.join(out, 'slot_materials.json'), 'w'), indent=1)
    print('default-material slots resolved to original materials: %d' % len(slot_map))
    # M33: MaterialInstanceActors (Matinee MaterialParamTracks drive their MIC's scalar / vector parameters, e.g. the
    # lobby faction emblems' Highlighted / Opacity): their MICs compile with every parameter settable at runtime, and
    # the actor -> MIC table lets the renderer route IRenderer::setFrontendMaterialParam to the right material.
    mia = {}
    for path in repo.index:
        if (repo.cls(path) or '') != 'MaterialInstanceActor' or 'default__' in path: continue
        mi = (repo.obj(path) or {}).get('MatInst')
        mi = mi.get('ref') if isinstance(mi, dict) else mi
        if mi: mia[path.split('.')[-1]] = mi
    json.dump({'generated_by': 'tools/render/build_materials.py', 'actors': mia},
              open(os.path.join(out, 'material_instance_actors.json'), 'w'), indent=1)
    mia_mats = {m.lower() for m in mia.values()}
    if mia: print('material instance actors: %d (runtime parameters)' % len(mia))
    # M72: materials the world references but this map's packages don't export (e.g. DES_IAC_WallPanelSign_p's
    # MICs: the destructible's class package is a seekfree stub; the objects are cooked into other maps' packages).
    # The packages of the other MP maps that export them are added as fallbacks (original cooked data, not a
    # substitute); the map's own copies still win.
    def usable(r, n):                 # a Material, or a MIC whose cooked body names its Parent (stubs don't)
        c = r.cls(n)
        return c == 'Material' or (c == 'MaterialInstanceConstant' and bool((r.obj(n) or {}).get('Parent')))
    missing = sorted(n for n in (names - {None}) | set(extra) if not usable(repo, n))
    print('materials missing from this map: %d' % len(missing))
    if missing:
        ufb = []
        for other in sorted(os.listdir(os.path.join(VS, 'Maps'))):
            if not other.startswith('MP_') or other == mapname or not missing: continue
            for pk in map_packages(other)[0]:
                pr = Repo([pk])
                found = [n for n in missing if usable(pr, n)]
                if found:
                    ufb.append(pk)
                    missing = [n for n in missing if n not in found]
        if ufb:
            print('materials from other maps packages: %s' % ', '.join(ufb))
            repo = Repo(list(reversed(map_packages(mapname)[0])), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx', 'UI_GFxHud_p.xxx'] + ufb)
    mats = sorted(names - {None}) + extra
    # M74 energy-death (Defrag) instances: the form-mesh package -> instance table goes with the render data
    ed = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'energy_death_materials.json'), encoding='utf-8'))
    defrag_mats = {m.lower() for m in ed['materials']}
    # M76 weapon impact decals: per-map tables (weapon / surface / material -> PhysMaterial) and their decal materials
    impact = impact_decals.build(repo, sorted(names - {None}))
    for dm in impact_decals.decal_materials(impact):
        if dm not in mats: mats.append(dm)
    print('impact decals: %d weapons, %d surfaces, %d materials with a PhysMaterial, decal materials %s' % (
        len(impact['weapons']), len(impact['surfaces']), len(impact['materials']), impact_decals.decal_materials(impact)))
    tr = TexResolver(repo, out)

    # M70: weapon mesh MaterialParameterModifiers (TnWeaponMesh.SetMaterialParameter(index, value) sets the named
    # parameter on the held weapon's material, e.g. PlasmaCannon_WEPMESH [1] = MPT_WeaponSpecific 'Overheat' = the charge
    # glow): those names are runtime parameters of WEP_ materials
    weapon_rt = set()
    for k in repo.index:
        if '_wepmesh' not in k or k.count('.') != 1: continue
        for mod in (repo.obj(k) or {}).get('MaterialParameterModifiers') or []:
            f = {x.get('name'): x.get('value') for x in mod} if isinstance(mod, list) else dict(mod)
            if f.get('ParameterName') and f.get('ParameterName') != 'None': weapon_rt.add(f['ParameterName'])
    if weapon_rt: print('weapon material parameters (runtime): %s' % ', '.join(sorted(weapon_rt)))

    def run():
        res = {}
        for mp in mats:
            try:
                # TnCharacterApplier targets character meshes and their weapon only
                rt = mp in extra and mp.split('.')[0].upper().startswith(('TR_', 'WEP_'))
                if mp.lower() in mia_mats:
                    rt = 'all'                 # Matinee-driven MIC (MaterialInstanceActor): parameters per frame
                if mp in extra and mp.split('.')[0].upper().startswith('UI_'):
                    rt = 'all'                 # Canvas materials: parameters set per draw (MaterialInstanceDynamic)
                wep_rt = weapon_rt if mp.split('.')[0].upper().startswith('WEP_') else ()
                if mp.lower() in defrag_mats:
                    wep_rt = ('Defrag',)       # M74: TnDefragger ramps the scalar 'Defrag' 1 -> 0 per draw owner
                mc = matc.MatCompiler(repo, mp, tr, runtime_params=rt, extra_runtime=wep_rt)
                glsl, info = mc.build()
                res[mp] = {'glsl': glsl, 'info': info, 'error': None}
            except Exception as ex:
                res[mp] = {'glsl': None, 'info': None, 'error': '%s: %s' % (type(ex).__name__, ex)}
        return res

    res = run()
    if tr.missing:
        n = umodel_export(repo, list(tr.missing), out)
        print('exported %d missing textures via umodel' % n)
        tr.cache.clear(); tr.missing.clear()
        res = run()
    json.dump(res, open(os.path.join(out, 'materials_glsl.json'), 'w'), indent=1)
    json.dump(impact, open(os.path.join(out, 'impact_decals.json'), 'w'), indent=1)
    # M74: form-mesh package -> energy-death instance, for the instances that compiled
    json.dump({'generated_by': 'tools/render/build_materials.py (energy_death_materials.json)',
               'by_package': {k: v for k, v in ed['by_package'].items() if (res.get(v) or {}).get('glsl')}},
              open(os.path.join(out, 'energy_death.json'), 'w'), indent=1)
    ok = sum(1 for v in res.values() if not v['error'])
    print('materials: %d ok / %d' % (ok, len(res)))
    for k, v in res.items():
        if v['error']: print('  ERR', k, v['error'])
    miss = sorted({str(t['object']) for v in res.values() if v['info'] for t in v['info']['textures'] if not t['file']})
    print('textures still missing:', len(miss))
    for m in miss[:30]: print('   ', m)


if __name__ == '__main__':
    main()
