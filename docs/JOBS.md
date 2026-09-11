# Background jobs and editor callbacks

Include `hammer_jobs.h`, declare `HA_CAP_JOBS` and/or `HA_CAP_EDITOR_QUEUE`,
and retrieve `HA_GetJobs(host)`. The versioned table is appended to the ABI-1
extensions table; old prefixes remain valid. New helpers return null on old hosts.

## Submit work

Create an `HA_JobV1` with its size, input string, worker function, notification
function and optional window ID. `submit(host->context, &request)` copies the
input and returns a nonzero job ID, or 0 for an inactive add-on, invalid request,
busy runtime, unknown window, thread creation failure or capacity limit.
Submit from normal execution after on_load has completed, not during on_load.

Workers run independently of the editor thread. They receive a borrowed
`HA_JobContextV1` and the copied input, and write a final result into the supplied
buffer. Return 1 for success or 0 for failure. C++ exceptions become failed jobs.

Worker context methods:

- `cancelled(context)`: check regularly and return promptly when requested.
- `report(context, percent, text)`: replace the latest progress update, 0..100.
- `post(context, text)`: queue a copied message for this job's notification function.

Keep all worker state local or owned by the worker. Do not use Qt, editor objects,
host APIs, or globals that on_shutdown might free. The context and input expire
when the worker returns. The worker functions remain loaded for process lifetime,
as do other add-on functions in the current loader.

## Delivery and ownership

`notify` runs on the editor thread, serialized with other runtime callbacks.
Its event identifies the job, progress and one of PROGRESS, MESSAGE, SUCCEEDED,
FAILED or CANCELLED. Strings are borrowed only during that callback.
Progress is coalesced; accepted messages retain their per-job order and precede
the terminal event. Cross-job order and ordering between progress and messages
are not guaranteed. A normal or explicitly cancelled job receives one terminal
event while its owner/scope remains available. Completed jobs release capacity
after their final notification is collected.

`post(host->context, callback, text, window_id)` schedules standalone editor work.
The callback receives MESSAGE with job_id 0. It never runs inline. Posting during
a callback defers that work until another dispatch pass. There is no synchronous
wait API. Keep editor callbacks short; this queue cannot prevent slow callbacks
from blocking the editor. The private UI bridge dispatches approximately every
50 ms while the event loop is running; panel rendering still uses normal refresh.

Window ID 0 gives process scope. Other IDs must be currently observed windows
from the editor-context API in this process. Destruction cancels scoped workers
and discards scoped callbacks; hiding a window does not destroy it. Worker and
callback cancellation also applies when the add-on fails or the runtime shuts
down. A throwing queued callback marks its add-on failed.

Shutdown does not join workers on the UI thread. Cancellation is cooperative:
a worker that ignores it can continue until process exit, without further UI
callbacks. Workers own their copied state independently of Runtime/Addon objects.
An abrupt process exit does not guarantee a final callback or completed file write.

## Limits and backpressure

- Up to 4 outstanding jobs per add-on, 16 per runtime, including cancelled workers
  that have not returned. This prevents cancellation from bypassing capacity.
- Inputs: 32768 UTF-8 bytes. Messages/results: 4096 bytes.
- Up to 32 queued messages per worker and 64 standalone posts per runtime.
- `report`, `post`, and `cancel` return 0 on rejection. No unbounded retry or wait
  occurs inside these APIs. Check results and retry later if appropriate.
- A job may stream many messages, but this is a bounded event queue, not a
  lossless byte stream or stdin/stdout pipe. Keep authoritative output separately
  when every byte must be retained.

The [compile_report example](../addons/compile_report/compile_report.cpp) demonstrates
background file inspection, progress, messages, cancellation, result delivery,
standalone queued callbacks and live panel updates. It is included in the portable
package. Generate your own copy with `--template compile_report`.

The `compile_report` example also observes Hammer output automatically using
[build-output snapshots](BUILD_OUTPUT.md). Its worker scan is an optional path
for existing saved logs; it is not required for normal Hammer builds.
