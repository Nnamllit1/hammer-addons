# Hammer Addons format 1 / native ABI 1

An add-on is a directory inside `game/bin/win64/tools/hammer-addons/addons/`.
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
```

The manifest is ASCII, at most 16 KiB, with exactly these six keys. Blank lines
and full-line `#` comments are allowed. Duplicate keys, unknown keys and sections
are rejected. IDs use `[a-z][a-z0-9_]{0,63}`, versions use three numeric components,
and `entry` is a simple DLL filename with letters, digits, underscores or hyphens.
No subdirectories, absolute paths or junctions/symlinks are accepted. This is a
folder format, not a ZIP installer. Add-ons are discovered in sorted folder order.

Build an x64 DLL exporting `HA_Query(uint32_t)` using `sdk/include/hammer_addons.h`.
Return a static `HA_AddonV1` for ABI 1 and null for unsupported ABIs. The loader
checks the struct size, ABI, ID, required capabilities and on_load callback.
`on_load` receives a stable host table; return 1 on success, 0 on failure.
Clean up partial initialization before returning failure.

Available host capabilities:

| Flag | Available functionality |
| --- | --- |
| `HA_CAP_LOGGING` | `host->log(context, message)` and the add-on directory |
| `HA_CAP_FACTORY_EVENTS` | `hammer.factory.request`, with the requested interface name as value |

These events run after Valve's `CreateInterface` returns. They are observations
of interface requests, **not document-change events or proof that the editor UI
is initialized**. `host.test` is emitted only by the standalone test host.
No map editing, cursor, selection, Qt UI or multiplayer API is implemented yet.

Callbacks are serialized on the thread invoking the factory. That thread is not
guaranteed to be the editor UI thread. Do not block it, call back into the factory,
or manipulate undocumented editor objects from these callbacks. Host logging is
thread-safe. No C++ objects, STL containers, exceptions or ownership cross the ABI.
All event strings are borrowed. The host table lives for the runtime's lifetime.

DLLs remain loaded until process exit; changing an add-on requires restarting
Hammer. Explicit `on_shutdown` is available in the standalone host, but is not
called from DLL_PROCESS_DETACH or guaranteed at editor termination. Native hot
reload requires a future protocol for detaching hooks, callbacks and threads.
Flush important state during normal execution.

Set `enabled=false` to disable an add-on, or create `hammer-addons/disabled` to
disable all add-ons while retaining original Hammer forwarding. Restart Hammer.
Diagnostics go to `hammer-addons/loader.log`, stdout and OutputDebugString.

Add-ons execute native code with the editor's permissions. ABI checks do not
sandbox DLLs, and their DllMain runs before HA_Query validation. Missing DLLs,
invalid manifests, incompatible APIs and ordinary C++ callback exceptions are
reported; access violations or memory corruption can still crash the editor.
Dependencies are loaded from the add-on DLL's directory and Windows System32.
