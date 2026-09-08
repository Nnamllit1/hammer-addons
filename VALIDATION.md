# Validation: 2026-09-07

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
