from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass
from pathlib import Path


def discover_cs2() -> Path | None:
    """Read Steam's library registry instead of assuming the game's drive."""
    steam = Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / "Steam"
    if os.name == "nt":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
        except OSError:
            pass
    libraries = [steam]
    registry = steam / "steamapps/libraryfolders.vdf"
    if registry.exists():
        libraries.extend(Path(p.replace("\\\\", "\\")) for p in
                         re.findall(r'"path"\s*"([^"]+)"', registry.read_text(encoding="utf-8")))
    for library in libraries:
        manifest = library / "steamapps/appmanifest_730.acf"
        dirname = "Counter-Strike Global Offensive"
        if manifest.exists():
            match = re.search(r'"installdir"\s*"([^"]+)"', manifest.read_text(encoding="utf-8"))
            if match:
                dirname = match[1]
        candidate = library / "steamapps/common" / dirname
        if (candidate / "game/csgo/gameinfo.gi").is_file():
            return candidate.resolve()
    return None


@dataclass(frozen=True)
class Config:
    workspace: Path
    cs2: Path | None

    @classmethod
    def load(cls, path: str | Path | None = None) -> Config:
        filename = Path(path or os.environ.get("H2MCP_CONFIG", "h2mcp.local.json")).resolve()
        data = json.loads(filename.read_text(encoding="utf-8")) if filename.exists() else {}
        workspace = Path(data.get("workspace", str(filename.parent)))
        if not workspace.is_absolute():
            workspace = filename.parent / workspace
        game = os.environ.get("H2MCP_CS2") or data.get("cs2_root")
        cs2 = Path(game).resolve() if game else discover_cs2()
        return cls(workspace.resolve(), cs2)

    @property
    def state(self) -> Path:
        path = self.workspace / ".h2mcp"
        path.mkdir(parents=True, exist_ok=True)
        return path

    def game_root(self) -> Path:
        if not self.cs2 or not (self.cs2 / "game/csgo/gameinfo.gi").is_file():
            raise ValueError("CS2 not found. Set cs2_root in h2mcp.local.json or H2MCP_CS2.")
        return self.cs2

    def executable(self, name: str) -> Path:
        path = self.game_root() / "game/bin/win64" / name
        if not path.is_file():
            raise ValueError(f"Missing {name}. Install Counter-Strike 2 Workshop Tools in Steam.")
        return path
