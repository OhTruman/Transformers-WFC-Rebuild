"""Map-independent HUD render data: Canvas fonts and objective-marker type setups.

    python build_hud.py <render_data_root>      -> <root>/_ui/fonts/<Font>.json + .png, <root>/_ui/hud_markers.json

Fonts (UE3 UFont, cooked): Characters = immutable FFontCharacter array {StartU, StartV, USize, VSize (int),
TextureIndex (byte), VerticalOffset (int)}; native tail = CharRemap TMap<WORD unicode, WORD index> (IsRemapped).
Marker types: class defaults TransGame.Default__TnObjectiveMarkerType* (AssetTools authored.db), merged down the
sprite chain (TnObjectiveMarkerType -> Sprite -> [Transformer] -> type) [HIGH: chain from class names].
"""
import json, os, shutil, sqlite3, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo  # noqa: E402

AUTHORED = r'F:/Transformers Rebuild/AssetTools/manifests/authored.db'
CONTENT = r'F:/Transformers Rebuild/ExtractedAssets/content'
FONTS = ['UI_TransEngineFonts_p.MarkerFont', 'UI_TransEngineFonts_p.SubtitleFont']


def font(repo, path):
    h = repo.find(path)
    p = repo.pkgs[h[0]]
    e = p.exports[h[1] - 1]
    d = p.data[e['serial_offset']:e['serial_offset'] + e['serial_size']]
    pk, tail = repo.native_tail(path)
    n = struct.unpack_from('>i', tail, 0)[0]
    remap = {struct.unpack_from('>H', tail, 4 + 4 * k)[0]: struct.unpack_from('>H', tail, 6 + 4 * k)[0] for k in range(n)}
    # the Characters array: count == remap size, 21-byte records
    i = d.find(struct.pack('>i', len(remap)))
    chars = []
    o = i + 4
    for _ in range(len(remap)):
        su, sv, us, vs = struct.unpack_from('>iiii', d, o)
        chars.append([su, sv, us, vs, d[o + 16], struct.unpack_from('>i', d, o + 17)[0]])
        o += 21
    obj = repo.obj(path) or {}
    texs = [t.get('ref') if isinstance(t, dict) else t for t in obj.get('Textures') or []]
    return {'font': path, 'characters': chars, 'remap': {str(k): v for k, v in remap.items()},
            'textures': texs, 'import': obj.get('ImportOptions')}


def markers():
    c = sqlite3.connect('file:%s?mode=ro' % AUTHORED, uri=True)
    rows = {o.split('Default__')[1]: json.loads(p or '{}') for o, p in
            c.execute("select opath, props from objects where opath like 'TransGame.Default__TnObjectiveMarkerType%'")}
    out = {}
    for name, props in rows.items():
        chain = ['TnObjectiveMarkerType', 'TnObjectiveMarkerTypeSprite']
        if name.startswith('TnObjectiveMarkerTypeTransformer') and name != 'TnObjectiveMarkerTypeTransformer':
            chain.append('TnObjectiveMarkerTypeTransformer')
        eff = {}
        for k in chain + [name]:
            eff.update(rows.get(k, {}))
        out[name] = eff
    return out


def main():
    root = sys.argv[1]
    ui = os.path.join(root, '_ui')
    os.makedirs(os.path.join(ui, 'fonts'), exist_ok=True)
    repo = Repo(['MP_IAC_Streets_BASE_m.xxx'], fallback=['TransGame.xxx'])
    for f in FONTS:
        if not repo.find(f):
            print('font %s: not cooked in the loaded packages, skipped' % f)
            continue
        fd = font(repo, f)
        name = f.split('.')[-1]
        pngs = []
        for t in fd['textures']:
            src = os.path.join(CONTENT, *t.split('.')[:-1], t.split('.')[-1] + '.png')
            dst = '%s_%s.png' % (name, t.split('.')[-1])
            if os.path.exists(src):
                shutil.copyfile(src, os.path.join(ui, 'fonts', dst))
            pngs.append(dst)
        fd['pages'] = pngs
        json.dump(fd, open(os.path.join(ui, 'fonts', name + '.json'), 'w'))
        print('font %s: %d chars, %d pages' % (name, len(fd['characters']), len(pngs)))
    m = markers()
    json.dump(m, open(os.path.join(ui, 'hud_markers.json'), 'w'), indent=1)
    print('marker types: %d' % len(m))


if __name__ == '__main__':
    main()
