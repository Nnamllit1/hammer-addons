# Native add-on failure handling

Hammer Addons catches C++ exceptions around calls it makes into add-ons. An
exception from an event, UI interaction, log subscription, Steam subscription
or queued editor callback marks its owner **Failed** in the manager. Other
add-ons continue receiving callbacks.

The failed owner immediately loses SDK callback dispatch and settings access.
Its jobs receive cancellation requests and queued callbacks are discarded.
Cancellation is cooperative: a worker must check its cancellation flag and
return. The framework does not block the GUI waiting for it.

UI bindings disappear at the next UI refresh. If a callback filled its response
buffer before throwing, the response is cleared before it reaches the UI.
Contributions registered before a failed `on_load` are never exposed, and that
instance's settings access is also revoked.

The framework does not call a failed instance's shutdown callback. Its state may
be only partially initialized or cleaned up. The DLL stays mapped; correct the
add-on and restart the tools. A normal callback return of `HA_ERROR` reports an
operation failure without disabling the add-on. SDK worker exceptions become
failed jobs and are reported through their completion callback.

## What this cannot contain

Native add-ons share the editor's address space and Windows permissions.
Access violations, stack/heap corruption, `abort`, `std::terminate`, explicit
process termination, and exceptions on private add-on threads can still crash
the entire host. A stuck native callback can also freeze the editor.

Catching an access violation does not restore damaged memory or make it safe to
continue. The framework does not enable broad SEH recovery or replace the
editor's process-wide exception handler. See Microsoft's
[exception-handling guidance](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170).
Reliable crash isolation requires a separate process; unrestricted in-process
DLL access cannot provide that boundary.

## Author responsibilities

- Return SDK error codes for expected failures. Catch exceptions inside your
  exported functions and callbacks; the host's exception guards are a fallback.
- Pass valid pointers and truthful buffer sizes, respect borrowed lifetimes,
  and never retain a job context after its worker returns. Null checks and size
  checks cannot validate arbitrary pointers or detect every out-of-bounds write.
- Use SDK jobs for background work and check cancellation regularly. Keep
  private thread exceptions inside that thread, and stop private callbacks
  before accepting a reload.
- Keep `DllMain` minimal. Partial initialization must be cleaned up by the
  add-on before returning failure.
- Treat a zero/failed result from host operations as a failure, including
  settings writes. Host settings and contribution entry points return errors
  for null contexts and contain their own C++ exceptions.

The regression suite uses throwing DLL fixtures to verify callback isolation,
immediate job cancellation, stale-context rejection, safe response buffers,
partial initialization failure and continued operation of a healthy sibling.
These tests do not establish recovery from arbitrary native memory corruption.
