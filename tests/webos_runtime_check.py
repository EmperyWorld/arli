#!/usr/bin/env python3
"""Run untrusted WebOS output in an isolated, networkless Node sandbox."""
import argparse
import json
import pathlib
import shutil
import subprocess


def check(workspace):
    root = pathlib.Path(workspace).resolve(strict=True)
    harness = pathlib.Path(__file__).resolve().parent
    node = shutil.which("node")
    bwrap = shutil.which("bwrap")
    if not node or not bwrap:
        raise RuntimeError("node and bwrap are required for the WebOS acceptance probe")
    command = [bwrap, "--die-with-parent", "--unshare-all", "--new-session"]
    for directory in ("/usr", "/lib", "/lib64"):
        if pathlib.Path(directory).exists():
            command += ["--ro-bind", directory, directory]
    command += ["--ro-bind", node, "/node", "--ro-bind", str(root), "/project", "--ro-bind", str(harness), "/harness",
                "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--chdir", "/project",
                "--clearenv", "--setenv", "PATH", "/usr/bin", "--", "/node",
                "--experimental-vm-modules", "--no-warnings", "/harness/webos_runtime_probe.cjs", "/project"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=40, check=False)
    if result.returncode:
        raise RuntimeError("sandbox probe failed: " + result.stderr[-2000:])
    return json.loads(result.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("workspace")
    parser.add_argument("report")
    args = parser.parse_args()
    try:
        report = check(args.workspace)
    except Exception as error:
        report = {"passed": False, "harness_error": str(error)}
    pathlib.Path(args.report).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    # Semantic failures belong in the report; infrastructure failures stop the live run.
    raise SystemExit(2 if "harness_error" in report else 0)
