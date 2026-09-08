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
