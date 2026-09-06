from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations

from .config import Config
from .core import Workshop

INSTRUCTIONS = """Use doctor first. Work in a managed addon project: create_addon,
list_templates, create_map, inspect_map, then edit_entity or add_entity. Entity IDs
are DMX UUIDs. Pass the sha256 returned by inspect_map/read_file when editing.
Changes affect saved source files; close/reload the map in Hammer around external
edits. create_script uses CS2's cs_script JavaScript API. Query search_definitions
for the installed entity properties. deploy_addon(apply=false) previews the files;
apply=true deploys, detecting external edits. compile_resource returns a job ID;
poll job_status to see the actual result and build log. launch_tools previews by
default, apply=true opens the application. Never describe a compile as successful
before checking the job. Use play_map(addon, map_name) to deploy, compile and open
the map in CS2 automatically. With rebuild=false it opens the existing compiled
map. Check job_status; a launched process alone does not prove the map loaded.
This is a file/tool bridge, not live viewport control.
"""


def create_server(config: Config) -> FastMCP:
    workshop = Workshop(config)
    server = FastMCP("h2mcp", instructions=INSTRUCTIONS)
    read = ToolAnnotations(readOnlyHint=True, destructiveHint=False, openWorldHint=False)
    write = ToolAnnotations(readOnlyHint=False, destructiveHint=False, openWorldHint=False)
    actions = {
        "doctor": (workshop.doctor, "Detect CS2 and Workshop Tools; report paths and capabilities.", read),
        "list_projects": (workshop.list_projects, "List managed addon projects in this workspace.", read),
        "list_installed_addons": (workshop.list_installed_addons, "List the installed CS2 source addons, without modifying them.", read),
        "create_addon": (workshop.create_addon, "Create an addon project with content, addoninfo and a practice CFG.", write),
        "list_files": (workshop.list_files, "List project files with a substring query and pagination.", read),
        "read_file": (workshop.read_file, "Read a UTF-8 project file and its SHA256 for a subsequent edit.", read),
        "write_file": (workshop.write_file, "Create or replace an addon text file. Existing files require expected_sha256; backups are automatic.", write),
        "list_templates": (workshop.templates, "List map templates from this CS2 installation.", read),
        "create_map": (workshop.create_map, "Create a text VMAP from an installed Valve template, preserving its geometry and entities.", write),
        "import_map": (workshop.import_map, "Copy a saved map from an installed addon into this project; convert binary DMX to text.", write),
        "inspect_map": (workshop.inspect_map, "Inspect map structure, entity class counts, UUIDs and properties; returns SHA256. Filters use exact classname.", read),
        "edit_entity": (workshop.edit_entity, "Edit one entity by UUID: properties, origin and angles. Requires current map SHA256; preserves unrelated text.", write),
        "add_entity": (workshop.add_entity, "Add a point entity to the map world. Requires current SHA256. Does not construct brush/trigger geometry.", write),
        "create_script": (workshop.create_script, "Create a CS2 point_script JavaScript starter and copy installed TypeScript API definitions when available.", write),
        "search_definitions": (workshop.search_definitions, "Search installed FGD entity/property documentation and return source snippets.", read),
        "search_assets": (workshop.search_assets, "Find loose CS2 content assets by path substring. Does not search packed VPK archives.", read),
        "deploy_addon": (workshop.deploy_addon, "Preview deployment to CS2. apply=true copies files; rejects addons not owned by this project and externally edited files. Never deletes files.", write),
        "compile_resource": (workshop.compile_resource, "Start resourcecompiler on a deployed source resource or map; returns a persistent job ID immediately. Map builds may take a long time.", write),
        "play_map": (workshop.play_map, "Automatically deploy the addon, compile the named map, and launch CS2 directly into it after a successful build. Set rebuild=false to play the already compiled version. Returns a job ID; check job_status. Starts a local game with -insecure.", write),
        "job_status": (workshop.job_status, "Read compiler job result and bounded log tail; jobs continue independently of the MCP connection.", read),
        "list_jobs": (workshop.list_jobs, "Find recent compiler jobs, optionally filtered by addon, after reconnecting.", read),
        "launch_tools": (workshop.launch, "Preview/open Hammer tools, local map playtest or VConsole. mode=hammer opens the addon tools; choose the map in Hammer. apply=true queues launch through the worker; check the returned job_id with job_status.", write),
    }
    for name, (function, description, annotation) in actions.items():
        server.add_tool(function, name=name, description=description, annotations=annotation)

    @server.resource("h2mcp://workflow")
    def workflow() -> str:
        """The recommended CS2 addon workflow and limitations."""
        return INSTRUCTIONS

    @server.prompt()
    def mapping_task(addon: str, objective: str) -> str:
        """Start a mapping or scripting task with the correct inspect/edit/build loop."""
        return (f"Work on CS2 addon {addon}. Objective: {objective}\n"
                "Run doctor, inspect the project and map before editing, query installed FGD definitions, "
                "make focused edits with current hashes, then deploy and compile when appropriate. "
                "Check the compiler log and report what was actually validated.")

    return server
