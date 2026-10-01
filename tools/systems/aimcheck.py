import json, struct, numpy as np
f = open('F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Characters/Optimus/robot.glb', 'rb').read()
n = struct.unpack_from('<I', f, 12)[0]; j = json.loads(f[20:20 + n])
off = 20 + n; blen = struct.unpack_from('<I', f, off)[0]; binb = f[off + 8: off + 8 + blen]
def acc(i):
    a = j['accessors'][i]; bv = j['bufferViews'][a['bufferView']]
    nc = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
    o = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    return np.frombuffer(binb, '<f4', a['count'] * nc, o).reshape(a['count'], nc)
nodes = j['nodes']; name = {nd['name']: i for i, nd in enumerate(nodes)}
par = {}
for i, nd in enumerate(nodes):
    for c in nd.get('children', []): par[c] = i

def qmul(a, b):
    x1, y1, z1, w1 = a; x2, y2, z2, w2 = b
    return np.array([w1*x2 + x1*w2 + y1*z2 - z1*y2, w1*y2 - x1*z2 + y1*w2 + z1*x2,
                     w1*z2 + x1*y2 - y1*x2 + z1*w2, w1*w2 - x1*x2 - y1*y2 - z1*z2])
def qinv(q): return np.array([-q[0], -q[1], -q[2], q[3]])

def pose(clipname):
    R = {i: np.array(nd.get('rotation', [0, 0, 0, 1]), float) for i, nd in enumerate(nodes)}
    a = [x for x in j['animations'] if x['name'] == clipname][0]
    for ch in a['channels']:
        if ch['target']['path'] != 'rotation': continue
        s = a['samplers'][ch['sampler']]
        R[ch['target']['node']] = acc(s['output'])[0].astype(float)
    G = {}
    def g(i):
        if i in G: return G[i]
        q = R[i] if i not in par else qmul(g(par[i]), R[i])
        G[i] = q; return q
    return R, g

aim = json.load(open('anim/Robot_ANIMTREE.json')  # objtree.py TR_Shared_ANIMTREE_p.Robot_ANIMTREE --flat --out anim/Robot_ANIMTREE.json)
prof = [v for v in aim.values() if v['class'] == 'TnAnimNodeAimOffset'][0]['props']['Profiles'][0]
Rc, gc = pose('Shooting_Aim_F_C'); Ru, gu = pose('Shooting_Aim_F_U'); Rl, gl = pose('Shooting_Aim_L_C')
for comp in prof['AimComponents'][:4]:
    b = name[comp['BoneName']]
    for key, (R2, g2) in (('CU', (Ru, gu)), ('LC', (Rl, gl))):
        q = comp[key]['Quaternion']; ue = np.array([q['X'], q['Y'], q['Z'], q['W']])
        loc = qmul(R2[b], qinv(Rc[b])); loc2 = qmul(qinv(Rc[b]), R2[b])
        mesh = qmul(g2(b), qinv(gc(b)))
        print(comp['BoneName'], key, 'ue', np.round(ue, 3))
        print('   local q2*qc^-1', np.round(loc, 3), ' qc^-1*q2', np.round(loc2, 3), ' mesh', np.round(mesh, 3))
