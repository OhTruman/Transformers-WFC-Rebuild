"""Assemble the slim playtest package from AssetTools' slim_files.tsv.

keep rows: copied byte for byte; xpr rows: written as verified XPRESS_HUFF twins (round trip checked).
Output: Rebuild/work/playtest_slim (Play.bat + README copied from work/playtest_pkg).
"""
import csv, os, shutil, sys, hashlib
from concurrent.futures import ThreadPoolExecutor

ROOT = 'F:/Transformers Rebuild'
sys.path.insert(0, ROOT + '/Rebuild/tools/render')
import package_lossless as pl

OUT = ROOT + '/Rebuild/work/playtest_slim'
TSV = ROOT + '/AssetTools/out/package/slim_files.tsv'
XPR = ROOT + '/AssetTools/out/package/xpr_twins.tsv'

xpr_dest = {}
with open(XPR, encoding='utf-8') as f:
    for r in csv.DictReader(f, delimiter='\t'):
        xpr_dest[r['source']] = r['dest_xpr']

rows = []
with open(TSV, encoding='utf-8') as f:
    for r in csv.DictReader(f, delimiter='\t'):
        if r['action'] != 'drop':
            rows.append(r)

cab = pl._api()
fails = []
totals = {'keep': 0, 'xpr': 0, 'bytes': 0}


def do(r):
    src = os.path.join(ROOT, r['source'])
    if r['action'] == 'xpr':
        dest = os.path.join(OUT, xpr_dest[r['source']])
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        data = open(src, 'rb').read()
        packed = pl.compress(cab, data)
        if pl.decompress(cab, packed) != data:
            return ('ROUNDTRIP', src)
        open(dest, 'wb').write(packed)
        return ('xpr', len(packed))
    dest = os.path.join(OUT, r['dest'])
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copyfile(src, dest)
    if os.path.getsize(dest) != os.path.getsize(src):
        return ('SIZE', src)
    return ('keep', os.path.getsize(dest))


with ThreadPoolExecutor(6) as ex:
    for res in ex.map(do, rows):
        if res[0] in ('keep', 'xpr'):
            totals[res[0]] += 1
            totals['bytes'] += res[1]
        else:
            fails.append(res)

for n in ('Play.bat', 'README.txt'):
    shutil.copyfile(ROOT + '/Rebuild/work/playtest_pkg/' + n, OUT + '/' + n)

print('keep %d, xpr %d, total %.2f GB, failures %d' % (totals['keep'], totals['xpr'], totals['bytes'] / 2**30, len(fails)))
for f_ in fails[:20]:
    print(f_)
sys.exit(1 if fails else 0)
