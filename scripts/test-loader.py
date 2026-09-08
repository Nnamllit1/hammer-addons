"""Exercise real native DLLs in temporary directories, without a CS2 install."""
import argparse
import shutil
import subprocess
import tempfile
import importlib.util
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', type=Path, required=True)
    args = parser.parse_args()
    binaries = args.bin.resolve()
    root = Path(__file__).resolve().parents[1]
    temp = root / 'build' / 'tests'
    temp.mkdir(parents=True, exist_ok=True)
    count = 0
    with tempfile.TemporaryDirectory(dir=temp) as folder:
        directory = Path(folder)
        package = directory / 'hammer-addons'
        hello = package / 'addons' / 'hello'
        hello.mkdir(parents=True)
        original = (root / 'addons/hello/addon.ini').read_text()
        (hello / 'addon.ini').write_text(original)
        shutil.copy2(binaries / 'hello.dll', hello)

        def run(expected, *messages):
            nonlocal count
            result = subprocess.run([str(binaries / 'ha_host.exe'), str(package)], capture_output=True, text=True, timeout=15)
            assert result.returncode == expected, result.stdout + result.stderr
            for message in messages:
                assert message in result.stdout, result.stdout
            count += 1

        run(0, 'loaded=1 rejected=0', 'host.test: standalone', 'hello: shutdown')
        (hello / 'addon.ini').write_text(original.replace('enabled=true', 'enabled=false'))
        run(0, 'loaded=0 rejected=0 disabled=1')
        for replacement in ['../hello.dll', 'C:/hello.dll', 'hello.dll:stream', 'folder/hello.dll']:
            (hello / 'addon.ini').write_text(original.replace('entry=hello.dll', 'entry=' + replacement))
            run(1, 'rejected=1')
        for text in [original + '\nid=hello\n', original.replace('abi=1', 'abi=2'), original.replace('id=hello', 'id=other'), original + '\nunknown=true\n']:
            (hello / 'addon.ini').write_text(text)
            run(1, 'rejected=1')
        (hello / 'addon.ini').write_text(original.replace('entry=hello.dll', 'entry=missing.dll'))
        run(1, 'LoadLibraryEx failed')
        (hello / 'addon.ini').write_text(original)
        bad = package / 'addons/bad'
        bad.mkdir()
        shutil.copy2(binaries / 'bad_abi.dll', bad)
        (bad / 'addon.ini').write_text(original.replace('id=hello', 'id=bad').replace('entry=hello.dll', 'entry=bad_abi.dll'))
        run(1, 'loaded=1 rejected=1', 'ABI, ID or required capabilities')
        (package / 'disabled').touch()
        run(0, 'disabled by marker', 'loaded=0 rejected=0')
        (package / 'disabled').unlink()
        (bad / 'addon.ini').write_text((bad / 'addon.ini').read_text().replace('enabled=true', 'enabled=false'))
        shutil.copy2(binaries / 'hammer.dll', directory)
        shutil.copy2(binaries / 'hammer_original.dll', directory)
        result = subprocess.run([str(binaries / 'proxy_test.exe'), str(directory / 'hammer.dll')], capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'hello: hammer.factory.request: ToolSystem2_001' in result.stdout, result.stdout
        assert 'All six exports forwarded' in result.stdout, result.stdout
        count += 1
        print(result.stdout)
        spec = importlib.util.spec_from_file_location('loader', root / 'scripts/loader.py')
        loader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(loader)
        loader.require_closed = lambda: None  # This fixture directory is not a running installation.
        fake_cs2 = directory / 'cs2'
        tools = fake_cs2 / 'game/bin/win64/tools'
        tools.mkdir(parents=True)
        shutil.copy2(binaries / 'hammer_original.dll', tools / 'hammer.dll')
        before = (tools / 'hammer.dll').read_bytes()
        profiles = [{'sha256': loader.sha(tools / 'hammer.dll')}]
        preview = loader.install(fake_cs2, root / 'dist', profiles=profiles)
        assert not preview['applied'] and not (tools / 'hammer_original.dll').exists()
        loader.install(fake_cs2, root / 'dist', apply=True, profiles=profiles)
        assert (tools / 'hammer_original.dll').read_bytes() == before
        assert loader.inspect(fake_cs2)['proxy_matches']
        count += 1
        installed = (tools / 'hammer.dll').read_bytes()
        (tools / 'hammer.dll').write_bytes(b'external update')
        try:
            loader.uninstall(fake_cs2, apply=True)
            raise AssertionError('must refuse to overwrite external update')
        except ValueError as error:
            assert 'externally' in str(error)
        assert (tools / 'hammer.dll').read_bytes() == b'external update'
        (tools / 'hammer.dll').write_bytes(installed)
        count += 1
        loader.uninstall(fake_cs2, apply=True)
        assert (tools / 'hammer.dll').read_bytes() == before
        assert not (tools / 'hammer_original.dll').exists()
        assert (tools / 'hammer-addons/addons/hello/hello.dll').exists()
        count += 1
        try:
            loader.install(fake_cs2, root / 'dist', apply=True, profiles=[])
            raise AssertionError('must reject an unknown binary')
        except ValueError as error:
            assert 'Unrecognized' in str(error)
        assert (tools / 'hammer.dll').read_bytes() == before
        count += 1
        # The distributed management tool must work without the source checkout.
        packaged = subprocess.run(['python', str(root / 'dist/scripts/loader.py'), 'inspect', '--cs2', str(fake_cs2)], capture_output=True, text=True, timeout=15)
        assert packaged.returncode == 0 and '"installed": false' in packaged.stdout, packaged.stdout + packaged.stderr
        count += 1
        # Compile and load a generated third-party add-on against only the public SDK.
        generated = directory / 'generated'
        subprocess.run(['python', str(root / 'dist/scripts/new-addon.py'), 'generated', '--output', str(generated)], check=True, capture_output=True, timeout=15)
        cache = (binaries.parent / 'CMakeCache.txt').read_text()
        cmake = re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache, re.M)[1]
        generator = re.search(r'^CMAKE_GENERATOR:INTERNAL=(.+)$', cache, re.M)[1]
        for command in [[cmake, '-S', str(generated), '-B', str(generated / 'build'), '-G', generator, '-A', 'x64', f'-DHAMMER_ADDONS_SDK={root / "dist/sdk"}'],
                        [cmake, '--build', str(generated / 'build'), '--config', 'Release'],
                        [cmake, '--install', str(generated / 'build'), '--config', 'Release', '--prefix', str(package / 'addons')]]:
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
        run(0, 'loaded=2 rejected=0 disabled=1', 'generated: host.test: standalone')
    print(f'{count} native integration scenarios passed.')


if __name__ == '__main__':
    main()
