"""Build the slim playtest package (M3): AssetTools' slim_files.tsv + xpr_twins.tsv, a freshly built exe tree, and the
optional HD texture pack. Extends Integration's assemble_slim.py (same keep / xpr rules, same codec and round trip).

  <AssetTools>/bin/py/python.exe tools/package/build_slim.py --bin <built bin dir> --head <sha> [--out DIR] [--no-hd] [--plan]

  --bin   the exe tree to ship (Release + PGO of the head; its wfc_rebuild.exe and the files the previous package's bin had:
          the four *-vgmstream-* DLLs, licenses/vgmstream, the tool exes, the .map). Replaces slim_files.tsv's bin rows.
  --head  the 09c head hash: written into README.txt / Play.bat's version lines.
  --version  README's version text (default "09c <head>, Release + PGO").
  --out   default <this worktree>/work/playtest_slim. Never Rebuild/work/playtest_slim (Integration copies the result) and never
          inside another lane's tree; an existing --out is replaced only if it carries this script's marker file.
  --hd    the HD pack source (default AssetTools/out/hd/pack): hd_index.json + ONLY the indexed files are copied to
          <out>/Transformers Rebuild/AssetTools/out/hd (the loader's default location; the source dir also holds non-PASS
          textures that must not ship); every row's byte count is checked. --no-hd: no HD folder.
  --overlay  a FROZEN AssetTools overlay folder (FROZEN.json: files[] = path relative to the folder, bytes, sha256), e.g.
          AssetTools/work/gltf_wfcmat (the skeletal glTFs with extras.wfc_material): each file replaces the source of the package
          row ExtractedAssets/<path> (packed as that row always was); every file is byte- and sha256-checked against FROZEN.json and
          must name exactly one package row, else the build stops. ExtractedAssets itself is never modified.
  --hd-pbr  the M6 modern material maps (HD Enhanced; e.g. AssetTools/out/hd/push_m6/pack): hd_pbr_index.json + ONLY the
          indexed DDS are copied into the same HD root (<out>/Transformers Rebuild/AssetTools/out/hd: the runtime reads
          hd_pbr_index.json beside hd_index.json, its dds paths relative to that root). Every row is checked as the loader does
          (tex / role / dds present, BC4 | BC7, space linear, file present, byte count) and, when FROZEN.json sits in the pack
          folder or its parent, byte + sha256 against it; a row that fails stops the build. Needs --hd.
  --hd-label  a name for the HD pack shipped (e.g. "D"), recorded in build_slim.json; --hd points at that pack's folder
          (any pack in the loader's index layout: hd_index.json + the DDS it names).
  --m5    ship the M5 runtime the exe was built with, taken from --bin (CMake copies it there only when the SDKs were fetched,
          so the DLLs match the exe): amd_fidelityfx_loader / upscaler / framegeneration _dx12.dll and D3D12\\D3D12Core.dll,
          AMD's license.md -> licenses/amd_fidelityfx/, Microsoft's Agility SDK LICENSE.txt (--agility-license, default
          third_party/d3d12_agility/LICENSE.txt) -> licenses/microsoft_d3d12_agility/. Never d3d12SDKLayers.dll (the debug
          layer: not needed at run time, not in the package). Without --m5 none of these files ship even if --bin has them.
  --plan  dry run: counts, bytes, missing sources; writes nothing.
Writes <out>/build_slim.json: head, flags (m5, hd + label) and the exact bin file list, which verify_slim.ps1 holds the
package to (no file missing, none extra). Run outside any PERF_HOLD (IO + CPU heavy). Verify afterwards with
tools/package/verify_slim.ps1 -Strict.
"""
import csv, json, os, shutil, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = 'F:/Transformers Rebuild'
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
TSV = ROOT + '/AssetTools/out/package/slim_files.tsv'
XPR = ROOT + '/AssetTools/out/package/xpr_twins.tsv'
BIN_DEST = 'Transformers Rebuild/Rebuild/build/release/bin'
HD_DEST = 'Transformers Rebuild/AssetTools/out/hd'
MARKER = '.build_slim_output'
MANIFEST = 'build_slim.json'
# M5 runtime (--m5): (path in --bin, path in the package's bin)
M5_FROM_BIN = [('amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_loader_dx12.dll'),
               ('amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll'),
               ('amd_fidelityfx_framegeneration_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll'),
               ('D3D12/D3D12Core.dll', 'D3D12/D3D12Core.dll'),
               ('license.md', 'licenses/amd_fidelityfx/license.md')]
AGILITY_LICENSE_DEST = 'licenses/microsoft_d3d12_agility/LICENSE.txt'
# render-data files shipped beside every map's materials_glsl.json, newer than slim_files.tsv
RENDER_EXTRA = ['materials_pbr.json']
# ... and shipped when the map has one (newer than slim_files.tsv; the UI dirs have none): verify_slim 2026-10-13 found the
# game opening every MP map's fog_volumes.json, missing from the package
RENDER_IF_PRESENT = ['fog_volumes.json', 'render_index_extra.json', 'lens_flares.json']   # + lens flares (verify on d5bb6863: Gorge / Debris read it)
#   # + the Escalation maps' weapon factory visuals (7f218986)
# AssetTools map manifests newer than slim_files.tsv, shipped into every packaged manifests/maps/<map>/ that has them in AssetTools:
# the Escalation nav (WorldSurvival.cpp svManifest: the original NavigationCell mesh + its generator gating; without them a packaged
# Escalation match silently falls back to the versus nav - Experimental 2026-10-23)
MANIFEST_IF_PRESENT = ['bot_nav_escalation.json', 'bot_nav_air_escalation.json', 'escalation_nav_blockers.json']
MANIFEST_MAPS = 'Transformers Rebuild/AssetTools/manifests/maps/'
# single AssetTools files newer than slim_files.tsv the game reads (ROOT-relative; shipped when present): the versus maps' transforming
# actors (verify_slim 2026-10-23 on be5b7cdc: a failing open on Streets)
ASSETTOOLS_IF_PRESENT = ['AssetTools/manifests/maps/mp_transforming_actors.json']
# whole AssetTools folders newer than slim_files.tsv (ROOT-relative; every file shipped): the Escalation horde bodies, read by
# ChassisDef as <ExtractedAssets/VerticalSlice>/../../AssetTools/out/escalation_characters/<id>/ (character.json, robot.glb, textures,
# weapons; 2026-10-23: every ESC_* chassis was UNAVAILABLE in the package)
ASSETTOOLS_TREES = ['AssetTools/out/escalation_characters',   # + the campaign chapters, shipped whole at their AssetTools path: one
                    # (the campaign chapters: CAMPAIGN_RENDER below, every chapter AssetTools has exported)
                    'AssetTools/manifests/maps/transforming_actors',   # the skeletal doors / bridges mp_transforming_actors.json names
                    'AssetTools/out/campaign_anims',   # animation sets character.json files point at (ESC_Soldier_Tank_Decepticon's Shared_ROBO_ANIM)
                    'Rebuild/work/render/_ui/fonts']   # the HUD's bitmap fonts, whole (SubtitleFont 2026-10-24 is newer than slim_files.tsv; MarkerFont is in it)
# The campaign (Integration 2026-10-23): every chapter under AssetTools/out/campaign_render, shipped at its AssetTools path (one campaign
# root, <WFC_CAMPAIGN_ROOT>/<chapter>/<unit>/, Play.bat sets it). A chapter with its own <chapter>/lightmaps/ (AssetTools' dedup: one
# copy per chapter, Rendering reads it) skips the per-unit <unit>/lightmaps/ copies. Plus the campaign's assets outside the render
# data (ExtractedAssets content, out/campaign_characters, out/campaign_anims): AssetTools' list, ROOT-relative paths.
CAMPAIGN_RENDER = 'AssetTools/out/campaign_render'
CAMPAIGN_OUTSIDE_TSV = 'AssetTools/manifests/campaign/campaign_assets_outside_render.tsv'
# ... and per chapter the manifests the runtime reads (a5d088db: WorldCampaign / Matinee / Runtime, MapState, CampaignObjectives):
# next_map/<CH>_<kind>.json and campaign/<ch prefix, lower case>_assets.json. The other next_map files (audits, ~3.3 GB) stay out.
CAMPAIGN_NEXT_MAP = ['kismet', 'matinee', 'pawn_factories', 'objectives', 'movers']
# The DLC map packs (Integration 2026-10-23), in the base package: only AssetTools' DLC-only content list merges into content/
# (the DLC packages otherwise carry low mips of base textures that would degrade the base maps); the DLC maps' data into
# ExtractedAssets/VerticalSlice/Maps/<map>; their render data beside the base maps' (Rebuild/work/render/<map>). A destination the
# package already has is never replaced (the base wins).
DLC_CONTENT_TSV = 'AssetTools/out/dlc_content/dlc_only_content.tsv'   # path = AssetTools/out/dlc_content/content/<rel>
DLC_CONTENT_SRC = 'AssetTools/out/dlc_content/content/'
DLC_MAPS = 'AssetTools/out/dlc_content/VerticalSlice/Maps'   # (moved from work/dlc_maps: the base layout, base-style URIs)
DLC_RENDER = 'AssetTools/out/dlc_render'
# the head's own data folders (this repo): every file ships from the head being packaged, at Rebuild/<dir>/ (slim_files.tsv listed 7
# of data/frontend's 9 files - iconic_presets.json and bot_skill.json were missing - and sources them from the live Rebuild tree)
HEAD_DATA_DIRS = ['data/frontend', 'data/gameplay', 'data/campaign']   # + the chapters' generated audio (SYSTEMS_CAMPAIGN_AUDIO_HOSTED)


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def norm(p):
    return os.path.normpath(p).replace('\\', '/').lower().rstrip('/')


def main():
    plan = '--plan' in sys.argv
    head = arg('--head')
    bin_dir = arg('--bin')
    out = os.path.abspath(arg('--out', os.path.join(REPO, 'work', 'playtest_slim')))
    hd_src = None if '--no-hd' in sys.argv else os.path.normpath(arg('--hd', ROOT + '/AssetTools/out/hd/pack'))
    if not head or not bin_dir:
        sys.exit('usage: build_slim.py --bin <built bin dir> --head <sha> [--out DIR] [--hd DIR | --no-hd] [--plan]')
    # output guard: inside this worktree's work/ (or an explicit dir outside every shared tree), never Integration's package
    n_out = norm(out)
    for bad in ('Rebuild/work', 'Rebuild/build', 'AssetTools', 'ExtractedAssets', 'Game Dump'):
        if n_out == norm(ROOT + '/' + bad) or n_out.startswith(norm(ROOT + '/' + bad) + '/'):
            sys.exit('refusing --out under %s/%s' % (ROOT, bad))
    if not n_out.startswith(norm(REPO) + '/'):
        sys.exit('refusing --out outside this worktree (%s)' % REPO)

    # ---- the file list (bin rows replaced by --bin) ----
    xpr_dest = {}
    with open(XPR, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            xpr_dest[r['source']] = r['dest_xpr']
    rows, tsv_bin, drop, tsv_hd = [], [], 0, set()
    with open(TSV, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            if r['action'] == 'drop':
                drop += 1
            elif r['dest'].startswith(HD_DEST + '/'):
                # AssetTools lists the HD pack in its dev layout (out/hd/pack/...); the package ships the root form at out/hd
                # (Integration, 2026-10-11; verify_slim -Strict), written by the HD step below - the two must select the same files
                tsv_hd.add(norm(os.path.join(ROOT, r['source'])))
            elif r['dest'].startswith(BIN_DEST + '/'):
                tsv_bin.append(r['dest'][len(BIN_DEST) + 1:])
            else:
                rows.append(r)
    # render-data files newer than AssetTools' slim_files.tsv, shipped beside each map's materials_glsl.json:
    # materials_pbr.json (Rendering's M6 Modern mode, tools/render/matc_pbr.py; the HD row's Enhanced level)
    have_dest = {r['dest'] for r in rows}
    problems = []
    # --vbake <dir>: the R1 sun-swap data (Systems' tools/systems/vbake: <dir>/<map>/fit.json + vbake_*.png) shipped as
    # <render data>/<map>/vbake/ beside each MP map's materials_glsl.json (Rendering reads <render data>/<map>/vbake; without
    # it Ray Tracing Realistic silently has no sun swap). Maps without a fit.json there ship none (reported).
    vbake_dir = arg('--vbake')
    vbake_maps = []
    if vbake_dir and not os.path.isdir(vbake_dir):
        problems_vb = ['--vbake: not a folder: %s' % vbake_dir]
    else:
        problems_vb = []
    for r in list(rows):
        if vbake_dir and not problems_vb and r['dest'].endswith('/materials_glsl.json'):
            mapdir = r['dest'][:-len('materials_glsl.json')]
            name = mapdir.rstrip('/').split('/')[-1]
            src_dir = os.path.join(vbake_dir, name)
            if name.startswith('MP_') and os.path.isfile(os.path.join(src_dir, 'fit.json')):
                pngs = sorted(f for f in os.listdir(src_dir) if f.startswith('vbake_') and f.endswith('.png'))
                # (+ fit_lights.json where present: vbake_lights_fit's sky / point / spot calibration for the runtime RT lightmap)
                files = ['fit.json'] + (['fit_lights.json'] if os.path.isfile(os.path.join(src_dir, 'fit_lights.json')) else []) + pngs
                for f in files:
                    src_abs = os.path.join(src_dir, f)
                    rows.append(dict(dest=mapdir + 'vbake/' + f, source=os.path.relpath(src_abs, ROOT).replace('\\', '/'),
                                     bytes=str(os.path.getsize(src_abs)), action='keep', reason='R1 vbake'))
                vbake_maps.append('%s (%d pages%s)' % (name, len(pngs), ', fit_lights' if 'fit_lights.json' in files else ''))
            elif name.startswith('MP_'):
                problems_vb.append('--vbake: no fit.json for %s in %s' % (name, vbake_dir))
        if not r['dest'].endswith('/materials_glsl.json'):
            continue
        for extra in RENDER_IF_PRESENT:
            dst = r['dest'][:-len('materials_glsl.json')] + extra
            src = r['source'][:-len('materials_glsl.json')] + extra
            if dst not in have_dest and os.path.isfile(os.path.join(ROOT, src)):
                rows.append(dict(dest=dst, source=src, bytes=str(os.path.getsize(os.path.join(ROOT, src))), action='keep', reason='render extra'))
                have_dest.add(dst)
        for extra in RENDER_EXTRA:
            dst = r['dest'][:-len('materials_glsl.json')] + extra
            src = r['source'][:-len('materials_glsl.json')] + extra
            if dst in have_dest:
                continue
            if os.path.isfile(os.path.join(ROOT, src)):
                rows.append(dict(dest=dst, source=src, bytes=str(os.path.getsize(os.path.join(ROOT, src))), action='keep', reason='render extra'))
                have_dest.add(dst)
            else:
                problems.append('render data: %s missing beside %s (Rendering: tools/render/matc_pbr.py)' % (src, r['source']))
    map_dirs = sorted({r['dest'][:r['dest'].index('/', len(MANIFEST_MAPS)) + 1] for r in rows
                       if r['dest'].startswith(MANIFEST_MAPS) and '/' in r['dest'][len(MANIFEST_MAPS):]})
    manifest_extra = []
    for d in map_dirs:
        for extra in MANIFEST_IF_PRESENT:
            dst = d + extra
            src = d[len('Transformers Rebuild/'):] + extra
            if dst not in have_dest and os.path.isfile(os.path.join(ROOT, src)):
                rows.append(dict(dest=dst, source=src, bytes=str(os.path.getsize(os.path.join(ROOT, src))), action='keep', reason='manifest extra'))
                have_dest.add(dst)
                manifest_extra.append(dst[len(MANIFEST_MAPS):])
    if manifest_extra:
        print('manifest extras: %d (%s)' % (len(manifest_extra), ', '.join(manifest_extra)))
    at_extra = 0
    for rel in ASSETTOOLS_IF_PRESENT:
        if 'Transformers Rebuild/' + rel not in have_dest and os.path.isfile(os.path.join(ROOT, rel)):
            rows.append(dict(dest='Transformers Rebuild/' + rel, source=rel, bytes=str(os.path.getsize(os.path.join(ROOT, rel))), action='keep', reason='assettools extra'))
            have_dest.add('Transformers Rebuild/' + rel); at_extra += 1
    for tree in ASSETTOOLS_TREES:
        top = os.path.join(ROOT, tree)
        if not os.path.isdir(top):
            problems.append('AssetTools tree missing: %s' % tree); continue
        for dp, _, fs in os.walk(top):
            for f in fs:
                src = os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, '/')
                if 'Transformers Rebuild/' + src not in have_dest:
                    rows.append(dict(dest='Transformers Rebuild/' + src, source=src, bytes=str(os.path.getsize(os.path.join(dp, f))), action='keep', reason='assettools tree'))
                    have_dest.add('Transformers Rebuild/' + src); at_extra += 1
    camp_files, camp_bytes, camp_skipped_lm, camp_chapters = 0, 0, 0, []
    # a source the package already ships under another name (an xpr twin: the compressed row of the same file) is not added again
    # (the overlay check counts the rows naming a source: two would stop the build)
    have_src = {r['source'] for r in rows}
    ctop = os.path.join(ROOT, CAMPAIGN_RENDER)
    for ch in sorted(os.listdir(ctop)) if os.path.isdir(ctop) else []:
        cdir = os.path.join(ctop, ch)
        if not os.path.isdir(cdir):
            continue
        chapter_lm = os.path.isdir(os.path.join(cdir, 'lightmaps'))
        # a folder starting with '_' is shared by the chapters (_transforming_actors, _umodel): copied whole, no chapter manifests
        mans = [] if ch.startswith('_') else \
               ['AssetTools/manifests/next_map/%s_%s.json' % (ch, k) for k in CAMPAIGN_NEXT_MAP] + \
               ['AssetTools/manifests/campaign/%s_assets.json' % ch.split('_')[0].lower()]
        for src in mans:
            dst = 'Transformers Rebuild/' + src
            if dst in have_dest:
                continue
            if not os.path.isfile(os.path.join(ROOT, src)):
                problems.append('campaign manifest missing: %s' % src)
                continue
            sz = os.path.getsize(os.path.join(ROOT, src))
            rows.append(dict(dest=dst, source=src, bytes=str(sz), action='keep', reason='campaign manifest'))
            have_dest.add(dst); camp_files += 1; camp_bytes += sz
        camp_chapters.append(ch + (' (chapter lightmaps)' if chapter_lm else ''))
        for dp, _, fs in os.walk(cdir):
            rel = os.path.relpath(dp, cdir).replace(os.sep, '/')
            if chapter_lm and rel.count('/') == 1 and rel.endswith('/lightmaps'):   # <unit>/lightmaps: the duplicates
                camp_skipped_lm += len(fs)
                continue
            for f in fs:
                src = os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, '/')
                dst = 'Transformers Rebuild/' + src
                if dst in have_dest:
                    continue
                sz = os.path.getsize(os.path.join(dp, f))
                rows.append(dict(dest=dst, source=src, bytes=str(sz), action='keep', reason='campaign render'))
                have_dest.add(dst); camp_files += 1; camp_bytes += sz
    out_files, out_bytes, out_missing = 0, 0, 0
    tsvp = os.path.join(ROOT, CAMPAIGN_OUTSIDE_TSV)
    if os.path.isfile(tsvp):
        for r in csv.DictReader(open(tsvp, encoding='utf-8'), delimiter='\t'):
            src = (r.get('path') or '').replace('\\', '/')
            if not src or src.endswith('.py'):   # (the list names its own export scripts: not game data)
                continue
            dst = 'Transformers Rebuild/' + src
            if dst in have_dest or src in have_src:
                continue
            if not os.path.isfile(os.path.join(ROOT, src)):
                out_missing += 1
                continue
            sz = os.path.getsize(os.path.join(ROOT, src))
            rows.append(dict(dest=dst, source=src, bytes=str(sz), action='keep', reason='campaign outside render'))
            have_dest.add(dst); out_files += 1; out_bytes += sz
    else:
        problems.append('campaign asset list missing: %s' % CAMPAIGN_OUTSIDE_TSV)
    if out_missing:
        problems.append('campaign asset list: %d listed files missing on disk' % out_missing)
    print('campaign: chapters %s; render %d files %.2f GB (%d per-unit lightmap copies skipped); outside render %d files %.2f GB'
          % (', '.join(camp_chapters), camp_files, camp_bytes / 1e9, camp_skipped_lm, out_files, out_bytes / 1e9))
    dlc = {'content': [0, 0], 'maps': [0, 0], 'render': [0, 0]}
    dlc_maps = []
    def dlc_add(kind, src, dst):
        if dst in have_dest or src in have_src:
            return
        sz = os.path.getsize(os.path.join(ROOT, src))
        rows.append(dict(dest=dst, source=src, bytes=str(sz), action='keep', reason='dlc ' + kind))
        have_dest.add(dst); dlc[kind][0] += 1; dlc[kind][1] += sz
    tsvd = os.path.join(ROOT, DLC_CONTENT_TSV)
    if os.path.isfile(tsvd):
        missing_dlc = 0
        for r in csv.DictReader(open(tsvd, encoding='utf-8'), delimiter='\t'):
            src = (r.get('path') or '').replace('\\', '/')
            if not src.startswith(DLC_CONTENT_SRC):
                continue
            if not os.path.isfile(os.path.join(ROOT, src)):
                missing_dlc += 1
                continue
            dlc_add('content', src, 'Transformers Rebuild/ExtractedAssets/content/' + src[len(DLC_CONTENT_SRC):])
        if missing_dlc:
            problems.append('DLC content list: %d listed files missing on disk' % missing_dlc)
    for tree, dest, kind in ((DLC_MAPS, 'Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps', 'maps'),
                             (DLC_RENDER, 'Transformers Rebuild/Rebuild/work/render', 'render')):
        top = os.path.join(ROOT, tree)
        for m in sorted(os.listdir(top)) if os.path.isdir(top) else []:
            if not os.path.isdir(os.path.join(top, m)):
                continue
            if kind == 'maps':
                dlc_maps.append(m)
            for dp, _, fs in os.walk(os.path.join(top, m)):
                for f in fs:
                    src = os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, '/')
                    dlc_add(kind, src, dest + '/' + src[len(tree) + 1:])
    print('dlc: maps %s; content %d files %.3f GB, map data %d files %.2f GB, render %d files %.2f GB'
          % (', '.join(dlc_maps) or 'none', dlc['content'][0], dlc['content'][1] / 1e9, dlc['maps'][0], dlc['maps'][1] / 1e9,
             dlc['render'][0], dlc['render'][1] / 1e9))
    head_data, head_replaced = 0, 0
    for d in HEAD_DATA_DIRS:
        top = os.path.join(REPO, d)
        if not os.path.isdir(top):
            problems.append('head data folder missing: %s (build_slim must run from the head tree)' % top); continue
        for f in sorted(os.listdir(top)):
            src_abs = os.path.join(top, f)
            if not os.path.isfile(src_abs):
                continue
            dst = 'Transformers Rebuild/Rebuild/' + d + '/' + f
            src = os.path.relpath(src_abs, ROOT).replace(os.sep, '/')
            row = dict(dest=dst, source=src, bytes=str(os.path.getsize(src_abs)), action='keep', reason='head data')
            if dst in have_dest:
                for i, r in enumerate(rows):
                    if r['dest'] == dst:
                        rows[i] = row; head_replaced += 1
            else:
                rows.append(row); have_dest.add(dst); head_data += 1
    print('assettools extras: %d files; head data: %d added, %d from the head instead of the live tree' % (at_extra, head_data, head_replaced))
    bin_rows = []
    for rel in tsv_bin:
        src = os.path.join(bin_dir, rel)
        if not os.path.isfile(src):
            problems.append('--bin lacks %s (the previous package shipped it)' % rel)
        else:
            bin_rows.append((src, BIN_DEST + '/' + rel))
    # Third-party licence texts the build copies next to the exe (bin/licenses/<lib>/...: vgmstream, opus, ...) always ship, even when
    # the previous package had none for that library (BSD / LGPL notices must accompany the binary). [Systems]
    shipped = {d for _, d in bin_rows}
    lic_root = os.path.join(bin_dir, 'licenses')
    for dp, _, fns in os.walk(lic_root):
        for fn in fns:
            rel = os.path.relpath(os.path.join(dp, fn), bin_dir).replace(os.sep, '/')
            if BIN_DEST + '/' + rel not in shipped:
                bin_rows.append((os.path.join(dp, fn), BIN_DEST + '/' + rel))
    # ---- the exe's build: RT shaders compiled in (Experimental 2026-10-13: a tree without third_party/dxc and
    # third_party/ffx_denoiser builds with no RT shaders - CMake defines WFC_RT_SHADERS / WFC_RT_DENOISER only when they
    # exist - and the Ray Tracing row then silently does nothing). DXC is build-time only (the DXIL is embedded as headers in
    # <build>/generated/rt; nothing of DXC ships). --no-rt-check skips this (a deliberately RT-less package).
    build_dir = os.path.normpath(os.path.join(bin_dir, '..'))
    if '--no-rt-check' not in sys.argv:
        ninja = os.path.join(build_dir, 'build.ninja')
        ninja_txt = open(ninja, encoding='utf-8', errors='replace').read() if os.path.isfile(ninja) else ''
        for define in ('WFC_RT_SHADERS=1', 'WFC_RT_DENOISER=1'):
            if define not in ninja_txt:
                problems.append('--bin was built without %s (%s): fetch tools/render/fetch_dxc.py + fetch_ffx_denoiser.py into the '
                                'build tree, re-configure and rebuild (or --no-rt-check)' % (define, ninja if ninja_txt else 'no build.ninja beside --bin'))
        for h in ('rt_shadow_ao.h', 'rt_reflect.h', 'ffx_dn_filter0.h'):
            if not os.path.isfile(os.path.join(build_dir, 'generated', 'rt', h)):
                problems.append('--bin: the RT shader header generated/rt/%s is missing in %s' % (h, build_dir))
    overlay_src, overlay_info = {}, None   # package row source (ROOT-relative) -> the overlay file that replaces it
    if arg('--overlay', None):
        import hashlib
        ov = os.path.normpath(arg('--overlay', None))
        fz_path = os.path.join(ov, 'FROZEN.json')
        if not os.path.isfile(fz_path):
            problems.append('--overlay %s has no FROZEN.json (only frozen overlays ship)' % ov)
        else:
            fz = json.load(open(fz_path, encoding='utf-8'))
            by_source = {}
            for r in rows:
                by_source.setdefault(r['source'], []).append(r)
            for f in fz.get('files', []):
                fp = os.path.join(ov, f['path'])
                if not os.path.isfile(fp):
                    problems.append('overlay file missing: %s' % fp); continue
                data = open(fp, 'rb').read()
                if len(data) != int(f['bytes']) or hashlib.sha256(data).hexdigest() != f['sha256'].lower():
                    problems.append('overlay file differs from FROZEN.json (bytes / sha256): %s' % fp); continue
                srow = 'ExtractedAssets/' + f['path']
                hits = [r for r in by_source.get(srow, []) if r['action'] in ('keep', 'xpr')]
                if len(hits) != 1:
                    problems.append('overlay file %s names %d shipped package rows (want 1: %s)' % (f['path'], len(hits), srow)); continue
                overlay_src[srow] = fp
                hits[0]['bytes'] = str(len(data))
            overlay_info = {'dir': ov, 'name': fz.get('name', ''), 'commit': fz.get('commit', ''), 'files': len(overlay_src)}
            print('overlay %s (%s): %d of %d files replace package rows' % (fz.get('name', ''), ov, len(overlay_src), len(fz.get('files', []))))
    m5 = '--m5' in sys.argv
    if m5:
        for src_rel, dst_rel in M5_FROM_BIN:
            src = os.path.join(bin_dir, src_rel)
            if not os.path.isfile(src):
                problems.append('--m5: --bin lacks %s (build with the FFX SDK / Agility runtime fetched: tools/render/fetch_ffx_sdk.py, '
                                'fetch_d3d12_agility.py)' % src_rel)
            else:
                bin_rows.append((src, BIN_DEST + '/' + dst_rel))
        lic = arg('--agility-license', os.path.join(REPO, 'third_party', 'd3d12_agility', 'LICENSE.txt'))
        if not os.path.isfile(lic):
            problems.append('--m5: Microsoft Agility SDK LICENSE.txt not found: %s' % lic)
        else:
            bin_rows.append((lic, BIN_DEST + '/' + AGILITY_LICENSE_DEST))
    problems += problems_vb
    if vbake_dir:
        print('R1 vbake from %s: %d maps: %s' % (vbake_dir, len(vbake_maps), ', '.join(vbake_maps)))
    missing = [r['source'] for r in rows if not os.path.isfile(os.path.join(ROOT, r['source']))]
    problems += ['source missing: %s' % m for m in missing[:20]]
    if len(missing) > 20:
        problems.append('... %d sources missing in all' % len(missing))
    no_twin = [r['source'] for r in rows if r['action'] == 'xpr' and r['source'] not in xpr_dest]
    problems += ['xpr row without a twin destination: %s' % s for s in no_twin[:10]]

    # ---- the HD pack (indexed files only) ----
    hd_rows = []
    if hd_src:
        idx_path = os.path.join(hd_src, 'hd_index.json')
        J = json.load(open(idx_path, encoding='utf-8'))
        idx_rows = next((J[k] for k in ('textures', 'rows', 'entries') if isinstance(J.get(k), list) and J[k]), None)   # the loader's order
        if not idx_rows:
            problems.append('HD index has no rows: %s' % idx_path)
        else:
            for r in idx_rows:
                if not r.get('key') or not r.get('dds'):
                    continue
                src = os.path.join(hd_src, r['dds'])
                if not os.path.isfile(src):
                    problems.append('HD row file missing: %s' % r['dds'])
                elif r.get('bytes') is not None and os.path.getsize(src) != int(r['bytes']):
                    problems.append('HD row byte count %d != %s: %s' % (os.path.getsize(src), r['bytes'], r['dds']))
                else:
                    hd_rows.append((src, HD_DEST + '/' + r['dds'].replace('\\', '/')))
            hd_rows.append((idx_path, HD_DEST + '/hd_index.json'))
            for extra in ('CREDITS.txt',):
                if os.path.isfile(os.path.join(hd_src, extra)):
                    hd_rows.append((os.path.join(hd_src, extra), HD_DEST + '/' + extra))
        if tsv_hd:
            mine = {norm(s) for s, _ in hd_rows}
            if mine != tsv_hd and arg('--hd-label'):        # (an explicitly chosen pack, e.g. the frozen D batches: the tsv lists the default)
                print('HD: pack %s (%d files) chosen over the slim_files.tsv HD rows (%d files)' % (arg('--hd-label'), len(mine), len(tsv_hd)))
            elif mine != tsv_hd:
                problems.append('HD selection differs from slim_files.tsv: %d only here, %d only in the tsv (e.g. %s)' % (
                    len(mine - tsv_hd), len(tsv_hd - mine), sorted(mine ^ tsv_hd)[:3]))

    # ---- the M6 modern material maps (--hd-pbr: HD Enhanced's roughness maps), into the same HD root ----
    hd_pbr_src = arg('--hd-pbr')
    hd_pbr_info = None
    if hd_pbr_src:
        hd_pbr_src = os.path.normpath(hd_pbr_src)
        pidx = os.path.join(hd_pbr_src, 'hd_pbr_index.json')
        if not hd_src:
            problems.append('--hd-pbr needs the HD pack (--hd): the runtime reads hd_pbr_index.json beside hd_index.json')
        elif not os.path.isfile(pidx):
            problems.append('--hd-pbr: no hd_pbr_index.json in %s' % hd_pbr_src)
        else:
            PJ = json.load(open(pidx, encoding='utf-8'))
            prow = next((PJ[k] for k in ('rows', 'textures', 'entries') if isinstance(PJ.get(k), list) and PJ[k]), None)   # the loader's order
            frozen, fdir = None, None
            for d in (hd_pbr_src, os.path.dirname(hd_pbr_src)):
                if os.path.isfile(os.path.join(d, 'FROZEN.json')):
                    frozen, fdir = json.load(open(os.path.join(d, 'FROZEN.json'), encoding='utf-8')), d
                    break
            fz = {norm(os.path.join(fdir, f['path'])): f for f in (frozen or {}).get('files', [])}
            taken = {norm(d) for _, d in hd_rows}
            bad, n = 0, 0
            for r in prow or []:
                dds = (r.get('dds') or '').replace('\\', '/')
                fmt = (r.get('format') or '').replace('PF_', '')
                src = os.path.join(hd_pbr_src, dds)
                why = None
                if not r.get('tex') or not r.get('role') or not dds: why = 'tex / role / dds missing'
                elif fmt not in ('BC4', 'BC7'): why = 'format %s (the loader takes BC4 / BC7)' % r.get('format')
                elif r.get('space') not in (None, '', 'linear'): why = 'space %s (the loader takes linear)' % r.get('space')
                elif not os.path.isfile(src): why = 'file missing'
                elif r.get('bytes') is not None and os.path.getsize(src) != int(r['bytes']): why = 'byte count %d != %s' % (os.path.getsize(src), r['bytes'])
                elif frozen is not None:
                    f = fz.get(norm(src))
                    if not f: why = 'not in FROZEN.json'
                    elif f.get('bytes') != os.path.getsize(src): why = 'FROZEN.json byte count %s != %d' % (f.get('bytes'), os.path.getsize(src))
                    else:
                        import hashlib
                        h = hashlib.sha256(open(src, 'rb').read()).hexdigest()
                        if h != f.get('sha256'): why = 'sha256 differs from FROZEN.json'
                dest = HD_DEST + '/' + dds
                if not why and norm(dest) in taken: why = 'destination already shipped by the HD pack'
                if why:
                    bad += 1
                    if bad <= 10: problems.append('HD M6 row %s: %s' % (dds or r.get('key'), why))
                    continue
                hd_rows.append((src, dest)); taken.add(norm(dest)); n += 1
            if not prow: problems.append('--hd-pbr: hd_pbr_index.json has no rows')
            if bad > 10: problems.append('... %d HD M6 rows failed in all' % bad)
            hd_rows.append((pidx, HD_DEST + '/hd_pbr_index.json'))
            hd_pbr_info = {'source': hd_pbr_src, 'name': (frozen or {}).get('name', ''), 'rows': len(prow or []), 'files': n,
                           'frozen': bool(frozen)}
            print('HD M6 modern material maps: %d of %d rows from %s%s' % (n, len(prow or []), hd_pbr_src,
                  ' (FROZEN.json %s: bytes + sha256 checked)' % frozen.get('name', '') if frozen else ' (no FROZEN.json: bytes checked)'))

    keep_b = sum(int(r['bytes']) for r in rows if r['action'] == 'keep')
    xpr_b = sum(int(r['bytes']) for r in rows if r['action'] == 'xpr')
    bin_b = sum(os.path.getsize(s) for s, _ in bin_rows)
    hd_b = sum(os.path.getsize(s) for s, _ in hd_rows)
    print('slim_files.tsv: %d keep (%.2f GB), %d xpr (%.2f GB before compression), %d drop; bin from --bin: %d files (%.3f GB); '
          'HD: %s; M5 runtime: %s' % (sum(r['action'] == 'keep' for r in rows), keep_b / 2**30, sum(r['action'] == 'xpr' for r in rows),
                      xpr_b / 2**30, drop, len(bin_rows), bin_b / 2**30,
                      '%d files (%.2f GB)' % (len(hd_rows), hd_b / 2**30) if hd_src else 'none', 'yes' if m5 else 'no'))
    bin_files = sorted(d[len(BIN_DEST) + 1:] for _, d in bin_rows)
    manifest = {'generator': 'tools/package/build_slim.py', 'head': head, 'version': arg('--version', '09c %s, Release + PGO' % head),
                'm5_runtime': m5, 'vbake': vbake_dir or '', 'hd': bool(hd_src), 'hd_label': arg('--hd-label', ''), 'hd_source': hd_src or '',
                'bin_dest': BIN_DEST, 'bin_files': bin_files, 'overlay': overlay_info, 'hd_pbr': hd_pbr_info or ''}
    if plan:
        print('bin files (%d):' % len(bin_files))
        for b in bin_files: print('  ' + b)
    for p in problems:
        print('PROBLEM:', p)
    if plan or problems:
        sys.exit(1 if problems else 0)

    # ---- write ----
    if os.path.exists(out):
        if not os.path.isfile(os.path.join(out, MARKER)):
            sys.exit('%s exists and was not made by build_slim.py: not replacing it' % out)
        shutil.rmtree(out)
    os.makedirs(out)
    open(os.path.join(out, MARKER), 'w').write('build_slim.py %s %s\n' % (head, time.strftime('%Y-%m-%d %H:%M')))
    sys.path.insert(0, ROOT + '/Rebuild/tools/render')
    import package_lossless as pl
    cab = pl._api()

    def do(r):
        src = overlay_src.get(r['source']) or os.path.join(ROOT, r['source'])
        if r['action'] == 'xpr':
            dest = os.path.join(out, xpr_dest[r['source']])
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            data = open(src, 'rb').read()
            packed = pl.compress(cab, data)
            if pl.decompress(cab, packed) != data:
                return ('ROUNDTRIP', src)
            open(dest, 'wb').write(packed)
            return ('xpr', len(packed))
        return copy((src, r['dest']))

    def copy(sd):
        src, rel = sd
        dest = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(src, dest)
        if os.path.getsize(dest) != os.path.getsize(src):
            return ('SIZE', src)
        return ('keep', os.path.getsize(dest))

    t0 = time.time()
    tot, fails = {'keep': 0, 'xpr': 0, 'bytes': 0}, []
    with ThreadPoolExecutor(6) as ex:
        for res in list(ex.map(do, rows)) + list(ex.map(copy, bin_rows + hd_rows)):
            if res[0] in ('keep', 'xpr'):
                tot[res[0]] += 1; tot['bytes'] += res[1]
            else:
                fails.append(res)
    # README / Play.bat from the template, version lines from --head
    for n in ('Play.bat', 'README.txt'):
        t = open(os.path.join(HERE, 'slim_template', n), encoding='utf-8', newline='').read()
        version = arg('--version', '09c %s, Release + PGO' % head)
        t = t.replace('09c 37eec59, Release + PGO', version).replace('09c b898c75 slim', '09c %s slim' % head)
        if n == 'README.txt' and hd_src:                 # (user 2026-10-12: everything new is opt-in; the original look is the default)
            t = t.rstrip('\r\n') + ('\n\nGraphics: the game starts with its original look. The optional HD pack is included but OFF by default:\n'
                                    'Settings > Graphics > HD Textures: Faithful (the new textures, the original materials) or Enhanced (the new\n'
                                    'textures plus modern, physically based materials); applies on the next map load. Ray-traced shadows /\n'
                                    'lighting, where the build has them, are also OFF by default and only run when switched on in the same menu.\n')
        if n == 'README.txt' and m5:
            t = t.rstrip('\r\n') + ('\n\nOptional graphics runtime (Graphics menu: FSR 3.1 upscaling, frame generation; off by default): AMD FidelityFX\n'
                                    'DLLs and Microsoft D3D12 Agility SDK runtime (D3D12\\D3D12Core.dll) beside the game; their licences are in\n'
                                    'Transformers Rebuild\\Rebuild\\build\\release\\bin\\licenses\\amd_fidelityfx and \\microsoft_d3d12_agility.\n')
        t = t.replace('\r\n', '\n').replace('\n', '\r\n')   # CRLF whatever the checkout's line endings: cmd mis-parses LF-only labels
        if head not in t:
            fails.append(('VERSION', n))
        open(os.path.join(out, n), 'w', encoding='utf-8', newline='').write(t)
    json.dump(manifest, open(os.path.join(out, MANIFEST), 'w', encoding='utf-8'), indent=1)
    print('wrote %s: keep %d, xpr %d, %.2f GB, %d s, failures %d' % (out, tot['keep'], tot['xpr'], tot['bytes'] / 2**30,
                                                                   time.time() - t0, len(fails)))
    for f_ in fails[:20]:
        print('FAIL:', f_)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
