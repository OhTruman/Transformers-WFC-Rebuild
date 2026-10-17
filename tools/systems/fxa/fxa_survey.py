"""Read-only survey of FaceFX .fxa archives (Systems, CAMPAIGN_FACEFX_FORMAT.md). argv[1] = content root.
Per file: header version, class table size, the actor name, face bones (names ending _XF<n>), animation groups, animation
names (DX_*) and how many animations carry a "<package>.<SoundCue>" link right before their name (BE u32 length-prefixed
strings)."""
import os, re, struct, sys, glob, json
root = sys.argv[1]
out = []
for p in sorted(glob.glob(os.path.join(root, '*', '*.fxa'))):
    d = open(p, 'rb').read()
    magic = d[:4]
    ver = struct.unpack('>I', d[4:8])[0]
    strs = [(m.start(), m.group().decode('ascii')) for m in re.finditer(rb'[\x20-\x7e]{4,}', d)]
    names = [s for _, s in strs]
    bones = sorted({s for s in names if re.fullmatch(r'[CLR]_\w+_XF\d', s)})
    anims = sorted({s for s in names if re.fullmatch(r'DX_\w+', s)})
    # an animation block: <u32 BE len><cue path><u32 BE len><anim name>
    linked = 0; cues = set()
    for m in re.finditer(rb'\x00\x00\x00([\x08-\x60])([A-Za-z0-9_]+\.[A-Za-z0-9_.]+)\x00\x00\x00([\x08-\x60])(DX_[A-Za-z0-9_]+)', d):
        if m.group(1)[0] == len(m.group(2)) and m.group(3)[0] == len(m.group(4)):
            linked += 1; cues.add(m.group(2).decode())
    groups = [s for s in ('Default', 'Dialogue') if s in names]
    out.append({'file': os.path.relpath(p, root).replace('\\', '/'), 'bytes': len(d), 'magic': magic.decode('latin1'),
                'version': ver, 'face_bones': len(bones), 'bones': bones, 'anim_names': len(anims),
                'anim_blocks_with_cue': linked, 'cue_banks': sorted({c.split('.')[0] for c in cues}), 'groups': groups})
tot = sum(r['anim_names'] for r in out)
for r in out:
    print('%-58s %7d B v%d bones %2d anims %3d with-cue %3d groups %s banks %s' % (r['file'], r['bytes'], r['version'],
          r['face_bones'], r['anim_names'], r['anim_blocks_with_cue'], r['groups'], r['cue_banks']))
print('files', len(out), 'total animation names', tot, 'versions', sorted({r['version'] for r in out}))
if len(sys.argv) > 2: json.dump(out, open(sys.argv[2], 'w', encoding='utf-8'), indent=1)
