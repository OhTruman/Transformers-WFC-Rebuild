"""Merge a campaign chapter's per-unit vbake fits into the chapter calibration the runtime reads from
<persistent unit>/vbake/: fit.json (the chapter k / p / tangent from the unit with the most lit-shadowed pairs; pages[] = every
unit page with its unit - the unit's own per-page k, else its unit k when that fit is good, else 0: no sun swap there), the
chosen fit_lights.json, and <unit>/vbake_<page>.png per unit.

usage: merge_chapter_fit.py <trace root> <out dir> <lights unit> [--sky-from <unit>] [--persistent <unit>]
(the persistent unit's pages go to the root of <out dir>: the runtime keys them without a unit)
"""
import json, os, shutil, sys

root, out, lunit = sys.argv[1], sys.argv[2], sys.argv[3]
sky_from = sys.argv[sys.argv.index('--sky-from') + 1] if '--sky-from' in sys.argv else None
persistent = sys.argv[sys.argv.index('--persistent') + 1] if '--persistent' in sys.argv else None
fits = {}
for u in sorted(os.listdir(root)):
    p = os.path.join(root, u, 'fit.json')
    if os.path.isfile(p):
        fits[u] = json.load(open(p, encoding='utf-8'))
best = max(fits, key=lambda u: fits[u].get('pairs', 0))
B = fits[best]
chapter = {k: B[k] for k in ('sun', 'sun_linear', 'k', 'p', 'tangent_y_sign', 'pairs', 'residual', 'basis', 'cone_deg', 'supersample') if k in B}
chapter['chapter_note'] = ('chapter sun fit from %s (the most lit / shadowed pairs); pages[] carry their unit: a unit\'s per-page k, '
                           'else its unit k when its fit is good (residual < 1.2, tangent and p as the chapter\'s), else 0 (no sun '
                           'swap: the bake\'s sun kept)' % best)
chapter['pages'] = []
if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(out)
stats = {'own': 0, 'unit': 0, 'zero': 0}
for u, F in fits.items():
    good = (not F.get('sunless') and F.get('residual', 99) < 1.2 and F.get('k', 0) > 0
            and F.get('tangent_y_sign') == B['tangent_y_sign'])
    perpage = {pg['page']: pg for pg in F.get('pages', [])}
    pngs = [f for f in os.listdir(os.path.join(root, u)) if f.startswith('vbake_') and f.endswith('.png')]
    dst = out if u == persistent else os.path.join(out, u)
    if pngs:
        os.makedirs(dst, exist_ok=True)
    for f in pngs:
        page = f[len('vbake_'):-len('.png')]
        shutil.copyfile(os.path.join(root, u, f), os.path.join(dst, f))
        if good and page in perpage and perpage[page].get('k') is not None:
            k = float(perpage[page]['k']); stats['own'] += 1
        elif good:
            k = float(F['k']); stats['unit'] += 1
        else:
            k = 0.0; stats['zero'] += 1
        chapter['pages'].append({'page': page, 'k': k} if u == persistent else {'unit': u, 'page': page, 'k': k})
L = json.load(open(os.path.join(root, lunit, 'fit_lights.json'), encoding='utf-8'))
L['chapter_note'] = 'chapter lights fit from %s' % lunit
if sky_from:
    S = json.load(open(os.path.join(root, sky_from, 'fit_lights.json'), encoding='utf-8'))
    for key in ('sky', 'sky_linear'):
        if key in S:
            L[key] = S[key]
    L['chapter_note'] += '; sky from %s (%s found no open sky)' % (sky_from, lunit)
json.dump(chapter, open(os.path.join(out, 'fit.json'), 'w', encoding='utf-8'), indent=1)
json.dump(L, open(os.path.join(out, 'fit_lights.json'), 'w', encoding='utf-8'), indent=1)
print('chapter fit from %s: k %.3f p %.2f tangent %+.0f; %d pages (%s); lights %s%s -> %s'
      % (best, B['k'], B['p'], B['tangent_y_sign'], len(chapter['pages']), stats, lunit, ', sky ' + sky_from if sky_from else '', out))
