"""Create allowlisted portable/SDK release archives and SHA256 checksums."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent

def package(version, destination):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:\.[a-z0-9]+)*)?', version):
        raise ValueError('Use a version such as v0.1.0-alpha.1')
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    base = re.search(r'project\(hammer_addons VERSION ([0-9.]+)', cmake)[1]
    if version[1:].split('-')[0] != base:
        raise ValueError('Release version must match CMakeLists.txt')
    destination.mkdir(parents=True, exist_ok=True)
    portable = destination / f'hammer-addons-{version}-windows-x64.zip'
    portable.write_bytes((ROOT / 'dist/hammer-addons-portable.zip').read_bytes())
    # Recheck the portable allowlist. Never ship arbitrary dist/ files or local logs.
    import importlib.util
    spec = importlib.util.spec_from_file_location('launcher_package', ROOT / 'scripts/package-launcher.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    with zipfile.ZipFile(portable) as archive:
        if set(archive.namelist()) != {'Hammer Addons/' + n for n in module.FILES} or archive.testzip():
            raise ValueError('Unexpected portable archive contents')
    sdk = destination / f'hammer-addons-{version}-sdk.zip'
    files = [ROOT/'dist/portable/addon_sign.exe', ROOT/'README.md', ROOT/'THIRD_PARTY.md', ROOT/'scripts/new-addon.py']
    files += sorted((ROOT/'sdk/include').glob('*.h'))
    files += sorted((ROOT/'docs').glob('*.md'))
    for folder in sorted((ROOT/'addons').iterdir()):
        if folder.is_dir():
            files += sorted(folder.glob('*.cpp')) + sorted(folder.glob('*.h')) + sorted(folder.glob('*.ini')) + sorted(folder.glob('*.hanote'))
    with zipfile.ZipFile(sdk, 'w', zipfile.ZIP_DEFLATED) as archive:
        for file in files:
            archive.write(file, 'Hammer Addons SDK/' + ('addon_sign.exe' if file.name=='addon_sign.exe' else file.relative_to(ROOT).as_posix()))
    with zipfile.ZipFile(sdk) as archive:
        if archive.testzip(): raise ValueError('SDK archive corrupted')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest = destination / 'release.json'
    manifest.write_text(json.dumps({'version': version, 'commit': commit, 'platform': 'windows-x64',
        'archives': [p.name for p in (portable, sdk)]}, indent=2) + '\n')
    sums = destination / 'SHA256SUMS.txt'
    sums.write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n'
                            for p in (portable, sdk, manifest)))
    print(f'Release artifacts ready: {destination}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'dist/release')
    args = parser.parse_args()
    package(args.version, args.output)
