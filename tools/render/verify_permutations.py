"""Verify translated materials against the ORIGINAL compiled static permutations.

Every cooked Material / MaterialInstanceConstant carries its compiled FMaterialResource in the
native tail. Its uniform-expression lists name exactly which scalar, vector and texture parameters
the shipped shader reads. If matc.py evaluated a static switch differently, or walked a different
branch, its set of parameters read will differ from the compiled set.

    python verify_permutations.py <render_data_dir> [MAP]
Writes <render_data_dir>/permutation_check.json and prints mismatches.
"""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages  # noqa: E402

KINDS = {'FMaterialUniformExpressionScalarParameter': 'Scalar',
         'FMaterialUniformExpressionVectorParameter': 'Vector',
         'FMaterialUniformExpressionTextureParameter': 'Texture',
         'FMaterialUniformExpressionTextureSetIntermediateParameter': 'Texture'}
ENGINE_ADDED = {'SelectionColor'}   # editor selection highlight, injected by the engine


def compiled_params(repo, path):
    p, t = repo.native_tail(path)
    ids = {i: KINDS[n] for i, n in enumerate(p.names) if n in KINDS}
    out = {'Scalar': set(), 'Vector': set(), 'Texture': set()}
    # the texture-expression block (u32 count, then {FName class, number, [param FName, number,] texture ref}) is
    # not 4-byte aligned in the cooked resource (MonitorScreen_Parent_MAT: offset 2 mod 4; RE fc5672 §3) -> 2-byte scan
    for o in range(0, len(t) - 16, 2):
        a, b = struct.unpack_from('>ii', t, o)
        if b == 0 and a in ids:
            ni, nn = struct.unpack_from('>ii', t, o + 8)
            if 0 <= ni < len(p.names) and nn == 0:
                out[ids[a]].add(p.names[ni])
    return out


def main():
    data = sys.argv[1]
    mapname = sys.argv[2] if len(sys.argv) > 2 else 'MP_IAC_Streets'
    repo = Repo(list(reversed(map_packages(mapname)[0])), fallback=['TransGame.xxx'])
    mats = json.load(open(os.path.join(data, 'materials_glsl.json')))
    report = {}
    bad = 0
    for name, v in sorted(mats.items()):
        info = v.get('info') or {}
        if not info.get('params_read'):
            continue
        src = (info.get('chain') or [None])[0] or info.get('master')
        try:
            comp = compiled_params(repo, src)
            # a MaterialInstanceConstant without its own static permutation carries no compiled resource: its
            # shader is the parent chain's (UE3 MIC without StaticParameters) -> verify against the master's
            if not any(comp.values()) and info.get('master') and info.get('master') != src:
                comp = compiled_params(repo, info['master'])
                src = info['master'] + ' (instance without own static permutation)'
        except Exception as ex:
            report[name] = {'error': str(ex)}
            continue
        mine = {k: set(x) for k, x in info['params_read'].items()}
        # a parameter expression authored without ParameterName compiles as a uniform named 'None'; the
        # translator reads it under its (empty) name -> attributable when the master graph has one
        master = info.get('master') or ''
        pre = master.lower() + '.'
        unnamed = {'Scalar': False, 'Vector': False, 'Texture': False}
        for path in repo.index:
            if not path.startswith(pre): continue
            cls = repo.cls(path) or ''
            kind = 'Scalar' if cls == 'MaterialExpressionScalarParameter' else 'Vector' if cls == 'MaterialExpressionVectorParameter' else None
            if kind and not (repo.obj(path) or {}).get('ParameterName'):
                unnamed[kind] = True
        for k in ('Scalar', 'Vector'):
            if unnamed[k] and 'None' in comp[k]: mine[k].add('None')
        diff = {}
        for k in ('Scalar', 'Vector', 'Texture'):
            c = comp[k] - ENGINE_ADDED
            if not c and k != 'Texture':
                continue
            missing = sorted(c - mine[k])           # compiled shader reads, translator does not
            extra = sorted(mine[k] - c)             # translator reads, compiled shader does not
            if missing or extra:
                diff[k] = {'compiled_only': missing, 'translated_only': extra}
        report[name] = {'source': src, 'match': not diff, 'diff': diff,
                        'compiled': {k: sorted(x) for k, x in comp.items()}}
        if diff:
            bad += 1
    json.dump(report, open(os.path.join(data, 'permutation_check.json'), 'w'), indent=1)
    checked = sum(1 for r in report.values() if 'match' in r)
    print('checked %d materials against compiled permutations: %d match, %d differ' % (checked, checked - bad, bad))
    for n, r in report.items():
        if r.get('diff'):
            print(' ', n, json.dumps(r['diff']))


if __name__ == '__main__':
    main()
