"""Delegate standalone setup entry points to the project's locked environment."""
import os
from pathlib import Path
import subprocess
import sys


def ensure_project_python():
    """Restart through the launcher; importing tools and asking for help stay cheap."""
    if os.environ.get("IKIGAI_PROJECT_PYTHON") == "1":
        return
    if "--help" in sys.argv[1:] or "-h" in sys.argv[1:]:
        return
    root = Path(__file__).resolve().parents[2]
    arguments = [str(Path(sys.argv[0]).resolve()), *sys.argv[1:]]
    if os.name == "nt":
        command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                   "-File", str(root / "utils/environment/python_env.ps1"), *arguments]
    else:
        command = ["sh", str(root / "run.sh"), *arguments]
    raise SystemExit(subprocess.call(command))
