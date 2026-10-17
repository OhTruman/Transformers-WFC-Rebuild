"""Fetch the Opus codec source (voice chat, ONLINE_MP_ARCHITECTURE.md section 7) into third_party/opus (gitignored), the dxc / ffx way.
Official release tarball from downloads.xiph.org, sha256-pinned; BSD-3-Clause (third_party/opus/COPYING goes to the credits list).
The build uses it when present (CMake add_subdirectory, static, no DLL) and degrades to "voice unavailable" when absent.
Run with AssetTools/bin/py/python.exe:  python -I tools/net/fetch_opus.py [--check]"""
import hashlib, io, os, sys, tarfile, urllib.request

VERSION = '1.5.2'
URL = 'https://downloads.xiph.org/releases/opus/opus-%s.tar.gz' % VERSION
SHA256 = '65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1'
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DEST = os.path.join(ROOT, 'third_party', 'opus')


def main():
    marker = os.path.join(DEST, '.fetched')
    if os.path.isfile(marker) and open(marker).read().strip() == SHA256:
        print('opus %s already in %s' % (VERSION, DEST))
        return 0
    if '--check' in sys.argv:
        print('opus missing (run tools/net/fetch_opus.py)')
        return 1
    print('downloading', URL)
    data = urllib.request.urlopen(URL, timeout=120).read()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        print('SHA256 MISMATCH: got %s, pinned %s - not extracted' % (got, SHA256))
        return 2
    prefix = 'opus-%s/' % VERSION
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as t:
        for m in t.getmembers():
            if not m.name.startswith(prefix) or '..' in m.name or m.issym() or m.islnk():
                continue   # only the release's own tree, no links, no path escapes
            rel = m.name[len(prefix):]
            if not rel:
                continue
            out = os.path.join(DEST, rel)
            if m.isdir():
                os.makedirs(out, exist_ok=True)
            elif m.isfile():
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with t.extractfile(m) as src, open(out, 'wb') as dst:
                    dst.write(src.read())
    open(marker, 'w').write(SHA256)
    print('opus %s -> %s (sha256 ok)' % (VERSION, DEST))
    return 0


if __name__ == '__main__':
    sys.exit(main())
