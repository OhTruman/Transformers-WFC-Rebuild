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
    bin_rows, problems = [], []
    for rel in tsv_bin:
        src = os.path.join(bin_dir, rel)
        if not os.path.isfile(src):
            problems.append('--bin lacks %s (the previous package shipped it)' % rel)
        else:
            bin_rows.append((src, BIN_DEST + '/' + rel))
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
            if mine != tsv_hd:
                problems.append('HD selection differs from slim_files.tsv: %d only here, %d only in the tsv (e.g. %s)' % (
                    len(mine - tsv_hd), len(tsv_hd - mine), sorted(mine ^ tsv_hd)[:3]))

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
                'm5_runtime': m5, 'hd': bool(hd_src), 'hd_label': arg('--hd-label', ''), 'hd_source': hd_src or '',
                'bin_dest': BIN_DEST, 'bin_files': bin_files}
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
        src = os.path.join(ROOT, r['source'])
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
        if n == 'README.txt' and hd_src:                 # (user 2026-10-12: the new textures are the default)
            t = t.rstrip('\r\n') + ('\n\nGraphics: the new HD textures are ON by default. To play with the original textures: Options > Graphics >\n'
                                    'HD Textures: Off (applies on the next map load). Ray-traced shadows / lighting, where the build has them,\n'
                                    'are OFF by default and only run when switched on in the same menu.\n')
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
