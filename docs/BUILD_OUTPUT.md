# Automatic Hammer build output

Build normally in Hammer (F9). The bundled **Build log report** panel follows
Hammer's existing build-output control automatically. Open the report from
**Help > Workshop Add-ons > Build log report**. No file picker, compiler wrapper,
extra process or native logging listener is required.

The example highlights lines containing warning/error/failure keywords. These
are diagnostic candidates, including potentially harmless phrases like "0 errors".
It does not infer compiler exit status or fixes. The panel shows the latest updated
source; it is not a multi-build history. A new live update cancels an optional
saved-file scan so an old worker cannot overwrite the live report.

## SDK

Include `hammer_build.h`, require `HA_CAP_BUILD_OUTPUT` (2048), and register a
`HA_BUILD_OBSERVER` contribution during on_load with tool `hammer`, no target,
options or controls. In its callback, use `HA_GetBuildOutput(event)` to read the
size-checked appended `HA_BuildOutputV1` pointer. See the complete
[compile_report example](https://github.com/Nnamllit1/hammer-addons/blob/main/addons/compile_report/compile_report.cpp); the scaffold
`--template compile_report` includes automatic observation and saved-log inspection.

- `build.output` contains a replacement snapshot, including empty text after clear.
- `build.closed` contains the last captured snapshot when the dialog or document
  is destroyed while the editor remains open. Closing the editor/process does not
  guarantee a final event. Hiding the dialog is not destruction.
- `window_id` identifies the editor window. `stream_id` identifies one output
  control for this process; it is not a compiler invocation or persistent ID.
- `sequence` increases when the document revision, title or closure changes.
- `title` is the build dialog's title, not an authoritative map path.
- `text` and `title` are borrowed UTF-8, valid only for the callback. Copy to retain.
- `HA_BUILD_TRUNCATED` means earlier text is omitted from this snapshot.

Callbacks run on the GUI thread through the runtime's serialized dispatcher.
Keep them short. Busy delivery retries the newest snapshot, not every intermediate
revision. Exceptions disable the add-on through the normal extension mechanism.
The capability means the observer API exists; it does not promise an open build
dialog or supported controls in every Valve release.

## Capture limits

The adapter recognizes `CQBuildMapDialog` with exactly one descendant
`CQAutoScrollingTextEdit` that derives from Qt's `QTextEdit`. This pair was observed
in the installed Hammer build; no vtable offsets or engine instructions are patched.
Unrelated consoles and add-on panels are excluded. If Valve changes the control
classes or makes the target ambiguous, capture does not attach.

Discovery runs with the normal UI refresh (750 ms). Document revision checks run
every 100 ms and copy only changed output. Up to 16 controls per editor are tracked.
Each snapshot includes at most the last 32768 UTF-16 units (at most 131072 UTF-8
bytes), preserves surrogate boundaries, and normalizes Qt paragraph separators to
newlines. Long-output line numbers in the example refer to this retained tail.

This is the text Hammer displays, not lossless stdout/stderr capture. Transient
text cleared between polls can be missed. Compiler stdin, exit codes, build-start
and build-finish semantics are not exposed. The adapter never changes the document,
starts a compiler, selects a project, or writes a log file.
