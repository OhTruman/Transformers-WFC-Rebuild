"""Extract a map's ORIGINAL baked/static lighting data for the runtime renderer.

Writes <out>/lighting.json:
  lightmaps.props   component object path -> {coeffs:[atlas x3], scales:[[r,g,b] x3], coordScale, coordBias}
                    (directional texture lightmap, FLightMap2D: 3 coefficients, verified in-shader basis)
  lights            every light component of the StaticLightCollectionActor(s), in glTF space, with
                    authored colour/brightness/radius/falloff/cone/channels (+ class defaults)
  fog               HeightFogComponent (Height, Density, LightColor, LightBrightness, Start/Extinction)
and copies all LightMapTexture2D atlases (umodel PNGs from <umodel_dir>) into <out>/lightmaps/.

    python build_lighting.py MP_IAC_Streets <out_dir> <umodel_png_dir>
"""
import json, os, shutil, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, tags_to_dict, ASSETTOOLS  # noqa: E402
sys.path.insert(0, ASSETTOOLS)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Engine class defaults (Engine.xxx Default__*): verified by reading the CDOs.
LIGHT_DEFAULTS = {'Brightness': 1.0, 'LightColor': [255, 255, 255, 0], 'bEnabled': True, 'CastShadows': True,
                  'CastStaticShadows': True, 'CastDynamicShadows': True}
POINT_DEFAULTS = {'Radius': 1024.0, 'FalloffExponent': 2.0, 'ShadowFalloffExponent': 2.0}
SPOT_DEFAULTS = {'OuterConeAngle': 44.0, 'InnerConeAngle': 0.0}
CHANNEL_DEFAULTS = {'BSP': True, 'Static': True, 'Dynamic': True, 'CompositeDynamic': True}
VS_MAPS = os.environ.get('WFC_VS_ROOT', r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice') + '/Maps'  # WFC_VS_ROOT: campaign VerticalSlice root (AssetTools/work/campaign_pilot/VerticalSlice); unset = ExtractedAssets (MP unchanged)
FOG_DEFAULTS = {'bEnabled': True, 'Density': 5e-05, 'LightBrightness': 0.1, 'LightColor': [255, 255, 255, 0],
                'ExtinctionDistance': 100000000.0, 'StartDistance': 0.0, 'Height': 0.0}


def fcolor_rgb(c):
    """Cooked FColor -> [R, G, B, A]. FColor serializes its DWColor (A<<24 | R<<16 | G<<8 | B) big-endian on Xenon;
    the reader lists the bytes as [B, G, R, A] (CONFIRMED: AssetTools authored.db field-named decode, e.g. the
    customization SkyLight LowerColor {R:78, G:148, B:186} reads [186, 148, 78, 0]). M21: lights / sky lower colour /
    height fog colour were consumed as RGB with R and B swapped."""
    c = list(c or [255, 255, 255, 0])
    return [c[2], c[1], c[0], c[3] if len(c) > 3 else 0] if len(c) >= 3 else c


def ue_to_gltf_pos(v):
    return [v[0] * 0.01, v[2] * 0.01, v[1] * 0.01]


def ue_to_gltf_dir(v):
    n = np.array([v[0], v[2], v[1]], 'f8')
    l = np.linalg.norm(n)
    return (n / l).tolist() if l > 0 else [0.0, -1.0, 0.0]


def lm_records(repo, pkg_index):
    p = repo.pkgs[pkg_index]; pr = repo.readers[pkg_index]
    out = {}
    kinds = {}
    vertex_lm = []
    vertex_samples = {}

    def lm_name(idx):
        if idx > 0 and idx - 1 < len(p.exports) and p.class_name(p.exports[idx - 1]) == 'LightMapTexture2D':
            return p.obj_name(idx)
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'StaticMeshComponent': continue
        try:
            used = pr.read_object(i + 1)[1]
        except Exception:
            continue
        nat = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        if len(nat) <= 40: continue
        ltype = struct.unpack_from('<i', nat, 16)[0]
        if ltype in (0, 1, 2): kinds[ltype] = kinds.get(ltype, 0) + 1
        if ltype == 1:   # LMT_1D: per-vertex lightmap samples
            vertex_lm.append(p.object_path(i + 1))
            v = parse_lightmap_1d(nat)
            if v: vertex_samples[p.object_path(i + 1)] = v
        if ltype != 2: continue
        # FLightMap2D, big-endian as FLightMap1D: [13] type=2, [17] LightGuids.Num, GUIDs (16 each), Owner (4),
        # then 3 x (Texture ref, ScaleVector) and CoordinateScale / CoordinateBias. M20: the coefficient block used
        # to be located by searching for the first component's first light GUID, so every component whose light list
        # starts with a different light was dropped unlit (Gorge 256 of 1137, Rust, Seed, Berth, Broken Hope).
        if struct.unpack_from('>i', nat, 13)[0] != 2: continue
        ng = struct.unpack_from('>i', nat, 17)[0]
        if ng < 0 or 21 + 16 * ng + 4 > len(nat): continue
        o = 21 + 16 * ng + 4
        co = []
        while o + 20 <= len(nat):
            nm = lm_name(struct.unpack_from('>i', nat, o)[0])
            if not nm: break
            co.append((nm, list(struct.unpack_from('>3f', nat, o + 4)))); o += 20
        if len(co) != 3 or o + 16 > len(nat): continue
        out[p.object_path(i + 1)] = {'coeffs': [c[0] for c in co], 'scales': [c[1] for c in co],
                                     'coordScale': list(struct.unpack_from('>2f', nat, o)),
                                     'coordBias': list(struct.unpack_from('>2f', nat, o + 8))}
    kinds['vertex_components'] = vertex_lm
    kinds['vertex_samples'] = vertex_samples
    return out, kinds


def component_flags(mapname):
    """Authored per-component render flags (AssetTools props_authored.json, effective values merged over the
    archetype chain): hidden actors (bHidden, not drawn in game) and components that receive no light
    (bAcceptsLights False or an empty LightingChannels set: UE3 lights only affect overlapping channels), and the
    shadow-casting exceptions (no_shadow: CastShadow False; dynamic_shadow: bCastDynamicShadow True). Keyed by component path and by 'actor:<Actor>' (world.glb tags actor-placed nodes that way)."""
    p = os.path.join(VS_MAPS, mapname, 'props_authored.json')
    if not os.path.exists(p):
        return {}
    out = {}
    for pr in json.load(open(p))['props']:
        r = pr.get('render') or {}
        f = {}
        if (pr.get('movement') or {}).get('bHidden'):
            f['hidden'] = True
        if r.get('bAcceptsLights') is False or pr.get('lighting_channels_on') == []:
            f['no_lights'] = True
        # shadow casting (M7 ray tracing): CastShadow False = casts no shadow at all; bCastDynamicShadow True is rare on
        # the static world (UE3 shadowed dynamic objects from it only through the light environment's visibility), so
        # only the exceptions are written
        if r.get('CastShadow') is False:
            f['no_shadow'] = True
        if r.get('bCastDynamicShadow') is True:
            f['dynamic_shadow'] = True
        if r.get('bAcceptsLights') is False or pr.get('lighting_channels_on') == []:
            pass
        elif pr.get('lighting_channels_on') and 'Static' not in pr['lighting_channels_on'] and \
                'Dynamic' in pr['lighting_channels_on']:
            # M41: Dynamic-channel primitives (movable InterpActors with an enabled LightEnvironment: the title scene's
            # transports / debris) are lit by the lights in the Dynamic channel (SkyLight included), not the static
            # set; lit as the static set they rendered near black
            f['dynamic_channel'] = True
        if f:
            out[pr['component']] = f
            if pr.get('container_class') != 'StaticMeshCollectionActor':
                out['actor:' + pr['actor']] = f
    return out


def parse_lightmap_1d(nat):
    """FLightMap1D (StaticMeshComponent LODData[0].LightMap, LightMapType 1), big-endian ints at offset 1:
    [13] type=1, [17] LightGuids.Num, GUIDs, Owner(4), DirectionalSamples bulk data
    (Flags, ElementCount, SizeOnDisk, OffsetInFile, ElementCount x FQuantizedDirectionalLightSample =
    3 x FColor, 4 bytes each, alpha byte last), then ScaleVectors[3] (FVector). Returns raw sample bytes
    (hex) + scales; vertex order = the mesh's LOD0 vertex buffer."""
    if len(nat) < 40 or struct.unpack_from('>i', nat, 13)[0] != 1:
        return None
    ng = struct.unpack_from('>i', nat, 17)[0]
    o = 21 + 16 * ng + 4                       # GUIDs, Owner
    _flags, count, size = struct.unpack_from('>iii', nat, o); o += 20   # bulk header: 5 ints on this build
    if count <= 0 or size != count * 12 or o + size + 36 > len(nat):
        return None
    data = nat[o:o + size]; o += size
    scales = [list(struct.unpack_from('>3f', nat, o + 12 * k)) for k in range(3)]
    # FColor serialized as a byte-swapped DWColor: A,R,G,B; each R/G/B channel of each coefficient is normalized to
    # 255 (max over the vertices), i.e. ScaleVectors = per-channel maxima. The alpha byte is not a lighting input and
    # is not constant across maps (Streets 255/254, Gorge / Rust 0): M24 - an "alpha >= 250" sanity check dropped
    # every vertex-lit component of Gorge (561) and Rust (635). Alignment is validated by the size and the ScaleVectors.
    if not all(all(abs(x) < 1e4 and x == x for x in s) for s in scales):
        return None
    return {'count': count, 'samples_hex': data.hex(), 'scales': scales}


def light_record(repo, cref, M):
    # one LightComponent (class defaults, its tags, LightingChannels) at the UE matrix M -> the lighting.json record
    cls = repo.cls(cref)
    c = dict(LIGHT_DEFAULTS)
    if cls == 'PointLightComponent': c.update(POINT_DEFAULTS)
    if cls == 'SpotLightComponent': c.update(POINT_DEFAULTS); c.update(SPOT_DEFAULTS)
    c.update({k2: v for k2, v in (repo.obj(cref) or {}).items()})
    ch = dict(CHANNEL_DEFAULTS)
    for t in c.get('LightingChannels') or []:
        if isinstance(t, dict) and 'name' in t: ch[t['name']] = bool(t['value'])
    pos = M[3, :3]; xaxis = M[0, :3]
    col = fcolor_rgb(c.get('LightColor'))
    return {
        'name': cref.split('.')[-1], 'class': cls,
        'position': ue_to_gltf_pos(pos), 'direction': ue_to_gltf_dir(xaxis),
        'color_srgb8': col[:3], 'brightness': c.get('Brightness'),
        'radius_m': (c.get('Radius') or 0) * 0.01, 'falloff_exponent': c.get('FalloffExponent'),
        'inner_cone_deg': c.get('InnerConeAngle'), 'outer_cone_deg': c.get('OuterConeAngle'),
        'enabled': bool(c.get('bEnabled', True)), 'cast_shadows': bool(c.get('CastShadows', True)),
        'cast_static_shadows': bool(c.get('CastStaticShadows', True)),
        'built_into_lightmap': bool(c.get('bHasLightEverBeenBuiltIntoLightMap', False)),
        'channels': ch, 'affects_classification': c.get('LightAffectsClassification'),
        'lower_color_srgb8': fcolor_rgb(c.get('LowerColor'))[:3],
        'lower_brightness': c.get('LowerBrightness', 0.0),
        # modulated (projected) shadow inputs; defaults from Engine Default__LightComponent /
        # Default__PointLightComponent (CastDynamicShadows True, ModShadowColor (0,0,0,1),
        # ShadowFalloffExponent 2.0). ModShadowColor is an FLinearColor (no conversion).
        'light_guid': [int(x) & 0xFFFFFFFF for x in (c.get('LightGuid') or [0, 0, 0, 0])],
        # DirectLightEnv relevance (native 0x82DD3EF0): RadiusOfInfluence, class default -1 (UE units)
        'radius_of_influence_m': (c.get('RadiusOfInfluence') if c.get('RadiusOfInfluence') is not None else -1.0) * 0.01,
        'use_volumes': bool(c.get('bUseVolumes', False)),
        'only_same_levels': bool(c.get('bOnlyAffectSameAndSpecifiedLevels', False)),
        'has_light_function': c.get('Function') is not None,
        'cast_dynamic_shadows': bool(c.get('CastDynamicShadows', True)),
        'cast_composite_shadow': bool(c.get('bCastCompositeShadow', False)),
        'force_dynamic': bool(c.get('bForceDynamicLight', False)),
        'mod_shadow_color': list(c.get('ModShadowColor') or [0.0, 0.0, 0.0, 1.0]),
        'shadow_falloff_exponent': c.get('ShadowFalloffExponent', 2.0),
        # copied into the DirectLightEnv shadow record (+0x70 / +0x74; 0 -> SystemSettings 128 / 1024)
        'min_shadow_resolution': int(c.get('MinShadowResolution') or 0),
        'max_shadow_resolution': int(c.get('MaxShadowResolution') or 0),
        'ue_matrix': M.tolist(),
    }


def lights(repo, pkg_index):
    p = repo.pkgs[pkg_index]; pr = repo.readers[pkg_index]
    res = []
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'StaticLightCollectionActor': continue
        tags, used = pr.read_object(i + 1)
        d = tags_to_dict(tags)
        comps = [x.get('ref') if isinstance(x, dict) else None for x in (d.get('LightComponents') or [])]
        raw = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        mats = np.frombuffer(raw[:64 * len(comps)], '>f4').reshape(-1, 4, 4).astype('f8')
        for k, cref in enumerate(comps):
            if not cref: continue
            res.append(light_record(repo, cref, mats[k]))
    return res


# placed light ACTORS outside the cooked StaticLightCollectionActors (e.g. Remnant's PointLightToggleable_1918: bToggleable,
# bForceDynamicLight, never built into a lightmap - RE): the actor's LightComponent at its Location / Rotation
STANDALONE_LIGHT_CLASSES = ('PointLightToggleable', 'PointLightMovable', 'SpotLightToggleable', 'SpotLightMovable',
                            'PointLight', 'SpotLight')


def standalone_lights(repo, pkg_index):
    p = repo.pkgs[pkg_index]; pr = repo.readers[pkg_index]
    res = []
    for i, e in enumerate(p.exports):
        if p.class_name(e) not in STANDALONE_LIGHT_CLASSES: continue
        if '.TheWorld.PersistentLevel.' not in p.object_path(i + 1): continue   # (prefab archetypes are templates)
        tags, _ = pr.read_object(i + 1)
        d = tags_to_dict(tags)
        lc = d.get('LightComponent')
        cref = lc.get('ref') if isinstance(lc, dict) else None
        if not cref: continue
        loc = d.get('Location') or [0.0, 0.0, 0.0]
        rot = d.get('Rotation') or [0, 0, 0]
        if isinstance(rot, dict): rot = [rot.get('Pitch', 0), rot.get('Yaw', 0), rot.get('Roll', 0)]
        pitch, yaw = rot[0] * np.pi / 32768.0, rot[1] * np.pi / 32768.0
        M = np.eye(4)
        M[0, :3] = [np.cos(pitch) * np.cos(yaw), np.cos(pitch) * np.sin(yaw), np.sin(pitch)]
        M[3, :3] = loc[:3]
        r = light_record(repo, cref, M)
        r['standalone_actor'] = p.object_path(i + 1).split('.')[-1]
        res.append(r)
    return res


def fog(repo, pkg_index):
    p = repo.pkgs[pkg_index]
    for i, e in enumerate(p.exports):
        if p.class_name(e) == 'HeightFogComponent' and 'Default__' not in p.obj_name(i + 1):
            f = dict(FOG_DEFAULTS); f.update(repo.obj(p.object_path(i + 1)) or {})
            f['height_m'] = f['Height'] * 0.01
            f['LightColor'] = fcolor_rgb(f.get('LightColor'))
            return f
    return None


def fogs(repo):
    """Every ENABLED HeightFogComponent of the map's level packages, in package then export order, up to 4 (stock UE3
    renders up to four height fog layers per view; RE MAP_LIGHTING_REFERENCE: six MP maps author 2-4, Gorge loses three
    - incl. a dense low layer - to fog()'s first-component rule)."""
    out = []
    for pi in range(len(repo.pkgs)):
        p = repo.pkgs[pi]
        for i, e in enumerate(p.exports):
            if p.class_name(e) != 'HeightFogComponent' or 'Default__' in p.obj_name(i + 1):
                continue
            f = dict(FOG_DEFAULTS); f.update(repo.obj(p.object_path(i + 1)) or {})
            if not f.get('bEnabled', True):
                continue
            f['height_m'] = f['Height'] * 0.01
            f['LightColor'] = fcolor_rgb(f.get('LightColor'))
            f['object'] = p.object_path(i + 1)
            out.append(f)
    # stock UE3 FScene::AddHeightFog keeps the list SORTED BY HEIGHT, highest first; the view takes the first 4 and each
    # layer spans [the next layer's Height, its own Height], the last down to -HALF_WORLD_MAX (RE: the 360 VS constants
    # FogMinHeight / FogMaxHeight c20 / c21, one float4 each = 4 layers; the order H)
    out.sort(key=lambda f: -f['Height'])
    return out[:4]


def volume_fogs(repo, mapname):
    """PostProcessVolumeHeightFogComponent layers (AssetTools 2026-10-23: fogs() matches the exact class, so these were
    skipped - 1 on MP_ESC_Remnant, 169 in the campaign) with the post-process volumes that link them (RE
    CAMPAIGN_PPV_HEIGHTFOG_RE.md: same layer parameters / conversion as a HeightFog; a view uses the volume fogs of the
    highest-priority volume at the pawn, priority-linked ones before the level fogs, the rest after, cap 4; density fades
    in / out over InterpolationDuration, 1 s by default). The brush comes from the map's collision.json volumes (glTF m)."""
    coll = {}
    cp = os.path.join(VS_MAPS, mapname, 'collision.json')
    if os.path.exists(cp):
        for v in json.load(open(cp, encoding='utf-8')).get('volumes', []):
            coll[v.get('actor')] = v.get('polygons_gltf') or []
    out = []
    for p in repo.pkgs:
        for i, e in enumerate(p.exports):
            if p.class_name(e) != 'PostProcessVolume' or 'Default__' in p.obj_name(i + 1):
                continue
            vpath = p.object_path(i + 1)
            o = repo.obj(vpath) or {}
            settings = {s['name']: s.get('value') for s in o.get('Settings') or [] if isinstance(s, dict)}
            links = settings.get('PostProcessVolumeHeightFog') or []
            fogs_ = []
            for link in links:
                lk = {s['name']: s.get('value') for s in link if isinstance(s, dict)}
                actor = (lk.get('PostProcessVolumeHeightFogActor') or {}).get('ref')
                comp = ((repo.obj(actor) or {}).get('Component') or {}).get('ref') if actor else None
                if not comp:
                    continue
                f = dict(FOG_DEFAULTS); f.update(repo.obj(comp) or {})
                if not f.get('bEnabled', True):
                    continue
                f['height_m'] = f['Height'] * 0.01
                f['LightColor'] = fcolor_rgb(f.get('LightColor'))
                f['object'] = comp
                f['priority'] = bool(lk.get('bPriority', False))
                f['transition_from_existing'] = bool(lk.get('bTransitionFromExistingHeightFog', False))
                fogs_.append(f)
            if not fogs_:
                continue
            name = vpath.split('.')[-1]
            out.append({'volume': name, 'priority': float(o.get('Priority', 0.0)), 'unbound': bool(o.get('bUnbound', False)),
                        'interpolation_s': float(settings.get('InterpolationDuration', 1.0) or 1.0),
                        'polygons_m': coll.get(name, []), 'fogs': fogs_})
    return out


PF_INVISIBLE, PF_TWOSIDED, PF_PORTAL = 0x1, 0x100, 0x04000000


def parse_model_component(p, t, model_idx, lmset):
    """FModelComponent native tail (licensee 144): [1][Model][Elements.Num] then per element
    [pad][Component][Material][Nodes: TArray<WORD>][ShadowMaps: TArray<int>][IrrelevantLights:
    TArray<FGuid>][LightMapRef: type(2) LightGuids(int32 n x 16) 0 3x(tex, ScaleVector4)
    CoordinateScale(2f) CoordinateBias(2f)][TArray<FGuid>]. The tagged-property reader can end 2 bytes early, so
    anchor on the Model reference."""
    key = struct.pack('>i', model_idx)
    a = t.find(key, 0, 16)
    if a < 4: return None
    o = a + 4
    ne = struct.unpack_from('>i', t, o)[0]; o += 4
    els = []
    for _ in range(ne):
        _pad, comp, mat, nn = struct.unpack_from('>iiii', t, o); o += 16
        nodes = list(struct.unpack_from('>%dH' % nn, t, o)); o += 2 * nn
        ns = struct.unpack_from('>i', t, o)[0]; o += 4 + 4 * ns
        ni = struct.unpack_from('>i', t, o)[0]; o += 4 + 16 * ni
        lt = struct.unpack_from('>i', t, o)[0]; o += 4
        lm = None
        if lt == 2:
            ng = struct.unpack_from('>i', t, o)[0]; o += 4 + 16 * ng + 4
            co = []
            for _c in range(3):
                ref = struct.unpack_from('>i', t, o)[0]
                co.append((p.obj_name(ref) if ref in lmset else None, list(struct.unpack_from('>3f', t, o + 4))))
                o += 20
            cs = list(struct.unpack_from('>2f', t, o)); cb = list(struct.unpack_from('>2f', t, o + 8)); o += 16
            if all(c[0] for c in co):
                lm = {'coeffs': [c[0] for c in co], 'scales': [c[1] for c in co], 'coordScale': cs, 'coordBias': cb}
        elif lt != 0:
            raise ValueError('unsupported BSP lightmap type %d' % lt)
        ng2 = struct.unpack_from('>i', t, o)[0]  # trailing TArray<FGuid> (light GUIDs)
        o += 4 + 16 * ng2
        els.append({'material': mat, 'nodes': nodes, 'lightmap': lm})
    return els


def bsp_lighting(repo, out):
    """Rebuild the level BSP from the COOKED FModelVertexBuffer (TexCoord + ShadowTexCoord,
    indexed by FBspNode.iVertexIndex) split per ModelComponent element, with its lightmap."""
    import bsp as bspmod
    # the level package with the largest level BSP (Streets: ART; UI levels may have none)
    best = None
    for pk in range(len(repo.pkgs)):
        p = repo.pkgs[pk]
        level = next((i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Level'), None)
        if level is None: continue
        for mi in [i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Model' and e['outer'] == level]:
            try:
                M = bspmod.read_model(p, mi)
                # [integration M06] len(), not truthiness: AssetTools bsp.read_model returns the nodes as a numpy array,
                # whose truth value raises (swallowed below), so every map lost its lit BSP (Streets included).
                if len(M['nodes']) > 0 and (best is None or len(M['nodes']) > len(best[2]['nodes'])): best = (pk, mi, M)
            except Exception:
                pass
    if best is None:
        print('bsp: no level BSP nodes in %s' % [x.path if hasattr(x, 'path') else '' for x in repo.pkgs])
        return {}
    pk, mi, M = best
    p = repo.pkgs[pk]; pr = repo.readers[pk]
    # [integration M06] the Level of the package that holds the BSP (the search loop above leaves `level` at the last
    # package scanned, e.g. BASE, so no ModelComponent of ART matched and the BSP came out empty)
    level = next((i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Level'), None)
    lmset = {i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'LightMapTexture2D'}
    e = p.exports[mi - 1]
    t = p.data[e['serial_offset'] + M['end_offset']:e['serial_offset'] + e['serial_size']]
    vbo = None
    pts = M['points']
    for o in range(0, len(t) - 8, 4):
        es, n = struct.unpack_from('>ii', t, o)
        if es == 36 and o + 8 + 36 * n == len(t):
            vbo = o + 8; break
    if vbo is None:
        print('bsp: FModelVertexBuffer not found; no BSP render data')
        return {}
    vb = np.frombuffer(t[vbo:], dtype=np.dtype([('pos', '>f4', 3), ('tx', '>u4'), ('tz', '>u4'),
                                                ('uv', '>f4', 2), ('suv', '>f4', 2)]))
    elements = []
    bad = 0
    for i, ex in enumerate(p.exports):
        if p.class_name(ex) != 'ModelComponent' or ex['outer'] != level: continue
        _, used = pr.read_object(i + 1)
        tail = p.data[ex['serial_offset'] + used - 8:ex['serial_offset'] + ex['serial_size']]
        try:
            els = parse_model_component(p, tail, mi, lmset)
        except Exception:
            els = None
        if els is None: bad += 1; continue
        for k, el in enumerate(els):
            el['key'] = '%s#%d' % (p.object_path(i + 1), k)
            m = el['material']
            el['material_path'] = (p.object_path(m) if m > 0 else p.full_path(m)) if m else None
            elements.append(el)
    # geometry per element
    prims = []
    lm_props = {}
    for el in elements:
        P, N, UV, SUV = [], [], [], []
        idx = []
        two = False
        for ni in el['nodes']:
            node = M['nodes'][ni]
            k = int(node['NumVertices'])
            if k < 3: continue
            s = M['surfs'][int(node['iSurf'])]
            fl = int(s['PolyFlags'])
            if fl & (PF_INVISIBLE | PF_PORTAL): continue
            two = two or bool(fl & PF_TWOSIDED)
            vi = int(node['iVertexIndex'])
            v = vb[vi:vi + k]
            gp = np.stack([v['pos'][:, 0], v['pos'][:, 2], v['pos'][:, 1]], 1).astype('f8') * 0.01
            nrm = M['vectors'][int(s['vNormal'])]
            gn = np.array([nrm[0], nrm[2], nrm[1]], 'f8'); gn /= (np.linalg.norm(gn) or 1)
            tris = [(0, j, j + 1) for j in range(1, k - 1)]
            # Winding from the whole polygon (Newell normal): BSP polygons often start with collinear
            # vertices, where the first triangle's normal is degenerate and its sign arbitrary.
            nw = np.zeros(3)
            for i in range(k):
                pa, pb = gp[i], gp[(i + 1) % k]
                nw += np.array([(pa[1] - pb[1]) * (pa[2] + pb[2]), (pa[2] - pb[2]) * (pa[0] + pb[0]),
                                (pa[0] - pb[0]) * (pa[1] + pb[1])])
            if np.dot(nw, gn) < 0: tris = [(a, cc, b) for a, b, cc in tris]
            base = len(P)
            for j in range(k):
                P.append(gp[j]); N.append(gn); UV.append(v['uv'][j]); SUV.append(v['suv'][j])
            for tr in tris: idx += [base + x for x in tr]
        if not idx: continue
        prims.append({'key': el['key'], 'material': el['material_path'], 'two_sided': two,
                      'P': np.array(P, 'f4'), 'N': np.array(N, 'f4'), 'UV': np.array(UV, 'f4'),
                      'SUV': np.array(SUV, 'f4'), 'I': np.array(idx, 'u4')})
        if el['lightmap']: lm_props[el['key']] = el['lightmap']
    write_glb(os.path.join(out, 'bsp.glb'), prims)
    print('BSP: %d elements (%d lightmapped), %d unparsed components, %d tris' %
          (len(prims), len(lm_props), bad, sum(len(q['I']) // 3 for q in prims)))
    return lm_props


def write_glb(path, prims, kind='bsp_element'):
    bin_ = bytearray()
    acc, views, meshes, nodes, mats = [], [], [], [], []
    matidx = {}

    def add(arr, typ, target):
        nonlocal bin_
        while len(bin_) % 4: bin_ += b'\0'
        off = len(bin_); bin_ += arr.tobytes()
        views.append({'buffer': 0, 'byteOffset': off, 'byteLength': arr.nbytes, 'target': target})
        a = {'bufferView': len(views) - 1, 'componentType': 5125 if arr.dtype == np.uint32 else 5126,
             'count': int(arr.shape[0]), 'type': typ}
        if typ == 'VEC3' and arr.dtype != np.uint32:
            a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
        acc.append(a)
        return len(acc) - 1
    for q in prims:
        mk = (q['material'], q['two_sided'])
        if mk not in matidx:
            mats.append({'name': (q['material'] or 'none').split('.')[-1], 'doubleSided': q['two_sided'],
                         'extras': {'wfc_material': q['material']}})
            matidx[mk] = len(mats) - 1
        attrs = {'POSITION': add(q['P'], 'VEC3', 34962), 'NORMAL': add(q['N'], 'VEC3', 34962),
                 'TEXCOORD_0': add(q['UV'], 'VEC2', 34962), 'TEXCOORD_1': add(q['SUV'], 'VEC2', 34962)}
        meshes.append({'primitives': [{'attributes': attrs, 'indices': add(q['I'], 'SCALAR', 34963),
                                       'material': matidx[mk]}]})
        ex = {'component': q['key'], 'kind': kind}
        if q.get('lightmap_component'): ex['lightmap_component'] = q['lightmap_component']
        nodes.append({'name': q['key'].split('.')[-1], 'mesh': len(meshes) - 1, 'extras': ex})
    j = {'asset': {'version': '2.0', 'generator': 'Rebuild-Rendering tools/render/build_lighting.py'},
         'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': meshes,
         'materials': mats, 'accessors': acc, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_)}]}
    js = json.dumps(j).encode()
    while len(js) % 4: js += b' '
    while len(bin_) % 4: bin_ += b'\0'
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(bin_), 0x004E4942)); f.write(bytes(bin_))


def read_glb(path):
    """-> [(node dict, 4x4 column-major matrix or None, [(P, N, UV1, I) per primitive])] (float64 / uint32 arrays)."""
    b = open(path, 'rb').read()
    n = struct.unpack_from('<I', b, 12)[0]
    j = json.loads(b[20:20 + n])
    binoff = 20 + n + 8
    dt = {5126: ('<f4', 4), 5125: ('<u4', 4), 5123: ('<u2', 2), 5121: ('<u1', 1)}
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

    def acc(i):
        a = j['accessors'][i]; v = j['bufferViews'][a['bufferView']]
        t, sz = dt[a['componentType']]; w = width[a['type']]
        off = binoff + v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', sz * w)
        if stride == sz * w:
            return np.frombuffer(b, t, a['count'] * w, off).reshape(a['count'], w).astype('f8' if t == '<f4' else 'u4')
        return np.array([np.frombuffer(b, t, w, off + k * stride) for k in range(a['count'])]).astype('f8' if t == '<f4' else 'u4')
    out = []
    for nd in j['nodes']:
        if 'mesh' not in nd: continue
        prims = []
        for pr in j['meshes'][nd['mesh']]['primitives']:
            at = pr['attributes']
            if 'TEXCOORD_1' not in at or 'indices' not in pr: prims.append(None); continue
            prims.append((acc(at['POSITION']), acc(at['NORMAL']) if 'NORMAL' in at else None, acc(at['TEXCOORD_1']),
                          acc(pr['indices'])[:, 0]))
        out.append((nd, nd.get('matrix'), prims))
    return out


class DecalReceivers:
    """Receiver geometry for the decals' lightmap coordinates (UE3: a static decal samples its receiver's lightmap at the
    receiver's own lightmap UV). Static-mesh receivers: the decal keeps whole receiver triangles, so each decal vertex is
    a receiver vertex (exact match in world.glb, glTF world space). BSP receivers: the decal polygon is clipped from the
    receiver's surfaces, so UV1 / normal are interpolated in the containing bsp.glb triangle of that ModelComponent; the
    lightmap element (#k) is the element of that triangle."""
    def __init__(self, mapname, out):
        self.world = read_glb(os.path.join(VS_MAPS, mapname, 'world.glb'))
        self.by_comp, self.by_actor = {}, {}
        for k, (nd, _, _) in enumerate(self.world):
            ex = nd.get('extras') or {}
            if ex.get('component'): self.by_comp[ex['component'].lower()] = k
            if ex.get('actor') and not ex.get('component'): self.by_actor[ex['actor'].lower()] = k
        self.bsp = read_glb(os.path.join(out, 'bsp.glb')) if os.path.exists(os.path.join(out, 'bsp.glb')) else []
        self._cache = {}

    def _node_world(self, k):
        if k in self._cache: return self._cache[k]
        nd, mtx, prims = self.world[k]
        M = np.array(mtx, 'f8').reshape(4, 4).T if mtx else np.eye(4)
        Nm = np.linalg.inv(M[:3, :3]).T
        P, N, U, I = [], [], [], []
        base = 0
        for q in prims:
            if q is None: continue
            P.append(q[0] @ M[:3, :3].T + M[:3, 3])
            n = (q[1] if q[1] is not None else np.tile([0, 1.0, 0], (len(q[0]), 1))) @ Nm.T
            N.append(n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)); U.append(q[2])
            I.append(q[3] + base); base += len(q[0])
        r = (np.vstack(P), np.vstack(N), np.vstack(U), np.concatenate(I)) if P else None
        self._cache[k] = r
        return r

    @staticmethod
    def _on_triangles(P, TP, TN, TU, tol):
        """Per point: the nearest triangle (in-plane distance + outside penalty) -> barycentric UV1 / normal, or None
        when a point is farther than tol (m) from every triangle."""
        a0, e1, e2 = TP[:, 0], TP[:, 1] - TP[:, 0], TP[:, 2] - TP[:, 0]
        n = np.cross(e1, e2); nn = (n * n).sum(1); ok = nn > 1e-18; nns = np.where(ok, nn, 1)
        UV = np.zeros((len(P), 2)); N = np.zeros((len(P), 3)); best = np.zeros(len(P), int)
        for i, x in enumerate(P):
            d = x - a0
            w2 = (np.cross(e1, d) * n).sum(1) / nns; w1 = (np.cross(d, e2) * n).sum(1) / nns; w0 = 1 - w1 - w2
            out_ = np.maximum(0, -np.minimum(np.minimum(w0, w1), w2)) * np.sqrt(np.maximum(nns, 0)) ** 0.5
            plane = np.abs((d * n).sum(1)) / np.sqrt(nns)
            score = np.where(ok, plane + out_, 1e9)
            j = int(score.argmin())
            if score[j] > tol: return None
            b = np.clip(np.array([w0[j], w1[j], w2[j]]), 0, None); b = b / max(b.sum(), 1e-12)
            UV[i] = b @ TU[j]; nv = b @ TN[j]; N[i] = nv / max(np.linalg.norm(nv), 1e-12); best[i] = j
        return UV, N, best

    def mesh(self, comp_path, actor_name, P):
        """-> (UV1, N) per decal vertex: the receiver's own vertex where one coincides (2 mm; the decal keeps whole
        receiver triangles), else interpolated in the receiver triangle the point lies on (5 cm); None otherwise."""
        k = self.by_comp.get(comp_path.lower())
        if k is None and actor_name: k = self.by_actor.get(actor_name.lower())
        if k is None: return None
        g = self._node_world(k)
        if g is None: return None
        RP, RN, RU, RI = g
        t = RI.reshape(-1, 3)
        r = self._on_triangles(P, RP[t], RN[t], RU[t], 0.05)
        if r is None: return None
        UV, N, _ = r
        for a in range(0, len(P), 256):                           # exact vertices win (no interpolation drift)
            d = ((P[a:a + 256, None, :] - RP[None, :, :]) ** 2).sum(-1)
            j = d.argmin(1); hit = np.sqrt(d[np.arange(len(j)), j]) < 0.002
            UV[a:a + 256][hit] = RU[j[hit]]; N[a:a + 256][hit] = RN[j[hit]]
        return UV, N

    def bsp_element(self, comp_path, P, I):
        """-> (element key per triangle list, UV1, N) for decal triangles on a ModelComponent, or None."""
        pre = comp_path.lower() + '#'
        tris = []
        for nd, _, prims in self.bsp:
            key = (nd.get('extras') or {}).get('component', '')
            if not key.lower().startswith(pre): continue
            for q in prims:
                if q is None: continue
                Q, NQ, UQ, IQ = q
                t = IQ.reshape(-1, 3)
                tris.append((key, Q[t], (NQ[t] if NQ is not None else None), UQ[t]))
        if not tris: return None
        TK = sum(([x[0]] * len(x[1]) for x in tris), [])
        TP = np.concatenate([x[1] for x in tris]); TU = np.concatenate([x[3] for x in tris])
        TN = np.concatenate([x[2] if x[2] is not None else np.tile([0, 1.0, 0], (len(x[1]), 3, 1)) for x in tris])
        a0, e1, e2 = TP[:, 0], TP[:, 1] - TP[:, 0], TP[:, 2] - TP[:, 0]
        n = np.cross(e1, e2); nn = (n * n).sum(1); ok = nn > 1e-18
        best = np.zeros(len(P), int); bary = np.zeros((len(P), 3))
        for i, x in enumerate(P):
            d = x - a0
            # barycentrics in each triangle's plane + distance to the plane
            w2 = (np.cross(e1, d) * n).sum(1) / np.where(ok, nn, 1); w1 = (np.cross(d, e2) * n).sum(1) / np.where(ok, nn, 1)
            w0 = 1 - w1 - w2
            outside = np.maximum(0, -np.minimum(np.minimum(w0, w1), w2))
            plane = np.abs((d * n).sum(1)) / np.sqrt(np.where(ok, nn, 1))
            score = np.where(ok, plane + outside * 10.0, 1e9)
            j = int(score.argmin()); best[i] = j; bary[i] = (w0[j], w1[j], w2[j])
        # the element of each decal triangle: its vertices' majority (they lie on one receiver surface)
        el = []
        for t in I.reshape(-1, 3):
            ks = [TK[best[v]] for v in t]
            el.append(max(set(ks), key=ks.count))
        UV = (TU[best] * bary[:, :, None]).sum(1); N = (TN[best] * bary[:, :, None]).sum(1)
        N = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        return el, UV, N


def decals(repo, out, mapname=None, lm_keys=None):
    """Static decals from their COOKED receiver geometry (DecalComponent native tail, licensee 144):
    [NumReceivers] x ([ReceiverComponent][VertexStride=28][NumVerts] verts{pos(3f, receiver-local),
    TangentX, TangentZ (packed), ShadowTexCoord(2f)} [IndexStride=2][NumIndices] u16 indices
    [NumTriangles][int32]). BSP receivers are clipped to the decal box; static-mesh receivers keep
    whole intersecting triangles (UE3 behaviour), so UVs may exceed [0,1] there.
    UV per the original decal vertex shader: 0.5 - (DecalWorldToTexCoordMatrix * (P - DecalWorldLocation)).xy
    + DecalOffset, with matrix rows HitTangent*TileX/Width, HitBinormal*TileY/Height (validated:
    clipped BSP receivers project to exactly [-0.5, 0.5])."""
    # the level package that holds the decals (Streets: ART)
    pk = max(range(len(repo.pkgs)),
             key=lambda k: sum(1 for e in repo.pkgs[k].exports if repo.pkgs[k].class_name(e) == 'DecalActor'))
    p = repo.pkgs[pk]; pr = repo.readers[pk]
    smca = {}
    for i, e in enumerate(p.exports):
        if p.class_name(e) == 'StaticMeshCollectionActor':
            t, used = pr.read_object(i + 1)
            d = tags_to_dict(t)
            comps = [x.get('ref') if isinstance(x, dict) else None for x in (d.get('StaticMeshComponents') or [])]
            raw = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
            if len(raw) >= 64 * len(comps):
                mats = np.frombuffer(raw[:64 * len(comps)], '>f4').reshape(-1, 4, 4).astype('f8')
                for k, c in enumerate(comps):
                    if c: smca[c.lower()] = mats[k]
    pj = json.load(open(os.path.join(VS_MAPS, mapname or p.name.rsplit('_', 2)[0], 'props.json'), encoding='utf-8'))
    byact = {x['actor']: np.array(x['ue_matrix']) for x in pj['props'] if not x.get('component')}

    # StaticMeshCollectionActor members: the component's own S(Scale*Scale3D) * R * T on top of the serialized
    # CachedParentToWorld (RE SetTransformedToWorld 0x82DC3508); AssetTools 8d8195e props.json carries the corrected
    # per-component matrix (validated 1906/1906), the native parent alone is only a fallback
    bycomp = {x['component'].lower(): np.array(x['ue_matrix']) for x in pj['props'] if x.get('component')}

    def receiver_matrix(ci):
        cls = p.class_name(p.exports[ci - 1])
        if cls == 'ModelComponent': return np.eye(4)
        m = bycomp.get(p.object_path(ci).lower())
        if m is not None: return m
        m = smca.get(p.object_path(ci).lower())
        if m is not None: return m
        return byact.get(p.obj_name(p.exports[ci - 1]['outer']))

    prims = []
    skipped = 0
    unplaced = 0
    malformed = 0
    import collections
    stats = collections.Counter()
    lmk = {k.lower(): k for k in (lm_keys or ())}
    try:
        recv = DecalReceivers(mapname or p.name.rsplit('_', 2)[0], out) if lm_keys is not None else None
    except (OSError, ValueError, KeyError) as ex:
        print('decals: receiver geometry unavailable (%s): decals stay unlightmapped' % ex); recv = None
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'DecalComponent': continue
        t, used = pr.read_object(i + 1)
        d = tags_to_dict(t)
        mat = (d.get('DecalMaterial') or {}).get('ref')
        tail = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        if not all(k in d for k in ('Location', 'HitTangent', 'HitBinormal')):
            # [integration M06] placement not serialized on the component (archetype / default values; Berth, Rust):
            # not guessed - the decal is skipped and counted [PARTIAL]
            unplaced += 1; continue
        L = np.array(d['Location']); T = np.array(d['HitTangent']); B = np.array(d['HitBinormal'])
        W = d.get('Width', 200.0); H = d.get('Height', 200.0)
        tx, ty = d.get('TileX', 1.0), d.get('TileY', 1.0); ox, oy = d.get('OffsetX', 0.0), d.get('OffsetY', 0.0)
        o = 4
        P_, N_, UV_, I_ = [], [], [], []
        recv_prims = []                                       # (lightmap key, P, N, UV, SUV, I) per lightmapped receiver
        # (D1_ORB_Crash2_ART: one component's receiver tail reads garbage counts / offsets - such a decal is skipped and
        # counted [PARTIAL] instead of ending the step)
        try:
          for _ in range(struct.unpack_from('>i', tail, 0)[0]):
            comp, stride, cnt = struct.unpack_from('>iii', tail, o); o += 12
            if stride < 0 or cnt < 0 or o + stride * cnt > len(tail): raise ValueError('receiver vertices out of range')
            verts = tail[o:o + stride * cnt]; o += stride * cnt
            es, ic = struct.unpack_from('>ii', tail, o); o += 8
            if es < 0 or ic < 0 or o + es * ic > len(tail): raise ValueError('receiver indices out of range')
            idx = struct.unpack_from('>%dH' % ic, tail, o); o += es * ic + 8
            M = receiver_matrix(comp) if comp > 0 else None
            if M is None: skipped += 1; continue
            if cnt == 0 or not idx: continue
            P = np.array([struct.unpack_from('>3f', verts, q * stride) for q in range(cnt)])
            Pw = (np.c_[P, np.ones(len(P))] @ M)[:, :3]
            u = 0.5 - ((Pw - L) @ T) * tx / W + ox
            v = 0.5 - ((Pw - L) @ B) * ty / H + oy
            Pg = np.stack([Pw[:, 0] * 0.01, Pw[:, 2] * 0.01, Pw[:, 1] * 0.01], 1)
            II = np.array(idx, 'u4')
            # the receiver's lightmap (texture lightmaps only; vertex-lightmapped receivers stay unlightmapped [PARTIAL])
            got = None
            if recv is not None and lm_keys is not None:
                rp = p.object_path(comp)
                if p.class_name(p.exports[comp - 1]) == 'ModelComponent':
                    r = recv.bsp_element(rp, Pg, II)
                    if r is not None:
                        els, SUV, NN = r
                        tri = II.reshape(-1, 3)
                        for ek in sorted(set(els)):
                            if ek.lower() not in lmk: continue
                            sel = tri[[k for k, x in enumerate(els) if x == ek]]
                            got = got or []
                            got.append((ek, sel.reshape(-1)))
                        if got: got = [(lmk[ek.lower()], ii, SUV, NN) for ek, ii in got]
                    stats['bsp ' + ('lightmapped' if got else 'unmatched / no LM')] += 1
                elif rp.lower() in lmk:
                    r = recv.mesh(rp, p.obj_name(p.exports[comp - 1]['outer']), Pg)
                    if r is not None: got = [(lmk[rp.lower()], II, r[0], r[1])]
                    stats['mesh texture-LM ' + ('matched' if got else 'UNMATCHED')] += 1
                else:
                    stats['mesh without texture LM'] += 1
            if got:
                for ek, ii, SUV, NN in got:
                    recv_prims.append((ek, Pg, NN, np.stack([u, v], 1), SUV, ii))
                continue
            base = len(P_)
            for q in range(cnt):
                P_.append([Pw[q, 0] * 0.01, Pw[q, 2] * 0.01, Pw[q, 1] * 0.01])
                N_.append([0.0, 1.0, 0.0]); UV_.append([u[q], v[q]])
            I_ += [base + x for x in idx]
        except (struct.error, ValueError, OverflowError, MemoryError):
            malformed += 1; continue
        for ek, Pg, NN, UV, SUV, ii in recv_prims:           # used vertices only, re-indexed
            used = np.unique(ii); remap = np.zeros(len(Pg), 'u4'); remap[used] = np.arange(len(used), dtype='u4')
            prims.append({'key': p.object_path(i + 1), 'material': mat, 'two_sided': False, 'lightmap_component': ek,
                          'P': Pg[used].astype('f4'), 'N': NN[used].astype('f4'), 'UV': UV[used].astype('f4'),
                          'SUV': SUV[used].astype('f4'), 'I': remap[ii]})
        if not I_: continue
        prims.append({'key': p.object_path(i + 1), 'material': mat, 'two_sided': False,
                      'P': np.array(P_, 'f4'), 'N': np.array(N_, 'f4'), 'UV': np.array(UV_, 'f4'),
                      'SUV': np.zeros((len(P_), 2), 'f4'), 'I': np.array(I_, 'u4')})
    write_glb(os.path.join(out, 'decals.glb'), prims, kind='decal')
    if unplaced: print('decals: %d components without serialized placement skipped (PARTIAL)' % unplaced)
    if malformed: print('decals: %d components with an unreadable receiver tail skipped (PARTIAL)' % malformed)
    print('decals: %d (%d tris), %d receivers without transform' %
          (len(prims), sum(len(q['I']) // 3 for q in prims), skipped))
    if stats: print('decal receivers: %s; %d lightmapped decal prims' % (dict(stats), sum(1 for q in prims if q.get('lightmap_component'))))


def postprocess(mapname, out):
    """Persistent-level post settings: the map's TnWorldInfo.DefaultPostProcessSettings (the BASE
    package is the persistent level: TransLevels.ini MapFilename) layered over Engine
    Default__WorldInfo, plus the authored ColorCorrectionTexture (Texture3D CLUT) decoded from its
    Xbox-tiled A8R8G8B8 volume (xbox_texture.decode_volume_argb8) into a 1024x32 PNG strip
    (u = x + 32*z, v = y)."""
    from PIL import Image
    import xbox_texture
    eng = Repo(['Engine.xxx'])
    from ue3obj import map_packages
    base = Repo([map_packages(mapname)[1]])

    def settings(tags):
        d = {}
        for t in tags or []:
            v = t['value']
            if isinstance(v, dict) and 'ref' in v: v = v['ref']
            elif isinstance(v, list) and v and isinstance(v[0], dict) and 'name' in v[0]: v = {x['name']: x['value'] for x in v}
            d[t['name']] = v
        return d
    wi_def = eng.obj('Engine.Default__WorldInfo') or {}
    pp = settings(wi_def.get('DefaultPostProcessSettings'))
    src = {k: 'Engine.Default__WorldInfo' for k in pp}
    wi = next((path for path in base.index if base.cls(path) == 'TnWorldInfo' and 'default__' not in path), None)
    over = settings((base.obj(wi) or {}).get('DefaultPostProcessSettings')) if wi else {}
    for k, v in over.items(): pp[k] = v; src[k] = wi
    res = {'settings': pp, 'source': src, 'world_info': wi, 'clut': None}
    clut = pp.get('ColorCorrectionTexture')
    if clut and base.find(clut):
        o = base.obj(clut) or {}
        sx, sy, sz = o.get('SizeX', 32), o.get('SizeY', 32), o.get('SizeZ', 32)
        _, tail = base.native_tail(clut)
        # bulk header [flags][count][size][offset][extra] then the top mip (verified: offset 20 decodes
        # to a smooth near-identity grade; offset 16 does not)
        vol = xbox_texture.decode_volume_argb8(tail[20:20 + sx * sy * sz * 4], sx, sy, sz)
        strip = vol.transpose(1, 0, 2, 3).reshape(sy, sz * sx, 4)       # row y, column z*sx + x
        fn = os.path.join(out, 'clut.png')
        Image.fromarray(strip).save(fn)
        res['clut'] = {'object': clut, 'file': fn.replace(os.sep, '/'), 'size': [sx, sy, sz],
                       'srgb': bool(o.get('SRGB', False))}       # Default__Texture3D SRGB=False
    # M25: PostProcessVolumes. UE3 gives a view inside an enabled PostProcessVolume that volume's Settings (highest
    # Priority wins) instead of the WorldInfo defaults. 8 of 9 non-Streets maps grade on one volume that contains
    # every player start (AssetTools completeness: Gorge 115/115, Berth 120/120, Remnant 4/4): its Settings (over the
    # FPostProcessSettings defaults) and its ColorCorrectionTexture become the map's effective grade. The volume brush
    # is not exported, so "inside" is taken from the player-start coverage [HIGH for gameplay views].
    ap = os.path.join(VS_MAPS, mapname, 'postprocess.json')
    if os.path.exists(ap):
        vols = [v for v in (json.load(open(ap, encoding='utf-8')).get('volumes') or [])
                if (v.get('props') or {}).get('bEnabled', True) is not False]
        if vols:
            v = max(vols, key=lambda x: float((x.get('props') or {}).get('Priority', 0.0) or 0.0))
            vs = dict(settings(wi_def.get('DefaultPostProcessSettings')))
            for k, x in ((v.get('props') or {}).get('Settings') or {}).items():
                # FVector props arrive as {X, Y, Z} from the AssetTools export; the WorldInfo path writes [x, y, z]
                vs[k] = [x['X'], x['Y'], x['Z']] if isinstance(x, dict) and set(x) == {'X', 'Y', 'Z'} else x
            res['settings'] = vs
            res['source'] = {k: v['volume'] for k in vs}
            res['volume'] = v['volume']
            vc = v.get('clut') or {}
            png = os.path.join(os.path.dirname(VS_MAPS.rstrip('/\\')), '..', vc.get('png', '')) if vc.get('png') else None
            if png and os.path.exists(png):
                fn = os.path.join(out, 'clut.png')
                shutil.copyfile(png, fn)
                res['clut'] = {'object': vc.get('object'), 'file': fn.replace(os.sep, '/'), 'size': vc.get('size', [32, 32, 32]),
                               'srgb': False}             # Default__Texture3D SRGB=False (AssetTools srgb_tag None)
            elif not vs.get('ColorCorrectionTexture'):
                res['clut'] = None
    return res


def light_visibility_volumes(repo, out):
    """WFC LightsVisibilitiesVolume: export the cooked native blob verbatim (lvv_<n>.bin) + its actor
    transform. The layout is being recovered natively (ReVa); facts confirmed so far (see
    test_light_visibility.py): octree head with big-endian int32 child indices and 32-byte light bitmasks,
    then (u16 light index, u16 visibility) pairs with visibility in 1/20 steps."""
    vols = []
    for pk in range(len(repo.pkgs)):
      p = repo.pkgs[pk]; pr = repo.readers[pk]
      for i, e in enumerate(p.exports):
        if p.class_name(e) != 'LightsVisibilitiesVolume':
            continue
        props, used = pr.read_object(i + 1)
        o = {t['name']: t['value'] for t in props}
        nat = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        fn = 'lvv_%d.bin' % len(vols)
        open(os.path.join(out, fn), 'wb').write(nat)
        vols.append({'name': p.object_path(i + 1), 'file': fn, 'bytes': len(nat),
                     'location_ue': list(o.get('Location') or [0, 0, 0]),
                     'draw_scale3d': list(o.get('DrawScale3D') or [1, 1, 1]),
                     'draw_scale': o.get('DrawScale', 1.0)})
    return vols


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--list-inline':
        # LightMapTexture2D referenced by <map>_ART_m: umodel -obj list for the atlases cooked inline in ART
        from ue3obj import map_packages
        r = Repo(map_packages(sys.argv[2])[0])
        # (indexed under the _LM outer; umodel exports the ones whose data the ART file carries, skips the rest)
        print(' '.join(sorted({p.split('.')[-1] for p in r.index if (r.cls(p) or '') == 'LightMapTexture2D'})))
        return
    if len(sys.argv) > 3 and sys.argv[1] == '--fogs-only':
        # <map> <src lighting.json> <out dir>: the same file + 'fogs' (A/B copies without the lightmap export)
        from ue3obj import map_packages
        mapname, src, out = sys.argv[2], sys.argv[3], sys.argv[4]
        pkgs, _persistent = map_packages(mapname)
        d = json.load(open(src, encoding='utf-8'))
        d['fogs'] = fogs(Repo(pkgs))
        os.makedirs(out, exist_ok=True)
        json.dump(d, open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
        print(mapname, len(d['fogs']), 'enabled height fog layers:', [(f['object'].split('.')[-1], f['Height'], f['Density']) for f in d['fogs']])
        return
    if len(sys.argv) > 3 and sys.argv[1] == '--volume-fogs-only':
        # <map> <src lighting.json> <out dir>: the same file + 'volume_fogs' when the map has any (else unchanged)
        from ue3obj import map_packages
        mapname, src, out = sys.argv[2], sys.argv[3], sys.argv[4]
        pkgs, _persistent = map_packages(mapname)
        d = json.load(open(src, encoding='utf-8'))
        vf = volume_fogs(Repo(pkgs), mapname)
        if vf: d['volume_fogs'] = vf
        os.makedirs(out, exist_ok=True)
        json.dump(d, open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
        print(mapname, len(vf), 'post-process volume(s) with height fog:', [(v['volume'], len(v['fogs']), len(v['polygons_m'])) for v in vf])
        return
    if len(sys.argv) > 3 and sys.argv[1] == '--standalone-only':
        # <map> <src lighting.json> <out dir>: the same file + the placed light actors its 'lights' lack (by LightGuid)
        from ue3obj import map_packages
        mapname, src, out = sys.argv[2], sys.argv[3], sys.argv[4]
        pkgs, _persistent = map_packages(mapname)
        repo = Repo(pkgs)
        d = json.load(open(src, encoding='utf-8'))
        have = {tuple(l['light_guid']) for l in d['lights']}
        add = [l for pi in range(len(pkgs)) for l in standalone_lights(repo, pi) if tuple(l['light_guid']) not in have]
        d['lights'] += add
        os.makedirs(out, exist_ok=True)
        json.dump(d, open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
        print(mapname, len(add), 'standalone lights added:', [(l['standalone_actor'], l['class'], l['brightness'], l['radius_m']) for l in add])
        return
    mapname, out, umodel_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.join(out, 'lightmaps'), exist_ok=True)
    from ue3obj import map_packages
    pkgs, _persistent = map_packages(mapname)
    repo = Repo(pkgs)
    props, kinds = lm_records(repo, 0)
    # every other level package of the map (Streets: the persistent BASE level, actor-placed StaticInterpActors /
    # collections with atlases cooked inline; UI levels: the streamed sublevels)
    for pi in range(1, len(pkgs)):
        bprops, bkinds = lm_records(repo, pi)
        props.update(bprops)
        for k, v in bkinds.items():
            if k == 'vertex_components': kinds[k] = kinds.get(k, []) + v
            elif k == 'vertex_samples': kinds[k] = dict(kinds.get(k, {}), **v)
            else: kinds[k] = kinds.get(k, 0) + v
    bsp_props = bsp_lighting(repo, out)
    props.update(bsp_props)
    L = []
    F = None
    for pi in range(len(pkgs)):
        L += lights(repo, pi)
        F = F or fog(repo, pi)
    have = {tuple(l['light_guid']) for l in L}
    for pi in range(len(pkgs)):
        L += [l for l in standalone_lights(repo, pi) if tuple(l['light_guid']) not in have]
    PP = postprocess(mapname, out)
    decals(repo, out, mapname, lm_keys=set(props))   # (texture-lightmap keys: decals sample their receiver's)
    atl = sorted({a for r in props.values() for a in r['coeffs']})
    copied = 0
    # (campaign chapters: the umodel dir is chapter-wide, one folder per level package, and LightMapTexture2D_<n> names repeat
    # across packages - D5 1, A1 27, A2 30 - so only this map's own packages' pages are copied: <level> and <level>_LM.
    # A dir without per-package folders (the MP maps) is walked whole, as before)
    own = {os.path.splitext(p)[0].lower() for p in pkgs}
    own |= {o + '_lm' for o in own}
    tops = [d for d in os.listdir(umodel_dir) if os.path.isdir(os.path.join(umodel_dir, d))] if os.path.isdir(umodel_dir) else []
    restrict = any(d.lower() in own for d in tops)
    for root, _, files in os.walk(umodel_dir):
        if restrict and os.path.relpath(root, umodel_dir).split(os.sep)[0].lower() not in own:
            continue
        for fn in files:
            if fn.startswith('LightMapTexture2D_') and fn.endswith('.png'):
                shutil.copyfile(os.path.join(root, fn), os.path.join(out, 'lightmaps', fn)); copied += 1
    # atlases not exported by the umodel step (e.g. BASE inline): the AssetTools slice export
    vs_lm = os.path.join(VS_MAPS, mapname, 'lightmaps')
    for a_ in atl:
        dst = os.path.join(out, 'lightmaps', a_ + '.png')
        if not os.path.exists(dst) and os.path.exists(os.path.join(vs_lm, a_ + '.png')):
            shutil.copyfile(os.path.join(vs_lm, a_ + '.png'), dst); copied += 1
    json.dump({'map': mapname,
               'note': 'Directional lightmaps: L = sum_i dot(N_t, B_i)^2 * tex_i.rgb(sRGB-decoded) * scales[i]; '
                       'B0=(0,sqrt(2/3),1/sqrt3) B1=(-1/sqrt2,-1/sqrt6,1/sqrt3) B2=(1/sqrt2,-1/sqrt6,1/sqrt3) '
                       '(decoded from the original Xenon base-pass shader microcode).',
               'lightmap_type_counts': {k: v for k, v in kinds.items() if k != 'vertex_samples'},
               'vertex_lightmaps': kinds.get('vertex_samples', {}),
               'component_flags': component_flags(mapname),
               'light_visibility_volumes': light_visibility_volumes(repo, out),
               'lightmaps': {'props': props, 'atlases': atl},
               'lights': L, 'fog': F, 'fogs': fogs(repo), 'postprocess': PP,
               # (only when the map has any: every other lighting.json stays byte-identical)
               **({'volume_fogs': VF} if (VF := volume_fogs(repo, mapname)) else {})},
              open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
    from collections import Counter
    print('lightmapped components: %d (types %s); atlases %d; copied %d PNGs' % (len(props), kinds, len(atl), copied))
    print('lights:', Counter(l['class'] for l in L))
    print('postprocess:', {k: v for k, v in PP['settings'].items() if 'Bloom' in k or 'DOF' in k or 'Scene' in k or 'Color' in k})
    print('fog:', {k: F[k] for k in ('Height', 'Density', 'LightColor', 'LightBrightness', 'StartDistance', 'ExtinctionDistance')} if F else None)


if __name__ == '__main__':
    main()
