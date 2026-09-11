"""Package only release files, excluding local logs, preferences and custom add-ons."""
import argparse
from pathlib import Path
import zipfile

FILES = (
    'Launch Workshop Tools.cmd', 'tools_launcher.exe',
    'hammer_addons_runtime.dll', 'hammer_addons_ui.dll', 'README.md', 'LAUNCHER.md',
    'BUILD_OUTPUT.md', 'TOOL_LOGS.md', 'PROJECT_CONTEXT.md', 'STEAM.md',
    'addons/steam_context/addon.ini', 'addons/steam_context/steam_context.dll',
    'addons/hello/addon.ini', 'addons/hello/hello.dll',
    'addons/compile_report/addon.ini', 'addons/compile_report/compile_report.dll',
    'addons/project_context/addon.ini', 'addons/project_context/project_context.dll',
    'addons/tool_console/addon.ini', 'addons/tool_console/tool_console.dll',
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
