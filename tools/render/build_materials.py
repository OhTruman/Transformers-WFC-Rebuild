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
from ue3obj import Repo, CONTENT, COOKED  # noqa: E402
import matc  # noqa: E402
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
        return {'srgb': bool(o.get('SRGB', True)), 'format': o.get('Format'),
                'address_x': o.get('AddressX', 'TA_Wrap'), 'address_y': o.get('AddressY', 'TA_Wrap'),
                'lod_group': o.get('LODGroup'), 'size': [o.get('SizeX'), o.get('SizeY')],
                'unpack_min': [um.get(k, 0.0) for k in range(4)] if isinstance(um, dict) else None,
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


def main():
    mapname = sys.argv[1]
    out = sys.argv[2]
    extra = sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    repo = Repo(['%s_BASE_m.xxx' % mapname, '%s_ART_m.xxx' % mapname])
    j = glb_json(os.path.join(VS, 'Maps', mapname, 'world.glb'))
    names = {m.get('extras', {}).get('wfc_material') for m in j['materials']}
    bspf = os.path.join(out, 'bsp.glb')          # BSP rebuilt by build_lighting.py (run it first)
    if os.path.exists(bspf):
        names |= {m.get('extras', {}).get('wfc_material') for m in glb_json(bspf).get('materials', [])}
    mats = sorted(names - {None}) + extra
    tr = TexResolver(repo, out)

    def run():
        res = {}
        for mp in mats:
            try:
                mc = matc.MatCompiler(repo, mp, tr)
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
    ok = sum(1 for v in res.values() if not v['error'])
    print('materials: %d ok / %d' % (ok, len(res)))
    for k, v in res.items():
        if v['error']: print('  ERR', k, v['error'])
    miss = sorted({str(t['object']) for v in res.values() if v['info'] for t in v['info']['textures'] if not t['file']})
    print('textures still missing:', len(miss))
    for m in miss[:30]: print('   ', m)


if __name__ == '__main__':
    main()
