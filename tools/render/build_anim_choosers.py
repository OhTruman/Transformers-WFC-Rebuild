"""AnimSet ChooserGroups -> <render root>/_ui/anim_choosers.json (map-independent).

    python build_anim_choosers.py <render root>

UAnimNodeSequence::SetAnim(name) in the WFC xex (RE 2026-10-05, Function_82E3FF48) first remaps the requested name
through the skeletal component's AnimSets' ChooserGroups, walked from the LAST set to the first: the first set with a
group of that name supplies the anim (a weighted pick when the group lists several); with no such group the name is
used as-is. The customization preview's SetAnim('Cust_Idle') resolves this way (Sideswipe / Barricade -> NAV_Idle).
Every MP chassis set is cooked into UI_PartyLobby_m; the output maps AnimSet object path -> {group: [[anim, weight]]}.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo  # noqa: E402


def props(x):
    if isinstance(x, dict):
        return x
    if isinstance(x, list) and all(isinstance(e, dict) and 'name' in e for e in x):
        return {e['name']: e.get('value') for e in x}
    return {}


def main():
    root = sys.argv[1]
    r = Repo(['UI_PartyLobby_m.xxx'])
    p = r.pkgs[0]
    out = {}
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'AnimSet':
            continue
        path = p.object_path(i + 1)
        o = r.obj(path) or {}
        groups = {}
        for g in o.get('ChooserGroups') or []:
            g = props(g)
            name = g.get('GroupName')
            anims = [[props(a).get('AnimName'), int(props(a).get('Weight') or 1)] for a in g.get('Anims') or []]
            if name and anims:
                groups[name] = anims
        if groups:
            out[path] = groups
    os.makedirs(os.path.join(root, '_ui'), exist_ok=True)
    with open(os.path.join(root, '_ui', 'anim_choosers.json'), 'w', encoding='utf-8') as f:
        json.dump({'generated_by': 'tools/render/build_anim_choosers.py', 'source': 'UI_PartyLobby_m', 'sets': out}, f, indent=1)
    print('anim choosers: %d sets, %d with Cust_Idle' % (len(out), sum(1 for g in out.values() if 'Cust_Idle' in g)))


if __name__ == '__main__':
    main()
