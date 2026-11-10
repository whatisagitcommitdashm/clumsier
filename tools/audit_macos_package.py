"""Reject unbundled development dependencies and publish the actual OS floor."""
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys

MACH_MAGIC = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}

def audit(root, architecture=None, minimum_macos="13.0", maximum_macos=None):
    root = Path(root).resolve()
    def version_tuple(value):
        parts = tuple(map(int, value.split('.')))
        return (parts + (0, 0, 0))[:3]
    floor = version_tuple(minimum_macos)
    if architecture not in (None, 'arm64', 'x86_64'):
        raise ValueError('Unsupported package architecture')
    binaries = []
    for p in root.rglob('*'):
        if p.is_symlink():
            if not p.resolve().is_relative_to(root):
                raise RuntimeError(f'External or broken runtime symlink: {p}')
            if not p.exists():
                raise RuntimeError(f'Broken runtime symlink: {p}')
        if not p.is_file() or p.is_symlink():
            continue
        with p.open('rb') as f:
            if f.read(4) not in MACH_MAGIC:
                continue
        arch_args = ['-arch', architecture] if architecture else []
        if architecture:
            slices = subprocess.check_output(['/usr/bin/lipo', '-archs', str(p)], text=True).split()
            if architecture not in slices:
                raise RuntimeError(f'Missing {architecture} slice in {p}')
        commands = subprocess.check_output(['/usr/bin/otool', *arch_args, '-l', str(p)], text=True)
        ids = set(re.findall(r'cmd LC_ID_DYLIB\s+cmdsize \d+\s+name (.*?) \(offset', commands))
        loads = subprocess.check_output(['/usr/bin/otool', *arch_args, '-L', str(p)], text=True)
        for line in loads.splitlines()[1:]:
            if not line[:1].isspace():
                continue  # universal-binary architecture header
            dep = line.strip().split(' (', 1)[0]
            # A dylib's own install-name is metadata, not a runtime dependency.
            if dep in ids:
                continue
            if dep.startswith('/') and not dep.startswith(('/System/Library/', '/usr/lib/')):
                raise RuntimeError(f'External dependency {dep} in {p}')
        versions = re.findall(r'\bminos (\d+(?:\.\d+){0,2})', commands)
        versions += re.findall(r'cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+(?:\.\d+){0,2})', commands)
        for version in versions:
            v = tuple(map(int, version.split('.')))
            floor = max(floor, (v + (0, 0, 0))[:3])
        binaries.append(str(p.relative_to(root)))
    if not binaries:
        raise RuntimeError('No Mach-O binaries found')
    if maximum_macos and floor > version_tuple(maximum_macos):
        raise RuntimeError(f'Bundled binary requires macOS {floor}, above requested {maximum_macos}')
    app = root / 'Applications/Clumsier.app'
    plist = app / 'Contents/Info.plist'
    with plist.open('rb') as f: metadata = plistlib.load(f)
    metadata['LSMinimumSystemVersion'] = '.'.join(map(str, floor))
    with plist.open('wb') as f: plistlib.dump(metadata, f)
    report = {'minimum_macos': metadata['LSMinimumSystemVersion'], 'architecture': architecture, 'binaries': binaries,
              'notarized': False, 'clean_machine_tested': False}
    (app / 'Contents/Resources/package-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Package dependency audit passed: {len(binaries)} Mach-O files, minimum macOS {metadata["LSMinimumSystemVersion"]}')
    return report

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('root')
    parser.add_argument('--architecture', choices=['arm64', 'x86_64'])
    parser.add_argument('--minimum-macos', default='13.0')
    parser.add_argument('--maximum-macos')
    args = parser.parse_args()
    audit(args.root, args.architecture, args.minimum_macos, args.maximum_macos)
