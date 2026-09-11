# Useful Workshop Tools add-ons

Research checked 2026-09-10. These priorities are product judgments based on
published workflows and specific reports, not a representative community survey.
Older problem reports identify areas to investigate; they do not establish that
a particular bug is still present in the current tools build.

| Priority | Opportunity | What the framework could improve | Remaining work |
| --- | --- | --- | --- |
| 1 | Build and asset diagnostics | Show actionable diagnostics beside the editor, with original evidence, instead of making users search raw output and forum threads | Automatic Hammer output capture, grouped problems and project-local source lookup are available; package-aware dependency resolution and broader diagnostic rules still need work |
| 2 | Radar authoring | Generate and preview radar output within the editor | Geometry access, rendering and output validation; no radar implementation is currently included |
| 3 | Playtest feedback inbox | Bring map-version-tagged feedback, screenshots and coordinates into a mapper's workflow | A feedback format and import UI; jumping the editor camera to a location needs a verified editor interface |
| 4 | Game-mode preflight checks | Check a saved project against a mode's requirements and explain which checks remain manual | Verified document/asset adapters and maintained rules, initially for one mode |

## Evidence and existing solutions

Mappers have reported missing-resource errors that remain difficult to diagnose:
[CS2 resource-loading report, with later follow-ups](https://www.reddit.com/r/hammer/comments/1mnd1ca/cs2_hammer_error_failed_loading_resource/).
An older report specifically describes missing materials inside props escaping the
available checks: [CS2 SDK feedback](https://www.reddit.com/r/hammer/comments/18m2jzr).
These support investigating dependency diagnostics; they are not proof that all
current engine checks miss those problems.

The [CS2KZ mapping guide](https://docs.cs2kz.org/mapping/guide) documents concrete
quality checks and troubleshooting, including error materials, post-processing
files, trigger physics and mapping-tool setup. This is a promising source of
explicit, mode-specific rules with explanations and references.

[RadGen](https://radargenerator.github.io/) is an existing radar-generation
solution. Radar authoring is an area for future investigation, not a capability
of the current framework.

[Source 2 Viewer](https://github.com/ValveResourceFormat/ValveResourceFormat) already
browses VPKs and inspects/extracts Source 2 assets. Before writing a dependency
reader, assess its supported interfaces and redistribution terms. A plain check
for loose files is insufficient: assets may exist inside mounted packages.

[Mapcore's contest playtesting FAQ](https://www.mapcore.org/forums/thread/29559-faq/)
describes external playtest scheduling and feedback workflows. Importing feedback
into the editor is a proposed improvement; this source does not establish demand
for this exact add-on. Validate it with mappers who run regular playtests.

## First useful package

The portable package includes **Build log report**, which automatically follows
Hammer's displayed compiler output, groups repeated problems and retains session
build history. **Project context** exposes the selected project's folders and
loose source files. Saved-log inspection is also available. **Live tool output**
is included, but its experimental native logging provider is currently disabled.
These tools do not validate a complete dependency graph or replace a compiler.

For ordinary players, an editor loader offers limited direct value. Feedback
collection and more reliable published maps could benefit them without requiring
them to install an editor extension. The initial audience is map and asset authors.
