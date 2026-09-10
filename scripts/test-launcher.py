"""Test portable packaging and runtime loading into a disposable Qt tools fixture."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--dist', type=Path, help='Distribution to test (defaults to checkout dist)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    distribution = args.dist.resolve() if args.dist else root / 'dist'
    binaries = args.bin.resolve()
    qt = root / 'build/deps/5.15.2/msvc2019_64'
    env = os.environ.copy()
    env['PATH'] = str(qt / 'bin') + os.pathsep + env['PATH']
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    def run(command, expected=0, timeout=100):
        result = subprocess.run(command if isinstance(command, str) else [str(v) for v in command], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=timeout)
        assert result.returncode == expected, (command, result.stdout, result.stderr)
        return result.stdout + result.stderr
    (root / 'build/tests').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='launcher space & test-', dir=root / 'build/tests') as temporary:
        work = Path(temporary)
        env['LOCALAPPDATA'] = str(work / 'localappdata')
        package = work / 'portable'
        for name in ['Launch Workshop Tools.cmd', 'tools_launcher.exe', 'hammer_addons_runtime.dll',
                     'hammer_addons_ui.dll', 'README.md', 'LAUNCHER.md', 'addons/hello/addon.ini', 'addons/hello/hello.dll']:
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(distribution / 'portable' / name, target)
        game = work / 'CS2'
        required = package / 'addons/requires_factory'
        required.mkdir()
        shutil.copy2(binaries / 'requires_factory.dll', required / 'requires_factory.dll')
        (required / 'addon.ini').write_text('[addon]\nformat=1\nid=requires_factory\nversion=0.1.0\nabi=1\nentry=requires_factory.dll\nenabled=true\n')
        fixture = game / 'game/bin/win64' 
        fixture.mkdir(parents=True)
        shutil.copy2(binaries / 'tools_fixture.exe', fixture / 'cs2.exe')
        shutil.copy2(binaries / 'picker_fixture.exe', fixture / 'csgocfg.exe')
        shutil.copy2(binaries / 'hammer_original.dll', fixture / 'assetbrowser.dll')
        help_text = run([package / 'tools_launcher.exe', '--help'])
        assert '--check' in help_text and '--addon' not in help_text and '--choose-project' not in help_text
        for retired in [['--addon', 'old_project'], ['--choose-project']]:
            assert 'Unknown' in run([package / 'tools_launcher.exe', *retired], 1)
        assert 'Unknown' in run([package / 'tools_launcher.exe', '--pid', '123'], 1)
        assert 'Unknown' in run([package / 'tools_launcher.exe', '--dll', 'other.dll'], 1)
        assert 'supported original' in run([package / 'tools_launcher.exe', '--cs2', game, '--check'], 1)
        # Real CMD entry point, including a package path containing spaces and ampersands.
        command = f'""{package / "Launch Workshop Tools.cmd"}" --help"'
        assert '--check' in run(subprocess.list2cmdline([os.environ['COMSPEC']]) + ' /d /s /c ' + command)
        dropped = f'""{package / "Launch Workshop Tools.cmd"}" "{game}" --check"'
        assert 'supported original' in run(subprocess.list2cmdline([os.environ['COMSPEC']]) + ' /d /s /c ' + dropped, 1)
        for mode in ['cancel', 'picker-reject']:
            run([binaries / 'launch_test.exe', fixture / 'csgocfg.exe', package / 'hammer_addons_runtime.dll', mode])
        run([binaries / 'launch_test.exe', fixture / 'cs2.exe', package / 'hammer_addons_runtime.dll', 'reject'])
        assert not (package / 'loader.log').exists(), 'Normal session must not initialize add-ons'
        output = run([binaries / 'launch_test.exe', fixture / 'cs2.exe', package / 'hammer_addons_runtime.dll', 'accept'])
        assert 'Own-process runtime loading' in output
        log = (package / 'loader.log').read_text()
        assert 'loaded hello' in log and 'UI initialization queued' in log
        assert 'factory.request' not in log and 'rejected requires_factory' in log
        output = run([binaries / 'launch_test.exe', fixture / 'csgocfg.exe', package / 'hammer_addons_runtime.dll', 'picker'])
        assert 'Own-process runtime loading' in output
        notes = package / 'addons/picker_notes'
        notes.mkdir()
        shutil.copy2(root / 'addons/picker_notes/addon.ini', notes / 'addon.ini')
        shutil.copy2(binaries / 'picker_notes.dll', notes / 'picker_notes.dll')
        output = run([binaries / 'launch_test.exe', fixture / 'csgocfg.exe', package / 'hammer_addons_runtime.dll', 'picker-ui'])
        assert 'Own-process runtime loading' in output
        log = (package / 'loader.log').read_text()
        assert 'loaded picker_notes' in log and 'UI binding 1 [project_picker]: Attached' in log
        output = run([binaries / 'launch_test.exe', fixture / 'csgocfg.exe', package / 'hammer_addons_runtime.dll', 'picker-cancel'])
        assert 'Picker cancellation passed' in output
    print('Portable launcher passed: CMD quoting, CLI restrictions, original-file validation, normal-session rejection, picker cancellation, selected-child handoff, picker panel/settings interaction, undock/close/reopen recovery and real DLL/UI initialization.')

if __name__ == '__main__':
    main()
