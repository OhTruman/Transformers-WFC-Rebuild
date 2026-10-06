"""Weapon impact decal tables for one map's render data (RE pass 5 s12 addenda 29 / 30).

Written by build_materials.py as <out>/impact_decals.json from the map's cooked packages:
  weapons    TnWeapon<X> class -> its WeaponMeshTemplate's WeaponEffectsType and DefaultDecal
  surfaces   PhysicalMaterial -> its property object's NoDecal, ApplyRandomRotationToDecal and
             WeaponTypeSpecificDecals groups ({types, decals})
  materials  every compiled material -> the PhysicalMaterial of its MIC chain (child first)
  default_surface  GEngine.DefaultPhysMaterial (ini DefaultPhysMaterialName = Metal_PHYSMAT): materials whose chain
             names none (RE s12 add. 34, HIGH: stock UE3 GetPhysicalMaterial chain)
A decal entry is {material, min_w, min_h, range_w, range_h, thickness, lifetime, uniform, random_rotation}. Fields
the cooked struct omits take the DecalTemplate struct defaults (RE add. 34, CONFIRMED): MinWidth / MinHeight 100,
Width / HeightRange 100, Thickness 10, Lifetime 30, NoClip false, UniformScale true, ApplyRandomRotation true.
"""


def _fields(v):
    if isinstance(v, list):
        return {x.get('name'): x.get('value') for x in v if isinstance(x, dict)}
    return dict(v or {})


def _ref(v):
    return (v or {}).get('ref') if isinstance(v, dict) else None


def _decal(v):
    f = _fields(v)
    mat = _ref(f.get('Material'))
    if not mat:
        return None
    return {'material': mat,
            'min_w': float(f.get('MinWidth', 100.0)), 'min_h': float(f.get('MinHeight', 100.0)),
            'range_w': float(f.get('WidthRange', 100.0)), 'range_h': float(f.get('HeightRange', 100.0)),
            'thickness': float(f.get('Thickness', 10.0)), 'lifetime': float(f.get('Lifetime', 30.0)),
            'uniform': str(f.get('UniformScale', True)) not in ('False', 'false', '0'),
            'random_rotation': str(f.get('ApplyRandomRotation', True)) not in ('False', 'false', '0')}


def _truthy(v):
    return str(v) in ('True', 'true', '1')


def build(repo, material_names):
    weapons, surfaces, materials = {}, {}, {}
    for k in repo.index:
        if '.default__tnweapon' not in k or k.count('.') != 1:
            continue
        o = repo.obj(k) or {}
        mesh = _ref(o.get('WeaponMeshTemplate'))
        if not mesh:
            continue
        m = repo.obj(mesh) or {}
        h = repo.find(k)
        cls = repo.pkgs[h[0]].object_path(h[1]).split('Default__', 1)[1]          # original case
        weapons[cls] = {'mesh': mesh,
                        'effects_type': (_ref(m.get('WeaponEffectsType')) or 'None').split('.')[-1],
                        'default_decal': _decal(m.get('DefaultDecal')) if m.get('DefaultDecal') else None}

    def surface(pm):
        if pm in surfaces:
            return
        o = repo.obj(pm) or {}
        prop = _ref(o.get('PhysicalMaterialProperty'))
        if not prop:
            surfaces[pm] = None                      # no property object: no impact decal (RE add. 29)
            return
        po = repo.obj(prop) or {}
        groups = []
        for g in po.get('WeaponTypeSpecificDecals') or []:
            f = _fields(g)
            types = [(_ref(t) or 'None').split('.')[-1] for t in (f.get('WeaponTypes') or [])] or ['None']
            decals = [d for d in (_decal(x) for x in (f.get('ImpactDecal') or [])) if d]
            groups.append({'types': types, 'decals': decals})
        surfaces[pm] = {'no_decal': _truthy(po.get('NoDecal')),
                        'random_rotation': not str(po.get('ApplyRandomRotationToDecal', True)) in ('False', 'false'),
                        'groups': groups}

    for name in material_names:
        p, guard = name, 0
        pm = None
        while p and guard < 16:                       # child first: the nearest PhysMaterial in the MIC chain
            o = repo.obj(p) or {}
            pm = _ref(o.get('PhysMaterial'))
            if pm:
                break
            p = _ref(o.get('Parent'))
            guard += 1
        if pm:
            materials[name] = pm
            surface(pm)
    default_pm = 'ENV_PHYSMAT_p.Metal_PHYSMAT'
    if repo.cls(default_pm):
        surface(default_pm)
    return {'generated_by': 'tools/render/impact_decals.py', 'weapons': weapons, 'surfaces': surfaces,
            'materials': materials, 'default_surface': default_pm if repo.cls(default_pm) else None}


def decal_materials(table):
    out = set()
    for w in table['weapons'].values():
        if w['default_decal']:
            out.add(w['default_decal']['material'])
    for s in table['surfaces'].values():
        for g in (s or {}).get('groups', []):
            for d in g['decals']:
                out.add(d['material'])
    return sorted(out)
