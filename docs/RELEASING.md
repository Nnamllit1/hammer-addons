# Publishing releases

Each release is an immutable versioned snapshot. Use a new version for code changes;
do not replace binaries on an existing release. Pre-release tags such as
`v0.1.0-alpha.1` publish as GitHub prereleases.

1. Set the base version in CMakeLists.txt and add `docs/releases/<tag>.md`.
2. Build and test with `build.bat -Test`, then commit and push the changes.
3. Create and push the version tag on that commit. The Release workflow rebuilds
   and tests that exact tag on Windows before publishing portable and SDK ZIPs,
   SHA256SUMS.txt and source provenance. A failed build does not publish a release.

The upload happens to a draft first. The publisher verifies asset checksums before
making it public. A retry accepts identical assets and refuses replacements.
The workflow uses GitHub's job-scoped token; no personal token is stored in the repo.

For local packaging, run `python scripts/package-release.py --version <tag>`.
The portable ZIP uses an explicit allowlist; local add-ons, diagnostics, logs,
preferences and game files are excluded. The SDK ZIP includes headers, example
sources, the scaffold script and documentation, with no Valve or Qt runtime binaries.
