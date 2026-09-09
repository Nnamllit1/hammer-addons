# Hammer Addons

A native **add-on framework for CS2 Workshop Tools**, with a versioned C SDK
and a shared add-on manager. It loads once through Asset Browser and hosts
add-ons in the Workshop Tools process.

The framework provides native DLL loading, lifecycle callbacks, logging,
interface-request observations, add-on panels and commands, persistent settings,
menu-action hooks, custom file handlers, and a manager with tool filters. Use it to install compatible add-ons or build your
own against the included SDK.

This is an unofficial project, independent of Valve.

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
Optional feature examples are built into `dist/examples/addons/`.
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
normally: the **Workshop Add-ons** panel appears in the **Asset Browser**.
In Hammer, open it through **Help > Workshop Add-ons > Show add-ons**.

The panel shows **Loader active**, loaded/disabled/failed counts, and each
add-on's version and status. Dock, float or close each panel independently.
In Asset Browser, use **Workshop Add-ons > Show add-ons** to reopen it.
In Hammer, the menu lives under **Help**, with no extra top-level menu or
automatically opened dock. Both menus also offer **About Hammer Addons**, which
opens the About tab with the framework version and project link.
**Open add-ons folder** opens your installed add-ons.

The **Tool** filter and **Tools** column distinguish Asset Browser, Hammer,
ModelDoc / Model Viewer, Material Editor and Particle Editor add-ons. Hammer
defaults to its own filter. Counts reflect the displayed list; changing a filter
does not load or unload DLLs.

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
python scripts/new-addon.py my_addon --output ../my_addon --tools hammer,modeldoc
cmake -S ../my_addon -B ../my_addon/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_addon/build --config Release
cmake --install ../my_addon/build --config Release --prefix ../my_addon/package
```

Copy `package/my_addon/` into the loader's `addons/` directory. See the
[format and API contract](docs/ADDON_FORMAT.md), [sample](addons/hello/hello.cpp)
and [C header](sdk/include/hammer_addons.h). ABI 1 add-ons remain compatible;
add an optional `tools=asset_browser,hammer` manifest field to declare
supported tools, or `tools=all` for a general add-on. Untagged older add-ons
appear as **Unspecified**. These tags describe intended tools for discovery;
they do not assert that an editor is open or delay DLL startup.
The Asset Browser entry point emits `tools.factory.request` observations.

Use `--template commands`, `--template panel_settings`, `--template menu_hooks`
or `--template note_import` to start from a feature example. Each SDK feature has
[a corresponding example add-on](docs/EXTENSIONS.md#examples).

## Current capabilities and limitations

The manager is available in Asset Browser and through Hammer's Help menu.
Tool tags identify an add-on's intended use; they do not provide editor APIs
or imply that an integration exists for every listed tool.

Add-ons can register commands and shortcuts, create dock panels with standard
controls, persist settings, wrap existing menu actions, and handle selected file
formats. The **Extensions** tab shows attached and waiting registrations for the
current window. See the [extension API and examples](docs/EXTENSIONS.md).

Hooks extend exposed Qt menu actions. Arbitrary native function interception,
document editing APIs, model conversion, and hot reload are not implemented.
A specific importer needs a verified action target and its own conversion logic.
Add-on changes require restarting Workshop Tools.

See the [API contract](docs/ADDON_FORMAT.md) for callback and threading rules,
[architecture](docs/ARCHITECTURE.md) for implementation details, and
[validation](VALIDATION.md) for tested builds and behavior.
