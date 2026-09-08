"""Inspect, install and restore Hammer Addons. Python 3.11+, standard library only."""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPORTS = ['BinaryProperties_GetValue', 'CreateInterface', 'ExtractModuleMetadata',
           'GetResourceManifestCount', 'GetResourceManifests', 'InstallSchemaBindings']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plain(path):
    path = Path(os.path.abspath(path))
    for item in [path, *path.parents]:
        if item.is_symlink() or (item.exists() and getattr(item.lstat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError(f'Reparse points are not supported: {item}')
    return path


def pe_info(path):
    data = path.read_bytes()
    try:
        if data[:2] != b'MZ':
            raise ValueError('not a PE DLL')
        pe = struct.unpack_from('<I', data, 60)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE signature')
        machine, sections = struct.unpack_from('<HH', data, pe + 4)
        opt = pe + 24
        if struct.unpack_from('<H', data, opt)[0] != 0x20b:
            raise ValueError('requires a PE32+ x64 DLL')
        table = opt + struct.unpack_from('<H', data, pe + 20)[0]
        def offset(rva):
            for i in range(sections):
                size, va, rawsize, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
                if va <= rva < va + max(size, rawsize):
                    return raw + rva - va
            raise ValueError('invalid PE address')
        export_rva = struct.unpack_from('<I', data, opt + 112)[0]
        export = offset(export_rva)
        base, functions, count, _, names, ordinals = struct.unpack_from('<IIIIII', data, export + 16)
        if count > 4096 or functions != count:
            raise ValueError('unsupported export table')
        found = {}
        for i in range(count):
            start = offset(struct.unpack_from('<I', data, offset(names) + i * 4)[0])
            name = data[start:data.index(0, start)].decode('ascii')
            ordinal = base + struct.unpack_from('<H', data, offset(ordinals) + i * 2)[0]
            found[name] = ordinal
        return {'sha256': hashlib.sha256(data).hexdigest(), 'machine': machine, 'exports': found}
    except (IndexError, struct.error, UnicodeError) as error:
        raise ValueError('invalid PE image') from error


def validate_exports(info):
    if info['machine'] != 0x8664 or info['exports'] != {name: i + 1 for i, name in enumerate(EXPORTS)}:
        raise ValueError('Hammer export names, ordinals or architecture do not match this proxy')


def paths(cs2):
    tools = plain(cs2 / 'game/bin/win64/tools')
    return tools, tools / 'hammer.dll', tools / 'hammer_original.dll', tools / 'hammer-addons/install.json'


def inspect(cs2):
    tools, target, original, state = paths(cs2)
    result = {'tools': str(tools), 'installed': state.exists(), 'hammer': pe_info(target)}
    if state.exists():
        record = json.loads(plain(state).read_text())
        result['proxy_matches'] = sha(target) == record['proxy_sha256']
        result['original_matches'] = original.exists() and sha(plain(original)) == record['original_sha256']
    return result


def require_closed():
    tasklist = Path(os.environ['SystemRoot']) / 'System32/tasklist.exe'
    result = subprocess.run([str(tasklist), '/FI', 'IMAGENAME eq cs2.exe', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True, check=True)
    if '"cs2.exe"' in result.stdout.lower():
        raise ValueError('Close CS2 and its Workshop Tools before installing or restoring the loader')


def atomic(path, data):
    plain(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, name = tempfile.mkstemp(dir=path.parent, prefix='.ha-')
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def install(cs2, distribution, apply=False, profiles=None):
    tools, target, original, state = paths(cs2)
    plain(target)
    if state.exists() or original.exists():
        raise ValueError('An installation or backup already exists; inspect and uninstall before reinstalling')
    info = pe_info(target)
    validate_exports(info)
    profiles = profiles if profiles is not None else json.loads((ROOT / 'compatibility.json').read_text())['profiles']
    if info['sha256'] not in {p['sha256'] for p in profiles}:
        raise ValueError('Unrecognized Hammer build. Run inspect; validate a new compatibility profile before installing')
    proxy = plain(distribution / 'hammer.dll')
    validate_exports(pe_info(proxy))
    if sha(proxy) == info['sha256']:
        raise ValueError('Distribution contains the original DLL instead of the loader')
    source = plain(distribution / 'hammer-addons')
    if not source.is_dir():
        raise ValueError('Build the distribution first')
    copies = []
    for file in sorted(source.rglob('*')):
        plain(file)
        if file.is_file():
            relative = file.relative_to(source)
            if relative.parts[0] != 'addons':
                raise ValueError('Unexpected distribution file')
            destination = plain(tools / 'hammer-addons' / relative)
            if destination.exists() and sha(destination) != sha(file):
                raise ValueError(f'Installed add-on differs; preserving {destination}')
            copies.append((file, destination))
    plan = {'action': 'install', 'target': str(target), 'backup': str(original),
            'original_sha256': info['sha256'], 'proxy_sha256': sha(proxy),
            'addon_files': [str(b) for _, b in copies], 'applied': False}
    if not apply:
        return plan
    require_closed()
    # The original is copied exclusively and verified before the proxy replaces it.
    # State is persisted first so recovery also works after an interrupted install.
    with plain(original).open('xb') as stream:
        stream.write(target.read_bytes())
        stream.flush()
        os.fsync(stream.fileno())
    if sha(original) != info['sha256']:
        raise ValueError('Original backup verification failed; Hammer was not replaced')
    atomic(state, json.dumps(plan, indent=2).encode())
    for file, destination in copies:
        atomic(destination, file.read_bytes())
    if sha(target) != info['sha256']:
        raise ValueError('Hammer changed during installation; refusing to overwrite it')
    atomic(target, proxy.read_bytes())
    plan['applied'] = True
    atomic(state, json.dumps(plan, indent=2).encode())
    return plan


def uninstall(cs2, apply=False):
    _, target, original, state = paths(cs2)
    record = json.loads(plain(state).read_text())
    if sha(plain(original)) != record['original_sha256']:
        raise ValueError('Original DLL backup has changed; refusing restoration')
    if sha(plain(target)) not in (record['proxy_sha256'], record['original_sha256']):
        raise ValueError('Hammer was updated or replaced externally. Refusing to overwrite that DLL with an older backup')
    result = {'action': 'uninstall', 'target': str(target), 'preserve_addons': True, 'applied': False}
    if apply:
        require_closed()
        atomic(target, original.read_bytes())
        if sha(target) != record['original_sha256']:
            raise ValueError('Restoration verification failed; backup retained')
        state.unlink()
        original.unlink()
        result['applied'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['inspect', 'install', 'uninstall'])
    parser.add_argument('--cs2', type=Path, required=True, help='CS2 installation root containing game/')
    parser.add_argument('--dist', type=Path, default=ROOT if (ROOT / 'hammer.dll').exists() else ROOT / 'dist')
    parser.add_argument('--apply', action='store_true', help='Apply the displayed operation; default is preview')
    args = parser.parse_args()
    try:
        if args.command == 'inspect':
            result = inspect(plain(args.cs2))
        elif args.command == 'install':
            result = install(plain(args.cs2), plain(args.dist), args.apply)
        else:
            result = uninstall(plain(args.cs2), args.apply)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
