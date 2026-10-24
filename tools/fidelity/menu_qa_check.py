"""Menu-flow QA checker (tools/fidelity/menu-qa.ps1): one PASS / FAIL / UNKNOWN table per flow from its FLOW trace (wfc.log).
   python menu_qa_check.py <out dir> <flow> [<flow> ...]   ->  <out dir>/MENU_QA.md (+ stdout)
Always checked: the flow reaches its end (else: the first missing checkpoint + the last script step = where it stuck), no modal
popup left open outside a '*quitbox' / '*prompt' / '*popup' checkpoint, at most one focused overlay, an input owner on every menu
screen. Flow-file lines '#expect <kind> <args>' add:
  mode <TAG>[|<TAG>]              the 'FLOW loading match world ... mode <TAG>' of the (first) match (Escalation = SV in the frontend)
  chassis-kept                    every match.selectCharacterResult ok=true, the same chassis until a 'changechar' checkpoint
  no-charselect-after-ingame      no 'uistate ... -> CharacterSelect' after the first InGame unless after a 'changechar' checkpoint
  state <checkpoint> <UiState>    the navcheck at <checkpoint> reports uiState=<UiState>
  level <checkpoint> <Level>      ... level=<Level>
  chassis <A>[|<B>]               the first successful pick's chassis (e.g. Car2|Car4 = Scout)
  owner <checkpoint> <text>       the navcheck inputOwner at <checkpoint> contains <text>
  log <regex> / nolog-after <anchor regex> <regex>   a log line must / must not appear (after the anchor)
  moved <cpA> <cpB> <metres>      pawn position (navcheck field pos=x,y,z) moved >= metres between the two checkpoints
  still <checkpoint> <jitter>     navcheck jitter <= jitter (frame-to-frame transform flips: the #16 / #28 flicker)
  shots <cpA> <cpB>               navcheck shots grew between the checkpoints
  form <cpA> <cpB>                navcheck form differs between the checkpoints (a transform happened)
The pawn checks read fields the Frontend adds to navcheck (requested 2026-10-23); missing fields -> UNKNOWN, never a guessed PASS."""
import json
import os
import re
import sys


def parse(log):
    nav, ui, modes, picks, steps, timeout = [], [], [], [], [], False
    for n, line in enumerate(log):
        if 'FLOW nav.check ' in line:
            kv = {}
            for tok in line.split('FLOW nav.check ', 1)[1].split(' '):
                if '=' in tok:
                    k, v = tok.split('=', 1)
                    kv[k] = v
            kv['_line'] = n
            nav.append(kv)
        m = re.search(r'FLOW uistate (\S+) -> (\S+)', line)
        if m:
            ui.append((n, m.group(1), m.group(2)))
        m = re.search(r'FLOW loading match world: .*mode (\S+)', line)
        if m:
            modes.append((n, m.group(1)))
        m = re.search(r'FLOW match\.selectCharacterResult ok=(\S+) player=\S+ chassis=(\S+)', line)
        if m:
            picks.append((n, m.group(1), m.group(2)))
        if 'FLOW script.' in line:
            steps.append((n, line.split('FLOW', 1)[1].strip()[:140]))
        if re.search(r'FLOW timeout|script timeout|FRONTEND script: timed out', line, re.I):
            timeout = True
    return nav, ui, modes, picks, steps, timeout


def pos(c):
    try:
        return [float(x) for x in c['pos'].split(',')]
    except (KeyError, ValueError):
        return None


def check_flow(out, flow):
    rows = []
    run = os.path.join(out, flow)
    ff = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'menu-qa', flow + '.txt')
    lines = [x.strip() for x in open(ff, encoding='utf-8')] if os.path.exists(ff) else []
    want = [x[len('navcheck:'):] for x in lines if x.startswith('navcheck:')] + ['flow.end']
    expects = [x[len('#expect '):].split() for x in lines if x.startswith('#expect ')]
    logf = os.path.join(run, 'wfc.log')
    if not os.path.exists(logf):
        return [('run', 'FAIL', 'no wfc.log (the exe did not start in the run dir)')]
    log = open(logf, encoding='utf-8', errors='replace').read().split('\n')
    nav, ui, modes, picks, steps, timeout = parse(log)
    by = {c.get('label'): c for c in nav}
    rj = os.path.join(run, 'run.json')
    info = json.load(open(rj, encoding='utf-8-sig')) if os.path.exists(rj) else {}
    todo = sum(1 for x in lines if 'TODO(' in x)
    pending = [x[len('# PENDING:'):].strip() for x in lines if x.startswith('# PENDING:')]
    for x in pending:
        rows.append(('pending step', 'UNKNOWN', x))
    if todo:   # menu steps not filled in yet: the checkpoints after them were reached without the real clicks
        rows.append(('flow script complete', 'UNKNOWN', f"{todo} TODO steps pending (the Frontend's canonical menu steps) - not a gate result"))
    rows.append(('exit', 'PASS' if str(info.get('exit')) == '0' else 'FAIL', f"exit {info.get('exit')}, {info.get('wall_s')} s wall"))
    missing = [w for w in want if w not in by]
    if missing:
        last = steps[-1][1] if steps else '-'
        rows.append(('reaches the end', 'FAIL', f"stuck before checkpoint '{missing[0]}' ({len(want) - len(missing)} / {len(want)} reached); last script step: {last}"))
    else:
        rows.append(('reaches the end', 'PASS', f"{len(want)} / {len(want)} checkpoints"))
    if timeout:
        rows.append(('timeout', 'FAIL', 'flow timeout reported'))
    for w in want:
        c = by.get(w)
        if not c:
            continue
        bad = []
        modal = re.search(r'quitbox|prompt|popup', w)
        if c.get('popup') == 'open' and not modal:
            bad.append('modal popup left open')
        try:
            if int(c.get('focusExtras', '0')) > 1:
                bad.append(f"focusExtras={c.get('focusExtras')}")
        except ValueError:
            pass
        in_match = c.get('level') == 'Match' and c.get('uiState') in ('InGame', 'Spectating')
        if in_match and c.get('inputOwner') not in (None, 'game'):
            bad.append(f"in play but input owned by {c.get('inputOwner')} (lost input / stale overlay)")
        if not in_match and not modal:
            if c.get('inputOwnerVisible') in ('false', '0'):
                bad.append(f"input owner {c.get('inputOwner')} not visible (dimmed / trapped screen)")
            if c.get('inputOwner') in (None, '-', '') and c.get('level') != 'Match':
                bad.append('no input owner')
        detail = f"ui {c.get('uiState')}, level {c.get('level')}, movie {c.get('openMovie')}, owner {c.get('inputOwner')}"
        rows.append((f"checkpoint {w}", 'FAIL' if bad else 'PASS', detail + (' | ' + '; '.join(bad) if bad else '')))
    first_ingame = next((n for n, _a, b in ui if b == 'InGame'), None)
    cc = [c['_line'] for c in nav if 'changechar' in c.get('label', '')]
    for ex in expects:
        k, a = ex[0], ex[1:]
        name = 'expect ' + ' '.join(ex)
        if k == 'mode':
            got = modes[0][1] if modes else None
            rows.append((name, 'PASS' if got in a[0].split('|') else 'FAIL', f"match mode {got}"))   # 'SV|ESC': the frontend's Escalation tag is SV (TnGameLobbyGameSurvival)
        elif k == 'chassis-kept':
            okp = [p for p in picks if p[1] == 'true']
            first = okp[0][2] if okp else None
            before = [p for p in okp if not cc or p[0] < cc[0]]
            drift = [p[2] for p in before if p[2] != first]
            failed = not okp or drift or any(p[1] != 'true' for p in picks)
            rows.append((name, 'FAIL' if failed else 'PASS', f"picks {[(p[1], p[2]) for p in picks]}"))
        elif k == 'no-charselect-after-ingame':
            again = [n for n, _x, b in ui if b == 'CharacterSelect' and first_ingame is not None and n > first_ingame and not (cc and n > cc[0])]
            rows.append((name, 'FAIL' if again else 'PASS', f"{len(again)} re-entries into CharacterSelect after the first InGame" + (f" (log line {again[0]})" if again else '')))
        elif k in ('state', 'level'):
            c = by.get(a[0])
            key = 'uiState' if k == 'state' else 'level'
            rows.append((name, 'UNKNOWN' if not c else ('PASS' if c.get(key) == a[1] else 'FAIL'), f"{key}={c.get(key) if c else '-'}"))
        elif k == 'moved':
            p1, p2 = pos(by.get(a[0], {})), pos(by.get(a[1], {}))
            if p1 is None or p2 is None:
                rows.append((name, 'UNKNOWN', 'no pos field on navcheck (FRONTEND_QA_GATE_HOOKS not in this build)'))
            else:
                d = sum((x - y) ** 2 for x, y in zip(p1, p2)) ** 0.5
                rows.append((name, 'PASS' if d >= float(a[2]) else 'FAIL', f"moved {d:.2f} m"))
        elif k == 'still':
            j = by.get(a[0], {}).get('jitter')
            rows.append((name, 'UNKNOWN' if j is None else ('PASS' if float(j) <= float(a[1]) else 'FAIL'), f"jitter={j}"))
        elif k == 'shots':
            s1, s2 = by.get(a[0], {}).get('shots'), by.get(a[1], {}).get('shots')
            rows.append((name, 'UNKNOWN' if s1 is None or s2 is None else ('PASS' if int(s2) > int(s1) else 'FAIL'), f"shots {s1} -> {s2}"))
        elif k == 'form':
            f1, f2 = by.get(a[0], {}).get('form'), by.get(a[1], {}).get('form')
            rows.append((name, 'UNKNOWN' if f1 is None or f2 is None else ('PASS' if f1 != f2 else 'FAIL'), f"form {f1} -> {f2}"))
        elif k == 'chassis':
            okp = [p for p in picks if p[1] == 'true']
            got = okp[0][2] if okp else None
            rows.append((name, 'PASS' if got in a[0].split('|') else 'FAIL', f"first pick chassis {got}"))
        elif k == 'owner':
            c = by.get(a[0])
            rows.append((name, 'UNKNOWN' if not c else ('PASS' if a[1] in (c.get('inputOwner') or '') else 'FAIL'), f"inputOwner={c.get('inputOwner') if c else '-'}"))
        elif k == 'log':
            rx = re.compile(' '.join(a))
            hit = next((n for n, x in enumerate(log) if rx.search(x)), None)
            rows.append((name, 'PASS' if hit is not None else 'FAIL', f"log line {hit}" if hit is not None else 'not in the log'))
        elif k == 'nolog-after':
            anchor, rx = re.compile(a[0]), re.compile(' '.join(a[1:]))
            start = next((n for n, x in enumerate(log) if anchor.search(x)), None)
            hits = [n for n, x in enumerate(log) if start is not None and n > start and rx.search(x)]
            rows.append((name, 'UNKNOWN' if start is None else ('FAIL' if hits else 'PASS'), 'anchor not found' if start is None else (f"{len(hits)} hits after line {start}: " + (log[hits[0]].strip()[:120] if hits else '-'))))
        else:
            rows.append((name, 'UNKNOWN', 'unknown expectation'))
    return rows


def main():
    out, flows = sys.argv[1], sys.argv[2:]
    md = ['# Menu-flow QA (USER_PLAYTEST_2026-10-23b section 43)', '']
    for f in flows:
        rows = check_flow(out, f)
        verdict = 'FAIL' if any(r[1] == 'FAIL' for r in rows) else ('UNKNOWN' if any(r[1] == 'UNKNOWN' for r in rows) else 'PASS')
        md += [f'## {f}: **{verdict}**', '', '| Check | Result | Detail |', '|---|---|---|']
        md += [f'| {a} | {b} | {c} |' for a, b, c in rows] + ['']
    open(os.path.join(out, 'MENU_QA.md'), 'w', encoding='utf-8').write('\n'.join(md))
    print('\n'.join(md))


if __name__ == '__main__':
    main()
