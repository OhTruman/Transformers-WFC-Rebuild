"""Authored map particle effects -> <out>/map_fx_runtime.json (renderer runtime input).

Sources (read only):
  ExtractedAssets/VerticalSlice/Maps/<Map>/map_fx.json   45 ParticleSystemComponents: template, owner, ue_matrix,
                                                          bAutoActivate (AssetTools a23c675)
  AssetTools/manifests/streets_pickup_fx.json             Pickup_FX / HealthPickup_FX / OvershieldPickup_FX, compiled
                                                          module streams decoded (pstream, M03)
  AssetTools/scripts/wfc/pstream.py                       the same decoder run here for FX_Level_Generic_p Steam_Sm_FX
Class defaults (authored.db CDOs): ParticleModuleRequired EmitterDuration 1.0, EmitterLoops 0, SpawnRate const 0,
SubImages 1x1, ScreenAlignment PSA_Square, bUseLocalSpace False; ParticleEmitter MaxPeakCount 1.
Only distributions with confidence CONFIRMED are emitted as such; PARTIAL ones are flagged.
Usage: python build_map_fx.py <Map> <out_dir>
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo  # noqa: E402

MANI = r'F:/Transformers Rebuild/AssetTools/manifests'
VS_MAPS = r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps'
CONTENT = r'F:/Transformers Rebuild/ExtractedAssets/content'
WFC = r'F:/Transformers Rebuild/AssetTools/scripts/wfc'

TYPE_KIND = {1: 'float constant', 2: 'float constant curve', 3: 'float uniform', 4: 'float uniform curve'}



def manifest(mapname, kind):
    """AssetTools per-map manifest <prefix>_<kind>.json (Streets: streets_movers.json ...). The prefix is the map's
    last name token in lower case, or WFC_MANIFEST_PREFIX. Returns None when the map has no such manifest."""
    pre = os.environ.get('WFC_MANIFEST_PREFIX') or mapname.split('_')[-1].lower()
    p = os.path.join(MANI, '%s_%s.json' % (pre, kind))
    if not os.path.exists(p):
        # [integration M06] the generic map pipeline writes manifests/next_map/<MAP>_<kind>.json (AssetTools
        # MAP_PIPELINE.md); Streets keeps its canonical streets_<kind>.json
        p = os.path.join(MANI, 'next_map', '%s_%s.json' % (mapname, kind))
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None

def tagged_dist(d, default_values):
    """Tagged FRawDistributionFloat (RequiredModule.SpawnRate) -> runtime distribution."""
    d = d or {}
    t = d.get('Type', 1)
    vals = d.get('LookupTable')
    if vals is None:
        return {'kind': 'float constant', 'values': default_values}
    return {'kind': TYPE_KIND.get(t, 'float constant'), 'values': vals}


def mesh_gltf(mesh_path):
    """'FX_Pickups_p.Mesh.LightBeam_WepPickup_STAT' -> content/FX_Pickups_p/Mesh/LightBeam_WepPickup_STAT.gltf"""
    parts = mesh_path.split('.')
    p = os.path.join(CONTENT, *parts) + '.gltf'
    return p.replace('\\', '/') if os.path.exists(p) else None


SPAWN_ONLY = {'PMI_Lifetime', 'PMI_Size', 'PMI_Color', 'PMI_Location', 'PMI_LocationPrimitiveSphere', 'PMI_Velocity',
              'PMI_Rotation', 'PMI_MeshRotation', 'PMI_MeshRotationRate', 'PMI_ColorByParameter'}
VISUAL_UPDATE = {'PMI_SizeMultiplyLife': ['LifeMultiplier'], 'PMI_ColorScaleOverLife': ['ColorScaleOverLife', 'AlphaScaleOverLife']}


def curve_is_one(d, t_max):
    """True when the distribution is exactly 1 for every relative time in [0, t_max]."""
    k, v = d['kind'], d['values']
    if 'curve' not in k:
        return all(abs(x - 1.0) < 1e-6 for x in v)
    comps = 3 if k.startswith('vector') else 1
    stride = comps * (2 if 'uniform' in k else 1)
    start, scale = v[2], v[3]
    n = (len(v) - 4) // stride
    for i in range(n):
        t = start + i / scale if scale else start
        if t > t_max + 1e-6 and i > 0:
            break
        if any(abs(x - 1.0) > 1e-6 for x in v[4 + i * stride:4 + (i + 1) * stride]):
            return False
    return True


def flag_analysis(lod):
    """The compiled per-module flag bytes (pstream flagA / flagB) are UNKNOWN. Two readings are plausible:
    (A) flagA = enabled, (B) flagA / flagB = spawn / update. A flagA=0 module of a spawn-only class contributes
    nothing under either reading (skipped). A flagA=0 size / colour / alpha over-life module contributes under (B)
    only: the emitter's look is flag-invariant only if that curve is exactly 1 over the relative times reached
    (immortal particles: t = 0 only)."""
    immortal = any(m['module'] == 'PMI_Lifetime' and all(abs(x) < 1e-9 for x in m['dists'].get('Lifetime', {}).get('values', [1]))
                   for m in lod['modules'])
    t_max = 0.0 if immortal else 1.0
    skipped, ambiguous, motion_unknown = [], [], []
    for m in lod['modules']:
        fa, fb = (m.get('raw_flags') or [1, 1])[:2]
        if m['module'] in SPAWN_ONLY and fa == 0:
            skipped.append(m['module'])
        elif m['module'] in VISUAL_UPDATE and fa == 0:
            if not all(curve_is_one(m['dists'][p], t_max) for p in VISUAL_UPDATE[m['module']] if p in m['dists']):
                ambiguous.append(m['module'])
        elif m['module'] == 'PMI_Gravity':
            motion_unknown.append('PMI_Gravity (GravityMultiplier / acceleration native; [%d,%d])' % (fa, fb))
    return {'skipped_spawn_modules': skipped, 'ambiguous_visual_modules': ambiguous,
            'motion_unknown': motion_unknown, 'visual_invariant': not ambiguous}


def system_runtime(name, s):
    out = {'name': name, 'lod_distances': s['props'].get('LODDistances', [0.0]),
           'lod_method': s['props'].get('LODMethod', 'PARTICLESYSTEMLODMETHOD_Automatic'), 'emitters': []}
    for e in s['emitters']:
        em = {'name': e['name'], 'max_peak_count': e['props'].get('MaxPeakCount', 1),
              'render_mode': e['props'].get('SpriteEmitterRenderMode', 'SERM_Normal'), 'lods': []}
        for L in e['lods']:
            req = L['RequiredModule']['props']
            td = (L.get('TypeDataModule') or {}).get('props') or {}
            tdc = (L.get('TypeDataModule') or {}).get('class') or ''
            lod = {'level': L['level'], 'material': req.get('Material'),
                   # M32: emitter kind (sprite / mesh / trail2 / beam2) and the SubUV interpolation method
                   'type_data': {'ParticleModuleTypeDataMesh': 'mesh', 'ParticleModuleTypeDataTrail2': 'trail2',
                                 'ParticleModuleTypeDataBeam2': 'beam2'}.get(tdc, 'sprite' if not tdc else tdc),
                   'subuv_method': req.get('InterpolationMethod', 'PSUVIM_None'),
                   # M44: Beam2 / Trail2 type data (MaxBeamCount caps live beams; taper / tessellation PARTIAL)
                   'beam_trail': {k: td[k] for k in ('MaxBeamCount', 'TaperMethod', 'InterpolationPoints', 'Speed',
                                                     'MaxParticleInTrailCount', 'TessellationFactor',
                                                     'bEmitOnlyWhenMoving', 'bConnectToSource') if k in td}
                                 if tdc in ('ParticleModuleTypeDataBeam2', 'ParticleModuleTypeDataTrail2') else None,
                   'required': {'emitter_duration': req.get('EmitterDuration', 1.0),
                                'emitter_loops': req.get('EmitterLoops', 0),
                                'spawn_rate': tagged_dist(req.get('SpawnRate'), [0.0]),
                                'burst_list': req.get('BurstList', []),
                                'use_local_space': bool(req.get('bUseLocalSpace', False)),
                                'screen_alignment': req.get('ScreenAlignment', 'PSA_Square'),
                                'subimages': [req.get('SubImages_Horizontal', 1), req.get('SubImages_Vertical', 1)]},
                   'mesh': None, 'modules': [], 'assignment_complete': L.get('assignment_complete', False),
                   'default_color': L.get('default_color'), 'size_param': L.get('size_param')}
            if lod['beam_trail'] is not None:
                # M55 (RE pass 5 s9, native TypeDataBeam2 Spawn / Trail2 vertex count): taper curves along the beam and
                # the trail tessellation; CDO defaults TaperFactor / TaperScale 1.0, TessellationStrength 1.0
                lod['beam_trail']['taper_factor'] = tagged_dist(td.get('TaperFactor'), [1.0])
                lod['beam_trail']['taper_scale'] = tagged_dist(td.get('TaperScale'), [1.0])
                lod['beam_trail']['TessellationStrength'] = td.get('TessellationStrength', 1.0)
                # M61 texture tiling (CDO: Beam2 / Trail2 TextureTile 1, TextureTileDistance 0, bTilePerParticle false;
                # no MP effect authors any of them - 735 LODs surveyed)
                lod['beam_trail']['TextureTile'] = td.get('TextureTile', 1)
                lod['beam_trail']['TextureTileDistance'] = td.get('TextureTileDistance', 0.0)
                lod['beam_trail']['bTilePerParticle'] = bool(td.get('bTilePerParticle', False))
                lod['beam_trail']['modules'] = L.get('beam_modules')         # M56: noise / sine waves / source / target
                # M60: BeamMethod (CDO PEB2M_Target) and Distance (CDO constant 25) for the Distance method
                lod['beam_trail']['BeamMethod'] = td.get('BeamMethod', 'PEB2M_Target')
                lod['beam_trail']['distance'] = tagged_dist(td.get('Distance'), [25.0])
            if td.get('Mesh'):
                lod['mesh'] = {'object': td['Mesh'], 'gltf': mesh_gltf(td['Mesh']),
                               'override_material': bool(td.get('bOverrideMaterial', False))}
            for m in L.get('compiled_modules', []):
                mod = {'module': m['module'], 'dists': {}, 'partial': [], 'raw_flags': m.get('raw_flags', [1, 1])}
                for i, dd in enumerate(m['distributions']):
                    prop = dd['property'] or ('DynamicParams[%d].ParamValue' % i)
                    mod['dists'][prop] = {'kind': dd['kind'], 'values': dd['values'], 'confidence': dd['confidence']}
                    if dd['confidence'] != 'CONFIRMED':
                        mod['partial'].append(prop)
                lod['modules'].append(mod)
            lod['flag_analysis'] = flag_analysis(lod)
            # RE MILESTONE04 pickup/objective presentation §2 (HIGH): flagA == membership in the executed module
            # list (bEnabled); flagA = 0 modules are disabled and not evaluated. flagB stays UNKNOWN (mesh-rotation
            # modules only). With that reading every emitter's look is determined.
            lod['disabled_modules'] = [m['module'] for m in lod['modules'] if (m.get('raw_flags') or [1, 1])[0] == 0]
            em['lods'].append(lod)
        em['renderable'] = True
        em['flag_reading'] = 'flagA = bEnabled (RE HIGH)'
        out['emitters'].append(em)
    return out


def default_color(tail, nrec):
    """M34: ParticleModuleColorByParameter's DefaultColor in the compiled LOD stream: after the module-order list
    (int count = number of module records, then bytes 0..n-1) and the following counted byte list, 16 bytes
    (12 zero + an int) and 4 more, an ARGB FColor. Verified against every emitter of MuzzleFlash_AssaultRifle_FX
    (ff 33 19 ff = the Ion blue (51, 25, 255)) and the editor thumbnails (EMP shotgun muzzle / squib red). Other stream
    layouts (alpha != 0xff at that position) are not decoded: None."""
    import struct
    for i in range(0, len(tail) - 8):
        n = struct.unpack_from('>i', tail, i)[0]
        if n != nrec or n < 2 or i + 4 + n + 4 > len(tail): continue
        if list(tail[i + 4:i + 4 + n]) != list(range(n)): continue
        j = i + 4 + n
        m = struct.unpack_from('>i', tail, j)[0]
        if not (0 <= m <= 64): continue
        k = j + 4 + m + 20
        if k + 4 > len(tail): return None
        a, r, g, b = tail[k:k + 4]
        if a == 0xFF: return [r, g, b, a]
        break
    # Fallback [MEDIUM]: other stream layouts carry the same FColor after a zero int; accepted only when the stream
    # holds exactly one such A = 0xFF candidate (reproduces every structurally decoded colour, e.g. the Ion blue in
    # MuzzleFlash_AssaultRifle_FX's BackJet / GLOW emitters).
    cands = [k for k in range(4, len(tail) - 3)
             if tail[k] == 0xFF and tail[k - 4:k] == b'\0\0\0\0' and tail[k + 1:k + 4] != b'\0\0\0']
    if len(cands) == 1:
        a, r, g, b = tail[cands[0]:cands[0] + 4]
        return [r, g, b, a]
    return None


RAW_KIND = {1: 'float constant', 2: 'float constant curve', 3: 'float uniform', 4: 'float uniform curve',
            7: 'vector constant', 8: 'vector constant curve', 9: 'vector uniform', 10: 'vector uniform curve'}


def raw_dist(d, default_values):
    """Tagged FRawDistribution read from the cooked object (ue3obj): the LookupTable holds float bit patterns.
    -> runtime distribution ([min, max, start time, time scale] + samples for curves, as FxDist)."""
    import struct as _st
    if isinstance(d, list):                        # tagged struct: [{name, type, value}, ...]
        d = {x.get('name'): x.get('value') for x in d if isinstance(x, dict)}
    if not isinstance(d, dict) or not d.get('LookupTable'):
        return {'kind': 'float constant', 'values': default_values}
    vals = []
    for x in d['LookupTable']:
        if x is None: x = 0       # unparsed stub entries: zero (RE pass 5 s11: the Squib Target (0, 0, 0))
        vals.append(_st.unpack('<f', _st.pack('<i', x))[0] if isinstance(x, int) else float(x))
    t = d.get('Type')
    if t is None:                                  # constant: no type byte cooked (1 float or a vector of 3)
        return {'kind': 'vector constant' if len(vals) == 3 else 'float constant', 'values': vals}
    return {'kind': RAW_KIND.get(t, 'float constant'), 'values': vals}


def beam_modules(R, p, t, blob, mods):
    """M56: the Beam2 modules a LOD uses (cooked LODs keep no Modules array: the LOD's serialized bytes reference each
    module's export index, big-endian int32; exactly one hit required, as the M47 Size scan). RE pass 5 addendum: beam
    LODs reference Source + Target (+ Noise / SineWave)."""
    import struct as _st
    out = {}
    for k, path, cls in mods:
        if blob.count(_st.pack('>i', k + 1)) != 1: continue
        o = R.obj(path.lower()) or {}
        if cls == 'ParticleModuleBeamNoise':
            out['noise'] = {
                'low_freq': bool(o.get('bLowFreq_Enabled', False)), 'frequency': int(o.get('Frequency', 0)),
                'frequency_low': int(o.get('Frequency_LowRange', 0)),
                # defaults: Engine.Default__ParticleModuleBeamNoise (cooked CDO): NoiseRange / NoiseSpeed 50,
                # NoiseTangentStrength 250, NoiseRangeScale 1, NoiseLockRadius 1, NoiseTension 0.5, NoiseTessellation 1
                'range': raw_dist(o.get('NoiseRange'), [50.0, 50.0, 50.0]),
                'range_scale': raw_dist(o.get('NoiseRangeScale'), [1.0]),
                'speed': raw_dist(o.get('NoiseSpeed'), [50.0, 50.0, 50.0]),
                'lock_time': float(o.get('NoiseLockTime', 0.0)), 'lock_radius': float(o.get('NoiseLockRadius', 1.0)),
                'tension': float(o.get('NoiseTension', 0.5)), 'tessellation': int(o.get('NoiseTessellation', 1)),
                'frequency_distance': float(o.get('FrequencyDistance', 0.0)),
                'tangent_strength': raw_dist(o.get('NoiseTangentStrength'), [250.0]),
                'apply_scale': bool(o.get('bApplyNoiseScale', False)),
                'scale': raw_dist(o.get('NoiseScale'), [1.0]),
                'smooth': bool(o.get('bSmooth', False)), 'oscillate': bool(o.get('bOscillate', False)),
                'use_noise_tangents': bool(o.get('bUseNoiseTangents', False)),
                'target_noise': bool(o.get('bTargetNoise', False)),
                'nr_scale_emitter_time': bool(o.get('bNRScaleEmitterTime', False)),
                'props_seen': sorted(o.keys())}
        elif cls == 'ParticleModuleBeamSineWave':
            waves = []
            for w in o.get('SineWaves') or []:
                f = {x['name']: x['value'] for x in w} if isinstance(w, list) else dict(w)
                waves.append({'amplitude': float(f.get('Amplitude', 0.0)), 'period': float(f.get('Period', 1.0)),
                              'speed': float(f.get('Speed', 0.0)), 'phase': float(f.get('PhaseOffset', 0.0)),
                              'direction': [float(c) for c in (f.get('Direction') or [0.0, 0.0, 0.0])]})
            out['sine_waves'] = waves
        elif cls in ('ParticleModuleBeamSource', 'ParticleModuleBeamTarget'):
            # M60: every authored Source / Target property; defaults = Engine.Default__ParticleModuleBeamSource /
            # Target (Source / Target (50, 50, 50), tangent (1, 0, 0), strength 25, Target LockRadius 10)
            k = 'Source' if cls.endswith('Source') else 'Target'
            out[k.lower()] = {
                'method': o.get(k + 'Method', 'PEB2STM_Default'), 'name': o.get(k + 'Name'),
                'position': raw_dist(o.get(k), [50.0, 50.0, 50.0]),
                'absolute': bool(o.get('b' + k + 'Absolute', False)),
                'tangent_method': o.get(k + 'TangentMethod', 'PEB2STTM_Direct'),
                'tangent': raw_dist(o.get(k + 'Tangent'), [1.0, 0.0, 0.0]),
                'strength': raw_dist(o.get(k + 'Strength'), [25.0]),
                'lock': bool(o.get('bLock' + k, False)), 'lock_tangent': bool(o.get('bLock' + k + 'Tangent', False)),
                'lock_strength': bool(o.get('bLock' + k + 'Strength', False)),
                'lock_radius': float(o.get('LockRadius', 10.0)),
                'props_seen': sorted(o.keys())}
    return out


def class_templates():
    """M51: ParticleSystem templates the shipped weapon / character data references (FlightEffect, muzzle, impact,
    vehicle FX, abilities) -> {package: {template}}. The original loads them with the class's own FX package
    (e.g. FX_IonBlaster_p, FX_PlasmaCannon_p), which is not always cooked into a map's level packages."""
    import glob, re
    pat = re.compile(r'"(FX_[A-Za-z0-9_]+\.[A-Za-z0-9_]+\.[A-Za-z0-9_]+)"')
    refs = {}
    vs = os.path.dirname(VS_MAPS)
    for f in glob.glob(vs + '/Weapons/*/weapon.json') + glob.glob(vs + '/Characters/*/character.json'):
        for t in pat.findall(open(f, encoding='utf-8').read()):
            refs.setdefault(t.split('.')[0], set()).add(t)
    return refs


def library(mapname):
    """M32: every ParticleSystem cooked into the map's level packages (weapon muzzle / tracer / impact templates
    are cooked into each MP BASE package) -> {template: (pstream summary, package)}. The runtime spawns these by name
    (IRenderer::spawnParticleEffect). M51: plus the class-data templates the level packages lack, from their own
    cooked FX package."""
    from ue3obj import map_packages, Repo as _R
    sys.path.insert(0, WFC)
    import pstream
    out = {}
    work = []                                       # (template, package hint)
    for pk in map_packages(mapname)[0]:
        p = _R([pk]).pkgs[0]
        pkn = pk[:-4] if pk.lower().endswith('.xxx') else pk
        work += [(p.object_path(i + 1), pkn) for i, e in enumerate(p.exports) if p.class_name(e) == 'ParticleSystem']
    # the class FX packages are seekfree stubs on Xenon: pstream resolves the template to the package that holds it
    work += [(t, None) for ts in class_templates().values() for t in sorted(ts)]
    repos = {}
    for t, hint in work:
        if t in out: continue
        try:
            s = pstream.system(t, hint) if hint else pstream.system(t)
        except Exception as ex:                    # class-data refs also name materials / meshes / modules
            if hint: print('  library: %s: %s' % (t, ex))
            continue
        if s.get('missing_in') or not s.get('emitters'): continue
        pkn = s['package']
        pk = pkn + '.xxx'
        if pk not in repos: repos[pk] = _R([pk]).pkgs[0]
        p = repos[pk]
        import objtree
        op = objtree.package(pkn)
        # M46 / M47: ParticleModuleSizeMultiplyLife driven by an instance parameter (DistributionVectorParticleParameter,
        # RE pass 4: the HoverFX 'Size'). The LODs' Modules arrays are stripped by the cook, so a LOD uses the module
        # when its serialized bytes reference the module's export index (big-endian int32; RE raw scan: CarHover_A's
        # shared _9193 is in all 7 level-0 LODs and no level-1 LOD) [HIGH]
        import struct as _st
        pidx = {p.object_path(k + 1).lower(): k for k, _e in enumerate(p.exports)}
        bmods = [(k, p.object_path(k + 1), p.class_name(e2)) for k, e2 in enumerate(p.exports)
                 if p.class_name(e2) in ('ParticleModuleBeamNoise', 'ParticleModuleBeamSineWave', 'ParticleModuleBeamSource',
                                         'ParticleModuleBeamTarget')
                 and p.object_path(k + 1).lower().startswith(t.lower() + '.')]
        if bmods:
            for e in s['emitters']:
                for L in e['lods']:
                    li = pidx.get(L['lod'].lower())
                    if li is None: continue
                    ex = p.exports[li]
                    bm = beam_modules(_R([pk]), p, t, p.data[ex['serial_offset']:ex['serial_offset'] + ex['serial_size']], bmods)
                    if bm: L['beam_modules'] = bm
        mods = []
        for i2, e2 in enumerate(p.exports):
            path2 = p.object_path(i2 + 1)
            if p.class_name(e2) == 'DistributionVectorParticleParameter' and path2.lower().startswith(t.lower() + '.')                         and 'sizemultiplylife' in path2.lower():
                d2 = _R([pk]).obj(path2.lower()) or {}
                mi = pidx.get(path2.rsplit('.', 1)[0].lower())
                if mi is not None:
                    mods.append((_st.pack('>i', mi + 1), {'name': d2.get('ParameterName'),
                                                          'constant': d2.get('Constant', [1.0, 1.0, 1.0])}))
        if mods:
            for e in s['emitters']:
                for L in e['lods']:
                    li = pidx.get(L['lod'].lower())
                    if li is None: continue
                    ex = p.exports[li]
                    blob = p.data[ex['serial_offset']:ex['serial_offset'] + ex['serial_size']]
                    for pat, sp in mods:
                        offs = [k for k in range(4, len(blob) - 3) if blob[k:k + 4] == pat]
                        if not offs: continue
                        # guards (RE: a raw int32 can collide with float bits / counts): exactly one hit, sitting in
                        # an object array - preceded by its count (1..32) or by another export of this system
                        # observed layout (all 18 Streets hits): int32 count (1) + one byte + the int32 module index
                        prev = _st.unpack_from('>i', blob, offs[0] - 4)[0]
                        cnt5 = _st.unpack_from('>i', blob, offs[0] - 5)[0] if offs[0] >= 5 else 0
                        in_array = 1 <= prev <= 32 or 1 <= cnt5 <= 32 or (0 < prev <= len(p.exports) and
                                   p.object_path(prev).lower().startswith(t.lower() + '.'))
                        if len(offs) == 1 and in_array:
                            L['size_param'] = sp
                        else:
                            print('  size param: %s %s: %d hit(s), prev int %d - not bound' % (t, L['lod'].split('.')[-1],
                                                                                              len(offs), prev))
        out[t] = s
    return out


def library_materials(lib):
    mats = set()
    for s in lib.values():
        for e in s['emitters']:
            for L in e['lods']:
                m = (L['RequiredModule']['props'] or {}).get('Material')
                if m: mats.add(m)
    return sorted(mats)


def main():
    if sys.argv[1] == '--list-materials':          # build_render_data: materials of the template library
        for m in library_materials(library(sys.argv[2])): print(m)
        return
    mapname, out = sys.argv[1], sys.argv[2]
    fx = json.load(open(os.path.join(VS_MAPS, mapname, 'map_fx.json'), encoding='utf-8'))
    pk = (manifest(mapname, 'pickup_fx') or {}).get('systems', {})
    systems = {}
    for name, s in pk.items():
        systems[name] = system_runtime(name, s)
    sys.path.insert(0, WFC)
    import pstream  # AssetTools decoder (read only)
    for comp in fx['particle_components']:
        t = comp['props'].get('Template')
        if t and t not in systems:
            pkg = (fx['particle_systems'].get(t) or {}).get('package')
            s = pstream.system(t, pkg)
            systems[t] = system_runtime(t, s)
    nlib = 0
    for t, s in library(mapname).items():
        if t not in systems:
            systems[t] = system_runtime(t, s); nlib += 1
    print('map fx: template library +%d systems' % nlib)
    inst = []
    for comp in fx['particle_components']:
        oc, t = comp['owner_class'], comp['props'].get('Template')
        highlight = t == 'FX_Pickups_p.FX.Pickup_FX' and oc != 'Emitter'
        # PickupEffect (highlight) is attached / rendered only for factory classes that list it in Components
        # (ammo crate, flag / bomb objectives: ShouldDisplayHighlightFx inherited True from TnWeaponPickupFactory,
        # RE MILESTONE04 pickup/objective §1); health / overshield never attach it (decompiled script).
        attached = not highlight or oc in ('TnAmmoCratePickupFactory', 'TnGameObjectivePickupFactoryFlag',
                                           'TnGameObjectivePickupFactoryBomb')
        # spawn state: PreBeginPlay -> InitializePickup -> SetPickupMesh -> SetPickupVisible activates the
        # highlight where ShouldDisplayHighlightFx (ammo crates; RE d50c2a9 P2), overriding bAutoActivate false
        active = bool(comp['props'].get('bAutoActivate', True)) or (highlight and attached)
        rule = {'TnGameObjectivePickupFactoryFlag': 'TransGame.TnGameRules_SingleFlagCTF',
                'TnGameObjectivePickupFactoryBomb': 'TransGame.TnGameRules_ScoreBombingRun'}.get(oc)
        inst.append({'component': comp['component'], 'owner': comp['owner'].split('.')[-1],
                     'owner_class': oc, 'template': t, 'role': 'highlight' if highlight else 'custom' if oc != 'Emitter' else 'level',
                     'attached': attached, 'auto_activate': active, 'required_game_rule': rule,
                     'ue_matrix': comp['ue_matrix'],
                     # PSC InstanceParameters (colour): read by ParticleModuleColorByParameter (Steam_Sm_FX:
                     # ColorParam 'SteamColor', FName in the compiled LOD stream)
                     'color_params': {ip['Name']: [ip['Color'][c] for c in ('R', 'G', 'B', 'A')]
                                      for ip in (comp['props'].get('InstanceParameters') or [])
                                      if ip.get('Name') and isinstance(ip.get('Color'), dict)}})
    # destructible state meshes -> authored StaticMeshComponents (cooked HmStaticMeshDestructionEffect.MeshComponents);
    # the renderer joins the state mesh to its lightmap through these
    dcomp = {}
    try:
        from ue3obj import map_packages
        drepo = Repo(map_packages(mapname)[0])
        for path in drepo.index:
            if (drepo.cls(path) or '') != 'HmStaticMeshDestructionEffect': continue
            for mc in (drepo.obj(path) or {}).get('MeshComponents') or []:
                cref = mc.get('ref') if isinstance(mc, dict) else mc
                sm = (drepo.obj(cref) or {}).get('StaticMesh') or {}
                mref = sm.get('ref') if isinstance(sm, dict) else sm
                if not cref or not mref: continue
                actor = cref.split('.HmDestructibleComponent')[0]
                dcomp.setdefault(actor, {}).setdefault(mref, []).append(cref)
    except Exception as ex:
        print('destructible components: %s' % ex)
    res = {'map': mapname, 'destructible_mesh_components': dcomp, 'source': 'AssetTools a23c675 map_fx.json + streets_pickup_fx.json + pstream (Steam_Sm_FX)',
           'instances': inst, 'systems': systems}
    json.dump(res, open(os.path.join(out, 'map_fx_runtime.json'), 'w'), indent=1)
    print('map fx: %d components (%d auto-active), %d systems' % (len(inst), sum(i['auto_activate'] for i in inst),
                                                                    len(systems)))
    for n, s in systems.items():
        for e in s['emitters']:
            L = e['lods'][0]
            print('  %-22s %-24s renderable=%-5s %s' % (n.split('.')[-1], e['name'], e['renderable'],
                                                       json.dumps(L['flag_analysis'])))


if __name__ == '__main__':
    main()
