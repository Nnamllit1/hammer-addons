# Useful Workshop Tools add-ons

Research checked 2026-09-10. These priorities are product judgments based on
published workflows and specific reports, not a representative community survey.
Older problem reports identify areas to investigate; they do not establish that
a particular bug is still present in the current tools build.

| Priority | Opportunity | What the framework could improve | Remaining work |
| --- | --- | --- | --- |
| 1 | Build and asset diagnostics | Show actionable diagnostics beside the editor, with original evidence, instead of making users search raw output and forum threads | Live process logging and saved-log inspection are implemented; reliable asset dependency resolution and root-cause rules still need work |
| 2 | Radar workflow integration | Run an existing radar generator, keep project settings together, display progress and point to generated output | Verified external-tool invocation, project context, and a RadGen-specific adapter |
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

[RadGen](https://radargenerator.github.io/) already generates customizable CS2
radars using material and selection-set workflows. A useful add-on should make
that existing workflow easier to run inside the tools; there is no reason to
claim radar generation is missing or to build a replacement first.

[Source 2 Viewer](https://github.com/ValveResourceFormat/ValveResourceFormat) already
browses VPKs and inspects/extracts Source 2 assets. Before writing a dependency
reader, assess its supported interfaces and redistribution terms. A plain check
for loose files is insufficient: assets may exist inside mounted packages.

[Mapcore's contest playtesting FAQ](https://www.mapcore.org/forums/thread/29559-faq/)
describes external playtest scheduling and feedback workflows. Importing feedback
into the editor is a proposed improvement; this source does not establish demand
for this exact add-on. Validate it with mappers who run regular playtests.

## First useful package

The portable package includes **Live tool output** and **Build log report**.
The former displays new messages from a verified in-process logging provider.
The latter scans selected plain-text logs off the UI thread and preserves line
numbers for diagnostic keyword matches. Neither claims to diagnose root causes,
validate a complete dependency graph or replace a compiler.

For ordinary players, an editor loader offers limited direct value. Feedback
collection and more reliable published maps could benefit them without requiring
them to install an editor extension. The initial audience is map and asset authors.
