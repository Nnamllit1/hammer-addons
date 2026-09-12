# Loading and reloading add-ons

The **Workshop Add-ons > Add-ons** tab provides two actions:

- **Load new add-ons** discovers newly installed packages without restarting the
  application. Only install native code you trust.
- **Reload selected** replaces a running add-on that explicitly supports hot
  reload. Select its row first. Other add-ons continue running.

These actions affect the current process. The Workshop project picker and the
editor process have separate instances; reloading one does not reload the other.
The framework itself and non-reloadable add-ons still require a restart.

## Try the example

Build the framework, then copy `dist/examples/addons/reload_counter/` into the
portable package's `addons/` folder. Click **Load new add-ons**. Open **Reload counter** from the add-on menu,
click **Increment**, select `reload_counter` in the manager and click
**Reload selected**. Reopen the counter panel: the saved count is preserved.

To try a code change, edit the example's label or button text, rebuild and copy
the new DLL into the installed add-on folder, then click **Reload selected**.
Finish copying or building before reloading.

## Author contract

Generate an example project with:

```powershell
python scripts/new-addon.py my_counter --output ../my_counter --template reload_counter
```

Add `reloadable=true` to `addon.ini` and export `HA_QueryReload(uint32_t)` from
`hammer_reload.h`. Return a static `HA_ReloadV1` for `HA_RELOAD_VERSION`, or null
for an unsupported version. Both the manifest and a valid export are required.
The main host/add-on ABI remains version 1; there is no new capability bit.
Older framework versions reject the new manifest key.

On reload, the framework:

1. Checks the replacement's manifest and process scope, then copies its files
   to a private generation directory while holding source files against writes.
2. Refuses the operation if SDK jobs or queued deliveries remain pending. It
   never blocks the GUI waiting for them; finish or cancel jobs and retry.
3. Calls the current instance's `prepare_reload()` on the GUI thread, with SDK
   callback dispatch serialized. Return **0** to refuse and leave the instance
   working unchanged. Return **1** only after removing all private hooks, timers,
   threads and external callbacks. Do not enqueue new work or block the GUI.
4. Removes the old instance from SDK dispatch, retires its contributions and
   subscriptions, and calls `on_shutdown()` once.
5. Loads the new DLL copy and calls its `on_load()`. On a hot load or reload this
   runs on the GUI thread; initial startup may use the runtime startup thread.
   UI bindings refresh on the manager's next update.

Persist state through settings in `prepare_reload()` or `on_shutdown()`, and read
it in the new `on_load()`. Do not pass old pointers into the new instance.
Contribution handles are new for each generation; stale handles are rejected.
After shutdown, the retired host context cannot submit jobs, queue editor
callbacks, update panels or access settings.

The framework cannot inspect arbitrary native code to prove cleanup is complete.
Opt in only when the add-on can meet this contract. Keep `DllMain` minimal and
perform initialization through `on_load()`.

## Limits and failures

Reloadable packages currently support one DLL plus data files. Bundled private
DLL dependencies are rejected because Windows module-name reuse can otherwise
bind a replacement to an older dependency. System DLL dependencies still work. Packages are limited to 1,024 entries and
1 GiB of files per generation.

Reloadable add-ons run from `runtime-cache/<generation>/<addon-id>/`, allowing
their installed DLL to be replaced while the application runs.
`host->addon_directory` points to that private copy, including its data files.
Use the settings API for writable persistent state. Checked package files are
locked during verification and loading.

Retired DLL images remain mapped until process exit. Reload stops dispatch to
old instances; it does not call `FreeLibrary` or run their detach handlers.
Windows or external code could otherwise still hold a return address or callback
into an unloaded module. Memory use can grow, and each add-on is limited to
32 loaded generations per process. Close all tools using this portable folder
before deleting `runtime-cache/` to reclaim disk space. Failed or declined
attempts can also leave unused copies there.

Manifest, copy and busy-job failures leave the current instance running.
A lifecycle exception disables it and requires a restart. If the replacement's
DLL entry point, ABI query or `on_load()` fails after the old instance has shut
down, the old instance is not restarted automatically. Check the manager's
Details column and restart after correcting the package.

Loading and reloading are explicit button actions; there is no file watcher.
Native add-ons remain unsandboxed and execute with your Windows permissions.
The reload contract does not restrict what their native code can do.
