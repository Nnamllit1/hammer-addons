"""Generate an independently buildable native Hammer add-on."""
import argparse
import re
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('id')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tools', default='all', help='Comma-separated tool IDs (default: all)')
    args = parser.parse_args()
    tags = [tag.strip() for tag in args.tools.split(',')]
    if (not tags or len(set(tags)) != len(tags) or
            any(tag not in {'all', 'asset_browser', 'hammer', 'modeldoc', 'material_editor', 'particle_editor'} for tag in tags) or
            ('all' in tags and len(tags) != 1)):
        parser.error('Use distinct supported tool IDs, or all alone.')
    if not re.fullmatch('[a-z][a-z0-9_]{0,63}', args.id) or args.id in {'con', 'prn', 'aux', 'nul', *(f'com{i}' for i in range(1, 10)), *(f'lpt{i}' for i in range(1, 10))}:
        parser.error('Use a lowercase identifier, starting with a letter; no Windows device names.')
    root = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=False)
    sample = (root / 'addons/hello/hello.cpp').read_text()
    (args.output / f'{args.id}.cpp').write_text(sample.replace('"hello"', f'"{args.id}"'))
    manifest = (root / 'addons/hello/addon.ini').read_text().replace('id=hello', f'id={args.id}').replace('entry=hello.dll', f'entry={args.id}.dll')
    manifest = re.sub(r'^tools=.*\n?', '', manifest, flags=re.M).rstrip() + '\ntools=' + ','.join(tags) + '\n'
    (args.output / 'addon.ini').write_text(manifest)
    (args.output / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(ADDON_NAME LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Requires Windows x64 MSVC")
endif()
if(NOT EXISTS "${HAMMER_ADDONS_SDK}/include/hammer_addons.h")
  message(FATAL_ERROR "Set HAMMER_ADDONS_SDK to the loader sdk/ directory")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(ADDON_NAME SHARED ADDON_NAME.cpp)
target_include_directories(ADDON_NAME PRIVATE "${HAMMER_ADDONS_SDK}/include")
install(TARGETS ADDON_NAME RUNTIME DESTINATION ADDON_NAME)
install(FILES addon.ini DESTINATION ADDON_NAME)
'''.replace('ADDON_NAME', args.id))
    print(f'Created {args.output}. Build with CMake and -DHAMMER_ADDONS_SDK=<checkout>/sdk')

if __name__ == '__main__':
    main()
