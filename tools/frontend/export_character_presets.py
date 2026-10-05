"""Export the multiplayer class presets' full character data for Create a Character.

TR_MPPlayerCharacterData_p.<Class>_PCD_MP (TnPlayerCharacterData.Data): names, specialty, per-faction chassis,
colours (PrimaryColors / SecondaryColors with their palette ids and swatch coordinates), weapons, abilities,
vehicle and melee weapons, skills. TransGame.Default__TnCharacterCustomizationData: slot unlock levels, specialty
order, palette count. The roster package (AssetTools mp_content) carries ids and loadouts but not the colour data
the customization bindings read (GetCharacterPrimaryColor / GetPrimaryPalette / CommitCharacter).

Reads, never writes: AssetTools/manifests/authored.db (read-only URI).
Writes: data/frontend/character_presets.json.
"""
import json, os, sqlite3

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
DB = os.path.join(ROOT, 'AssetTools', 'manifests', 'authored.db').replace('\\', '/')
OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'frontend', 'character_presets.json')


def main():
    db = sqlite3.connect('file:' + DB + '?mode=ro', uri=True)
    presets = {}
    for opath, props in db.execute("select opath, props from objects where opath like 'TR_MPPlayerCharacterData_p.%_PCD_MP'"):
        data = (json.loads(props) or {}).get('Data') or {}
        presets[opath.rsplit('.', 1)[-1]] = data
    (cprops,) = next(db.execute("select props from objects where opath = 'TransGame.Default__TnCharacterCustomizationData'"))
    custom = json.loads(cprops)
    out = {'source': 'TR_MPPlayerCharacterData_p.*_PCD_MP (TnPlayerCharacterData.Data), '
                      'TransGame.Default__TnCharacterCustomizationData; authored.db',
           'customizationData': {k: custom.get(k) for k in ('UnlockCharacterSlotLevels', 'SpecialtyClasses',
                                                             'NumberOfColorPalettes', 'ClassVersion')},
           'presets': presets}
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('wrote', os.path.abspath(OUT), len(presets), 'presets')


if __name__ == '__main__':
    main()
