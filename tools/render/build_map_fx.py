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


def library(mapname):
    """M32: every ParticleSystem cooked into the map's level packages (weapon muzzle / tracer / impact templates
    are cooked into each MP BASE package) -> {template: (pstream summary, package)}. The runtime spawns these by name
    (IRenderer::spawnParticleEffect)."""
    from ue3obj import map_packages, Repo as _R
    sys.path.insert(0, WFC)
    import pstream
    out = {}
    for pk in map_packages(mapname)[0]:
        p = _R([pk]).pkgs[0]
        for i, e in enumerate(p.exports):
            if p.class_name(e) != 'ParticleSystem': continue
            t = p.object_path(i + 1)
            if t in out: continue
            pkn = pk[:-4] if pk.lower().endswith('.xxx') else pk
            try:
                s = pstream.system(t, pkn)
            except Exception as ex:
                print('  library: %s: %s' % (t, ex)); continue
            if s.get('missing_in'): continue
            import objtree
            op = objtree.package(pkn)
            # M46 / M47: ParticleModuleSizeMultiplyLife driven by an instance parameter (DistributionVectorParticleParameter,
            # RE pass 4: the HoverFX 'Size'). The LODs' Modules arrays are stripped by the cook, so a LOD uses the module
            # when its serialized bytes reference the module's export index (big-endian int32; RE raw scan: CarHover_A's
            # shared _9193 is in all 7 level-0 LODs and no level-1 LOD) [HIGH]
            import struct as _st
            pidx = {p.object_path(k + 1).lower(): k for k, _e in enumerate(p.exports)}
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
