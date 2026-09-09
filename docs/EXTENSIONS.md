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
| [hello](../addons/hello/hello.cpp) | Loading, logging, shutdown; factory observations on the legacy proxy |
| [commands](../addons/commands/commands.cpp) | Menu command, shortcut, tool scope, callback feedback |
| [panel_settings](../addons/panel_settings/panel_settings.cpp) | Dock panel, label, button, text field, checkbox, choice, persistent settings |
| [menu_hooks](../addons/menu_hooks/menu_hooks.cpp) | Before/after callbacks and suppressing an existing menu action |
| [note_import](../addons/note_import/note_import.cpp) | File extension filters, file callback, extending an import action with a handler chooser |

Builds put optional example DLLs in `dist/examples/addons/`. Copy selected example
folders into the portable package's `addons/` directory and restart Workshop
Tools. Only `hello` is installed by default. Example source is also packaged.

Generate an independently buildable project:

```powershell
python scripts/new-addon.py my_panel --output ../my_panel --template panel_settings
cmake -S ../my_panel -B ../my_panel/build -A x64 -DHAMMER_ADDONS_SDK="$PWD/sdk"
cmake --build ../my_panel/build --config Release
cmake --install ../my_panel/build --config Release --prefix ../my_panel/package
```

Templates are `hello`, `commands`, `panel_settings`, `menu_hooks` and
`note_import`. `--tools` changes manifest discovery tags. Change each
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
  `HA_BUTTON`, `HA_TEXT`, `HA_CHECKBOX` and `HA_CHOICE`.
  A checkbox uses `0`/`1`; a choice uses a zero-based index and
  newline-separated option labels.
- For these kinds, `target` is an existing menu path such as `File`.
  An empty target uses the Workshop Add-ons menu.

Panels start hidden. Text changes dispatch on editing finished; checkbox and
choice changes dispatch immediately; buttons dispatch `panel.click`.
Initial values are registration-time snapshots. This version does not expose
widget pointers, arbitrary layouts, embedded web views, or programmatic control
updates. Settings are shared between windows; already-open controls do not
automatically refresh when another window changes a setting.

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
Keep them short. A worker may perform independent conversion work, but this
version has no job/progress or GUI-thread completion API. Do not block the UI
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
1 on success and 0 on failure. Writes use an exclusive temporary file and atomic
replacement. Concurrent sessions can reject a competing write; check the result.
Persist during normal execution because editor exit does not guarantee shutdown
callbacks.
