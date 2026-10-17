import json, sys, difflib
R = 'F:/Transformers Rebuild/Rebuild-Systems/work/matscan/'
m = sys.argv[1]
for k in sys.argv[2:]:
    a = json.load(open(R + 'cur/%s/materials_glsl.json' % m, encoding='utf-8'))[k]['glsl']; b = json.load(open(R + 'vc/%s/materials_glsl.json' % m, encoding='utf-8'))[k]['glsl']
    print('=====', k)
    for l in difflib.unified_diff(a.splitlines(), b.splitlines(), lineterm='', n=0):
        if l.startswith(('---', '+++', '@@')): continue
        print(l[:260])
