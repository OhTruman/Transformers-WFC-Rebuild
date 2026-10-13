"""HD texture pack verification (M3 HD finish): per map, HD on vs off - texture memory (WFC_MEMSTATS) against a budget,
the compressed sRGB path (HD textures uploaded as sRGB S3TC blocks + the shader's PWL correction, not decoded), the pack
state the runtime reports, and a lockstep shot set (HD on / off / |diff| sheets for eye spot-checks).

    python hd_verify.py plan   <exe> <out dir> [--hd-root DIR] [--maps 501,504,...] [--profile ini]
        writes <out>/cmd/<map>_<on|off>.cmd and <out>/steps.txt (one run per line: dir|cmd|args|log, the guarded
        runner's format); run the steps with the machine's guarded runner (PERF_HOLD aware), then:
    python hd_verify.py report <out dir> [--budget-mb 8192]
        reads <out>/runs/*/wfc.log + shots, writes <out>/report.md and <out>/sheet_<map>.png

TDM maps run the frontend flow -> 64p TDM in lockstep (seeded; shots at match steps 300 .. 1500); the Escalation-only
maps (BrokenHope, Remnant: no TDM; Escalation itself does not play in the rebuild yet) render through the scene viewer
(WFC_FRONTENDSCENE) from a fixed camera, with WFC_HDFORCE=1 for the HD-on run (the viewer has no frontend HD setting).
Render data: WFC_RENDER_DATA (default the integration tree's work/render). HD pack: WFC_HD_ROOT (default: the
runtime's own default, AssetTools out/hd[/pack]).
"""
import argparse, os, re, sys

TDM_MAPS = {501: 'MP_IAC_Seed', 502: 'MP_IAC_Berth', 503: 'MP_UND_Complex', 504: 'MP_IAC_Rust', 507: 'MP_ORB_Debris',
            508: 'MP_IAC_Streets', 509: 'MP_KON_Molten', 510: 'MP_UND_Gorge'}
SV_MAPS = {505: 'MP_ESC_BrokenHope', 506: 'MP_ESC_Remnant'}
B = chr(92)

FLOW = ('wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;'
        'wait:t=1;call:Online.EditGameMode,%s;call:Online.PlayPrivateGame,%s;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;'
        'call:Online.SetSelectedMapID,%d;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=30;quit')

PROFILE = """[ProfileData]

[PCSettings]
Width=2560
Height=1440
Fullscreen=0
TextureQuality=2
VSync=0
FrameLimit=0
Upscaling=0
HDTextures=0
Anisotropy=16
FrameGeneration=0
"""


def plan(a):
    out = os.path.abspath(a.out)
    os.makedirs(os.path.join(out, 'cmd'), exist_ok=True)
    prof = os.path.join(out, 'profile.ini')
    open(prof, 'w', newline='\r\n').write(open(a.profile).read() if a.profile else PROFILE)
    maps = [int(x) for x in a.maps.split(',')] if a.maps else sorted(TDM_MAPS) + sorted(SV_MAPS)
    steps = []
    for mid in maps:
        name = TDM_MAPS.get(mid) or SV_MAPS.get(mid)
        if not name: print('unknown map id', mid); continue
        for hd in ('on', 'off'):
            run = '%s_%s' % (name, hd); rd = os.path.join(out, 'runs', run)
            L = ['@echo off', 'if not exist "%s%sshots" mkdir "%s%sshots"' % (rd, B, rd, B),
                 'copy /y "%s" "%s%swfc_profile.ini" >nul' % (prof, rd, B), 'cd /d "%s"' % rd,
                 'set WFC_AUTODETECT=0', 'set WFC_AUTODETECT_WINDOWED=1', 'set WFC_LOGEVERY=0', 'set WFC_LOGTIME=1', 'set WFC_MEMSTATS=1',
                 'set WFC_NOMOUSE=1', 'set WFC_SKIPINTRO=1', 'set WFC_LOCKSTEP=1', 'set WFC_LOCKSTEP_EXACT=1',
                 'set WFC_HDTEXTURES=%d' % (1 if hd == 'on' else 0)]
            if hd == 'on' and mid not in TDM_MAPS: L.append('set WFC_HDFORCE=1')   # (the scene viewer has no frontend HD setting)
            if a.hd_root: L.append('set "WFC_HD_ROOT=%s"' % a.hd_root)
            if a.render_data: L.append('set "WFC_RENDER_DATA=%s"' % a.render_data)
            if mid in TDM_MAPS:
                mode = 'TDM'
                L += ['set WFC_BOOT=frontend', 'set WFC_FLOWSEED=1', 'set WFC_SEED=7', 'set WFC_SIMHASH=0', 'set WFC_PLAYERBOT=1',
                      'set WFC_SHOTMATCH=%s%sshots,300,1500,300' % (rd, B),
                      'set WFC_LOBBY_OPTIONS=ExtendedPlayers=1;BotsAutobot=31;BotsDecepticon=32;PointsToWin=9999;TimeLimit=300',
                      'set WFC_FRONTEND_SCRIPT=' + FLOW % (mode, mode, mid)]
            else:   # scene viewer, a fixed camera over the map's centre (BrokenHope / Remnant: Escalation only)
                L += ['set WFC_FRONTENDSCENE=%s' % name, 'set "WFC_SCENECAM=0,0,2500,-35,45,0,90"', 'set WFC_SMOKE_FRAMES=180',
                      'set WFC_SHOT=%s%sshots%sm00180.bmp' % (rd, B, B)]
            L.append('"%s"' % os.path.abspath(a.exe))
            cmd = os.path.join(out, 'cmd', run + '.cmd')
            open(cmd, 'w', newline='').write('\r\n'.join(L) + '\r\n')
            steps.append('|'.join([out, cmd, '', os.path.join(out, run + '.out')]))
    open(os.path.join(out, 'steps.txt'), 'w', newline='\r\n').write('\n'.join(steps) + '\n')
    print('%d runs planned: %s' % (len(steps), os.path.join(out, 'steps.txt')))


MEM = re.compile(r'MEMSTATS ([^:]+): lightmaps \d+ = ([\d.]+) MB, other textures \d+ = ([\d.]+) MB .*?SRGB ([\d.]+) MB; (\d+) from the '
                 r'original blocks, (\d+) HD\), static meshes \d+ = ([\d.]+) MB')


def parse(log):
    r = {'mem': None, 'pack': '', 'warn': [], 'map': ''}
    if not os.path.exists(log): return r
    for l in open(log, encoding='utf-8', errors='replace'):
        if 'HD texture pack' in l: r['pack'] = l.split('] ', 1)[-1].strip()
        if '[warn ]' in l and ('HD' in l or 'texture' in l.lower()): r['warn'].append(l.split('] ', 1)[-1].strip())
        m = MEM.search(l)
        if m: r['mem'] = dict(when=m.group(1), lm=float(m.group(2)), tex=float(m.group(3)), srgb=float(m.group(4)),
                             dds=int(m.group(5)), hd=int(m.group(6)), mesh=float(m.group(7)))   # (the last = the match map)
    return r


def report(a):
    import numpy as np
    from PIL import Image, ImageDraw
    out = os.path.abspath(a.out); runs = os.path.join(out, 'runs')
    names = sorted({d.rsplit('_', 1)[0] for d in os.listdir(runs)}) if os.path.isdir(runs) else []
    rows = ['# HD texture verification', '', 'Budget: %d MB (lightmaps + textures + static meshes, GL-side bytes).' % a.budget_mb, '',
            '| map | HD | pack | textures MB | of it sRGB MB | HD textures | original-block textures | lightmaps MB | meshes MB | total MB | budget | sRGB path | shots on vs off (mean / p99 / max) |',
            '|---|---|---|---|---|---|---|---|---|---|---|---|---|']
    for n in names:
        res = {hd: parse(os.path.join(runs, '%s_%s' % (n, hd), 'wfc.log')) for hd in ('on', 'off')}
        shots_on = os.path.join(runs, n + '_on', 'shots'); shots_off = os.path.join(runs, n + '_off', 'shots')
        diffs = []; sheet = []
        if os.path.isdir(shots_on) and os.path.isdir(shots_off):
            for f in sorted(os.listdir(shots_on)):
                if not os.path.exists(os.path.join(shots_off, f)): continue
                A = Image.open(os.path.join(shots_off, f)).convert('RGB'); Bm = Image.open(os.path.join(shots_on, f)).convert('RGB')
                d = np.abs(np.asarray(Bm, dtype=np.int16) - np.asarray(A, dtype=np.int16)).max(-1)
                diffs.append((d.mean(), np.percentile(d, 99), d.max()))
                W = 853; h = int(A.height * W / A.width); r = Image.new('RGB', (3 * W, h + 20), (24, 24, 24))
                dm = Image.fromarray(np.clip(np.abs(np.asarray(Bm, dtype=np.int16) - np.asarray(A, dtype=np.int16)) * 4, 0, 255).astype(np.uint8))
                for k, im in enumerate((A, Bm, dm)): r.paste(im.resize((W, h), Image.LANCZOS), (k * W, 20))
                dr = ImageDraw.Draw(r)
                for k, t in enumerate(('HD off', 'HD on', '|on - off| x4')): dr.text((k * W + 6, 4), '%s %s  %s' % (n, f, t), fill=(230, 230, 230))
                sheet.append(r)
        if sheet:
            S = Image.new('RGB', (sheet[0].width, sum(x.height for x in sheet))); y = 0
            for x in sheet: S.paste(x, (0, y)); y += x.height
            S.save(os.path.join(out, 'sheet_%s.png' % n))
        for hd in ('on', 'off'):
            m = res[hd]['mem']
            if not m:
                rows.append('| %s | %s | %s | (no MEMSTATS) |||||||||' % (n, hd, res[hd]['pack'][:60])); continue
            total = m['lm'] + m['tex'] + m['mesh']
            srgb_ok = '-' if hd == 'off' else ('yes' if m['hd'] > 0 and m['srgb'] > 0 else 'NO')
            sh = ' / '.join('%.2f' % s for s in (np.mean([d[0] for d in diffs]), np.mean([d[1] for d in diffs]), max(d[2] for d in diffs))) if (diffs and hd == 'on') else ''
            rows.append('| %s | %s | %s | %.0f | %.0f | %d | %d | %.0f | %.0f | %.0f | %s | %s | %s |' % (
                n, hd, (res[hd]['pack'] or '-')[:70], m['tex'], m['srgb'], m['hd'], m['dds'], m['lm'], m['mesh'], total,
                'ok' if total <= a.budget_mb else 'OVER', srgb_ok, sh))
        for hd in ('on', 'off'):
            for w in res[hd]['warn'][:3]: rows.append('|  | %s warn | %s |||||||||||' % (hd, w[:150]))
    # the pack state each run reported, in full (texture count + root: which pack a run actually used)
    rows += ['', '## Pack state per run', '']
    for n in names:
        for hd in ('on', 'off'):
            rows.append('- %s %s: %s' % (n, hd, parse(os.path.join(runs, '%s_%s' % (n, hd), 'wfc.log'))['pack'] or '-'))
    # information: the HD pack's size on disk (its index rows; the budget above is in-game texture memory per map)
    import json
    roots = set()
    for n in names:
        m = re.search(r'\((.+)\)\s*$', parse(os.path.join(runs, n + '_on', 'wfc.log'))['pack'])
        if m: roots.add(m.group(1))
    for root in sorted(roots):
        try:
            J = json.load(open(os.path.join(root, 'hd_index.json'), encoding='utf-8'))
            R = next((J[k] for k in ('rows', 'textures', 'entries') if isinstance(J.get(k), list) and J[k]), [])
            tot = sum(os.path.getsize(os.path.join(root, r['dds'])) for r in R if os.path.exists(os.path.join(root, r['dds'])))
            if isinstance(J.get('total_bytes'), int) and J['total_bytes'] != tot:
                rows += ['', 'HD pack index total_bytes %d differs from the files found (%d)' % (J['total_bytes'], tot)]
            rows += ['', 'HD pack on disk (information): %s: %d textures, %.0f MB' % (root, len(R), tot / 1048576.0)]
        except (OSError, ValueError, KeyError) as ex:
            rows += ['', 'HD pack on disk: %s unreadable (%s)' % (root, ex)]
    open(os.path.join(out, 'report.md'), 'w', encoding='utf-8').write('\n'.join(rows) + '\n')
    print('\n'.join(rows))


def main():
    p = argparse.ArgumentParser(); sp = p.add_subparsers(dest='cmd', required=True)
    q = sp.add_parser('plan'); q.add_argument('exe'); q.add_argument('out'); q.add_argument('--hd-root'); q.add_argument('--maps')
    q.add_argument('--profile'); q.add_argument('--render-data')
    r = sp.add_parser('report'); r.add_argument('out'); r.add_argument('--budget-mb', type=int, default=8192)
    a = p.parse_args()
    plan(a) if a.cmd == 'plan' else report(a)


if __name__ == '__main__':
    main()
