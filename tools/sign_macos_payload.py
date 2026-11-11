"""Sign staged Mach-O leaves before enclosing bundles; never mutate live apps."""
from pathlib import Path
import subprocess
import sys
from audit_macos_package import MACH_MAGIC

def sign(root, identity):
    root = Path(root).resolve()
    if root == Path('/') or not (root / 'Applications/Clumsier.app').is_dir():
        raise ValueError('Expected a staged Clumsier package root')
    def codesign(path):
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', identity,
                        '--timestamp=none', str(path)], check=True)
    for p in sorted(root.rglob('*')):
        if p.is_symlink() or not p.is_file():
            continue
        with p.open('rb') as f:
            if f.read(4) not in MACH_MAGIC:
                continue
        codesign(p)
    bundles = [p for p in root.rglob('*') if not p.is_symlink() and p.is_dir()
               and p.suffix in ('.framework', '.app', '.bundle')]
    for p in sorted(bundles, key=lambda p: len(p.parts), reverse=True):
        codesign(p)
    for app in (root / 'Applications').glob('*.app'):
        subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], check=True)

if __name__ == '__main__':
    sign(sys.argv[1], sys.argv[2])
