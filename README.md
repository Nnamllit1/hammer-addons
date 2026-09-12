# Hammer Addons

A native **add-on framework for CS2 Workshop Tools**, with a versioned C SDK
and a shared add-on manager for the Workshop project picker and editors. A portable launcher loads it into a dedicated Workshop Tools session.

The framework provides native DLL loading, lifecycle callbacks, logging,
add-on panels and commands, persistent settings,
menu-action hooks, custom file handlers, editor metadata observations, background jobs,
queued editor callbacks, automatic Hammer build-output observation, and a manager with tool filters. Use it to install compatible add-ons or build your
own against the included SDK.

The manager can [load newly installed add-ons and reload opt-in DLLs](docs/HOT_RELOAD.md)
without restarting Workshop Tools. The SDK includes a reloadable counter example.

This is an unofficial project, independent of Valve.

## Included tools

- **Build log report:** automatically group build diagnostics, inspect suggested checks,
  copy asset paths, and reveal project-local sources. Session history compares new
  and resolved diagnostic messages between complete captured builds. Saved text logs
  can also be inspected with background scanning and cancellation.
- **Steam account and friends:** automatically view your Steam identity and friends
  with presence, plus profile and friends overlay shortcuts when available.
  See the [Steam integration guide](docs/STEAM.md).
- **Project context:** view the selected project's folders and find project-local
  source files without configuring paths.
- **Live tool output:** an example of the native log subscription API. Its native
  provider is currently disabled while a live-session stability issue is investigated.

These tools are included in the portable package under **Workshop Add-ons** (under Help
in Hammer). See [build-output capture and SDK](docs/BUILD_OUTPUT.md) and
[native logging status](docs/TOOL_LOGS.md).

## Install and start

**[Download a release](https://github.com/Nnamllit1/hammer-addons/releases)** or start with the
[step-by-step installation guide](docs/INSTALLATION.md).
It covers extracting the portable ZIP, your first launch, installing add-ons,
troubleshooting, and optional desktop and Steam shortcuts.
For command-line options and migration, use the [advanced launcher guide](docs/LAUNCHER.md).

> **Add-ons can contain malware.** Native add-on DLLs run code with your Windows
> account's permissions, much like an EXE. Only install add-ons from sources and
> publishers you trust; a file sent by someone is not automatically safe.
> Add-ons are not sandboxed, and `-insecure` does not protect your computer.

Extract **dist/hammer-addons-portable.zip** (or use **dist/portable/**) outside your CS2 installation and double-click
**Launch Workshop Tools.cmd**. The complete folder is required: the CMD script,
launcher EXE, runtime/UI DLLs and addons folder. End users do not need Python or
PowerShell. The launcher detects Steam libraries; alternatively, drag the CS2
folder or cs2.exe onto the CMD file.

Each launch opens Valve's normal Workshop Tools project picker, just like Steam.
Choose or create a project there, then launch tools. The framework does not select
or remember a project for you. Create a desktop shortcut to the CMD file, or add **tools_launcher.exe** as a non-Steam game for one-click launching
from your Steam library. See the [launcher guide](docs/LAUNCHER.md).

The launcher starts its own `-tools -insecure` session and loads the framework
from the portable folder. Normal gameplay uses Valve's original files through a
fresh Steam launch. No Valve DLL is replaced and no global loader setting is changed.

**Migrating an older installation:** close CS2 and run the existing uninstaller
first. It restores the verified original DLL and preserves your add-ons. Copy
those add-on folders into the portable package. The launcher refuses old proxy
installations; [migration instructions](docs/LAUNCHER.md#migrating-a-replacement-dll-installation)
explain the paths and command.

Drop compatible native add-ons into the portable `addons/` folder and click
**Load new add-ons** in the manager, or restart Workshop Tools. Set `enabled=false` in its manifest to disable it on the next launch, or create
a `disabled` file beside the portable runtime to disable all add-ons. Native
add-ons execute with the editor's permissions and are not sandboxed.

The project picker has a **Workshop Add-ons** button in its status bar for its
add-ons window. The **Workshop Add-ons** manager appears in Asset Browser. Hammer keeps it under
**Help > Workshop Add-ons**, hidden until opened. Its Add-ons tab shows loaded,
disabled and failed packages and supports tool filters. The Extensions tab shows
bindings for the current editor, and About links to the project documentation.
Dock, float or close each panel independently. **Open add-ons folder** opens the
portable package's add-ons directory.

## Build from source

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
appear as **Unspecified**. Editor tags describe intended tools for discovery; they do not assert that an
editor is open or delay DLL startup. Use `project_picker` to explicitly enable
an add-on in the separate project-picker application; existing `all` add-ons
continue to load only in the editor process.
Factory-request observations belong to the legacy proxy entry point. The portable
launcher does not advertise `HA_CAP_FACTORY_EVENTS`; add-ons requiring that
capability need to be adapted.

Use `--template commands`, `--template panel_settings`, `--template menu_hooks`,
`--template note_import`, `--template picker_notes`, `--template editor_watch` or
`--template live_status`, `--template compile_report`, `--template project_context` or `--template tool_console` to start from a feature example. Each SDK feature has
[a corresponding example add-on](docs/EXTENSIONS.md#examples).

Add-ons can observe session/window identity, reported document paths and window
metadata changes, and update read-only panel text. See [editor observations and
live panels](docs/EDITOR_CONTEXT.md), including their limits. These observations
do not expose map objects, unsaved scene operations or save events.

See [project context and source lookup](docs/PROJECT_CONTEXT.md) for project-aware
add-ons. [Panel tables](docs/EXTENSIONS.md#tables) provide selectable read-only rows.

See [background jobs and queued callbacks](docs/JOBS.md) for worker lifecycle,
threading and cancellation contracts.

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
