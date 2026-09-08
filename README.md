# Hammer Addons

A native **add-on framework for CS2 Workshop Tools**, with a versioned C SDK
and a shared add-on manager. Hammer remains the main editor focus, while the
framework starts in the Asset Browser and can support tools beyond map editing.

This project provides add-on loading, lifecycle callbacks, diagnostics and the
foundation for verified editor integrations. **Hammer multiplayer will be a
separate project**, built on this framework once it is mature enough.

## Build

Requires Windows x64, Python 3.11+ and Visual Studio 2022 or 2026 with Desktop
development with C++ and CMake tools.

```powershell
.\build.bat -Test
```

The first build downloads the matching Qt 5.15.2 development package (33 MiB)
from Qt's official archive, verifies its pinned SHA256, and caches it under
`build/deps`. Later builds use the cache. The distribution contains our
`assetbrowser.dll` proxy, UI DLL, standalone host, SDK and `hello` sample.
It uses Workshop Tools' existing Qt runtime; no Qt DLLs are installed.
Tests use fixture DLLs and temporary folders, so CS2 is not required.

## Install into Workshop Tools

Close CS2 and Workshop Tools, then substitute your Steam installation path:

```powershell
python scripts/loader.py inspect --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive"
python scripts/loader.py install --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
```

Omit `--apply` to preview. The installer checks the supported Asset Browser
binary, backs it up byte-for-byte, then installs the proxy. Start Workshop Tools
normally: the **Workshop Add-ons** panel appears in the **Asset Browser**, before
you open Hammer. It also appears inside Hammer when you open the editor.

The panel shows **Loader active**, loaded/disabled/failed counts, and each
add-on's version and status. Dock, float or close each panel independently.
Use **Workshop Add-ons > Show add-ons** to reopen it, or **Open add-ons folder**
to access your installed add-ons. Both windows display the same runtime state.

```text
game/bin/win64/
  assetbrowser.dll                 Framework entry point
  assetbrowser_original.dll        Verified original Valve DLL
  tools/
    hammer.dll                     Original Valve Hammer
    hammer-addons/
      install.json                 Module and ownership hashes
      loader.log
      hammer_addons_ui.dll         Shared add-on manager
      addons/hello/
        addon.ini
        hello.dll
```

Each add-on has its own folder under `tools/hammer-addons/addons/`. Drop
compatible add-ons there and restart **Workshop Tools**. Set `enabled=false`
in a manifest to disable one add-on, or create `tools/hammer-addons/disabled`
to disable all add-ons while keeping original tools forwarding and the manager.
Native add-ons run with Workshop Tools' permissions; they are not sandboxed.

### Upgrading an earlier Hammer-only installation

Run the current uninstaller first, then install again. It recognizes old
installation records, restores the original Hammer DLL, and preserves add-on
folders. The new installation starts from Asset Browser. Only one loader entry
point should be installed at a time.

```powershell
python scripts/loader.py uninstall --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
python scripts/loader.py install --cs2 "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" --apply
```

The installer preserves differing existing add-on files by refusing to overwrite
them. To keep a customized sample during an update, omit its folder from your
new distribution before installing.

Uninstall verifies hashes, restores the recorded original module, removes an
unchanged framework UI DLL, and preserves add-ons. After a Steam update, inspect
again. The tool refuses to overwrite a new Valve DLL with an older backup.
Only module-specific builds in `compatibility.json` are accepted.

## Write an add-on

```powershell
python scripts/new-addon.py my_addon --output ../my_addon
cmake -S ../my_addon -B ../my_addon/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_addon/build --config Release
cmake --install ../my_addon/build --config Release --prefix ../my_addon/package
```

Copy `package/my_addon/` into the loader's `addons/` directory. See the
[format and API contract](docs/ADDON_FORMAT.md), [sample](addons/hello/hello.cpp)
and [C header](sdk/include/hammer_addons.h). ABI 1 add-ons remain compatible;
the Asset Browser entry point emits `tools.factory.request` observations.

## Framework direction

The next steps are a richer add-on manager, explicit UI/command registration,
dependency and capability handling, and verified adapters for editor operations.
Hammer integration remains a priority; other Workshop Tools can be supported
through the same framework as their interfaces are validated.

Document editing APIs, add-on-owned panels and hot reload are not implemented.
Multiplayer, shared cursors, synchronization and networking belong in the
separate future Hammer multiplayer project.

See [architecture](docs/ARCHITECTURE.md) and [validation](VALIDATION.md) for the
implemented behavior and test evidence. This is an unofficial project, not a
Valve SDK. It replaces the retired h2mcp application; existing local map projects
and add-on folder names are preserved.
