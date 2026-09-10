# Validation: 2026-09-07 through 2026-09-08

## Native build and integration

- Windows x64 Release build passed with Visual Studio 2026, MSVC 19.51 and Windows SDK 10.0.26100.0.
- **20 integration scenarios passed**, using actual compiled DLLs and temporary directories.
- Proxy tests exercised all six exports by name, ordinal availability, concurrent first resolution, integer/floating/mixed arguments, stack arguments and pointer writes.
- The real sample DLL loaded, logged events and shut down in the standalone host.
- Disabled add-ons/global disable, malformed/duplicate manifests, unsupported ABI, ID mismatch, path traversal and missing DLLs were handled without breaking the other sample.
- Installer tests checked preview, exact original backup, proxy installation, hash inspection, refusal to overwrite external updates, restoration, preservation of add-ons and rejection of unknown builds.
- A newly generated third-party add-on compiled against only the packaged SDK, installed beside the sample, and received events successfully. The packaged installer ran independently of source-only modules.

## Actual CS2 Workshop Tools

- Inspected the installed `tools/hammer.dll`: x64, six named exports with ordinals 1..6. The original SHA256 is recorded in `compatibility.json`.
- Installed the proxy through `scripts/loader.py install --apply`, preserving and verifying `hammer_original.dll`.
- Started the user's CS2 Workshop Tools with the existing `h2mcp_demo` addon.
- Process 36660 presented a window titled `Hammer`. Its loader log recorded:

```text
36660 [hammer-addons] hello: Hello from a native Hammer add-on!
36660 [hammer-addons] loaded hello 0.1.0
36660 [hammer-addons] hello: hammer.factory.request: ToolSystem2_001
```

This verifies native add-on execution inside the real editor and forwarding of its
factory request. It does not verify document manipulation, map saving, collaborative
editing, arbitrary third-party add-ons, or compatibility with other Hammer builds.

## Migration

The Python MCP application and launchers were removed. The global `h2mcp` Codex
MCP registration was removed using `codex mcp remove h2mcp`. Existing local projects
were preserved; the pre-pivot working source was archived privately at
`.h2mcp/before-native-pivot.zip`, including uncommitted work. Neither maps nor that
archive are included in the public repository.

## Initial migration build verification, 2026-09-08

- Final installed proxy SHA256: `6e8fb57231c2a47b1c52111f5066d74ef6de778f8c2c463cea87f463a46b8997`.
- The uninstaller restored the original on the real installation before this final build was installed. Subsequent inspection reported both `proxy_matches=true` and `original_matches=true`.
- Opened Hammer using its Asset Browser toolbar icon. Process 22720 logged the sample greeting, successful add-on load and `ToolSystem2_001` request with the final proxy installed.
- Visually inspected the running Hammer window with its normal toolbars and empty document area. No document editing or map-save claim is made.
- The public repository is now [Nnamllit1/hammer-addons](https://github.com/Nnamllit1/hammer-addons). The [hosted Windows build](https://github.com/Nnamllit1/hammer-addons/actions/runs/34180704898) passed for implementation commit `fbd4510`, including all 20 integration scenarios.

## In-editor UI and factory reentrancy, 2026-09-08

- Windows x64 Release build passed after adding the UI DLL; all **21 native integration scenarios** and the separate Qt UI integration executable passed.
- A real fixture add-on calls the proxy factory from both on_load and on_event, directly and from a worker thread that the callback joins. It verifies Valve's pointer and error result for existing/missing interfaces, one load callback and exactly two outer event callbacks. The regression has a subprocess timeout to detect deadlocks.
- Native status snapshots were parsed and checked against actual loaded/rejected/disabled results, including invalid manifests, missing DLLs, ABI failure and global disable.
- UI tests verified GUI-thread creation from a worker, waiting for the editor window, excluding Asset Browser, status rows/counts, skipping busy snapshots, duplicate-start prevention, close/menu reopen, and recreation after closing the editor.
- The installer packaged our UI DLL, recorded its hash, and removed it on restoration only when unchanged. Existing add-on files were preserved.
- Installed the final proxy and UI in the supported real CS2 installation. The existing hello sample binary and manifest were preserved during this loader update. No Qt DLLs were copied into CS2.
- In process 24536, visually verified the Hammer Addons top menu and bottom dock. Closed the dock and reopened it with the real Show add-ons menu action.
- After correcting label encoding, reopened Hammer in process 6784 and visually verified **Loader active**, **1 loaded | 0 disabled | 0 failed**, and **hello / 0.1.0 / Loaded / Ready**. Left this empty editor session open.
- Final proxy SHA256: `b4649db27e549e6d74ed96d5a1c728eaa0e8184bc5f7e3af38d7c99cfff3b08b`.
- Final UI SHA256: `e746f838fece4d35bcaa36665a1659829b5123323fa6001d98bd491c299c39f2`.
- Local screenshot: `build/evidence/hammer-addons-panel.png` (ignored build artifact).
- The hosted CI result above belongs to the earlier migration. These new UI/reentrancy results were verified locally; document editing and collaboration remain future work.

## Shared Workshop Tools manager and Asset Browser startup, 2026-09-08

This supersedes the earlier Hammer-only installation described above.

- The default distribution now installs the Asset Browser proxy. Module-specific compatibility checks accept the inspected original hash `eeb93ce111642027ffd91d6ae6bed4eae7973b70d29e74d64ba80ba9e714b3c8`.
- **23 native scenarios and the multi-window Qt UI integration test passed** with `build.bat -Test`.
- The Asset Browser fixture starts the runtime with no Hammer DLL present, forwards all six exports, and passes the same direct/joined-worker reentrancy regression.
- Migration tests restored a legacy Hammer installation with no module field, refused a second simultaneous installation, installed Asset Browser, and verified the original Hammer stayed unchanged.
- UI tests covered Asset Browser before Hammer, simultaneous independent panels, shared status refresh, close/menu reopen in each window, exclusion of unrelated windows, and editor recreation.
- Migrated the real installation using the current installer. Original Hammer SHA256 matches the preserved Valve hash; no Hammer proxy or Hammer backup remains installed. Existing hello DLL and manifest were preserved.
- Started Workshop Tools as process 13276. Before opening Hammer, its loaded modules included `assetbrowser.dll`, `assetbrowser_original.dll` and the manager UI, with no Hammer DLL loaded.
- Visually verified the Workshop Add-ons dock and top menu inside Asset Browser, showing hello 0.1.0 as Loaded. Tested close and Show add-ons through the real menu.
- Opened Hammer and visually verified its own Workshop Add-ons dock. The process log contains exactly one hello greeting and one successful hello load across both windows, plus `tools.factory.request: AssetBrowserSystem_001`.
- Installed proxy SHA256: `140c91463a73faaa1d99deab6c63b4894a125c93bc34f8e61d45a34f5d3823f2`.
- Installed UI SHA256: `a05ea63a23e32064edc1020da0f3fcaac8f11619a2f9622fa02694a0f7a04829`.
- Local screenshots: `build/evidence/workshop-addons-asset-browser.png` and `build/evidence/workshop-addons-hammer.png`. These are ignored build artifacts.
- Framework scope and contributor guidance now separate general add-on infrastructure from a future, independent Hammer multiplayer project. No multiplayer implementation is claimed.

## Hammer Help/About and tool tags, 2026-09-08

- Windows Release build, **34 native integration scenarios**, and the Qt UI test passed.
- Manifests accept optional tool tags while preserving format/ABI 1 and untagged manifests. Tests cover single/multiple tags, whitespace, all, missing tags, unknown/duplicate/empty IDs and invalid all combinations.
- The generator accepts --tools and its generated tagged add-on compiles and loads against the packaged SDK.
- UI checks cover reusing Hammer's existing Help menu, hidden startup, About/Show tab navigation, matching multiple tags, all-tools inclusion, unspecified tags, no-match messages, independent filters and unchanged filters across refresh.
- Installed in Workshop Tools process 26660, preserving the existing untagged hello sample and its DLL.
- Visually verified Asset Browser's Add-ons/About tabs, Tool filter and Tools column; the existing sample appears as Unspecified.
- Opened Hammer and visually verified no additional top-level menu and no visible manager dock at startup. Invoked Help > Workshop Add-ons > About Hammer Addons; verified the About tab, version 0.1.0 and project link.
- Tool tags are author-declared discovery metadata, not deferred activation or dependency enforcement. No ModelDoc editor API implementation is implied by its filter category.
- Local evidence: build/evidence/tool-filter.png, build/evidence/hammer-hidden-manager.png, build/evidence/hammer-help-about.png.


## UI and workflow extension SDK, 2026-09-09

- Release build and both Qt integration executables passed. The native loader,
  forwarding, installer and generator suite passed **35 scenarios**.
- Real example DLLs exercise commands, standard panel controls, isolated persistent
  settings, before/native/after menu callbacks, handled suppression, custom file
  selection and built-in/custom importer routing.
- Regression checks cover duplicate/late registrations, tool and phase scope,
  missing/ambiguous targets, late menus and menu recreation, visible binding
  diagnostics, cancelled selection, shortcut updates/restoration, busy callbacks,
  exceptions removing only the failed add-on, and cleanup at runtime shutdown.
- All five packaged generator templates compile and load against the packaged SDK.
- Live Workshop Tools process 9888 loaded commands, the existing compiled untagged
  hello, menu_hooks, note_import and panel_settings. The original hello DLL and
  manifest were preserved, demonstrating old ABI-1 add-on compatibility.
- Visually verified the example dock and its five control types in Asset Browser,
  five loaded add-ons, the Extensions tab, native File-menu placement and the
  command callback's status-bar response. All Asset Browser bindings reported
  Attached, including Help/About. The framework still installs two DLLs; no Qt
  runtime DLLs or per-editor loader DLLs were added.
- The live native popup-menu automation was inconsistent. Do not treat it as
  evidence of a completed native file-picker callback or native ModelDoc import.
  These callback/routing behaviors are verified by the automated Qt fixture.
- The note example's ModelDoc File/Import target is unverified and remains waiting
  when absent. It is not a Blender/model converter or a document-editing API.
- Local visual evidence: build/evidence/extensions-panel.png,
  build/evidence/extensions-command.png, build/evidence/extensions-bindings.png.
- Final built proxy SHA256:
  a95d25a7c0eb12546156c69e0b6ac921b4ef3ccf557117cc1dfce7fe792b54c9.
- Final built UI SHA256:
  7ebe1547a94da899a36137072340e740798d27f8b4aa2c2e42cc500fc1a1b05e.
- Installed the final UI hash above and reopened Workshop Tools as process 31760.
  All five add-ons loaded and all five Asset Browser registrations attached.
  Original Valve Hammer retained SHA256
  b4755b0a909d0d19ca35f14346643765b32eb4bc18951a777991549ce913dd78.


## Portable CMD launcher, 2026-09-09

- Added a portable CMD entry point backed by a native x64 launcher and runtime.
  The shipped folder needs no Python or PowerShell installation. Steam libraries
  are detected automatically; a CS2 folder/executable can be dropped onto the CMD.
- Release build, both existing Qt tests, the new portable launcher integration
  suite, and all **35 native integration scenarios passed**.
- Launcher checks cover CMD paths with spaces/ampersands and dropped game paths,
  restricted CLI options, original-DLL validation, normal-session rejection before
  add-on initialization, runtime loading into a disposable Qt process, visible
  add-on UI, and rejection of unavailable factory-observation capabilities.
- Restored the real installation's verified original Asset Browser DLL using the
  existing uninstaller. Its SHA256 is
  eeb93ce111642027ffd91d6ae6bed4eae7973b70d29e74d64ba80ba9e714b3c8.
  Existing add-ons were preserved and copied into the local portable test package.
- Live process 21292 was created by the new launcher with -tools -insecure -nop4
  -addon h2mcp_demo. Runtime and UI modules loaded from build/live-portable, while
  assetbrowser.dll loaded from the original CS2 installation. No proxy backup or
  replacement installation record remains active.
- All five existing add-ons loaded. Asset Browser's manager and example panel were
  visually verified. Binding logs show all Asset Browser contributions attached.
  Local screenshot: build/evidence/portable-launcher.png.
- The tested path uses ordinary Windows DLL loading into only the launcher's own
  child; no global registry override, .local redirect or game-file replacement.
  Secure-server matchmaking was not tested and no VAC certification is claimed.
- Legacy CreateInterface observations are unavailable on the portable entry point;
  HA_CAP_FACTORY_EVENTS is not advertised. Existing UI and settings APIs remain.
- build.bat creates dist/hammer-addons-portable.zip with a fixed release file list,
  excluding local logs, project preferences and additional installed add-ons.


## Native project picker and picker add-ons (2026-09-09)

- Verified local Steam launch metadata selects `game/bin/win64/csgocfg.exe`
  with `-steam -retail -gpuraytracing -vulkan`. The portable launcher now opens
  that picker on every launch and adds `-insecure -nop4`; no project is chosen
  or remembered by the framework, and old project CLI flags are removed.
- Release build passed. All 35 native loader scenarios, offscreen UI tests and
  extension SDK tests passed. Launcher tests cover cancellation, owned-child
  handoff, rejecting insecure-flag omissions, normalized paths, picker scope,
  real Qt panel/settings callbacks and cancellation after picker initialization.
- Live Valve picker PID 20128 showed the loader button and loaded `picker_notes`;
  `UI binding 1 [project_picker]: Attached` was logged. Its selected CS2 child
  PID 17672 loaded `hello` from the portable runtime. Logs are local under
  `build/picker-addons.*.log` and `build/picker-package/portable/loader.log`.
- The picker runtime requires explicit `project_picker` opt-in. Existing editor
  add-ons do not execute there. The owned add-ons window supports the existing
  panel/command/settings SDK; Valve project-list and launch-button hooks are
  not implemented. No normal gameplay or VAC admission claim is made.


## Picker footer layout correction (2026-09-09)

The first picker integration passed button-discovery checks but inserted its
button into QMainWindow's internal layout. Live diagnostics showed
`QMainWindowLayout`, index -1, and an unmanaged 100x30 button at the top-left.
The button now uses QStatusBar.addPermanentWidget, with a minimum sizeHint and
separate loader-active status text. A real-window screenshot confirmed the full
button in the bottom footer with unobstructed project controls.

The picker fixture now uses QMainWindow with central content and checks button
visibility, minimum size, status-bar ancestry and placement below that content,
as well as opening the manager and interacting with the example. Launcher
integration tests passed after the correction. Temporary diagnostic logging was
removed from the shipping UI.


## Picker manager reopening (2026-09-10)

The picker button now restores and redocks the manager's Add-ons panel before
showing its host window. Closing the floating dock and then the host no longer
leaves an empty manager on the next click. Existing panel instances and add-on
settings are retained.

The picker fixture repeats undock -> close dock -> close host -> click picker
button twice and checks visible docked contents, one manager dock and preserved
example text. Launcher integration tests passed against the rebuilt UI DLL.
The fixture allows one Qt event pass for initial status-bar geometry to settle.


## CS2 build 25218825 compatibility (2026-09-10)

Steam's installed app manifest reports build 25218825. The updated Asset Browser
has SHA256 `a47a94eeff6f3e3f777ebdcbe3e6b6f6cdf064e0c527e3a2928ca52c6bbfe2e6`,
a valid Valve Corp. Authenticode signature, x64 architecture and the expected six
export names/ordinals. Qt remains 5.15.2.0. No replacement-loader install record
exists. Added this inspected build alongside the previous supported hashes.

The rebuilt launcher's --check passed. Launcher regression tests passed, including
unknown-build rejection. Live picker PID 15904 loaded picker_notes; its CS2 child
PID 31036 displayed Asset Browser with Loader active and 5 loaded / 0 failed.
Menu/panel bindings attached. Local evidence is in build/compat-25218825.*.log and
the portable loader.log. No Valve files were changed.

Unknown-build errors now include the observed SHA256 and explain that a CS2 update
may require a newer Hammer Addons release, instead of suggesting restoration first.


## Editor observation infrastructure (2026-09-10)

Added HA_EDITOR_OBSERVER, size-checked HA_EditorStateV1 snapshots and owned-panel
HA_SetPanelText updates for labels/read-only text views. ABI-1 prefixes are retained.
The stream identifies Qt windows by session UUID/window ID and reports coalesced
metadata changes, never scene operations or inferred document paths.

Release build, editor_test, existing UI/extension tests, launcher integration and
35 native loader scenarios passed. New tests cover old ABI prefixes, foreign-panel
write rejection, required typed observer state, distinct windows sharing one
session, UTF-8 reported paths, missing paths, ordered changes, no duplicate idle
updates, live panel text and cached destruction metadata. Both new scaffold
templates generated successfully.

Live Asset Browser PID 31476 loaded editor_watch and live_status (7 add-ons total,
0 failed). Its Editor observations panel reported editor.opened with session/window
identity and explicitly reported that the editor supplied no document path.
Evidence: build/editor-context.*.log and the portable loader.log. This verifies
window observation, not map data access or real-time editing.
