# Native loader and collaboration direction

Hammer Addons replaces this repository's previous MCP application. The current
deliverable is a general native add-on loader. Simultaneous editing is its intended
future application, not a feature of this initial release.

```text
CS2 Workshop Tools
  tools/hammer.dll                  our six-export proxy
    tools/hammer_original.dll       exact backed-up Valve binary
    tools/hammer-addons/
      install.json                  ownership and hash record
      loader.log                    diagnostics
      disabled                      optional global disable marker
      addons/<id>/addon.ini + DLL    versioned C ABI
```

The proxy forwards five entry points through x64 assembly thunks. These preserve
RCX/RDX/R8/R9 and XMM0..3, provide shadow space and stack alignment while resolving
the original function, restore the original stack, then tail-jump. We do not guess
the C++ signatures of those exports. CreateInterface uses its conventional Source
factory signature and returns Valve's pointer/result unchanged. Original DLL
resolution uses an absolute sibling path and checks all six exports.

DllMain only records the module handle. Loading the original or add-on libraries,
parsing manifests and running callbacks happen lazily on exported calls. Add-ons
start after the first original CreateInterface returns. The runtime and loaded
modules have process lifetime. There is no detour, executable instruction patch,
code injection into an existing process, network service or editor-memory write
in this version. The installer replaces the editor module with the proxy and
preserves the original byte-for-byte.

This is an unofficial extension point. Finding `ToolSystem2_001`,
`EventMapDocumentModified_t`, undo-system names and Qt types in the installed DLL
is evidence for investigation, not a recovered editor SDK. A successful loader
test is not proof that geometry can be manipulated safely.

The installer accepts only listed original SHA256 hashes and exact export
names/ordinals on x64. It refuses existing unowned backups, externally changed
files and installation while CS2 runs. Uninstall also checks hashes. Steam may
replace the proxy during an update; inspect before reinstalling, never restore an
old Hammer DLL over a newly updated one. The remaining native ABI and dependencies
still require testing against each supported build.

## Next milestones

1. Recover the minimum document/selection interface for one supported Hammer build.
2. Read a selected entity's stable identity and transform; apply a transform as a
   native undoable transaction on the editor thread; save/reopen and verify it.
3. Synchronize that transaction between two independent Hammer instances, with
   stable IDs, ordered operations and suppression of echoed remote changes.
4. Add participant cursors/cameras, object ownership during edits, reconnect
   snapshots, conflict handling, per-user undo and mesh topology transactions.
5. Build immutable session revisions and distribute the same compiled map/assets
   for a shared playtest. The old mapping prototype demonstrated file/compiler
   workflows but did not expose Hammer's unsaved document.

The loader API should expose only verified capabilities. Unsupported editor builds
must not receive guessed pointers or offsets. Editor integration belongs in a
small version-specific adapter; networking and add-ons should use a stable API.

## References

- Local `game/bin/sdkenginetools.txt` registers `tools/hammer.dll`.
- Local exports: BinaryProperties_GetValue (1), CreateInterface (2),
  ExtractModuleMetadata (3), GetResourceManifestCount (4), GetResourceManifests (5),
  InstallSchemaBindings (6). `scripts/loader.py inspect` reproduces this inspection.
- [Microsoft DLL initialization guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
