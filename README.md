# h2mcp

A local **MCP server for CS2 mapping and modding**, written in Python. It connects an AI client to saved Hammer maps, addon source files, Valve's resource compiler and Workshop Tools. No API key or paid service is required by the server.

## Get started on Windows

Install Python 3.11+ and [uv](https://docs.astral.sh/uv/getting-started/installation/), CS2, and its Workshop Tools. Then run:

```powershell
.\setup.bat
.\test.bat
.\install-codex.bat
```

The BAT wrappers run the local PowerShell scripts without changing your system execution policy. `setup.ps1` installs the locked dependencies into `.venv`, detects CS2 through Steam's library registry, and starts the hidden build worker. If detection fails, set `cs2_root` in `h2mcp.local.json` to the directory containing `content` and `game`.

`install-codex.ps1` tests the actual stdio MCP connection and registers **h2mcp** with Codex using absolute paths. Reload MCP servers or start a new Codex session to load the tools. Registration cannot change the tool list of an already running agent turn.

For other MCP clients, register the following stdio command, replacing the paths with this checkout's location:

```json
{
  "mcpServers": {
    "h2mcp": {
      "command": "I:\\DEV\\h2mcp\\.venv\\Scripts\\python.exe",
      "args": ["-m", "h2mcp", "--config", "I:\\DEV\\h2mcp\\h2mcp.local.json", "serve"]
    }
  }
}
```

`start-server.bat` is an equivalent manual launcher. The process speaks MCP on stdin/stdout; logs go to stderr. You do not need to leave a separate server window open when using Codex.

## What it can do

| Area | MCP tools |
| --- | --- |
| Installation and projects | `doctor`, `list_projects`, `list_installed_addons`, `create_addon` |
| Addon text files | `list_files`, `read_file`, `write_file` |
| Hammer maps | `list_templates`, `create_map`, `import_map`, `inspect_map`, `edit_entity`, `add_entity` |
| Scripting and reference | `create_script`, `search_definitions`, `search_assets` |
| Build and run | `deploy_addon`, `compile_resource`, `job_status`, `list_jobs`, `launch_tools` |

There is also a `h2mcp://workflow` resource and a `mapping_task` prompt.

Map creation copies a Valve template from your installation, preserving its geometry. Binary VMAPs are converted by the installed `dmxconvert.exe`. Entity changes edit only the relevant spans in the text DMX; existing meshes, unknown fields and IDs are preserved. You can move/rotate entities, change keyvalues, and add point entities such as spawns, props, lights and `point_script`. Brush/trigger geometry construction and viewport manipulation are still done in Hammer.

`create_script` writes a JavaScript starter using `cs_script/point_script` and copies the API declarations from the installed `cs_script_demo` when available. The practice CFG is placed in the addon's game files. Script files need a configured `point_script` entity to run; creating a file alone does not attach it to a map. Use `search_definitions` to consult the installed FGD before configuring entities.

`search_assets` searches loose source files only; assets packed in VPK archives are outside its current scope. Map warnings check structural information and spawn presence, not gameplay correctness or leaks.

## Example workflow

Ask Codex after loading the server:

> Use h2mcp to create an addon named my_cs2_map from the wingman template. Inspect its spawns, add a named marker at 0 0 128, and create a JavaScript starter. Deploy it and compile the script, then report the build result.

The same operations are usable from PowerShell through a real MCP client:

```powershell
.\h2mcp.bat doctor
.\h2mcp.bat call create_addon --args-file examples\create-addon.json
.\h2mcp.bat call create_map --args-file examples\create-map.json
.\h2mcp.bat call create_script --args-file examples\create-script.json
```

For other tools, place their arguments in a JSON file and pass `--args-file`. This avoids PowerShell's nested quote escaping. Tool schemas are provided by MCP's `tools/list`.

1. `inspect_map(addon, path)` returns entity UUIDs and the map's SHA256. Filter by exact `classname` and use `offset`/`limit` on large maps.
2. `edit_entity` or `add_entity` requires `expected_sha256` from that inspection. Origins and angles are three numbers separated by spaces. Re-inspect after changes.
3. `deploy_addon(addon)` previews the exact file list; `apply=true` copies it to CS2's `content/csgo_addons/<addon>` and `game/csgo_addons/<addon>`.
4. `compile_resource(addon, path)` starts a background job. Use source paths such as `maps/scripts/main.js` or `maps/de_practice.vmap`. Full map compilation can be expensive and requires suitable Workshop Tools hardware.
5. `job_status(job_id)` returns the actual exit status and build log. A queued/running job is not a successful build.
6. `launch_tools(addon, mode="hammer", apply=true)` opens the addon's Workshop Tools. Select the map in Hammer. Use `mode="play", map_name="de_practice"` after compiling a map, or `mode="vconsole"` for Valve's console.

Hammer and playtest launches include `-insecure`. Launch previews are available with the default `apply=false`. Actual launches use the independent worker too; check the returned `job_id` for a `launched` status and PID. This reports process creation, not successful UI initialization.

## Files and editing

```text
h2mcp.local.json                 Local paths; not committed
projects/<addon>/project.json    Project marker
projects/<addon>/content/        Editable source maps, scripts and assets
projects/<addon>/game/           Addon metadata and runtime configuration
.h2mcp/backups/                  Original bytes before file replacement
.h2mcp/deployments/              Per-file deployment hashes
.h2mcp/jobs/<id>/status.json      Compiler status and command
.h2mcp/jobs/<id>/build.log        Full compiler output
```

Existing text files require a current SHA256 to replace them. Writes use atomic replacement and save the old bytes to a backup. Paths are confined to managed projects or the specific installed addon; traversal, Windows alternate data streams and escaping symlinks/junctions are rejected.

Deployment never deletes files and refuses to take over an unrelated installed addon. Subsequent deployments stop before copying if a target file was edited externally. If you edit the installed map in Hammer, use `import_map` to copy that saved version into a new project map path for review. There is currently no automatic two-way merge or conflict-resolution tool. Avoid editing the same map simultaneously in Hammer and through MCP.

Compilation uses an independent local queue worker, started by `setup.ps1` or `start-worker.ps1` outside the MCP process tree. It has no network listener and keeps status/logs on disk. This allows compilation to continue when the MCP client disconnects. `doctor` reports its heartbeat. After login/reboot, run `start-worker.ps1` again; interrupted builds are marked accordingly and queued work resumes. Run `h2mcp.ps1 stop-worker` to stop it after the current compile. OS logout or process cleanup can still stop the worker.

Only install a local MCP server in clients you trust: its write tools can edit addon files and its launch/build tools can start Valve programs. No HTTP listener, credentials, game injection or arbitrary shell tool is used.

## Development and validation

```powershell
.\test.bat
.\.venv\Scripts\python.exe scripts\live-demo.py --addon h2mcp_demo
```

The live demo creates a new addon, converts Valve's wingman template, adds and moves a marker, then re-inspects it through MCP. The addon name must be unused. Add `--deploy` to copy that demo into CS2 and start compiling the JavaScript. The script writes its report to `.h2mcp/live-demo.json`; poll the returned compile job ID separately.

The automated tests cover byte-preserving map edits, path confinement, backups/stale hashes, deployment conflicts, map import and compiler exit/log recording. `smoke` separately tests the MCP handshake, tool discovery, a tool call, a resource and a prompt.

See [VALIDATION.md](VALIDATION.md) for the completed local MCP, DMX and actual Valve compiler checks. `scripts/build-demo.py` attaches the starter to the existing `h2mcp_demo` map, deploys it and queues a full map build.

The official [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk/tree/v1.x) provides transport and schemas. This project pins its supported 1.x API (`mcp<2`) and locks exact dependencies in `uv.lock`. Codex registration follows the [Codex MCP command interface](https://learn.chatgpt.com/docs/developer-commands#codex-mcp). Valve format and compiler behavior are checked against the locally installed templates, FGD files, script examples and executable help output.
