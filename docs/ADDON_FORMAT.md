# Hammer Addons format 1 / native ABI 1

An add-on is a directory inside the portable package's `addons/` folder.
Older proxy installations used `game/bin/win64/tools/hammer-addons/addons/`.
The directory name must match its manifest ID. For example:

```text
hello/
  addon.ini
  hello.dll
```

```ini
[addon]
format=1
id=hello
version=0.1.0
abi=1
entry=hello.dll
enabled=true
tools=asset_browser,hammer
```

The manifest is ASCII, at most 16 KiB, with six required keys and the optional `tools` key. Blank lines
and full-line `#` comments are allowed. Duplicate keys, unknown keys and sections
are rejected. IDs use `[a-z][a-z0-9_]{0,63}`, versions use three numeric components,
and `entry` is a simple DLL filename with letters, digits, underscores or hyphens.
No subdirectories, absolute paths or junctions/symlinks are accepted. This is a
folder format, not a ZIP installer. Add-ons are discovered in sorted folder order.

## Tool tags

The optional `tools` field is a comma-separated list of intended tools:

| ID | Manager label |
| --- | --- |
| `project_picker` | Workshop project picker (explicit process opt-in) |
| `asset_browser` | Asset Browser |
| `hammer` | Hammer |
| `modeldoc` | ModelDoc / Model Viewer |
| `material_editor` | Material Editor |
| `particle_editor` | Particle Editor |
| `all` | All editors in the CS2 tools process (must be used alone) |

Whitespace around IDs is ignored. Empty, duplicate and unknown tags are rejected.
Old format-1 manifests without this field remain valid and display as Unspecified.
The native ABI stays at version 1. Older loader builds that only accept six keys
must be upgraded before installing a tagged manifest.

Editor tags are author-declared metadata for display/filtering, not capability
checks. They do not postpone on_load until a named editor opens or guarantee an
editor API exists. Add-ons must handle editor readiness themselves.

`project_picker` explicitly opts into the separate `csgocfg.exe` application.
Picker-only add-ons are skipped in the CS2 editor process; manifests such as
`tools=project_picker,asset_browser` enable loading in both processes. Existing
`all` and untagged add-ons keep running only in the editor process. This exception
prevents existing DLLs from unexpectedly starting inside the picker.
Each process has its own add-on instances; settings use the same per-user store.
The C ABI remains version 1. This is activation routing, not a security sandbox.
Legacy proxy/standalone hosts do not provide this portable process routing.

All-editor add-ons appear under each editor filter, but not the picker filter.
Untagged add-ons appear under All add-ons and Unspecified. Counts follow the filter.

Build an x64 DLL exporting `HA_Query(uint32_t)` using `sdk/include/hammer_addons.h`.
Return a static `HA_AddonV1` for ABI 1 and null for unsupported ABIs. The loader
checks the struct size, ABI, ID, required capabilities and on_load callback.
`on_load` receives a stable host table; return 1 on success, 0 on failure.
Clean up partial initialization before returning failure.

Host capabilities (query the current host; not every entry point exposes all flags):

| Flag | Available functionality |
| --- | --- |
| `HA_CAP_LOGGING` | `host->log(context, message)` and the add-on directory |
| `HA_CAP_FACTORY_EVENTS` | Legacy proxy only: `tools.factory.request`, with the requested interface name as value |
| `HA_CAP_UI` | Commands, shortcuts and standard-control dock panels |
| `HA_CAP_SETTINGS` | Per-user, per-add-on string settings |
| `HA_CAP_MENU_HOOKS` | Before/after and suppression of exposed menu actions |
| `HA_CAP_IMPORTERS` | Filtered file handlers and importer-choice routes |
| `HA_CAP_EDITOR_EVENTS` | Session/window metadata observations (not scene edits) |
| `HA_CAP_LIVE_PANELS` | Updating owned read-only labels and text views |

The portable launcher does not advertise factory observations. Its add-ons initialize
on the runtime startup thread; UI callbacks retain their GUI-thread contract.
The legacy Asset Browser proxy emits these events after Valve's
`CreateInterface` returns. They are observations of interface requests, **not document-change events or proof that the editor UI
is initialized**. `host.test` is emitted only by the standalone test host.
Include `hammer_extensions.h` for the [UI and workflow extension contract](EXTENSIONS.md).
Its optional host-table pointer is appended after the original ABI-1 prefix;
compiled older add-ons remain compatible. Map editing, cursor and selection APIs
are not implemented.

Lifecycle and factory callbacks run on the invoking thread, which is not
guaranteed to be the editor UI thread. UI extension callbacks run on the Qt GUI
thread. All callback dispatch is serialized, and busy factory observations are skipped. Do not block it or manipulate undocumented
editor objects from these callbacks. Calling the proxy factory from a callback
still forwards the request to Valve and preserves its pointer/result. A
process-wide, nonblocking observation guard suppresses additional notifications
and initialization while add-on work is already running, including calls made
by another thread. Factory events are therefore best-effort observations, not
a complete trace of every request. This prevents loader-induced recursion and
initialization deadlocks; it cannot fix a loop entirely inside an add-on.
Host logging is thread-safe. No C++ objects, STL containers, exceptions or ownership cross the ABI.
All event strings are borrowed. The host table lives for the runtime's lifetime.

DLLs remain loaded until process exit; changing an add-on requires restarting
Workshop Tools. Explicit `on_shutdown` is available in the standalone host, but is not
called from DLL_PROCESS_DETACH or guaranteed at editor termination. Native hot
reload requires a future protocol for detaching hooks, callbacks and threads.
Flush important state during normal execution.

Set `enabled=false` to disable an add-on, or create `disabled` beside the runtime DLL to
disable all add-ons while retaining original tools forwarding. Restart Workshop Tools.
The Workshop Add-ons dock lists loaded, disabled and failed add-ons with reasons.
Its top menu reopens the dock and opens the add-ons folder. Diagnostics also go to `hammer-addons/loader.log`, stdout and OutputDebugString.

Add-ons execute native code with the editor's permissions. ABI checks do not
sandbox DLLs, and their DllMain runs before HA_Query validation. Missing DLLs,
invalid manifests, incompatible APIs and ordinary C++ callback exceptions are
reported; access violations or memory corruption can still crash the editor.
Dependencies are loaded from the add-on DLL's directory and Windows System32.

The legacy Hammer proxy emitted `hammer.factory.request`. New installations
emit `tools.factory.request` from Asset Browser. Neither is a document-change
event. The native ABI remains version 1; add-ons filtering event names should
handle the new name as appropriate.
