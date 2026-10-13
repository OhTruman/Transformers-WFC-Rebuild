"""Offline GLSL check of the material fragment shaders (no GL context): rebuilds the text buildProgram composes
(render/gl/WfcPipeline.cpp: kFSHead + the normal-node helper + runtime-parameter uniforms + per-slot sampler / fetch
helpers + [M6: kFSPbrHelpers] + the material function + kFSPrologue + the main) from the C++ raw strings and a map's
material bodies, and compiles each with glslang (.toolchain/glslang, Khronos' reference front end).

  python tools/render/glsl_validate.py <materials json> [--pbr] [--limit N]
      materials json: materials_glsl.json (original bodies, kFSMainLM) or, with --pbr, materials_pbr.json (matc_pbr.py
      output, kFSMainLMModern + kFSPbrHelpers + the roughness-map slot)
"""
import json, os, re, subprocess, sys, tempfile, ast

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CPP = os.path.join(ROOT, 'src', 'render', 'gl', 'WfcPipeline.cpp')
GLSLANG = os.path.join(ROOT, '.toolchain', 'glslang', 'bin', 'glslang.exe')


def raw(src, name):
    m = re.search(r'const char\* %s = R"\((.*?)\)";' % name, src, re.S)
    if not m: raise SystemExit('raw string %s not found' % name)
    return m.group(1)


def compose(src, body, ncube_slots, n2d_slots, pbr, rt_params):
    fs = raw(src, 'kFSHead')
    fs += 'vec3 wfcNormalNode(MatIn m) { return tangentToWorldUE(m, m.normal); }\n'
    for n in rt_params: fs += 'uniform vec4 uRT_%s; uniform int uRTSet_%s;\n' % (n, n)
    code = body
    for k in sorted(set(ncube_slots) | set(n2d_slots)):
        n = str(k)
        if k in ncube_slots:
            fs += 'uniform samplerCube uTex%s;\nvec4 wfcSC_%s(vec3 d) { return texture(uTex%s, d); }\n' % (n, n, n)
            fs += 'vec4 wfcSCB_%s(vec3 d, float b) { return texture(uTex%s, d, b); }\n' % (n, n)
            code = code.replace('wfcSampleCube(%s, ' % n, 'wfcSC_%s(' % n).replace('wfcSampleCubeBias(%s, ' % n, 'wfcSCB_%s(' % n)
            if 'wfcSampleCubeMean(%s)' % n in code:     # (as buildProgram: the cube's mean colour, six 1 x 1 faces)
                fs += 'vec4 wfcSCM_%s() { return (%s) / 6.0; }\n' % (n, ' + '.join('textureLod(uTex%s, vec3(%s), 16.0)' % (n, d) for d in
                      ('1.0, 0.0, 0.0', '-1.0, 0.0, 0.0', '0.0, 1.0, 0.0', '0.0, -1.0, 0.0', '0.0, 0.0, 1.0', '0.0, 0.0, -1.0')))
                code = code.replace('wfcSampleCubeMean(%s)' % n, 'wfcSCM_%s()' % n)
        else:
            fs += 'uniform sampler2D uTex%s; uniform vec4 uUnpackMin%s; uniform vec4 uUnpackScale%s;\n' % (n, n, n)
            fs += 'vec4 wfcS2D_%s(vec2 uv) { return texture(uTex%s, uv) * uUnpackScale%s + uUnpackMin%s; }\n' % (n, n, n, n)
            code = code.replace('wfcSample2D(%s, ' % n, 'wfcS2D_%s(' % n)
    if pbr: fs += raw(src, 'kFSPbrHelpers')
    fs += 'void wfcMaterial(in MatIn m, out MatOut o) {\n' + code + '\n}\n'
    fs += raw(src, 'kFSPrologue')
    fs += raw(src, 'kFSMainLMModern').replace('@SPECOCC@', '4.0') if pbr else raw(src, 'kFSMainLM')
    return fs


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    pbr = '--pbr' in sys.argv
    limit = int(sys.argv[sys.argv.index('--limit') + 1]) if '--limit' in sys.argv else 0
    src = open(CPP, encoding='utf-8').read()
    data = json.load(open(a[0], encoding='utf-8'))
    glsl_json = data
    if pbr:   # slot kinds from the original materials_glsl.json next to the pbr file's source map
        orig = json.load(open(a[1], encoding='utf-8')) if len(a) > 1 else {}
    ok = bad = 0; errs = []
    for k, (name, m) in enumerate(sorted(data.items())):
        if limit and k >= limit: break
        body = m.get('glsl') or ''
        if not body: continue
        om = (orig.get(name) if pbr else m) or {}
        info = om.get('info') or {}
        if isinstance(info, str):
            try: info = ast.literal_eval(info)
            except Exception: info = {}
        texs = info.get('textures') or []
        cube = {t['slot'] for t in texs if t.get('kind') == 'cube' and 'slot' in t}
        d2 = {t['slot'] for t in texs if t.get('kind', '2d') != 'cube' and 'slot' in t}
        if pbr and m.get('rough_slot') is not None:
            d2.add(m['rough_slot'])
            body = body.replace('uPbrRoughOn != 0', 'true')
        used = {int(x) for x in re.findall(r'wfcSample(?:2D|Cube|CubeBias)\((\d+),', body)}
        d2 |= {s for s in used if s not in cube}
        rtp = sorted(set(re.findall(r'uRTSet_(\w+)', body)))
        fs = compose(src, body, cube, d2, pbr, rtp)
        with tempfile.NamedTemporaryFile('w', suffix='.frag', delete=False, encoding='utf-8') as f:
            f.write(fs); path = f.name
        r = subprocess.run([GLSLANG, path], capture_output=True, text=True)
        os.unlink(path)
        if r.returncode == 0: ok += 1
        else:
            bad += 1
            if len(errs) < 6: errs.append((name, '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'ERROR' in l)[:600]))
    print('%d compile, %d fail' % (ok, bad))
    for n, e in errs: print('== %s\n%s' % (n, e))


if __name__ == '__main__':
    main()
