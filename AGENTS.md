# Working on Hammer Addons

- This repository is now a C++20 Windows x64 native Hammer add-on loader. The old h2mcp server is retired.
- Run `build.bat -Test` (or `test.bat`) to build the DLLs, package dist/ and run native integration/installer tests.
- SDK: sdk/include/hammer_addons.h. Manifest: addons/hello/addon.ini. Specify ABI changes explicitly; never pass C++ ownership across DLL boundaries.
- DllMain must stay minimal. Never load plugins, start threads, invoke callbacks or wait on locks from it.
- Preserve all six original Hammer exports and their ordinals; test integer, floating, stack and mixed argument forwarding.
- Installation changes only the specific Hammer module and owned loader files. Keep a verified original backup; refuse unknown versions and external modifications.
- Do not guess document interfaces, vtables or offsets. Existing events report factory requests, not document edits or editor readiness.
- Simultaneous collaboration is the end goal, not an implemented feature. Report actual loader/editor evidence precisely.
- projects/, .h2mcp/, build/, dist/ and local config are ignored. Preserve generated maps and private migration backups; never commit Valve binaries, templates or account credentials.
- Native plugin creation here does not refer to Codex plugins; do not introduce MCP or Codex plugin packaging.
