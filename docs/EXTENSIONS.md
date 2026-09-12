# UI and workflow extensions

Include `hammer_extensions.h` and obtain the versioned table with
`HA_GetExtensions(host)` during `on_load`. Declare required capabilities in
`HA_AddonV1`; return failure if the table or a required registration is unavailable.
The original ABI-1 host prefix is unchanged, so existing compiled add-ons still
load. Extension add-ons require this newer loader.

## Examples

Every public SDK feature has an example:

| Example | Features |
| --- | --- |
| [reload_counter](../addons/reload_counter/reload_counter.cpp) | Opt-in DLL reload, cleanup contract and state preserved through settings |
| [editor_watch](../addons/editor_watch/editor_watch.cpp) | Editor session/window metadata, lifecycle observations, read-only event history |
| [live_status](../addons/live_status/live_status.cpp) | Programmatic label updates without overwriting editable inputs |
| [compile_report](../addons/compile_report/compile_report.cpp) | Automatic Hammer build-output observation; optional saved-log background jobs, cancellation and queued callbacks |
| [tool_console](../addons/tool_console/tool_console.cpp) | Live native tool output, severity filters and subscriptions |
| [steam_context](../addons/steam_context/steam_context.cpp) | Automatic Steam identity/friends snapshots, presence, paginated table and user-triggered overlay actions |
| [project_context](../addons/project_context/project_context.cpp) | Verified project folders and project-local source lookup |
| [hello](../addons/hello/hello.cpp) | Loading, logging, shutdown; factory observations on the legacy proxy |
| [commands](../addons/commands/commands.cpp) | Menu command, shortcut, tool scope, callback feedback |
| [picker_notes](../addons/picker_notes/picker_notes.cpp) | Project-picker opt-in, panel and persistent reminder |
| [panel_settings](../addons/panel_settings/panel_settings.cpp) | Dock panel, label, button, text field, checkbox, choice, persistent settings |
| [menu_hooks](../addons/menu_hooks/menu_hooks.cpp) | Before/after callbacks and suppressing an existing menu action |
| [note_import](../addons/note_import/note_import.cpp) | File extension filters, file callback, extending an import action with a handler chooser |

Builds put optional example DLLs in `dist/examples/addons/`. Copy selected example
folders into the portable package's `addons/` directory and click **Load new
add-ons** in the manager. See [hot loading and reloading](HOT_RELOAD.md).
`hello`, `compile_report`, `project_context`, `steam_context` and `tool_console` are installed by default. Example source is also packaged.

Generate an independently buildable project:

```powershell
python scripts/new-addon.py my_panel --output ../my_panel --template panel_settings
cmake -S ../my_panel -B ../my_panel/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_panel/build --config Release
cmake --install ../my_panel/build --config Release --prefix ../my_panel/package
```

Templates are `hello`, `commands`, `panel_settings`, `menu_hooks` and
`note_import`, `picker_notes`, `editor_watch`, `live_status`, `compile_report`, `project_context`, `steam_context`, `reload_counter` and `tool_console`. `--tools` changes manifest tool tags. Change each
contribution's `tool` and `target` in source to change its UI placement.

## Registration and ownership

Call `register_contribution(host->context, &definition)` during `on_load`
only. Set all descriptor `size` fields. A nonzero handle means the definition
was accepted, not that a target window already exists. Strings and control
descriptors are copied. Callback code and `user` data must remain valid for the
add-on lifetime. IDs use `[a-z][a-z0-9_]{0,63}` and are unique per add-on.
The current limits are 128 contributions per add-on and 64 controls per panel.

Each contribution specifies `all` or one tool ID. Hooks and import routes
require one explicit tool. UI objects belong to their editor window and are
recreated when that window reopens. Add-on DLLs still initialize once per process.
There is no runtime unregister or native hot reload API.

The shared UI adapter recognizes Asset Browser, Hammer, ModelDoc, Material Editor
and Particle Editor window titles. A recognized window must expose Qt
`QMainWindow` menus. Recognition is not an editor document API, and adapters
must be tested against the specific tools build.

## Menus and panels

- `HA_COMMAND`: a menu action; `options` optionally contains a portable
  shortcut such as `Ctrl+Alt+G`. Existing shortcut conflicts leave it waiting.
- `HA_PANEL`: a menu action opens an add-on dock. Controls are `HA_LABEL`,
  `HA_BUTTON`, `HA_TEXT`, `HA_CHECKBOX`, `HA_CHOICE` and read-only `HA_TEXT_VIEW`.
  A checkbox uses `0`/`1`; a choice uses a zero-based index and
  newline-separated option labels.
- For these kinds, `target` is an existing menu path such as `File`.
  An empty target uses the Workshop Add-ons menu.

Panels start hidden. Text changes dispatch on editing finished; checkbox and
choice changes dispatch immediately; buttons dispatch `panel.click`.
Initial values are registration-time snapshots. This version does not expose
widget pointers, arbitrary layouts or embedded web views. Read-only labels and
text views support [live text updates](EDITOR_CONTEXT.md#update-read-only-panel-output).
Settings are shared between windows; editable controls do not automatically
refresh when another window changes a setting.

## Existing actions and import workflows

`HA_MENU_HOOK` targets an existing action, for example `Help/About`.
The adapter replaces its menu entry with a wrapper and transfers its shortcut.
Before callbacks run in add-on discovery/registration order:

- `HA_CONTINUE` lets the next hook run and ultimately invokes the original.
- `HA_HANDLED` suppresses the original action.
- `HA_ERROR` or a busy runtime also stops this invocation.

After callbacks run when the original action's `trigger()` returns, not when
background work started by that action finishes. They do not run for a suppressed
action. Original enabled/visible state and shortcut changes are mirrored; the
native menu entry and shortcut are restored when the wrapper is removed.

`HA_IMPORTER` adds an independent file-picker command. `options` lists
lowercase extensions without dots, for example `hanote` or `foo,bar`.
The callback receives the selected absolute UTF-8 path in `event->value`.
The runtime checks file existence and extension. The add-on must validate contents
and perform its own conversion or import. Cancelling does not invoke the callback.

`HA_IMPORT_ROUTE` wraps an existing import action. Activating it opens a
chooser containing the built-in importer and each registered add-on handler.
The built-in option invokes the preserved native action. A custom option opens
that handler's filtered picker. If menu hooks also target this action, their
after phase refers to opening the chooser, not completing the chosen importer.

The note example reads a tiny text file and displays its text in the status bar.
It does not create editor assets or convert models. Its ModelDoc `File/Import`
route demonstrates the contract in the integration fixture; that exact native
menu path is **not yet verified**. A missing target stays visibly waiting.
The independent Asset Browser reader can use `dist/examples/example.hanote`.

These are **Qt menu-action hooks**, not arbitrary engine function interception.
Toolbar buttons or direct native calls retaining the original action bypass the
wrapper. Checkable actions, action groups, submenus, and ambiguous targets are
unsupported. Extending a particular native importer beyond its menu requires a
separately verified editor adapter. Blender conversion is not included.

## Target diagnostics

Paths are slash-separated, case-sensitive labels. Mnemonics (`&`), trailing
ellipses and shortcut text are ignored. Prefix a segment with `@` to match its
Qt object name instead. Localization and tools updates can change labels.

Missing or ambiguous targets are retried as menus appear; the adapter never
guesses another location. Open the manager's **Extensions** tab to see registrations
for that window, target paths, and attached/waiting status. The status is also
written to `loader.log`. Manifest filters apply to the Add-ons list; the
Extensions tab describes the current window.

## Callbacks and settings

UI callbacks run on Qt's GUI thread, serialized with other add-on callbacks.
Keep them short. Use the [jobs and queued callback API](JOBS.md) for background work, progress,
cancellation and delivery back to the editor thread. Do not block the UI
waiting for a worker that needs the UI thread.

Event strings and the response buffer are borrowed for the call. Return
`HA_HANDLED` on successful commands/imports and write optional, NUL-terminated
UTF-8 feedback within `capacity`. Feedback appears in the editor status bar.
Do not throw across the C ABI. The host catches ordinary C++ callback exceptions,
marks that add-on failed and removes its contributions on the next refresh.
Native crashes are not isolated.

Settings live under `%LOCALAPPDATA%\HammerAddons\settings\<addon-id>\`.
Keys follow the same ID rules; values are strings of at most 4096 bytes.
`get_setting` returns required bytes including NUL, 1 for an absent key's empty
string, or 0 on error. Undersized buffers are untouched. `set_setting` returns
1 on success and 0 on failure. Writes use a unique sibling temporary file per write and atomic
replacement. A stale `.pending` file from an interrupted session does not block
later saves. Concurrent sessions use last-successful-replacement wins semantics;
I/O or sharing failures still return 0, so check the result. Failed writes clean
up their own temporary files and leave the previous setting intact.
Persist during normal execution because editor exit does not guarantee shutdown
callbacks.

## Project-picker add-ons

Use `tools=project_picker` in the manifest and `project_picker` as the contribution
scope. The picker gains a **Workshop Add-ons** button which opens
an owned add-ons window. Panels, commands and settings use the same SDK there.
Targets refer to this add-ons window's menus; Valve's project-list widgets and
Launch Tools button are not currently exposed as SDK hooks. Project creation,
selection and launch remain Valve's own UI.

`picker_notes` demonstrates a reminder saved between launches. In the picker,
click **Workshop Add-ons**, then choose
**Workshop Add-ons > Project picker notes** to open the example panel. Generate a project:

```powershell
python scripts/new-addon.py my_picker --template picker_notes --output ../my_picker
```

Build it using the CMake steps above, then copy its package into `addons/` and
restart the launcher. The picker runtime needs no additional DLL in the package.

Editor metadata observers use `HA_EDITOR_OBSERVER`. See the [editor observation
contract and examples](EDITOR_CONTEXT.md) for delivery, identity and limitations.

[Build output](BUILD_OUTPUT.md) exposes Hammer build-dialog snapshots through
`HA_BUILD_OBSERVER` and `HA_CAP_BUILD_OUTPUT`. The `compile_report` example uses
this automatically. [Native tool logging](TOOL_LOGS.md) is a separate subscription
API; its production provider is currently disabled pending live stability checks.


## Tables

Declare `HA_CAP_TABLES` (8192) and add an `HA_TABLE` control to a panel. Its options
are tab-separated column headings (up to eight). Each value line is a stable row
ID followed by tab-separated cells, for example `missing_material\tError\t2\tMissing material`.
IDs follow contribution-ID syntax and must be unique. Tables allow at most 64 rows
and the existing 4096-byte panel-value limit. Tabs/newlines are separators, not
escapable cell content; replace them with spaces in messages before adding rows.

Update rows with `HA_SetPanelText`. Selecting a row emits `panel.change` with the
control ID and the selected row ID in `event->value`. Selection survives refreshes
when the same ID remains. Programmatic refreshes do not emit selection callbacks;
stale selections for removed rows are rejected. Treat row IDs as identifiers,
not row indices. The `compile_report` example demonstrates a Problems table.

Panels scroll vertically when their controls exceed the available dock space.
Project-aware extensions can use the [project context API](PROJECT_CONTEXT.md).

Steam identity, friends, presence and user-triggered overlay actions are available
through the [Steam API](STEAM.md) and the bundled `steam_context` example.
