# Release guide

This guide is for contributors preparing a Hammer Addons release.

## Prepare a version

1. Update the base version in `CMakeLists.txt` and add release notes at
   `docs/releases/<tag>.md`. Describe user-visible changes, installation or migration
   steps, and known limitations.
2. Run `build.bat -Test` and commit the release changes.
3. Tag that commit, for example `v0.1.0-alpha.1`, and push the commit and tag.
   GitHub Actions builds and tests the tagged source before publishing its downloads.

Tags with a suffix such as `-alpha.1` are prereleases. Publish fixes under a new
version so each release remains a consistent source-and-binary snapshot.

## Downloads

Each release provides:

- **Windows x64 portable ZIP:** the launcher and bundled add-ons. Extract it
  outside CS2 and follow the included installation guide.
- **SDK ZIP:** C headers, example source, the add-on scaffold script, and documentation.
- **SHA256SUMS.txt:** checksums for the downloads and release metadata.
- **release.json:** the version, target platform, and source commit.

To prepare these files locally after building:

```powershell
python scripts/package-release.py --version v0.1.0-alpha.1
```

The files are written to `dist/release/`. If a release workflow fails, review its
failed step on the repository's Actions page. A retry can reuse matching uploaded
files; changed binaries require a new version.
