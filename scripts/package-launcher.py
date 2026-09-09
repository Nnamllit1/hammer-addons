"""Package only release files, excluding local logs, preferences and custom add-ons."""
import argparse
from pathlib import Path
import zipfile

FILES = (
    'Launch Workshop Tools.cmd', 'tools_launcher.exe',
    'hammer_addons_runtime.dll', 'hammer_addons_ui.dll', 'README.md', 'LAUNCHER.md',
    'addons/hello/addon.ini', 'addons/hello/hello.dll',
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, required=True)
    args = parser.parse_args()
    source = args.dist / 'portable'
    for name in FILES:
        if not (source / name).is_file():
            parser.error(f'Missing portable release file: {name}')
    output = args.dist / 'hammer-addons-portable.zip'
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in FILES:
            archive.write(source / name, 'Hammer Addons/' + name)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('Portable archive verification failed')
    print(f'Portable launcher package: {output}')

if __name__ == '__main__':
    main()
