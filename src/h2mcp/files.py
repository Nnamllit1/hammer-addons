from __future__ import annotations

import hashlib
import os
import re
import stat
import tempfile
import uuid
from pathlib import Path

RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}


def addon_name(name: str) -> str:
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", name) or name in RESERVED:
        raise ValueError("Use a lowercase addon name starting with a letter, then letters, numbers or underscores.")
    return name


def contained(root: Path, relative: str) -> Path:
    """Resolve symlinks/junctions and reject Windows ADS and device paths too."""
    normalized = relative.replace("\\", "/")
    parts = normalized.split("/")
    if not relative or any(p in ("", ".", "..") or p.endswith((" ", ".")) or
                           p.split(".")[0].lower() in RESERVED for p in parts):
        raise ValueError("Expected a normal relative path without parent traversal or device names.")
    if any(c in normalized for c in ':*?"<>|\x00') or any(ord(c) < 32 for c in normalized):
        raise ValueError("Invalid path characters.")
    cursor = root
    for part in ("", *parts):
        cursor = cursor / part if part else cursor
        if cursor.is_symlink() or (cursor.exists() and getattr(cursor.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError("Paths through symlinks/junctions are not supported.")
    target = (root / normalized).resolve()
    if not target.is_relative_to(root.resolve()):
        raise ValueError("Path escapes the allowed directory (possibly through a symlink/junction).")
    return target


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".h2mcp-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def save(path: Path, data: bytes, backups: Path, expected_sha256: str | None = None) -> dict:
    backup = None
    if path.exists():
        old = path.read_bytes()
        if expected_sha256 is None:
            raise ValueError("File exists. Read it first and pass its sha256 to replace it.")
        if digest(old) != expected_sha256:
            raise ValueError("File changed since it was read; read it again before editing.")
        backup = backups / f"{uuid.uuid4().hex}-{path.name}"
        atomic_write(backup, old)
    elif expected_sha256:
        raise ValueError("Expected an existing file, but it no longer exists.")
    atomic_write(path, data)
    return {"path": str(path), "sha256": digest(data), "backup": str(backup) if backup else None}
