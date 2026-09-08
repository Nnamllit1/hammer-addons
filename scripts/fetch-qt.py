"""Fetch the pinned matching Qt headers/libraries for building our UI DLL."""
import argparse
import hashlib
import subprocess
import urllib.request
from pathlib import Path

VERSION = '5.15.2'
SHA256 = 'e563de40230295841c2eece26e347de709edc4bf035515cc239cae3c994c9af6'
URL = ('https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/'
       'qt.qt5.5152.win64_msvc2019_64/'
       '5.15.2-0-202011130602qtbase-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    args = parser.parse_args()
    cache = Path(__file__).resolve().parents[1] / 'build/deps'
    sdk = cache / '5.15.2/msvc2019_64'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'qtbase-5.15.2.7z'
    if not archive.exists():
        pending = archive.with_suffix('.download')
        print('Downloading matching Qt 5.15.2 development package (33 MiB)...', flush=True)
        with urllib.request.urlopen(URL, timeout=120) as response, pending.open('wb') as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if hashlib.sha256(pending.read_bytes()).hexdigest() != SHA256:
            raise ValueError('Qt download checksum mismatch')
        pending.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise ValueError('Cached Qt archive checksum mismatch')
    if not (sdk / 'lib/cmake/Qt5/Qt5Config.cmake').exists():
        subprocess.run([args.cmake, '-E', 'tar', 'xf', str(archive)], cwd=cache, check=True)
    print(sdk)


if __name__ == '__main__':
    main()
