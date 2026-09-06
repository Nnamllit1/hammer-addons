# Validation on 2026-09-06

Tested on Windows against the user's installed CS2 Workshop Tools.

- `setup.bat`: locked dependency installation, Steam/CS2 detection and worker detection passed.
- `test.bat`: **28 automated tests passed**, followed by an actual stdio MCP smoke test.
- MCP: handshake, discovery of **21 tools with structured output schemas**, `doctor`, resource read and prompt retrieval passed.
- Live MCP map workflow: copied Valve's wingman template, added an `info_target`, moved it to `64 0 128`, and verified the entity through a subsequent tool call.
- Valve `dmxconvert`: accepted the edited map as binary DMX and converted it back to text; marker identity and position were preserved.
- JavaScript: generated a `cs_script/point_script` starter. Valve reported **1 compiled, 0 failed** and wrote `maps/scripts/main.vjs_c`.
- Map: attached that script through a `point_script` entity, deployed the source and compiled with Valve's resource compiler. It reported **23 compiled, 0 failed** in approximately 34 seconds and wrote `maps/de_h2mcp_demo.vpk` (11,177,386 bytes).
- Both successful compiler jobs ran through the independent worker after the submitting MCP connection closed.
- Codex: `codex mcp get h2mcp --json` confirmed the enabled stdio registration with absolute interpreter/config paths.

The initial compiler test exposed Windows MCP client process-tree cleanup. Compilation was moved to an independent worker; the interrupted job is retained as `interrupted`, rather than reported as a success.

The sample uses Valve's template geometry. This validation did **not** include visual inspection in Hammer, an in-game playtest, or bot-navigation validation. The map build log reports zero navigation areas; generating useful navigation and gameplay geometry remains part of mapping work in Hammer. Compiler success alone is not proof that a map is ready to publish.

Local artifacts (ignored by Git):

- Demo project: `projects/h2mcp_demo/`
- Script build: `.h2mcp/jobs/52254bec76e745858dd8202c42195c1e/`
- Map build: `.h2mcp/jobs/be28721b310543abba9e41cd150be0e1/`
- Demo source and outputs are also deployed to the CS2 installation under the addon name `h2mcp_demo`.
