# Build your first add-on

## Build the framework and SDK

On Windows x64, install Python 3.11+ and Visual Studio 2022 or 2026 with
**Desktop development with C++** and CMake tools. Clone the
[repository](https://github.com/Nnamllit1/hammer-addons), then run from its root:

```powershell
.\build.bat -Test
```

The first build downloads and verifies the matching Qt development package.
Native tests use fixture DLLs, so CS2 is not required to build or run them.
The packaged SDK is in `dist/sdk/`; example DLLs are in `dist/examples/addons/`.

## Generate a project

From the repository root:

```powershell
python scripts/new-addon.py my_addon --output ../my_addon --tools hammer,modeldoc
cmake -S ../my_addon -B ../my_addon/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_addon/build --config Release
cmake --install ../my_addon/build --config Release --prefix ../my_addon/package
```

Copy `../my_addon/package/my_addon/` into the portable framework's `addons/`
directory. Open **Workshop Add-ons** and click **Load new add-ons**.
See [installation](INSTALLATION.md) for starting the tools.

## Choose an example

Add `--template panel_settings` to start with a dock panel and persistent settings,
or `--template reload_counter` for an explicit reload lifecycle example.
The [example catalog](EXTENSIONS.md#examples) lists all templates and the APIs
they demonstrate.

Read the [package and ABI contract](ADDON_FORMAT.md),
[callback failure rules](FAILURE_HANDLING.md), and
[reload lifecycle](HOT_RELOAD.md) before extending an example with private
threads or callbacks. Tool tags identify intended applications; they do not
provide document-editing APIs or delay DLL initialization until an editor opens.
