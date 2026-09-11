# Advanced Workshop Tools launcher guide

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
  compile_report/
    addon.ini
    compile_report.dll
  tool_console/
    addon.ini
    tool_console.dll
```

Every launch opens Valve's `csgocfg.exe` project picker, the same program used by
Steam's Workshop Tools entry. Select or create a Workshop project there, then
click **Launch Tools**. Closing the picker cancels without starting the CS2 editor process.
Explicitly opted-in picker add-ons can already run in that window.
There is no launcher project list, automatic single-project selection, saved
project choice or opt-in picker flag. Old `launcher-project.txt` files are ignored.
Workshop projects are separate from native framework add-ons in the portable package.

## Desktop and Steam

Create a desktop shortcut to the CMD file using Windows Explorer. Moving the
package requires updating shortcuts. Remove any old `--addon` or
`--choose-project` arguments; these options have been retired.

For Steam, choose **Games > Add a Non-Steam Game to My Library**, browse to
`tools_launcher.exe`, and name the entry **CS2 Workshop Tools with Add-ons**.
Use the executable for Steam's shortcut; it performs the same launch as the CMD
file. Optional arguments go in that shortcut's Launch Options. This creates a
separate library entry; it does not replace Valve's existing CS2 Play choices.

## Commands

```bat
"Launch Workshop Tools.cmd"
"Launch Workshop Tools.cmd" "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive"
tools_launcher.exe --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --check
```

`--check` validates the package and the supported original Asset Browser hash,
then exits without starting a process or opening the project picker. The CMD
window stays open on an error so its explanation can be read.

## Migrating a replacement-DLL installation

This section applies only to the older installer that replaced a Valve DLL.
The portable ZIP does not include that installer; use your previous full checkout
or distribution for the command below.

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

The native helper starts Valve's `csgocfg.exe` with the installed Steam tools
arguments (`-steam -retail -gpuraytracing -vulkan`) plus `-insecure -nop4`.
Valve's picker supplies the selected project and tools arguments to its CS2 child. It refuses to launch while
any `cs2.exe` or `csgocfg.exe` is already running. There is no PID-attach option, arbitrary DLL
option, background watcher, registry modification or game-file replacement.

A Windows job owns the picker and its children during startup. Windows debug
creation events identify the CS2 child created by this picker before it starts.
The picker runtime initializes while debug events continue to be serviced.
The helper verifies the child's executable path and job membership, then detaches
startup tracking before loading the runtime. It does not attach to an existing PID.
There is no deadline while the user is choosing a project.

The helper asks Windows to load
`hammer_addons_runtime.dll` from the portable folder, resolves its explicit
initialization export, and starts it outside DllMain. The runtime independently
requires `cs2.exe` with both tools/insecure arguments, or `csgocfg.exe` with
`-insecure` for explicitly opted-in picker add-ons. Failed or
timed-out startup terminates only the helper's own child. Successful startup
releases the job's kill-on-close policy so the launcher can exit.

The editor runtime waits for Asset Browser and Qt; the picker waits for Qt.
Each loads its applicable native add-ons and queues the UI
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

Closing the project picker normally cancels startup. An abnormal picker exit
instead reports its hexadecimal exit code as a launch failure; include that
code when reporting a crash.

First-use package and publisher approval happens before starting the picker, so
it is outside the process-startup deadline. Unapproved or modified packages remain
unloaded. See [signatures, local approvals and CI keys](SIGNING.md).

The package must be writable for logs and local approval records. Keep it outside
CS2 and do not use junction/symlink paths. Steam must already be available for the
normal CS2 startup flow. If CS2 exits or relaunches into another process, the
helper reports failure rather than attaching to that other process.

After project selection, the startup deadline is approximately 60 seconds for the supported Qt tools
application, plus the Windows loading steps. Antivirus or process restrictions may
reject runtime loading; report the error rather than changing security settings.

## References

- [Steam: adding a non-Steam shortcut](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)
- [Microsoft process creation debug events](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events)
- [Microsoft CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
- [Microsoft DLL entry-point constraints](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-entry-point-function)
