# Live tool output

**The native provider is currently disabled in the portable launcher.** Standalone
DLL tests passed, but live startup tests encountered heap corruption with the
native listener and/or a temporary diagnostic probe. A later picker crash also
occurred with both disabled, so the cause is not isolated and is not attributed
solely to the listener.
The `tool_console` example therefore shows unavailable; a matching hash alone
is not treated as proof of live stability.

For automatic compiler output, use **Help > Workshop Add-ons > Build log report**
in Hammer. That observes Hammer's build dialog independently of this provider:
[build-output capture and SDK](BUILD_OUTPUT.md).

The API below remains available for development and isolated tests. There is no
production switch to enable the experimental native listener.

## SDK

Include `hammer_logs.h`, declare `HA_CAP_TOOL_LOGS`, and use `HA_GetLogs(host)`.
The capability denotes API availability; `available(host->context)` separately
reports whether a verified native provider was attached to this process.
An unsupported provider leaves the rest of the framework usable.

`subscribe(context, callback, minimum_severity)` returns an owned subscription ID,
or 0 if unavailable, invalid, busy or over the eight-subscription-per-add-on limit.
It is allowed during on_load or active execution. Severity 0 accepts everything;
the named levels are detailed=1, message=2, warning=3, assert=4 and error=5.
`unsubscribe(context, id)` removes only that add-on's subscription. Subscriptions
start with newly captured messages; there is no history replay.

Callbacks run on the editor thread through the same serialized dispatcher as jobs.
`HA_LogEventV1` contains severity, engine channel ID, message sequence, truncation
flag, cumulative capture drop count and borrowed text. Copy any text you retain.
A callback exception disables its add-on. Stopped/failed owners receive no more
callbacks. Do not forward received messages back into the native logger: that can
create a feedback loop over successive UI dispatches.

The native listener only copies into a bounded buffer. It does not invoke add-on
callbacks, access widgets, write files, or change engine logging policies. Messages
marked do-not-echo are skipped. The buffer holds 256 messages of at most 2048 UTF-8
bytes; full/contended capture drops messages and increments dropped_total. Up to 32
messages are dispatched per UI tick. This is diagnostic output, not a lossless log.

## Compatibility and scope

The experimental adapter accepts only the `tier0.dll` hashes in compatibility.json's
logging_profiles. Its private interface follows the CS2 SDK's ILoggingListener
method order and LoggingContext prefix; a test against the original installed DLL
checks channel/severity/text and worker-thread delivery. Unknown builds report
unavailable until their interface has been inspected and tested.

A listener is registered in the current native logging state. Native code can
create separate/cleared logging states or bypass that logger; those messages may
not reach this listener. It sees only output after attachment in this process.
A separate resourcecompiler.exe is outside its scope unless its output is relayed
into this process's logger. No claim is made that it captures a complete Hammer
compilation transcript, compiler stdin, or every stdout/stderr write.

Native logging states can retain listener pointers. The adapter keeps its listener
and bounded buffer alive for process lifetime, like the loader's retained DLLs,
instead of freeing a callback still referenced by another logging state. The
native adapter is not currently attached by either production entry point.

Reference: [CS2 SDK logging interface](https://github.com/alliedmodders/hl2sdk/blob/cs2/public/tier0/logging.h).
Example: [tool_console](https://github.com/Nnamllit1/hammer-addons/blob/main/addons/tool_console/tool_console.cpp), also available
as `--template tool_console`.
