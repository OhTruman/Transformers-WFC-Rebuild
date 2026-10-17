"""Read-only probe of a FaceFX .fxa archive: printable strings with their offsets (argv[1] = file, argv[2] = max lines)."""
import re, sys
d = open(sys.argv[1], 'rb').read()
lim = int(sys.argv[2]) if len(sys.argv) > 2 else 200
n = 0
for m in re.finditer(rb'[\x20-\x7e]{4,}', d):
    print('%06x %s' % (m.start(), m.group().decode('ascii')))
    n += 1
    if n >= lim: break
print('size', len(d))
