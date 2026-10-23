"""Read-only: per campaign chapter, list PostProcessVolumes (priority, enabled, key settings, LUT), WorldInfo default PP overrides, height fogs and post-process Kismet / matinee op counts.
usage: campaign_pp_extract.py <CHAPTER> [...]   e.g. A2_KON (run from RE-Workspace)"""
import sqlite3,json,collections,sys
db=sqlite3.connect(r"F:\Transformers Rebuild\AssetTools\manifests\authored.db")
pc=json.load(open('work/q/pkg_chapter.json'))
def ch(o):
    v=pc.get(o.split('.')[0]); return v if isinstance(v,str) else (v[0] if v else None)
KEYS=['bEnableBloom','bEnableDOF','bEnableMotionBlur','bEnableSceneEffect','Bloom_Scale','Bloom_Threshold','DOF_FocusType','DOF_FocusDistance','DOF_FocusInnerRadius','DOF_FocusFarFalloff','DOF_FocusNearFalloff','DOF_MaxFarBlurAmount','DOF_MaxNearBlurAmount','DOF_BlurKernelSize','MotionBlur_Amount','MotionBlur_MaxVelocity','Scene_Desaturation','Scene_HighLights','Scene_MidTones','Scene_Shadows','ColorCorrectionTexture','Scene_InterpolationDuration','Bloom_InterpolationDuration']
def short(s):
    out={}
    for k in KEYS:
        if k in s:
            v=s[k]
            if isinstance(v,dict): v=tuple(round(v[c],3) for c in ('X','Y','Z') if c in v)
            elif isinstance(v,float): v=round(v,3)
            elif isinstance(v,str): v=v.split('.')[-1]
            out[k.replace('Scene_','S_').replace('Bloom_','B_').replace('MotionBlur_','MB_').replace('DOF_','D_')]=v
    return out
for CH in sys.argv[1:]:
    print('######',CH)
    for cl in ('PostProcessVolume','DynamicPostProcessVolume','PostProcessVolumeHeightFog','ColorScaleVolume'):
        for o,p in db.execute("select opath,props from objects where class=?",(cl,)):
            if ch(o)!=CH: continue
            d=json.loads(p)
            print(cl[:6], o.split('.')[0], o.split('.')[-1], 'prio',d.get('Priority',0),'enabled',d.get('bEnabled',True), short(d.get('Settings',{})))
    for o,p in db.execute("select opath,props from objects where class in ('TnWorldInfo','WorldInfo') and props like '%DefaultPostProcessSettings%'"):
        if ch(o)==CH: print('WorldInfo default', o.split('.')[0], short(json.loads(p)['DefaultPostProcessSettings']), {k:v for k,v in json.loads(p).items() if 'PostProcess' in k and k!='DefaultPostProcessSettings'})
    for o,p in db.execute("select opath,props from objects where class in ('HeightFogComponent','PostProcessVolumeHeightFogComponent')"):
        if ch(o)==CH:
            d=json.loads(p); print('Fog', o.split('.')[0], o.split('.')[-2], {k:(round(v,4) if isinstance(v,float) else v) for k,v in d.items() if k in('Density','LightColor','LightBrightness','ExtinctionDistance','StartDistance','bEnabled','Height','FogHeight')})
    c=collections.Counter()
    for o,cl,p in db.execute("select opath,class,props from objects where class in ('SeqAct_SetPostProcessChain','SeqAct_SetDOFParams','SeqAct_SetMotionBlurParams','InterpTrackFade','InterpTrackColorScale','SeqAct_CameraFade','HmSeqAct_CameraFade','TnSeqAct_CameraFade','SeqAct_TnFade','InterpTrackLinearColorProp')"):
        if ch(o)==CH:
            c[cl]+=1
            if cl in ('SeqAct_SetPostProcessChain','SeqAct_SetDOFParams','SeqAct_SetMotionBlurParams'): print('KISMET',cl,o.split('PersistentLevel.')[-1],p[:300])
    print('counts',dict(c))
