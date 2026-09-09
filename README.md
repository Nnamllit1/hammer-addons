# Hammer Addons

A native **add-on framework for CS2 Workshop Tools**, with a versioned C SDK
and a shared add-on manager. A portable launcher loads it into a dedicated Workshop Tools session.

The framework provides native DLL loading, lifecycle callbacks, logging,
add-on panels and commands, persistent settings,
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
portable CMD launcher, native helper, runtime/UI DLLs, SDK and `hello` sample.
Legacy proxies and a standalone host remain available for regression testing.
Optional feature examples are built into `dist/examples/addons/`.
It uses Workshop Tools' existing Qt runtime; no Qt DLLs are installed.
Tests use fixture DLLs and temporary folders, so CS2 is not required.

## Launch Workshop Tools with add-ons

Extract **dist/hammer-addons-portable.zip** (or use **dist/portable/**) outside your CS2 installation and double-click
**Launch Workshop Tools.cmd**. The complete folder is required: the CMD script,
launcher EXE, runtime/UI DLLs and addons folder. End users do not need Python or
PowerShell. The launcher detects Steam libraries; alternatively, drag the CS2
folder or cs2.exe onto the CMD file.

Choose a Workshop project if prompted. The launcher remembers successful choices;
use `--choose-project` to change projects. Create a desktop shortcut to the CMD
file, or add **tools_launcher.exe** as a non-Steam game for one-click launching
from your Steam library. See the [launcher guide](docs/LAUNCHER.md).

The launcher starts its own `-tools -insecure` session and loads the framework
from the portable folder. Normal gameplay uses Valve's original files through a
fresh Steam launch. No Valve DLL is replaced and no global loader setting is changed.

**Migrating an older installation:** close CS2 and run the existing uninstaller
first. It restores the verified original DLL and preserves your add-ons. Copy
those add-on folders into the portable package. The launcher refuses old proxy
installations; [migration instructions](docs/LAUNCHER.md#migrating-a-replacement-dll-installation)
explain the paths and command.

Drop compatible native add-ons into the portable `addons/` folder and restart
Workshop Tools. Set `enabled=false` in an add-on manifest to disable it, or create
a `disabled` file beside the portable runtime to disable all add-ons. Native
add-ons execute with the editor's permissions and are not sandboxed.

The **Workshop Add-ons** manager appears in Asset Browser. Hammer keeps it under
**Help > Workshop Add-ons**, hidden until opened. Its Add-ons tab shows loaded,
disabled and failed packages and supports tool filters. The Extensions tab shows
bindings for the current editor, and About links to the project documentation.
Dock, float or close each panel independently. **Open add-ons folder** opens the
portable package's add-ons directory.

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
Factory-request observations belong to the legacy proxy entry point. The portable
launcher does not advertise `HA_CAP_FACTORY_EVENTS`; add-ons requiring that
capability need to be adapted.

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
