# Workshop Tools add-on framework

Hammer Addons supplies general native add-on infrastructure for CS2 Workshop
Tools. A single runtime serves editors in the same tools process.

## Startup and ownership

```text
CS2 Workshop Tools
  assetbrowser.dll                  six-export proxy, active at tool startup
    assetbrowser_original.dll       exact backed-up Valve binary
    tools/hammer-addons/
      install.json                  module identity and ownership hashes
      loader.log                    diagnostics
      hammer_addons_ui.dll          manager in Asset Browser and Hammer
      disabled                      optional global add-on disable marker
      addons/<id>/addon.ini + DLL    versioned C ABI
  tools/hammer.dll                  unmodified Valve editor
```

The proxy forwards five entry points through x64 assembly thunks, preserving
argument registers, floating-point registers, stack arguments, alignment and
unwind metadata. CreateInterface returns Valve's pointer/result unchanged.
Resolution uses an absolute sibling path and checks all six exports.

DllMain only records the module handle. Libraries, manifests and callbacks are
loaded after the original factory returns. The runtime and loaded add-on modules
have process lifetime. The installer uses a proxy DLL and preserves the original
byte-for-byte; it does not patch executable instructions or inject into a running
process.

A nonblocking observation guard prevents recursive initialization/event dispatch.
A nested call, including one from a worker joined by an add-on callback, still
reaches Valve without waiting on the add-on. Observations may be skipped while
another callback is running. The default proxy reports `tools.factory.request`
for Asset Browser interface requests; it does not observe every Workshop Tools
module or expose document changes.

The old Hammer proxy remains a build/test fixture for migration and regression
coverage. It is not included in new distributions or installed alongside the
Asset Browser proxy. Legacy installation records without a module field still
restore Hammer correctly. Existing `tools/hammer-addons` folders and ABI 1
add-ons keep their identities and layout.

## Shared manager

The UI DLL uses public Qt 5.15.2 Widgets APIs matching the supported tools build.
A worker retries UI startup for up to 60 seconds if Qt or QApplication is not
ready yet. Widget creation is queued onto the Qt application thread.

The controller recognizes visible Asset Browser, Source 2 Tools, Hammer,
ModelDoc, Material Editor and Particle Editor QMainWindows by title. Each supported window owns one dock. Asset Browser displays it at startup with
a top-level manager menu. Hammer starts with the dock hidden and places its
manager submenu under Help. The manager offers Add-ons, About and Extensions tabs. Panels close and
reopen independently, refresh from the same runtime, and are recreated with
their parent editor. Manifest tool tags are discovery metadata used by each
panel's independent filter; they do not change shared startup or callback
dispatch. Hammer defaults to its own tag filter. Unrelated windows and dialogs are left alone. At present,
live verification covers Asset Browser and Hammer.

A private C bridge copies JSON status snapshots; no Qt or STL ownership crosses
DLL boundaries. Refreshes skip a busy callback lock rather than blocking the UI.
Qt runtime DLLs are neither packaged nor replaced. The extension SDK copies registration descriptors during on_load. The UI adapter
owns per-window commands, docks and menu wrappers; a private C invocation bridge
dispatches callbacks by handle. No Qt objects cross the public SDK. Missing or
ambiguous action targets stay pending, and binding status appears in Extensions.

Menu hooks preserve the original QAction, transfer its shortcut and invoke it
only after before-handlers continue. Import routes add a handler chooser with a
built-in path. They do not intercept native function calls or expose document
objects. See [extension contracts and examples](EXTENSIONS.md).

## Compatibility and development

Installation checks module-specific original SHA256 hashes, export names,
ordinals and x64 architecture. It refuses unknown builds, existing backups,
external changes and installation while CS2 runs. Uninstall restores only the
verified original for the recorded module. Steam updates require re-inspection.

The framework roadmap is:
1. Improve the shared manager and add-on diagnostics.
2. Extend the UI API with dynamic control updates and asynchronous jobs.
3. Add dependency, capability and version negotiation.
4. Validate editor adapters for document/selection operations and native
   undoable transactions.
5. Establish a lifecycle protocol before implementing native hot reload.

Finding interface names or Qt types in a binary does not establish a supported
editor SDK. Only verified interfaces belong in adapters; do not guess vtables,
document layouts or offsets.

## References

- Local `game/bin/win64/assetbrowser.dll`: the six exports and module hash are
  recorded in `compatibility.json`; `loader.py inspect` reproduces them.
- Local `game/bin/sdkenginetools.txt` registers editor modules such as Hammer.
- [Microsoft DLL initialization guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Qt QDockWidget API](https://doc.qt.io/qt-5/qdockwidget.html)
- [Qt queued invocation](https://doc.qt.io/qt-5/qmetaobject.html#invokeMethod)
