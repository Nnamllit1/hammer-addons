"""Exercise real native DLLs in temporary directories, without a CS2 install."""
import argparse
import shutil
import subprocess
import tempfile
import importlib.util
import re
import json
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

        def approve_fixtures(target=None):
            target = target or package
            for addon in (target/'addons').iterdir():
                if addon.is_dir() and (addon/'addon.ini').is_file():
                    subprocess.run([str(binaries/'addon_sign.exe'),'approve',str(addon),'--store',str(target/'local-approvals')],capture_output=True,timeout=30)

        def run(expected, *messages):
            nonlocal count
            approve_fixtures()
            result = subprocess.run([str(binaries / 'ha_host.exe'), str(package)], capture_output=True, text=True, timeout=15)
            assert result.returncode == expected, result.stdout + result.stderr
            for message in messages:
                assert message in result.stdout, result.stdout
            snapshot = json.loads(next(line[7:] for line in result.stdout.splitlines() if line.startswith('STATUS ')))
            states = [addon['state'] for addon in snapshot['addons']]
            if 'disabled by marker' in result.stdout:
                assert snapshot['notice'] and not states, snapshot
            else:
                assert states.count('Loaded') == int(re.search(r'loaded=(\d+)', result.stdout)[1]), snapshot
                assert states.count('Failed') == int(re.search(r'rejected=(\d+)', result.stdout)[1]), snapshot
                assert states.count('Disabled') == int(re.search(r'disabled=(\d+)', result.stdout)[1]), snapshot
            count += 1
            return snapshot

        run(0, 'loaded=1 rejected=0', 'host.test: standalone', 'hello: shutdown')
        assert run(0)['addons'][0]['tools'] == ['all']
        (hello / 'addon.ini').write_text(original.replace('tools=all\n', ''))
        assert run(0)['addons'][0]['tools'] == []
        for tags, expected in [('asset_browser,hammer', ['asset_browser','hammer']),
                               ('modeldoc', ['modeldoc']), ('hammer, modeldoc', ['hammer','modeldoc'])]:
            (hello / 'addon.ini').write_text(original.replace('tools=all', 'tools=' + tags))
            assert run(0)['addons'][0]['tools'] == expected
        for tags in ['', 'unknown', 'hammer,hammer', 'all,hammer', 'hammer,', ',hammer']:
            (hello / 'addon.ini').write_text(original.replace('tools=all', 'tools=' + tags))
            run(1, 'rejected=1')
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
        approve_fixtures()
        result = subprocess.run([str(binaries / 'proxy_test.exe'), str(directory / 'hammer.dll')], capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'hello: hammer.factory.request: ToolSystem2_001' in result.stdout, result.stdout
        assert 'All six exports forwarded' in result.stdout, result.stdout
        count += 1
        print(result.stdout)
        recursive = package / 'addons/reentrant'
        recursive.mkdir()
        shutil.copy2(binaries / 'reentrant.dll', recursive)
        (recursive / 'addon.ini').write_text(original.replace('hello', 'reentrant'))
        approve_fixtures()
        result = subprocess.run([str(binaries / 'proxy_test.exe'), str(directory / 'hammer.dll')],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.count('reentrant on_load passed') == 1, result.stdout
        assert result.stdout.count('reentrant on_event passed') == 2, result.stdout
        count += 1
        print('Reentrant factory calls passed in on_load/on_event, on the callback thread and a joined worker.')
        shutil.rmtree(recursive)  # Test-owned directory, after the fixture process has exited.
        # The general entry point must load add-ons before any Hammer DLL exists.
        browser_dir = directory / 'browser-only'
        browser_package = browser_dir / 'tools/hammer-addons'
        shutil.copytree(package, browser_package)
        shutil.copy2(binaries / 'assetbrowser.dll', browser_dir)
        shutil.copy2(binaries / 'hammer_original.dll', browser_dir / 'assetbrowser_original.dll')
        nested = browser_package / 'addons/reentrant'
        nested.mkdir()
        shutil.copy2(binaries / 'reentrant.dll', nested)
        (nested / 'addon.ini').write_text(original.replace('hello', 'reentrant'))
        approve_fixtures(browser_package)
        result = subprocess.run([str(binaries / 'proxy_test.exe'), str(browser_dir / 'assetbrowser.dll')],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.count('reentrant on_load passed') == 1, result.stdout
        assert result.stdout.count('reentrant on_event passed') == 2, result.stdout
        assert 'hello: tools.factory.request: ToolSystem2_001' in result.stdout, result.stdout
        assert 'hammer.factory.request' not in result.stdout, result.stdout
        count += 1
        print('Asset Browser-only bootstrap and reentrant forwarding passed without Hammer.')
        spec = importlib.util.spec_from_file_location('loader', root / 'scripts/loader.py')
        loader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(loader)
        loader.require_closed = lambda: None  # This fixture directory is not a running installation.
        fake_cs2 = directory / 'cs2'
        tools = fake_cs2 / 'game/bin/win64/tools'
        tools.mkdir(parents=True)
        shutil.copy2(binaries / 'hammer_original.dll', tools / 'hammer.dll')
        hammer_before = (tools / 'hammer.dll').read_bytes()
        shutil.copy2(binaries / 'hammer_original.dll', tools.parent / 'assetbrowser.dll')
        before = (tools.parent / 'assetbrowser.dll').read_bytes()
        profiles = [{'module': 'assetbrowser', 'sha256': loader.sha(tools.parent / 'assetbrowser.dll')}]
        preview = loader.install(fake_cs2, root / 'dist', profiles=profiles)
        assert not preview['applied'] and not (tools.parent / 'assetbrowser_original.dll').exists()
        loader.install(fake_cs2, root / 'dist', apply=True, profiles=profiles)
        assert (tools.parent / 'assetbrowser_original.dll').read_bytes() == before
        assert loader.inspect(fake_cs2)['proxy_matches']
        assert loader.inspect(fake_cs2)['module'] == 'assetbrowser'
        assert (tools / 'hammer.dll').read_bytes() == hammer_before
        assert (tools / 'hammer-addons/hammer_addons_ui.dll').exists()
        count += 1
        installed = (tools.parent / 'assetbrowser.dll').read_bytes()
        (tools.parent / 'assetbrowser.dll').write_bytes(b'external update')
        try:
            loader.uninstall(fake_cs2, apply=True)
            raise AssertionError('must refuse to overwrite external update')
        except ValueError as error:
            assert 'externally' in str(error)
        assert (tools.parent / 'assetbrowser.dll').read_bytes() == b'external update'
        (tools.parent / 'assetbrowser.dll').write_bytes(installed)
        count += 1
        loader.uninstall(fake_cs2, apply=True)
        assert (tools.parent / 'assetbrowser.dll').read_bytes() == before
        assert not (tools.parent / 'assetbrowser_original.dll').exists()
        assert not (tools / 'hammer-addons/hammer_addons_ui.dll').exists()
        assert (tools / 'hammer-addons/addons/hello/hello.dll').exists()
        count += 1
        try:
            loader.install(fake_cs2, root / 'dist', apply=True, profiles=[])
            raise AssertionError('must reject an unknown binary')
        except ValueError as error:
            assert 'Unrecognized' in str(error)
        assert (tools.parent / 'assetbrowser.dll').read_bytes() == before
        count += 1
        # The distributed management tool must work without the source checkout.
        packaged = subprocess.run(['python', str(root / 'dist/scripts/loader.py'), 'inspect', '--cs2', str(fake_cs2)], capture_output=True, text=True, timeout=15)
        assert packaged.returncode == 0 and '"installed": false' in packaged.stdout, packaged.stdout + packaged.stderr
        count += 1
        # Migration restores the old Hammer proxy using its field-less state.
        shutil.copy2(binaries / 'hammer_original.dll', tools / 'hammer_original.dll')
        shutil.copy2(binaries / 'hammer.dll', tools / 'hammer.dll')
        old_state = tools / 'hammer-addons/install.json'
        old_state.write_text(json.dumps({'original_sha256': loader.sha(tools / 'hammer_original.dll'),
                                        'proxy_sha256': loader.sha(tools / 'hammer.dll')}))
        assert loader.inspect(fake_cs2)['module'] == 'hammer'
        try:
            loader.install(fake_cs2, root / 'dist', apply=True, profiles=profiles)
            raise AssertionError('must not install a second loader over a legacy installation')
        except ValueError as error:
            assert 'already exists' in str(error)
        loader.uninstall(fake_cs2, apply=True)
        assert (tools / 'hammer.dll').read_bytes() == hammer_before
        loader.install(fake_cs2, root / 'dist', apply=True, profiles=profiles)
        assert loader.inspect(fake_cs2)['module'] == 'assetbrowser'
        assert (tools / 'hammer.dll').read_bytes() == hammer_before
        loader.uninstall(fake_cs2, apply=True)
        count += 1
        # Compile and load a generated third-party add-on against only the public SDK.
        generated = directory / 'generated'
        subprocess.run(['python', str(root / 'dist/scripts/new-addon.py'), 'generated', '--output', str(generated), '--tools', 'hammer,modeldoc'], check=True, capture_output=True, timeout=15)
        assert 'tools=hammer,modeldoc' in (generated / 'addon.ini').read_text()
        cache = (binaries.parent / 'CMakeCache.txt').read_text()
        cmake = re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache, re.M)[1]
        generator = re.search(r'^CMAKE_GENERATOR:INTERNAL=(.+)$', cache, re.M)[1]
        for command in [[cmake, '-S', str(generated), '-B', str(generated / 'build'), '-G', generator, '-A', 'x64', f'-DHAMMER_ADDONS_SDK={root / "dist/sdk"}'],
                        [cmake, '--build', str(generated / 'build'), '--config', 'Release'],
                        [cmake, '--install', str(generated / 'build'), '--config', 'Release', '--prefix', str(package / 'addons')]]:
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
        run(0, 'loaded=2 rejected=0 disabled=1', 'generated: host.test: standalone')
        # Exercise every packaged template, independently of the checkout sources.
        templates = directory / 'templates'
        templates.mkdir()
        project = ['cmake_minimum_required(VERSION 3.24)', 'project(examples LANGUAGES CXX)']
        for template in ['commands', 'panel_settings', 'menu_hooks', 'note_import', 'compile_report', 'tool_console', 'project_context', 'steam_context']:
            ident = 'generated_' + template
            output = templates / ident
            result = subprocess.run(['python', str(root / 'dist/scripts/new-addon.py'), ident,
                                     '--output', str(output), '--template', template], capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, result.stdout + result.stderr
            assert f'id={ident}' in (output / 'addon.ini').read_text()
            project.append(f'add_subdirectory({ident})')
        (templates / 'CMakeLists.txt').write_text('\n'.join(project))
        for command in [[cmake, '-S', str(templates), '-B', str(templates / 'build'), '-G', generator, '-A', 'x64', f'-DHAMMER_ADDONS_SDK={root / "dist/sdk"}'],
                        [cmake, '--build', str(templates / 'build'), '--config', 'Release', '--parallel'],
                        [cmake, '--install', str(templates / 'build'), '--config', 'Release', '--prefix', str(package / 'addons')]]:
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
        run(0, 'loaded=10 rejected=0 disabled=1', 'loaded generated_note_import', 'loaded generated_compile_report', 'loaded generated_tool_console')

    print(f'{count} native integration scenarios passed.')


if __name__ == '__main__':
    main()
