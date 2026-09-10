# Editor observations and live panel text

Include `hammer_editor.h` for editor-context helpers and live panel text updates.
These interfaces use ABI 1 with size-checked extensions to existing structures.
No C++ or Qt object crosses the public DLL boundary.

## Observe an editor

Register an `HA_EDITOR_OBSERVER` contribution during `on_load`. Set its tool to
an editor ID or `all`, leave target/options empty, and provide no controls.
Declare `HA_CAP_EDITOR_EVENTS` in your required capabilities.

The observer has no menu action. Its callback receives:

| Phase | Meaning |
| --- | --- |
| `editor.opened` | First available snapshot for this observer and window |
| `editor.changed` | Reported window metadata changed since the previous observation |
| `editor.closed` | The Qt window was destroyed; metadata is the last cached snapshot |

Use `HA_GetEditorState(event)` to access the optional `HA_EditorStateV1`:

| Field | Contract |
| --- | --- |
| `session_id` | Generated UUID for this process's UI-adapter session |
| `window_id` | Stable ID for this observed window; never reused in that session |
| `sequence` | Increasing metadata observation counter for the window |
| `title` | Current Qt window title |
| `document_path` | Exact value reported by Qt's windowFilePath, or empty |
| `flags` | Visible, active, Qt modified indicator, and document-path availability |

Identify a window by **(session_id, window_id)**. An ID identifies an editor
window, not a map: opening another file in the same window keeps the window ID.
A separate picker process has a separate session ID.

Missing document paths are normal when an editor does not populate windowFilePath.
The adapter does not infer paths from titles or scan project directories.
`HA_EDITOR_WINDOW_MODIFIED` describes Qt's indicator; its absence is not proof
that a map has no unsaved changes.

Callbacks run on the UI thread and are serialized by the runtime. Strings and
the state pointer are borrowed only during the callback; copy what you retain.
The manager's Extensions tab reports whether an observer is attached.

## Delivery limits

This is a metadata observation stream, not a scene-operation log. The normal UI
refresh samples windows about every 750 ms. Intermediate changes can be coalesced;
unchanged samples do not produce duplicate events. Busy observers retry with the
newest snapshot. A window may hide when closed by the user rather than be destroyed;
that produces a visibility change, not `editor.closed`.

Callbacks are best effort during destruction, and abrupt process exit cannot
deliver final notifications. An old UI bridge reports the observer as unavailable.
For `project_picker`, the observed window is the framework's add-ons window;
Valve's project-selection widgets are not exposed by this API.

No save event, map contents, entity identity, selection, undo transaction,
scene revision, network transport or remote editing is implied. The sequence
number must not be used as a map revision or concurrency token.

## Update read-only panel output

Declare `HA_CAP_LIVE_PANELS`. Add `HA_TEXT_VIEW` for a read-only multiline view,
or use an existing `HA_LABEL`, then retain the handle returned when registering
the panel:

```cpp
HA_SetPanelText(host, panel_handle, "events", text);
```

Returns 1 when the model was updated, or 0 when the host is too old, the handle
belongs to another add-on, the control/type is invalid, or the runtime is busy.
Values must be NUL-terminated UTF-8, at most 4096 bytes. Updates appear on the next
UI refresh in every window showing that contribution. They do not invoke a UI
callback or write a persistent setting.

This API deliberately updates read-only output. Editable controls keep their
existing user-input behavior. Do not retain host pointers after add-on shutdown,
and keep editor callbacks short.

## Examples

- [editor_watch](../addons/editor_watch/editor_watch.cpp): session/window identity,
  lifecycle observations, explicit unavailable paths, and a bounded visible history.
- [live_status](../addons/live_status/live_status.cpp): a button updates a label
  shared by that add-on's panels in the running tools process.

After building, copy the chosen folder from `dist/examples/addons/` into the
portable `addons/` folder and restart Workshop Tools. Open **Workshop Add-ons >
Editor observations** or **Workshop Add-ons > Live panel example**. In Hammer,
the framework menu is under **Help**.

Generate an independent add-on from either template:

```powershell
python scripts/new-addon.py my_observer --template editor_watch --output ../my_observer
python scripts/new-addon.py my_status --template live_status --output ../my_status
```
