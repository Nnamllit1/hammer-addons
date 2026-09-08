# Hammer Addons

A native **add-on loader for CS2's Hammer editor**, with a versioned C SDK.
The long-term goal is simultaneous map editing in independent Hammer sessions.
This first version loads native add-ons and exposes logging and interface-request
events; **live document editing and multiplayer are not implemented yet**.

The project was previously h2mcp. The MCP server has been retired in favour of
this native editor framework. Existing local map projects are preserved.

## Build

Requires Windows x64, Python 3.11+ (standard library only), and Visual Studio 2022
or 2026 with Desktop development with C++ and CMake tools.

```powershell
.\build.bat -Test
```

Build outputs are in `dist/`: `hammer.dll`, a standalone test host, the SDK and
the `hello` add-on. Tests use fixture DLLs and a temporary installation; CS2 is
not required to build or run them. No Python virtual environment or MCP client
is needed.

## Install into Hammer

Close CS2 and Workshop Tools. Substitute your Steam installation path:

```powershell
python scripts/loader.py inspect --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive"
python scripts/loader.py install --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
```

Omit `--apply` to preview. The installer checks the exact supported original DLL,
backs it up as `hammer_original.dll`, then installs the proxy as `hammer.dll`.
It never edits instructions inside Valve's binary. Start Workshop Tools normally
and open Hammer. The sample writes to
`game/bin/win64/tools/hammer-addons/loader.log`.

```text
game/bin/win64/tools/
  hammer.dll                   Hammer Addons proxy
  hammer_original.dll          Your original Valve DLL
  hammer-addons/
    install.json               Installation hashes
    loader.log
    addons/
      hello/
        addon.ini
        hello.dll
```

Each add-on has its own folder. Drop compatible add-ons into `addons/` and restart
Hammer. Set `enabled=false` in an add-on's manifest to disable it. Creating a file
named `hammer-addons/disabled` disables all add-ons while retaining forwarding.
Native add-ons run with the editor's permissions; they are not sandboxed.

To restore the original DLL:

```powershell
python scripts/loader.py uninstall --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
```

Restoration verifies hashes and preserves add-on folders. After a Steam update,
inspect the installation again. The tool refuses to overwrite a new Valve DLL
with an older backup. Only builds in `compatibility.json` are accepted; matching
export names alone does not establish compatibility.

## Write an add-on

```powershell
python scripts/new-addon.py my_addon --output ../my_addon
cmake -S ../my_addon -B ../my_addon/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_addon/build --config Release
cmake --install ../my_addon/build --config Release --prefix ../my_addon/package
```

Copy `package/my_addon/` into the loader's `addons/` directory. See the
[format and API contract](docs/ADDON_FORMAT.md),
[sample source](addons/hello/hello.cpp), and
[C header](sdk/include/hammer_addons.h).

## Project direction

The next editor milestone is reading a selected entity and applying a transform
through Hammer's own undo system. Only then can we synchronize operations between
two editors. Cursors, independent cameras, object locking, reconnect recovery,
geometry changes and shared playtests build on that foundation.

See [architecture](docs/ARCHITECTURE.md) and [validation](VALIDATION.md) for what is
implemented and actually tested. This is an unofficial project, not a Valve SDK.
