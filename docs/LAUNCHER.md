# Portable Workshop Tools launcher

Extract the complete portable package outside the CS2 installation and double-click
**Launch Workshop Tools.cmd**. Windows CMD calls the bundled native executable.
Python, PowerShell and a separate runtime installation are not required.

The launcher finds CS2 through Steam's current-user registry entry and
`steamapps/libraryfolders.vdf`. If detection fails, drag the CS2 installation
folder or `cs2.exe` onto the CMD file. Keep the CMD file beside the supplied EXE,
both DLLs and the `addons` folder.

```text
Launch Workshop Tools.cmd
tools_launcher.exe
hammer_addons_runtime.dll
hammer_addons_ui.dll
addons/
  hello/
    addon.ini
    hello.dll
```

Choose a Workshop project on the first launch if several exist. Successful launches
remember that choice in `launcher-project.txt`. Use `--choose-project` to
choose again, or `--addon my_project` to specify it explicitly. The project list
comes from `content/csgo_addons`; these are Valve Workshop projects, separate
from native framework add-ons in the portable package.

## Desktop and Steam

Create a desktop shortcut to the CMD file using Windows Explorer. Its target can
also include `--addon my_project`. Moving the package requires updating shortcuts.

For Steam, choose **Games > Add a Non-Steam Game to My Library**, browse to
`tools_launcher.exe`, and name the entry **CS2 Workshop Tools with Add-ons**.
Use the executable for Steam's shortcut; it performs the same launch as the CMD
file. Optional arguments go in that shortcut's Launch Options. This creates a
separate library entry; it does not replace Valve's existing CS2 Play choices.

## Commands

```bat
"Launch Workshop Tools.cmd"
"Launch Workshop Tools.cmd" --addon my_project
"Launch Workshop Tools.cmd" --choose-project
"Launch Workshop Tools.cmd" "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive"
tools_launcher.exe --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --check
```

`--check` validates the package and the supported original Asset Browser hash,
then exits without starting a process or changing the remembered project. The CMD
window stays open on an error so its explanation can be read.

## Migrating a replacement-DLL installation

Close CS2 and Workshop Tools. From the full framework checkout/distribution, run:

```bat
python scripts/loader.py uninstall --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
```

The existing uninstaller verifies the backup and restores Valve's original DLL.
It preserves native add-ons under `game/bin/win64/tools/hammer-addons/addons`.
Copy the desired add-on folders into the portable package's `addons` directory;
preserve any files you customized. Restart through the new launcher.

The launcher refuses to run while old installation records or proxy backups remain,
when a `cs2.exe.local` override exists, or when Asset Browser does not match
a supported original hash. Steam updates may require a new compatibility profile
and launcher build. The old installer is retained for restoration and regression
testing; replacing a game DLL is not the portable launch workflow.

## Process boundaries

The native helper creates its own CS2 process with fixed `-tools -insecure -nop4`
arguments and an optional validated Workshop project. It refuses to launch while
any `cs2.exe` is already running. There is no PID-attach option, arbitrary DLL
option, background watcher, registry modification or game-file replacement.

A Windows job owns the child during startup. The helper asks Windows to load
`hammer_addons_runtime.dll` from the portable folder, resolves its explicit
initialization export, and starts it outside DllMain. The runtime independently
requires the CS2 executable name and both tools/insecure arguments. Failed or
timed-out startup terminates only the helper's own child. Successful startup
releases the job's kill-on-close policy so the launcher can exit.

The runtime waits for Asset Browser and Qt, loads native add-ons, and queues the UI
on the editor thread. DLLs are ordinary visible modules loaded using Windows APIs;
there is no manual mapping, executable patching or anti-cheat bypass. Normal
gameplay uses a fresh, ordinary Steam launch with Valve's original files.
Live tools compatibility does not constitute a VAC compatibility certification.

Existing panel, command, settings, menu-hook and importer APIs remain available.
The portable runtime does not proxy CreateInterface and does not advertise
`HA_CAP_FACTORY_EVENTS`. Add-ons requiring factory observations are rejected;
optional users of those observations must check capabilities.

## Diagnostics and limits

`loader.log` lives beside the portable runtime. The manager's Add-ons and
Extensions tabs show load and binding status. Per-add-on settings retain their
existing per-user storage.

The package must be writable for logs and the remembered project. Keep it outside
CS2 and do not use junction/symlink paths. Steam must already be available for the
normal CS2 startup flow. If CS2 exits or relaunches into another process, the
helper reports failure rather than attaching to that other process.

The startup deadline is approximately 60 seconds for the supported Qt tools
application, plus the Windows loading steps. Antivirus or process restrictions may
reject runtime loading; report the error rather than changing security settings.

## References

- [Steam: adding a non-Steam shortcut](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)
- [Microsoft CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
- [Microsoft DLL entry-point constraints](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-entry-point-function)
