"""An independent queue worker, started outside the MCP process tree."""
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from .files import atomic_write


def worker_status(state: Path) -> dict:
    try:
        status = json.loads((state / "worker.json").read_text())
        return {**status, "running": status.get("running", False) and time.time() - status["heartbeat"] < 10}
    except (OSError, ValueError, KeyError):
        return {"running": False}


def run(directory: Path) -> None:
    status = directory / "status.json"
    job = json.loads(status.read_text())
    job.update(status="running", started=time.time())
    if job.get("kind") == "play_map":
        job["phase"] = "compiling"
    atomic_write(status, json.dumps(job).encode())
    try:
        with (directory / "build.log").open("wb") as log:
            process = subprocess.Popen(job["command"], cwd=job["cwd"], stdout=log, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            job["pid"] = process.pid
            atomic_write(status, json.dumps(job).encode())
            if job.get("kind") == "launch":
                # GUI tools must outlive a one-shot MCP client and not block builds.
                job.update(status="launched")
            else:
                code = process.wait()
                job.update(status="succeeded" if code == 0 else "failed", exit_code=code)
                if job.get("kind") == "play_map":
                    if code != 0:
                        job.update(phase="compile_failed", error="Map compilation failed; CS2 was not launched.")
                    elif not Path(job["expected_map"]).is_file():
                        job.update(status="failed", phase="compile_failed", error="Compiler returned success but the map VPK is missing; CS2 was not launched.")
                    else:
                        job.update(status="running", phase="launching")
                        atomic_write(status, json.dumps(job).encode())
                        with (directory / "game.log").open("wb") as game_log:
                            game = subprocess.Popen(job["launch_command"], cwd=job["cwd"],
                                                    stdin=subprocess.DEVNULL, stdout=game_log, stderr=subprocess.STDOUT,
                                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                            # Detect immediate startup failure without waiting for the play session to end.
                            try:
                                startup_code = game.wait(timeout=3)
                            except subprocess.TimeoutExpired:
                                job.update(status="launched", phase="launched", game_pid=game.pid,
                                           note="CS2 process started; map loading is not yet verified.")
                            else:
                                job.update(status="failed", phase="launch_failed", game_exit_code=startup_code,
                                           error="CS2 exited during startup. Close an existing CS2 instance and retry; see game.log.")
    except Exception as error:
        job.update(status="failed", error=str(error))
    job["finished"] = time.time()
    atomic_write(status, json.dumps(job, indent=2).encode())


def serve(state: Path) -> None:
    state.mkdir(parents=True, exist_ok=True)
    # Hold a process lock so two workers cannot claim the same compiler job.
    with (state / "worker.lock").open("a+b") as lock:
        lock.seek(0)
        if os.name == "nt":
            import msvcrt
            if lock.read(1) == b"":
                lock.write(b"0")
                lock.flush()
            lock.seek(0)
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                raise ValueError("A build worker is already running for this workspace.") from None
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        stopped = threading.Event()
        status = {"pid": os.getpid(), "running": True, "heartbeat": time.time(), "version": 2}
        def heartbeat():
            while not stopped.is_set():
                status["heartbeat"] = time.time()
                atomic_write(state / "worker.json", json.dumps(status).encode())
                stopped.wait(1)
        for file in (state / "jobs").glob("*/status.json"):
            job = json.loads(file.read_text())
            if job.get("status") == "running":
                job.update(status="interrupted", error="The previous worker stopped before recording a result.")
                atomic_write(file, json.dumps(job).encode())
        (state / "worker.stop").unlink(missing_ok=True)
        thread = threading.Thread(target=heartbeat, daemon=True)
        thread.start()
        try:
            while not (state / "worker.stop").exists():
                files = sorted((state / "jobs").glob("*/status.json"), key=lambda p: p.stat().st_mtime)
                for file in files:
                    if json.loads(file.read_text()).get("status") == "queued":
                        run(file.parent)
                        break
                else:
                    stopped.wait(0.25)
        finally:
            stopped.set()
            thread.join(timeout=3)
            status.update(running=False, heartbeat=time.time())
            atomic_write(state / "worker.json", json.dumps(status).encode())


if __name__ == "__main__":
    serve(Path(sys.argv[1]))
