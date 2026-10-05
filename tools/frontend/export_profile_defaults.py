"""Export the original player-profile fields and defaults (TransGame.Default__TnProfileSettings).

ProfileMappings name every field (Id -> Name); DefaultSettings hold the fresh-profile value and owner of each Id.
The frontend's LocalProfile serves these through <OnlinePlayerData:ProfileData.Name> and persists only what the
player changes.

Reads, never writes: AssetTools/manifests/authored.db (read-only URI).
Writes: data/frontend/profile_defaults.json.
"""
import json, os, sqlite3

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
DB = os.path.join(ROOT, 'AssetTools', 'manifests', 'authored.db').replace('\\', '/')
OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'frontend', 'profile_defaults.json')


def main():
    db = sqlite3.connect('file:' + DB + '?mode=ro', uri=True)
    (props,) = next(db.execute("select props from objects where opath = 'TransGame.Default__TnProfileSettings'"))
    p = json.loads(props)
    names = {m['Id']: m['Name'] for m in p['ProfileMappings']}
    fields = []
    for d in p['DefaultSettings']:
        s = d['ProfileSetting']
        data = s['Data']
        kind = data['Type']
        if kind == 'SDT_Bool':
            value = 'True' if data['Value1'] else 'False'
        elif kind == 'SDT_Int32':
            value = str(data['Value1'])
        else:
            value = data.get('ValueStr') or str(data.get('Value1', ''))
        fields.append({'id': s['PropertyId'], 'name': names.get(s['PropertyId']), 'type': kind, 'default': value,
                       'owner': d['Owner']})
    out = {'source': 'TransGame.Default__TnProfileSettings (DefaultSettings + ProfileMappings), authored.db',
           'versionNumber': p.get('VersionNumber'), 'fields': fields}
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('wrote', os.path.abspath(OUT), len(fields), 'fields')


if __name__ == '__main__':
    main()
