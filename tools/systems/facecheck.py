import json, struct, numpy as np
exec(open('aimcheck.py').read().split("aim = json.load")[0])
def gpos(clip):
    R, T = {}, {}
    for i, nd in enumerate(nodes):
        R[i] = np.array(nd.get('rotation', [0, 0, 0, 1]), float); T[i] = np.array(nd.get('translation', [0, 0, 0]), float)
    a = [x for x in j['animations'] if x['name'] == clip][0]
    for ch in a['channels']:
        s = a['samplers'][ch['sampler']]; v = acc(s['output'])[0].astype(float)
        if ch['target']['path'] == 'rotation': R[ch['target']['node']] = v
        elif ch['target']['path'] == 'translation': T[ch['target']['node']] = v
    def rot(q, v):
        u = q[:3]; w = q[3]
        return v + 2 * np.cross(u, np.cross(u, v) + w * v)
    G = {}
    def g(i):
        if i in G: return G[i]
        if i not in par: G[i] = (R[i], T[i]); return G[i]
        pq, pt = g(par[i]); G[i] = (qmul(pq, R[i]), pt + rot(pq, T[i])); return G[i]
    return lambda nm: g(name[nm])[1]
for clip in ['Nav_StrafeJog_F', 'NAV_Idle', 'Shooting_Aim_F_C']:
    P = gpos(clip)
    face = (P('L_Face02_EyelidUp_XF1') + P('R_Face02_EyelidUp_XF1')) / 2 - P('C_Spine04_Head_XB')
    toes = P('C_Spine04_Head_XB') - P('C_Spine00_Hips_XB')
    lr = P('L_Arm01_Clav_XB') - P('R_Arm01_Clav_XB')
    hand = P('R_Arm04_Hand_XB') - P('R_Arm03_Elbow_XB')
    print(clip, 'eyes-head', np.round(face, 3), ' L-R clav', np.round(lr, 3), ' R forearm', np.round(hand / np.linalg.norm(hand), 3))
