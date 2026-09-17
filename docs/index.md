# Hammer Addons

Native add-ons for **CS2 Workshop Tools**, with a shared add-on manager and a
versioned C SDK. Install compatible extensions or build your own commands,
dock panels, import handlers, and editor workflows.

## Start here

| What would you like to do? | Guide |
| --- | --- |
| Install the framework and run Workshop Tools | [Installation](INSTALLATION.md) |
| Load or reload an add-on without restarting | [Hot loading and reloading](HOT_RELOAD.md) |
| Create your first native add-on | [Developer quickstart](DEVELOPING.md) |
| Find an SDK feature and working example | [Extensions and examples](EXTENSIONS.md) |
| Update an existing installation | [Release notes](releases/v0.1.0-alpha.4.md) |

## What is included?

- **Build log report:** group Hammer build diagnostics and compare captured builds.
- **Project context:** inspect the selected project's folders and locate source files.
- **Steam account and friends:** view identity and presence, with overlay shortcuts
  when available.
- **Native extension SDK:** commands, panels, persistent settings, menu hooks,
  file handlers, background jobs, and editor observations.

The portable launcher opens Valve's project picker and loads the framework into
its own Workshop Tools session. It does not replace Valve DLLs. Get the portable
package from [GitHub Releases](https://github.com/Nnamllit1/hammer-addons/releases).

## Before installing add-ons

Native DLLs run with your Windows account's permissions. Only install code from
publishers you trust. The framework is not a sandbox; see
[failure handling](FAILURE_HANDLING.md) for the limits of callback isolation.

Editor observations currently expose metadata, not map-object editing or unsaved
scene operations. The native tool-log provider is currently disabled; automatic
[Hammer build-output capture](BUILD_OUTPUT.md) is a separate feature.

This is an unofficial project, independent of Valve. The documentation tracks
`main`; consult the release notes for the features in a particular download.
