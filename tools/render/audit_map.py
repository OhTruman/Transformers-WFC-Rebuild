"""Machine-readable map render audit: EXPECTED (cooked level content) vs ACTIVE (what the renderer loaded).

Usage: python audit_map.py <Map> <work/render/<Map>>
Needs, in the render data dir: lighting.json, materials_glsl.json, permutation_check.json (verify_permutations.py)
and active_dump.jsonl (run the game once with WFC_AUDIT_DUMP=<dir>/active_dump.jsonl).
EXPECTED comes from the cooked level packages (class census) and the AssetTools map exports
(ExtractedAssets/VerticalSlice/Maps/<Map>: props.json, props_authored.json, map_fx.json).
Writes <dir>/map_audit.json. Status values: rendered_correctly, rendered_incorrectly, not_rendered,
intentionally_invisible, unknown. "rendered_correctly" means every check the tools can make passed
(original material compiled + permutation match, baked lighting bound where authored); it is not a
pixel comparison against the original game.
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3obj  # noqa: E402

EXTRACTED = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps'
LEVEL_PKGS = ['%s_ART_m.xxx', '%s_BASE_m.xxx', '%s_AUDIO_m.xxx']
INVISIBLE = {'BlockingVolume', 'TriggerVolume', 'TnForcedDirVolume', 'BRUSH', 'Sequence', 'TnWorldInfo', 'Model',
             'CameraActor', 'TnSpawnCluster', 'TnTeamPlayerStart', 'TnFreeForAllPlayerStart', 'AmbientSound',
             'HmAmbientSoundLineEmitter', 'HmAmbientSoundVolumeEmitter', 'BeastSettingsReferer',
             'TnAssetReferencesMultiplayer', 'TnKingOfTheHillZone', 'TnBombPlantPoint', 'TnDominationPoint',
             'TnFlagCapturePoint', 'StaticLightCollectionActor'}
GAMEPLAY_VISUAL = {'TnAmmoCratePickupFactory', 'TnHealthPickupFactory', 'TnOverShieldPickupFactory',
                   'TnGameObjectivePickupFactoryBomb', 'TnGameObjectivePickupFactoryFlag'}


def load_jsonl(p):
    return [json.loads(l) for l in open(p, encoding='utf-8') if l.strip()]


def owner_of(component):
    """Actor name of a dumped component: 'actor:<A>' or '<lvl>.TheWorld.PersistentLevel.<A>.<C>'."""
    if component.startswith('actor:'):
        return component[6:]
    parts = component.split('.')
    return parts[3] if len(parts) > 4 else None


def main():
    mapname, rd = sys.argv[1], sys.argv[2]
    md = os.path.join(EXTRACTED, mapname)
    lighting = json.load(open(os.path.join(rd, 'lighting.json')))
    mats = json.load(open(os.path.join(rd, 'materials_glsl.json')))
    mats = mats.get('materials', mats)
    perm = json.load(open(os.path.join(rd, 'permutation_check.json')))
    perm = perm.get('materials', perm)
    active = load_jsonl(os.path.join(rd, 'active_dump.jsonl'))
    props = json.load(open(os.path.join(md, 'props.json')))
    authored = json.load(open(os.path.join(md, 'props_authored.json')))
    fx = json.load(open(os.path.join(md, 'map_fx.json')))

    audit = {'map': mapname, 'categories': {}, 'notes': [
        'EXPECTED: cooked level packages + AssetTools map exports; ACTIVE: renderer upload dump + compiled materials.',
        'rendered_correctly = all tool checks pass (original graph compiled, permutation match, baked lighting bound '
        'where authored); not a pixel comparison with the original game.']}
    cats = audit['categories']

    # ---- class census (EXPECTED actor population per level)
    census = collections.Counter()
    for pat in LEVEL_PKGS:
        R = ue3obj.Repo([pat % mapname])
        p = R.pkgs[0]
        for i, e in enumerate(p.exports, 1):
            path = p.object_path(i)
            if '.PersistentLevel.' in path and path.count('.') <= 3:
                census[p.class_name(e)] += 1
    cats['actor_census'] = {'expected': dict(sorted(census.items())),
                            'intentionally_invisible_classes': sorted(c for c in census if c in INVISIBLE)}

    # ---- materials (per material in ACTIVE)
    used = collections.Counter(a['material'] for a in active)
    mat_rows = {}
    for m, n in used.items():
        e = mats.get(m)
        pc = perm.get(m) or {}
        if not e or e.get('error'):
            st, why = 'rendered_incorrectly', 'no compiled original graph (glTF fallback)'
        elif pc.get('match', True):
            st, why = 'rendered_correctly', 'original graph; parameter set matches the compiled permutation'
        else:
            diff = pc.get('diff', {})
            only_none = all(set(v.get('compiled_only', [])) <= {'None'} and not v.get('translated_only')
                            for v in diff.values())
            dead_tex = all(k == 'Texture' and not v.get('compiled_only') for k, v in diff.items())
            if only_none:
                st, why = 'unknown', 'compiled permutation has an unnamed (None) parameter not attributable'
            elif dead_tex:
                st, why = 'unknown', 'translation samples textures the original compiler eliminated: %s' % diff
            else:
                st, why = 'rendered_incorrectly', 'permutation differs: %s' % diff
        mat_rows[m] = {'submeshes': n, 'status': st, 'reason': why}
    cats['materials'] = {'expected': len(used), 'active_original': sum(1 for a in active if a['program'] == 'original'),
                         'status_counts': dict(collections.Counter(r['status'] for r in mat_rows.values())),
                         'items': mat_rows}

    # ---- static meshes (props.json) vs ACTIVE components
    lm_props = {k.lower() for k in lighting['lightmaps']['props']}
    vertex_lm = {k.lower() for k in (lighting.get('lightmap_type_counts') or {}).get('vertex_components', [])}
    by_owner_mesh = collections.defaultdict(list)
    for a in active:
        if a['component'].startswith('bsp:') or not a['component']:
            continue
        o = owner_of(a['component'])
        by_owner_mesh[(o, a['source_mesh'])].append(a)
    movers = {m['actor']: m for m in authored.get('movers', [])}
    rows, seen = [], collections.Counter()
    for pr in props['props']:
        key = (pr['actor'], pr['mesh'])
        seen[key] += 1
        subs = by_owner_mesh.get(key, [])
        comps = sorted({s['component'] for s in subs})
        row = {'actor': pr['actor'], 'class': pr['class'], 'mesh': pr['mesh'], 'sublevel': pr.get('sublevel')}
        if len(comps) < seen[key]:
            row.update(status='not_rendered', reason='no ACTIVE component for this actor/mesh instance')
        else:
            sub_comp = comps[min(seen[key], len(comps)) - 1]
            ss = [s for s in subs if s['component'] == sub_comp]
            bad = [s['material'] for s in ss if mat_rows.get(s['material'], {}).get('status') == 'rendered_incorrectly']
            unk = [s['material'] for s in ss if mat_rows.get(s['material'], {}).get('status') == 'unknown']
            # authored baked lighting: a lightmap record exists for the component (or the actor's component)
            has_lm = any(k.split('.persistentlevel.')[-1].startswith(pr['actor'].lower() + '.') for k in lm_props) \
                if pr['class'] != 'StaticMeshCollectionActor' else sub_comp.lower() in lm_props
            lmapped = all(s['lightmapped'] for s in ss)
            mv = movers.get(pr['actor'])
            driven = any(x.get('mover', 0) for x in ss)
            if any(x.get('authored_hidden', 0) for x in ss):
                row.update(status='intentionally_invisible',
                           reason='authored bHidden; unhidden only by Kismet SeqAct_ToggleHidden under '
                                  'TnGameRules_SingleFlagCTF / ScoreBombingRun (rule-gated, resident)')
                rows.append(row)
                continue
            if sub_comp.lower() in vertex_lm and not lmapped:
                row.update(status='rendered_incorrectly',
                           reason='vertex (LMT_1D) lightmap not bound; dynamic light environment used instead')
            elif sub_comp.lower() in vertex_lm and not bad:
                row.update(status='rendered_correctly',
                           reason='vertex (LMT_1D) lightmap bound (decode gamma 2.2 is PROV)')
            elif has_lm and not lmapped:
                row.update(status='rendered_incorrectly', reason='authored lightmap not bound (dynamic lighting used)')
            elif bad:
                row.update(status='rendered_incorrectly', reason='material(s): %s' % sorted(set(bad)))
            elif mv and mv.get('Physics') == 'PHYS_Rotating' and not driven:
                row.update(status='rendered_incorrectly',
                           reason='PHYS_Rotating mover drawn static (RotationRate %s not driven)'
                           % mv.get('rotation_deg_per_s'))
            elif driven and not unk:
                row.update(status='rendered_correctly',
                           reason='authored mover driven (PHYS_Rotating / SkyBeam Matinee), %s' %
                           ('lightmapped' if lmapped else 'dynamic lighting'))
            elif unk:
                row.update(status='unknown', reason='material(s) not verifiable: %s' % sorted(set(unk)))
            else:
                row.update(status='rendered_correctly',
                           reason='lightmapped' if lmapped else 'dynamic lighting (no authored lightmap)')
        rows.append(row)
    cats['static_meshes'] = {'expected': len(props['props']), 'active_components': len(
        {a['component'] for a in active if a['component'] and not a['component'].startswith('bsp:')
         and owner_of(a['component']) and 'DecalActor' not in a['component']}),
        'status_counts': dict(collections.Counter(r['status'] for r in rows)), 'items': rows}

    # ---- BSP
    bsp = [a for a in active if 'ModelComponent' in a['component']]
    n_lm = sum(a['lightmapped'] for a in bsp)
    cats['bsp'] = {'active_elements': len(bsp), 'lightmapped': n_lm,
                   'status_counts': {'rendered_correctly': n_lm, 'unknown': len(bsp) - n_lm},
                   'unknown_reason': 'cooked element has LightMapType 0 and no IrrelevantLights/ShadowMaps/light '
                                     'GUIDs (no static interactions): drawn with the dynamic light environment, '
                                     'assumed equivalent to UE3 uncached light interactions [PROV]',
                   'unlit_elements': sorted({a['component'] for a in bsp if not a['lightmapped']})[:400],
                   'note': 'static-mesh lightmap types: %s (1 = vertex/1D, not decoded; 2 = texture)' % {
                       k: v for k, v in (lighting.get('lightmap_type_counts') or {}).items() if k != 'vertex_components'}}

    # ---- decals
    dec_active = {a['component'] for a in active if 'DecalActor' in a['component']}
    drows = []
    for d in fx['decals']:
        comp = d['component']
        st = 'rendered_correctly' if any(comp.split('.')[3] in c for c in dec_active) else 'not_rendered'
        drows.append({'component': comp, 'material': (d.get('props') or {}).get('DecalMaterial'), 'status': st})
    cats['decals'] = {'expected': len(fx['decals']), 'active': len(dec_active),
                      'status_counts': dict(collections.Counter(r['status'] for r in drows)), 'items': drows}

    # ---- particle systems (level emitters + prefab/pickup components)
    erows = []
    fxp = os.path.join(rd, 'active_dump.jsonl.fx.jsonl')
    fxs = {r['component']: r for r in (load_jsonl(fxp) if os.path.exists(fxp) else [])}
    for pc in fx['particle_components']:
        tpl = (pc.get('props') or {}).get('Template')
        owner = pc.get('owner_class')
        r = fxs.get(pc['component'].lower())
        if r is None:
            st, why = 'not_rendered', 'not instantiated by the renderer'
        elif not r['attached']:
            st, why = 'intentionally_invisible', 'PickupEffect not attached for this factory class (script)'
        elif not r['rule_active']:
            st, why = 'intentionally_invisible', 'factory gated on %s (not active)' % r['rule']
        elif r['drawable_emitters'] == 0:
            st, why = 'unknown', 'no emitter has a flag-invariant look (pstream flagA/flagB semantics UNKNOWN)'
        elif not r['active']:
            st, why = 'intentionally_invisible', 'inactive in its authored/script spawn state'
        elif r['drawable_emitters'] < r['emitters']:
            st, why = 'unknown', 'drawn: %d of %d emitters (rest: flag semantics UNKNOWN)' % (
                r['drawable_emitters'], r['emitters'])
        else:
            st, why = 'rendered_correctly', 'all emitters simulated from decoded modules (UE3 module semantics HIGH)'
        erows.append({'component': pc['component'], 'owner_class': owner, 'template': tpl, 'status': st, 'reason': why})
    cats['emitters'] = {'expected': len(fx['particle_components']),
                        'active': sum(1 for r in erows if r['status'] in ('rendered_correctly', 'unknown')),
                        'templates': dict(collections.Counter(r['template'] for r in erows)),
                        'status_counts': dict(collections.Counter(r['status'] for r in erows)), 'items': erows}

    # ---- lights / fog / volumes / post
    lc = collections.Counter(l['class'] for l in lighting['lights'])
    cats['lights'] = {'expected': dict(lc), 'active_dynamic_environment': sum(1 for l in lighting['lights'] if l.get('enabled')),
                      'status': 'rendered_correctly',
                      'note': 'static contribution baked in lightmaps; dynamic objects use UberLight envs (TotalLightCount 2)'}
    cats['lights_visibility_volume'] = {'expected': census.get('LightsVisibilitiesVolume', 0), 'active': 1,
                                        'status': 'rendered_correctly',
                                        'reason': 'native octree decode + query drive DirectLightEnv baked-light '
                                                  'visibility (ReverseEngineering b52dca9)'}
    fogs = lighting['fog'] if isinstance(lighting['fog'], list) else [lighting['fog']]
    n_on = sum(1 for f in fogs if f.get('bEnabled'))
    cats['height_fog'] = {'expected_actors': census.get('HeightFog', 0), 'components': len(fogs), 'enabled': n_on,
                          'active_layers': 1,
                          'status': 'rendered_correctly' if n_on == 1 else 'rendered_incorrectly'}
    cats['postprocess'] = {'expected': len(lighting.get('postprocess', {})), 'status': 'rendered_correctly'}
    cats['destructibles'] = {'expected': census.get('TnStaticDestructibleActor', 0), 'active': 1,
                             'status': 'rendered_correctly',
                             'reason': 'intact state (Base mesh + authored texture lightmap) at its authored placement '
                                       'outside the playable space; destroyed-state presentation PARTIAL'}
    cats['domination_totems'] = {'expected': 3, 'status': 'intentionally_invisible',
                                 'reason': 'NEU_EnergonTotem_SKEL + EnergonTotem_StandBy loop drawn only under '
                                           'TnGameRules_ScoreDomination (Conquest)'}
    # pickup / objective factories (render_index.json pickup_factory_visuals + pickup_fx_components): the renderer
    # draws the ammo-crate mesh and every factory's effects; Gameplay drives state through setMapEffectState
    by_owner = collections.defaultdict(list)
    for r in erows:
        by_owner[r['component'].rsplit('.', 1)[0].lower()].append(r['status'])
    prow = []
    for pc in fx['particle_components']:
        owner = pc['component'].rsplit('.', 1)[0]
        cls = pc.get('owner_class')
        if cls not in GAMEPLAY_VISUAL or any(x['actor'] == owner for x in prow):
            continue
        sts = by_owner.get(owner.lower(), [])
        if 'rendered_correctly' in sts:
            st = 'rendered_correctly'
            why = ('PROP_NEU_AmmoPickup_STAT mesh (yaw 10000/s, CullDistance 8000) + Pickup_FX beam; mesh attach offset '
                   'UNKNOWN (factory origin, PARTIAL)') if cls == 'TnAmmoCratePickupFactory' else 'custom pickup effect drawn'
        elif sts and all(x == 'intentionally_invisible' for x in sts):
            st, why = 'intentionally_invisible', 'objective factory Disabled outside its game rule (hidden, no collision)'
        else:
            st, why = 'unknown', 'no presentation instantiated'
        prow.append({'actor': owner, 'class': cls, 'status': st, 'reason': why})
    cats['gameplay_visual_actors'] = {'expected': {c: census[c] for c in GAMEPLAY_VISUAL if census.get(c)},
                                      'status_counts': dict(collections.Counter(r['status'] for r in prow)),
                                      'note': 'flag / bomb factory at-rest visual UNKNOWN (AssetTools); state from Gameplay',
                                      'items': prow}

    summary = collections.Counter()
    for c in cats.values():
        for k, v in (c.get('status_counts') or {}).items():
            summary[k] += v
        if 'status_counts' not in c and isinstance(c.get('expected'), int) and c.get('status'):
            summary[c['status']] += c['expected']
    audit['summary'] = dict(summary)
    out = os.path.join(rd, 'map_audit.json')
    json.dump(audit, open(out, 'w'), indent=1)
    print('map audit -> %s' % out)
    for k, c in cats.items():
        brief = {x: c[x] for x in ('expected', 'active', 'active_elements', 'lightmapped', 'status_counts', 'status')
                 if x in c}
        print('  %-24s %s' % (k, json.dumps(brief)[:300]))
    print('  summary', audit['summary'])


if __name__ == '__main__':
    main()
