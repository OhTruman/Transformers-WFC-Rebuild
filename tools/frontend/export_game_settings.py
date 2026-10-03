"""Export the TnOnlineGameSettings* class defaults the frontend needs (host options, lobby URL fields) from the
AssetTools authored database (read-only) into data/frontend/game_settings.json.

Source: AssetTools/manifests/authored.db, objects 'TransGame.Default__TnOnlineGameSettings*' (CONFIRMED authored
defaults). Subclass defaults are merged over their parent (TDMPrivate <- TDM <- Base) the way UE3 class defaults
inherit. HANDOFF: AssetTools to carry these fields in frontend_modes.json; the rebuild then reads them there.

Usage (from the worktree root):
  ../AssetTools/bin/py/python.exe tools/frontend/export_game_settings.py
"""
import json
import os
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DB = os.environ.get('WFC_AUTHORED_DB', os.path.normpath(os.path.join(ROOT, '..', 'AssetTools', 'manifests', 'authored.db')))
OUT = os.path.join(ROOT, 'data', 'frontend', 'game_settings.json')

KEEP = ['GameClass', 'LobbyGameClass', 'LobbyMapName', 'GameModeTag', 'NumRequiredPlayers', 'TeamType', 'NumPublicConnections',
        'NumPrivateConnections', 'bUsesStats', 'TimeLimits', 'Rules', 'FriendlyName', 'Description', 'RulesFormat',
        'LocalizedSettings', 'Properties', 'LocalizedSettingsMappings', 'PropertyMappings', 'bIconicMode']


def parent_of(name):
    for suffix in ('Private', 'Public', 'Solo'):
        if name.endswith(suffix) and name != 'TnOnlineGameSettings' + suffix:
            return name[: -len(suffix)]
    return None if name == 'TnOnlineGameSettingsBase' else 'TnOnlineGameSettingsBase'


def main():
    con = sqlite3.connect('file:%s?mode=ro' % DB, uri=True)
    raw = {}
    for opath, props in con.execute("select opath, props from objects where opath like 'TransGame.Default__TnOnlineGameSettings%'"):
        raw[opath.split('Default__')[1]] = json.loads(props) if props else {}
    out = {}

    def merged(name, depth=0):
        if name not in raw or depth > 4:
            return {}
        p = parent_of(name)
        base = dict(merged(p, depth + 1)) if p else {}
        base.update(raw[name])
        return base

    for name in sorted(raw):
        m = merged(name)
        out[name] = {k: m[k] for k in KEEP if k in m}
    doc = {
        'generated_by': 'tools/frontend/export_game_settings.py',
        'source': 'AssetTools/manifests/authored.db TransGame.Default__TnOnlineGameSettings* (CONFIRMED authored defaults), merged over parents',
        'classes': out,
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8') as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
    print('wrote %s (%d classes)' % (OUT, len(out)))


if __name__ == '__main__':
    sys.exit(main())
