# Project context

**Workshop Add-ons > Project context** shows the selected Workshop project's
content and game folders. Enter a project-relative source path, such as
`materials/example.vmat`, to find that file. In Hammer, the menu is under Help.

Context is available after selecting a project with the portable launcher. The
project picker itself has no selected editor session, so it does not provide this
information. A window title is not used to guess which project is active.

## SDK

Include `hammer_project.h`, declare `HA_CAP_PROJECT_CONTEXT` (4096), then call
`HA_GetProject(host)`. The table has two functions:

- `current(context)` returns a borrowed `HA_ProjectInfoV1` or NULL if unavailable.
  It contains the addon ID, installation root, content root, and game root.
- `source_path(context, relative, output, capacity)` resolves an existing loose
  source file within this project's content folder. It returns the required UTF-8
  buffer size including NUL, or zero if the path cannot be resolved. A short buffer
  is left unchanged; query its size first and retry. Files can change between calls.

The project is determined from the tools process's explicit `-addon` argument.
The provider validates the CS2 executable layout, tools/insecure session flags,
project directory names, and the existence of both project folders at startup.
Duplicate or invalid project arguments leave context unavailable. The snapshot is
immutable for the process; it does not update the project selected in another session.

Returned strings remain valid until host shutdown. Copy them if needed beyond
that lifetime. The lookup may access the filesystem; use a background job for
bulk checks. Absolute paths, parent traversal, alternate streams, Windows device
aliases, and reparse points are rejected.

This API identifies the project, not the current document or unsaved map state.
Source lookup does not inspect mounted VPKs, base-game assets, compiled dependencies,
or custom search paths. An unresolved loose source does not prove an asset is missing.

The bundled `project_context` add-on demonstrates both functions. Create an add-on
with `--template project_context` to start from that example.
